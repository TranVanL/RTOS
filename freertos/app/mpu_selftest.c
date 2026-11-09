/* MPU self-test: an UNPRIVILEGED task writes to a region it only may read.
 * Expected: MemManage fault (DACCVIOL), recorded here, execution continues.
 * Any other fault is not hidden: it stops in an infinite loop. */
#include <stdio.h>
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include "app.h"

#define REGION_SIZE 32

#define SCB_CFSR ( *( volatile uint32_t * ) 0xE000ED28UL )
#define SCB_MMFAR ( *( volatile uint32_t * ) 0xE000ED34UL )

static uint8_t ucProtected[ REGION_SIZE ] __attribute__( ( aligned( REGION_SIZE ) ) );
static volatile uint8_t ucExpected[ REGION_SIZE ] __attribute__( ( aligned( REGION_SIZE ) ) );

/* Written only by the (privileged) handler, read by the reporter task. */
static volatile uint32_t ulFaultCount, ulLastCFSR, ulLastMMFAR, ulLastPC;

void vHandleMemoryFault( uint32_t * pulFrame ) __attribute__( ( used ) );
void vHandleMemoryFault( uint32_t * pulFrame )
{
    if( ucExpected[ 0 ] == 1 )
    {
        uint32_t ulPC = pulFrame[ 6 ];
        uint16_t usHalf = *( uint16_t * ) ulPC;

        ulLastCFSR = SCB_CFSR;
        ulLastMMFAR = SCB_MMFAR;
        ulLastPC = ulPC;
        ulFaultCount++;
        SCB_CFSR = ulLastCFSR;                 /* write-1-to-clear */
        ulPC += ( ( ( usHalf >> 11 ) >= 0x1D ) ? 4 : 2 ); /* 32-bit Thumb-2 prefixes 0b11101/11110/11111 */
        pulFrame[ 6 ] = ulPC;
        ucExpected[ 0 ] = 0;
    }
    else
    {
        for( ; ; ) { }
    }
}

static StackType_t xTestStack[ configMINIMAL_STACK_SIZE ] __attribute__( ( aligned( configMINIMAL_STACK_SIZE * sizeof( StackType_t ) ) ) );

static void prvViolator( void * pv )
{
    ( void ) pv;
    for( ; ; )
    {
        ucExpected[ 0 ] = 1;
        ucProtected[ 0 ] = 0xAA;               /* RO for this task -> MemManage */
        vTaskDelay( pdMS_TO_TICKS( 1000 ) );
    }
}

static void prvReporter( void * pv )
{
    ( void ) pv;
    for( ; ; )
    {
        vTaskDelay( pdMS_TO_TICKS( 5000 ) );
        printf( "mpu_test faults=%lu CFSR=0x%08lx MMFAR=0x%08lx PC=0x%08lx (protected@0x%08lx)\n",
                ( unsigned long ) ulFaultCount, ( unsigned long ) ulLastCFSR, ( unsigned long ) ulLastMMFAR,
                ( unsigned long ) ulLastPC, ( unsigned long ) ucProtected );
    }
}

void mpu_selftest_start( void )
{
    TaskParameters_t xParams =
    {
        .pvTaskCode = prvViolator, .pcName = "mpu_viol", .usStackDepth = configMINIMAL_STACK_SIZE,
        .pvParameters = NULL, .uxPriority = tskIDLE_PRIORITY + 1, .puxStackBuffer = xTestStack,
        .xRegions =
        {
            { ucProtected,                     REGION_SIZE, portMPU_REGION_PRIVILEGED_READ_WRITE_UNPRIV_READ_ONLY | portMPU_REGION_EXECUTE_NEVER },
            { ( void * ) ucExpected,           REGION_SIZE, portMPU_REGION_READ_WRITE | portMPU_REGION_EXECUTE_NEVER },
            { 0, 0, 0 }
        }
    };

    xTaskCreateRestricted( &xParams, NULL );
    xTaskCreate( prvReporter, "mpu_rep", configMINIMAL_STACK_SIZE, NULL, ( tskIDLE_PRIORITY + 1 ) | portPRIVILEGE_BIT, NULL );
}
