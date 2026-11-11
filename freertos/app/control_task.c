/* M1 Part 6: 1 kHz control task, Restricted (unprivileged) under the ARM_CM3_MPU port.
 *
 * Loop per tick:  plant -> encoder -> speed estimator -> cascade PID -> V -> plant.
 * The OS-free core lives in common/control/; this file is the FreeRTOS adapter only.
 *
 * Memory the Restricted task may touch (everything else -> MemManage):
 *   (a) its own stack                      - 4 KB, aligned to its size
 *   (b) g_ctl  control data + log ring + jitter arrays - 128 KB, power of 2, aligned to its size, RW
 *   (c) g_shared seqlock state             - 64 B, RW (single writer = this task)
 *   (d) TIMER0 registers                   - 32 B, READ-ONLY (cycle counter for jitter; the PPB/SysTick
 *       is not reachable from unprivileged code, TIMER0 is a normal peripheral).
 * Flash (code, const) is readable by unprivileged tasks.
 *
 * The control task does NOT printf: it pushes samples into a ring buffer in (b); a low priority
 * privileged task drains the ring to the UART as CSV and reads (c) with the seqlock.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "app.h"

#include "plant.h"
#include "encoder.h"
#include "pid.h"

#define CONTROL_PERIOD_TICKS   1U
#define CONTROL_STACK_WORDS    1024U                       /* 4 KB */
#define CONTROL_DATA_SIZE      131072U                     /* 128 KB, power of 2 (jitter arrays) */
#define JITTER_SAMPLES         10000U
#define JITTER_SKIP_TICKS      500U                        /* skip boot-time transients */
#define TIMER0_BASE            0x40000000UL                /* CMSDK APB TIMER0, free-running down counter @ 25 MHz */
#define TIMER0_CTRL            ( *( volatile uint32_t * ) ( TIMER0_BASE + 0x0 ) )
#define TIMER0_VALUE           ( *( volatile uint32_t * ) ( TIMER0_BASE + 0x4 ) )
#define TIMER0_RELOAD          ( *( volatile uint32_t * ) ( TIMER0_BASE + 0x8 ) )
#define LOG_RING_LEN           256U                        /* power of 2 */
#define CONTROL_PRIORITY       ( configMAX_PRIORITIES - 1 )
#define TWO_PI_F               6.28318530718f
#define STEP_REF_RAD           1.0f
#define STEP_HALF_PERIOD_TICKS 1000U                       /* ref toggles 0 <-> STEP_REF every 1 s */

typedef struct
{
    uint32_t ulTick;
    int32_t  lCount;
    int32_t  lPos_urad;
    int32_t  lVel_mrads;
    int32_t  lVolt_mV;
} LogSample_t;

typedef struct
{
    plant_params_t xPlantParams;
    plant_state_t xPlant;
    encoder_params_t xEncParams;
    encoder_state_t xEnc;
    speed_estimator_t xSpeed;
    cascade_params_t xCascadeParams;
    cascade_state_t xCascade;

    uint32_t ulTick;            /* control ticks executed since boot (counts every period) */
    uint32_t ulMissedDeadlines; /* xTaskDelayUntil() reported "not delayed" */
    uint32_t ulWriteIdx;        /* ring: written by control task only */
    uint32_t ulDropped;         /* samples lost because the drain task was too slow */
    volatile uint32_t ulReadIdx;/* ring: advanced by the drain task (privileged, can write here) */
    LogSample_t xRing[ LOG_RING_LEN ];

    /* Jitter / cost capture (Part 7). Raw TIMER0 counts @ 25 MHz = 1 count per 40 ns virtual. */
    uint32_t ulPrevStart;
    uint32_t ulJitterN;          /* samples captured so far */
    uint32_t ulJitterMissedBase; /* missed-deadline counter when capture started */
    uint32_t ulJitterMissedEnd;
    volatile uint32_t ulJitterDone;
    uint16_t usPeriod[ JITTER_SAMPLES ];   /* counts between consecutive cycle starts (ideal 25000) */
    uint16_t usCostCtrl[ JITTER_SAMPLES ]; /* encoder + speed estimator + cascade */
    uint16_t usCostPlant[ JITTER_SAMPLES ];/* plant model (test fixture on the node, not controller cost) */
} ControlData_t;

