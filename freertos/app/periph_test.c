/* Peripheral verification for docs/sil_environment.md section 4 (QEMU mps2-an385).
 * Exactly one test is compiled in, selected with -DPERIPH_TEST=<none|wdt|wdt_kick|dwt|icount>.
 * Test tasks are privileged: they touch peripherals and the NVIC. */
#include <stdio.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "app.h"

#if defined( PERIPH_TEST_WDT ) || defined( PERIPH_TEST_WDT_KICK ) || defined( PERIPH_TEST_DWT ) || defined( PERIPH_TEST_ICOUNT )

#define PRIV_PRIO( p )    ( ( ( p ) | portPRIVILEGE_BIT ) )

#define WDT_BASE     0x40008000UL
#define WDT_LOAD     ( *( volatile uint32_t * ) ( WDT_BASE + 0x000 ) )
#define WDT_VALUE    ( *( volatile uint32_t * ) ( WDT_BASE + 0x004 ) )
#define WDT_CTRL     ( *( volatile uint32_t * ) ( WDT_BASE + 0x008 ) )   /* bit0 INTEN, bit1 RESEN */
#define WDT_INTCLR   ( *( volatile uint32_t * ) ( WDT_BASE + 0x00C ) )
#define WDT_RIS      ( *( volatile uint32_t * ) ( WDT_BASE + 0x010 ) )
#define WDT_LOCK     ( *( volatile uint32_t * ) ( WDT_BASE + 0xC00 ) )
#define WDT_UNLOCK_KEY 0x1ACCE551UL

/* Lives in .noinit: must survive the watchdog reset (M1 and M5 rely on this). */
typedef struct { uint32_t ulMagic, ulBootCount, ulNmiCount; } NoInit_t;
static NoInit_t xNoInit __attribute__( ( section( ".noinit" ) ) );
#define NOINIT_MAGIC 0xC0DEF00DUL

static void prvBootBanner( void )
{
    if( xNoInit.ulMagic != NOINIT_MAGIC )
    {
        xNoInit.ulMagic = NOINIT_MAGIC; xNoInit.ulBootCount = 0; xNoInit.ulNmiCount = 0;
        printf( "[noinit] cold start (magic was not present)\n" );
    }
    xNoInit.ulBootCount++;
    printf( "[noinit] boot #%lu, nmi_count carried over = %lu\n", ( unsigned long ) xNoInit.ulBootCount, ( unsigned long ) xNoInit.ulNmiCount );
}
#endif

#if defined( PERIPH_TEST_WDT ) || defined( PERIPH_TEST_WDT_KICK )

/* First expiry -> NMI. The handler deliberately does NOT clear the interrupt, so the second
 * expiry (RESEN set) resets the machine. Minimal: no FreeRTOS API, no printf. */
void NMI_Handler( void )
{
    xNoInit.ulNmiCount++;
}

static void prvWdtTask( void * pv )
{
    TickType_t xLast = xTaskGetTickCount();
    uint32_t ulIter = 0;

    ( void ) pv;
    prvBootBanner();
    WDT_LOCK = WDT_UNLOCK_KEY;
    WDT_LOAD = 12500000UL;                 /* 0.5 s if the watchdog clock is 25 MHz */
    WDT_CTRL = 0x3UL;                      /* INTEN | RESEN */
    printf( "[wdt] armed: LOAD=%lu CTRL=0x%lx\n", ( unsigned long ) WDT_LOAD, ( unsigned long ) WDT_CTRL );

    for( ; ; )
    {
        vTaskDelayUntil( &xLast, pdMS_TO_TICKS( 100 ) );
        #if defined( PERIPH_TEST_WDT_KICK )
            WDT_INTCLR = 1;                /* kick: clears the interrupt and reloads the counter */
        #endif
        if( ( ++ulIter % 5 ) == 0 )
        {
            printf( "[wdt] t=%lu ms VALUE=%lu RIS=%lu nmi=%lu\n", ( unsigned long ) ( xLast * portTICK_PERIOD_MS ),
                    ( unsigned long ) WDT_VALUE, ( unsigned long ) WDT_RIS, ( unsigned long ) xNoInit.ulNmiCount );
        }
    }
}
void periph_test_start( void ) { xTaskCreate( prvWdtTask, "wdt_test", configMINIMAL_STACK_SIZE * 2, NULL, PRIV_PRIO( tskIDLE_PRIORITY + 1 ), NULL ); }

