/* Kernel / Plus-TCP callbacks required by the configuration. */
#include <stdarg.h>
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_IP.h"

void vAssertCalled( void )
{
    volatile unsigned long ulLoop = 0;

    taskENTER_CRITICAL();
    while( ulLoop == 0UL ) { portNOP(); }
    taskEXIT_CRITICAL();
}

volatile uint32_t ulIdleCount; /* work done while idle; used by the -icount test */
void vApplicationIdleHook( void ) { ulIdleCount++; }
void vApplicationTickHook( void ) { }

void vApplicationStackOverflowHook( TaskHandle_t pxTask, char * pcTaskName )
{
    ( void ) pxTask;
    printf( "Stack overflow: %s\n", pcTaskName );
    for( ; ; ) { }
}

void vApplicationMallocFailedHook( void )
{
    printf( "Malloc failed\n" );
    for( ; ; ) { }
}

void vLoggingPrintf( const char * pcFormat, ... )
{
    va_list arg;
    va_start( arg, pcFormat );
    vprintf( pcFormat, arg );
    va_end( arg );
}

unsigned long ulGetRunTimeCounterValue( void ) { return 0; }

void vApplicationGetIdleTaskMemory( StaticTask_t ** ppxTCB, StackType_t ** ppxStack, uint32_t * pulSize )
{
    static StaticTask_t xTCB;
    static StackType_t uxStack[ configMINIMAL_STACK_SIZE ] __attribute__( ( aligned( configMINIMAL_STACK_SIZE * sizeof( StackType_t ) ) ) );
    *ppxTCB = &xTCB; *ppxStack = uxStack; *pulSize = configMINIMAL_STACK_SIZE;
}

void vApplicationGetTimerTaskMemory( StaticTask_t ** ppxTCB, StackType_t ** ppxStack, uint32_t * pulSize )
{
    static StaticTask_t xTCB;
    static StackType_t uxStack[ configTIMER_TASK_STACK_DEPTH ] __attribute__( ( aligned( configTIMER_TASK_STACK_DEPTH * sizeof( StackType_t ) ) ) );
    *ppxTCB = &xTCB; *ppxStack = uxStack; *pulSize = configTIMER_TASK_STACK_DEPTH;
}