typedef union
{
    ControlData_t xData;
    uint8_t ucPad[ CONTROL_DATA_SIZE ];
} ControlBlock_t;

typedef char ControlDataFits_t[ ( sizeof( ControlData_t ) <= CONTROL_DATA_SIZE ) ? 1 : -1 ];

/* Single-writer seqlock: writer makes seq odd, writes, makes seq even. */
typedef struct
{
    volatile uint32_t ulSeq;
    volatile uint32_t ulTick;
    volatile int32_t lCount;
    volatile int32_t lPos_urad;
    volatile int32_t lVel_mrads;
    volatile int32_t lVolt_mV;
    volatile uint32_t ulMissed;
    volatile uint32_t ulTickCheck;   /* == ulTick in every consistent snapshot */
    volatile uint32_t ulFaultExpected; /* CONTROL_TEST_VIOLATE: armed by control task, consumed by the fault handler */
} SharedState_t;

static ControlBlock_t g_ctl __attribute__( ( aligned( CONTROL_DATA_SIZE ) ) );
static SharedState_t g_shared __attribute__( ( aligned( 64 ) ) );
static StackType_t g_stack[ CONTROL_STACK_WORDS ] __attribute__( ( aligned( CONTROL_STACK_WORDS * sizeof( StackType_t ) ) ) );
static TaskHandle_t g_ctl_handle;

/* Called from vHandleMemoryFault() (privileged) in mpu_selftest.c. */
volatile uint32_t * pulControlFaultExpected( void ) { return &g_shared.ulFaultExpected; }

#ifdef CONTROL_TEST_VIOLATE
/* Privileged-only variable the control task is NOT allowed to write (not in any of its regions). */
static volatile uint32_t ulForbidden __attribute__( ( aligned( 32 ) ) );
#define VIOLATE_AT_TICK 5000U
#endif

static inline int32_t lScale( float f, float k )
{
    float x = f * k;
    return ( int32_t ) ( ( x >= 0.0f ) ? ( x + 0.5f ) : ( x - 0.5f ) );
}