#elif defined( PERIPH_TEST_DWT )

#define DEMCR      ( *( volatile uint32_t * ) 0xE000EDFCUL )
#define DWT_CTRL   ( *( volatile uint32_t * ) 0xE0001000UL )
#define DWT_CYCCNT ( *( volatile uint32_t * ) 0xE0001004UL )

static void prvDwtTask( void * pv )
{
    volatile uint32_t ulSpin;
    uint32_t ulA, ulB, ulC;

    ( void ) pv;
    DEMCR |= ( 1UL << 24 );                /* TRCENA */
    DWT_CYCCNT = 0;
    DWT_CTRL |= 1UL;                       /* CYCCNTENA */
    printf( "[dwt] DEMCR=0x%08lx DWT_CTRL=0x%08lx\n", ( unsigned long ) DEMCR, ( unsigned long ) DWT_CTRL );
    for( ; ; )
    {
        ulA = DWT_CYCCNT;
        for( ulSpin = 0; ulSpin < 100000UL; ulSpin++ ) { }
        ulB = DWT_CYCCNT;
        for( ulSpin = 0; ulSpin < 200000UL; ulSpin++ ) { }
        ulC = DWT_CYCCNT;
        printf( "[dwt] before=%lu after100k=%lu after300k=%lu  d1=%lu d2=%lu\n", ( unsigned long ) ulA, ( unsigned long ) ulB,
                ( unsigned long ) ulC, ( unsigned long ) ( ulB - ulA ), ( unsigned long ) ( ulC - ulB ) );
        vTaskDelay( pdMS_TO_TICKS( 1000 ) );
    }
}
void periph_test_start( void ) { xTaskCreate( prvDwtTask, "dwt_test", configMINIMAL_STACK_SIZE * 2, NULL, PRIV_PRIO( tskIDLE_PRIORITY + 1 ), NULL ); }

#elif defined( PERIPH_TEST_ICOUNT )

extern volatile uint32_t ulIdleCount;      /* incremented by the idle hook */

/* Work done by the idle task between two consecutive 1 ms ticks. With a deterministic virtual
 * clock (-icount) this is nearly constant; without it, it follows host scheduling. */
static void prvIcountTask( void * pv )
{
    TickType_t xLast = xTaskGetTickCount();
    uint32_t ulPrev = ulIdleCount, ulN = 0, ulMin = 0xFFFFFFFFUL, ulMax = 0;
    uint64_t ullSum = 0, ullSumSq = 0;

    ( void ) pv;
    for( ; ; )
    {
        vTaskDelayUntil( &xLast, 1 );
        uint32_t ulNow = ulIdleCount, ulD = ulNow - ulPrev;
        ulPrev = ulNow;
        if( ulD < ulMin ) ulMin = ulD;
        if( ulD > ulMax ) ulMax = ulD;
        ullSum += ulD; ullSumSq += ( uint64_t ) ulD * ulD; ulN++;
        if( ulN == 2000 )
        {
            uint32_t ulMean = ( uint32_t ) ( ullSum / ulN );
            uint64_t ullVar = ( ullSumSq / ulN ) - ( uint64_t ) ulMean * ulMean;
            uint32_t ulSd = 0; while( ( uint64_t ) ( ulSd + 1 ) * ( ulSd + 1 ) <= ullVar ) ulSd++;
            printf( "[icount] ticks=2000 idle/tick min=%lu max=%lu mean=%lu stddev=%lu\n", ( unsigned long ) ulMin,
                    ( unsigned long ) ulMax, ( unsigned long ) ulMean, ( unsigned long ) ulSd );
            ulN = 0; ulMin = 0xFFFFFFFFUL; ulMax = 0; ullSum = 0; ullSumSq = 0;
        }
    }
}
void periph_test_start( void ) { xTaskCreate( prvIcountTask, "icount", configMINIMAL_STACK_SIZE * 2, NULL, PRIV_PRIO( configMAX_PRIORITIES - 4 ), NULL ); }

#else
void periph_test_start( void ) { }
#endif