static void prvControlTask( void * pv )
{
    ControlData_t * c = &g_ctl.xData;
    TickType_t xLast;
    float fRef = 0.0f, fVolt = 0.0f;

    ( void ) pv;
    plant_default_params( &c->xPlantParams );
    plant_reset( &c->xPlant );
    c->xEncParams = ( encoder_params_t ) { .counts_per_rev = 4096U, .noise_std_rad = 0.0f, .seed = 1U, .force_stuck = false };
    encoder_reset( &c->xEnc, &c->xEncParams );
    speed_estimator_reset( &c->xSpeed, 0.5f );       /* alpha as tuned in Part 5 */
    cascade_default_params( &c->xCascadeParams );
    cascade_reset( &c->xCascade );

    xLast = xTaskGetTickCount();
    for( ; ; )
    {
        /* Test profile: fixed square reference (Comm + commands from QNX come in M4). */
#ifdef CONTROL_TEST_STEP
        fRef = ( ( c->ulTick / STEP_HALF_PERIOD_TICKS ) & 1U ) ? 0.0f : STEP_REF_RAD;
#else
        fRef = 0.0f;
#endif
        const uint32_t ulT0 = TIMER0_VALUE;             /* down counter */
        const int32_t lCount = encoder_step( &c->xEnc, &c->xEncParams, c->xPlant.position_rad );
        const float fVel = speed_estimator_step( &c->xSpeed, lCount, 0.001f, c->xEncParams.counts_per_rev );
        const float fPos = ( float ) lCount * TWO_PI_F / ( float ) c->xEncParams.counts_per_rev;
        fVolt = cascade_step( &c->xCascade, &c->xCascadeParams, fRef, fPos, fVel );
        const uint32_t ulT1 = TIMER0_VALUE;
        ( void ) plant_step( &c->xPlant, &c->xPlantParams, fVolt, 0.001f );
        const uint32_t ulT2 = TIMER0_VALUE;

        if( c->ulTick >= JITTER_SKIP_TICKS && c->ulJitterN < JITTER_SAMPLES )
        {
            if( c->ulJitterN == 0 ) { c->ulJitterMissedBase = c->ulMissedDeadlines; }
            c->usPeriod[ c->ulJitterN ] = ( uint16_t ) ( c->ulPrevStart - ulT0 );   /* wrap-safe */
            c->usCostCtrl[ c->ulJitterN ] = ( uint16_t ) ( ulT0 - ulT1 );
            c->usCostPlant[ c->ulJitterN ] = ( uint16_t ) ( ulT1 - ulT2 );
            c->ulJitterN++;
            if( c->ulJitterN == JITTER_SAMPLES )
            {
                c->ulJitterMissedEnd = c->ulMissedDeadlines;
                c->ulJitterDone = 1;
            }
        }
        c->ulPrevStart = ulT0;

#ifdef CONTROL_LOG_CSV
        {
            LogSample_t * s = &c->xRing[ c->ulWriteIdx & ( LOG_RING_LEN - 1U ) ];
            if( ( c->ulWriteIdx - c->ulReadIdx ) >= LOG_RING_LEN )
            {
                c->ulDropped++;                         /* ring full: drop, never block */
            }
            else
            {
                s->ulTick = c->ulTick;
                s->lCount = lCount;
                s->lPos_urad = lScale( c->xPlant.position_rad, 1.0e6f );
                s->lVel_mrads = lScale( c->xPlant.velocity_rad_s, 1.0e3f );
                s->lVolt_mV = lScale( fVolt, 1.0e3f );
                c->ulWriteIdx++;
            }
        }
#endif

        g_shared.ulSeq++;                               /* odd: write in progress */
        __asm volatile ( "dmb" ::: "memory" );
        g_shared.ulTick = c->ulTick;
        g_shared.lCount = lCount;
        g_shared.lPos_urad = lScale( c->xPlant.position_rad, 1.0e6f );
        g_shared.lVel_mrads = lScale( c->xPlant.velocity_rad_s, 1.0e3f );
        g_shared.lVolt_mV = lScale( fVolt, 1.0e3f );
        g_shared.ulMissed = c->ulMissedDeadlines;
        g_shared.ulTickCheck = c->ulTick;
        __asm volatile ( "dmb" ::: "memory" );
        g_shared.ulSeq++;                               /* even: consistent */

        c->ulTick++;

#ifdef CONTROL_TEST_VIOLATE
        if( c->ulTick == VIOLATE_AT_TICK )
        {
            g_shared.ulFaultExpected = 1;
            ulForbidden = 0xBAD0BAD0UL;                 /* outside every region -> MemManage */
        }
#endif

        if( xTaskDelayUntil( &xLast, CONTROL_PERIOD_TICKS ) == pdFALSE )
        {
            c->ulMissedDeadlines++;                     /* woke up late: a tick was skipped */
        }
    }
}

/* Privileged, low priority: seqlock reader + CSV drain. Never delays the control task. */
static void prvDrainTask( void * pv )
{
    ControlData_t * c = &g_ctl.xData;
    uint32_t ulTornReads = 0, ulSnapshots = 0, ulRetries = 0;
    TickType_t xNextReport = xTaskGetTickCount() + pdMS_TO_TICKS( 5000 );

    ( void ) pv;
    for( ; ; )
    {
        uint32_t ulBurst = 0;

#ifdef CONTROL_LOG_CSV
        while( c->ulReadIdx != c->ulWriteIdx && ulBurst < 32U )
        {
            const LogSample_t * s = &c->xRing[ c->ulReadIdx & ( LOG_RING_LEN - 1U ) ];
            printf( "C,%lu,%ld,%ld,%ld,%ld\n", ( unsigned long ) s->ulTick, ( long ) s->lCount,
                    ( long ) s->lPos_urad, ( long ) s->lVel_mrads, ( long ) s->lVolt_mV );
            c->ulReadIdx++;
            ulBurst++;
        }
#endif
        ( void ) ulBurst;

        if( c->ulJitterDone == 1 )
        {
            c->ulJitterDone = 2;
            printf( "JITTER_BEGIN n=%lu skip=%lu missed_in_window=%lu timer_hz=25000000 ideal_period=25000\n",
                    ( unsigned long ) JITTER_SAMPLES, ( unsigned long ) JITTER_SKIP_TICKS,
                    ( unsigned long ) ( c->ulJitterMissedEnd - c->ulJitterMissedBase ) );
            for( uint32_t i = 0; i < JITTER_SAMPLES; i++ )
            {
                printf( "J,%lu,%u,%u,%u\n", ( unsigned long ) i, c->usPeriod[ i ], c->usCostCtrl[ i ], c->usCostPlant[ i ] );
            }
            printf( "JITTER_END\n" );
        }

        /* Seqlock read from another task. */
        {
            SharedState_t xSnap;
            uint32_t ulS0, ulS1;
            do
            {
                ulS0 = g_shared.ulSeq;
                __asm volatile ( "dmb" ::: "memory" );
                xSnap.ulTick = g_shared.ulTick;
                xSnap.ulTickCheck = g_shared.ulTickCheck;
                xSnap.lCount = g_shared.lCount;
                __asm volatile ( "dmb" ::: "memory" );
                ulS1 = g_shared.ulSeq;
                if( ( ulS0 & 1U ) || ( ulS0 != ulS1 ) ) { ulRetries++; }
            } while( ( ulS0 & 1U ) || ( ulS0 != ulS1 ) );
            ulSnapshots++;
            if( xSnap.ulTick != xSnap.ulTickCheck ) { ulTornReads++; }
        }

        if( ( int32_t ) ( xTaskGetTickCount() - xNextReport ) >= 0 )
        {
            xNextReport += pdMS_TO_TICKS( 5000 );
            printf( "control tick=%lu missed=%lu dropped=%lu seqlock snaps=%lu retries=%lu torn=%lu stack_hwm=%lu words\n",
                    ( unsigned long ) c->ulTick, ( unsigned long ) c->ulMissedDeadlines, ( unsigned long ) c->ulDropped,
                    ( unsigned long ) ulSnapshots, ( unsigned long ) ulRetries, ( unsigned long ) ulTornReads,
                    ( unsigned long ) uxTaskGetStackHighWaterMark( g_ctl_handle ) );
        }

        vTaskDelay( 1 );
    }
}

void control_task_start( void )
{
    TaskParameters_t xParams =
    {
        .pvTaskCode = prvControlTask, .pcName = "control", .usStackDepth = CONTROL_STACK_WORDS,
        .pvParameters = NULL, .uxPriority = CONTROL_PRIORITY, .puxStackBuffer = g_stack,
        .xRegions =
        {
            { &g_ctl,    CONTROL_DATA_SIZE,        portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER },
            { &g_shared, sizeof( g_shared ),       portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER },
            { ( void * ) TIMER0_BASE, 32,         portMPU_REGION_READ_ONLY | portMPU_REGION_EXECUTE_NEVER },
        }
    };

    TIMER0_RELOAD = 0xFFFFFFFFUL;                       /* free-running 32-bit down counter */
    TIMER0_CTRL = 1UL;                                  /* enable, no interrupt */
    memset( &g_ctl, 0, sizeof( g_ctl ) );
    memset( ( void * ) &g_shared, 0, sizeof( g_shared ) );
    xTaskCreateRestricted( &xParams, &g_ctl_handle );
    xTaskCreate( prvDrainTask, "ctl_drain", configMINIMAL_STACK_SIZE * 2, NULL, ( tskIDLE_PRIORITY + 1 ) | portPRIVILEGE_BIT, NULL );
}
