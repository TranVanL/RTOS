/* UDP echo on NODE_UDP_PORT: send every datagram back to its sender. Whole M0 application. */
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "app.h"

static volatile uint32_t ulRxCount, ulTxCount;

static void prvUdpEchoTask( void * pvParameters )
{
    static uint8_t ucBuf[ 1472 ];
    struct freertos_sockaddr xBind, xFrom;
    socklen_t xFromLen;
    Socket_t xSock;
    TickType_t xNextReport = xTaskGetTickCount() + pdMS_TO_TICKS( 5000 );

    ( void ) pvParameters;
    xSock = FreeRTOS_socket( FREERTOS_AF_INET, FREERTOS_SOCK_DGRAM, FREERTOS_IPPROTO_UDP );
    configASSERT( xSock != FREERTOS_INVALID_SOCKET );
    memset( &xBind, 0, sizeof( xBind ) );
    xBind.sin_family = FREERTOS_AF_INET;
    xBind.sin_port = FreeRTOS_htons( NODE_UDP_PORT );
    configASSERT( FreeRTOS_bind( xSock, &xBind, sizeof( xBind ) ) == 0 );
    { TickType_t xTo = pdMS_TO_TICKS( 1000 ); FreeRTOS_setsockopt( xSock, 0, FREERTOS_SO_RCVTIMEO, &xTo, sizeof( xTo ) ); }
    printf( "udp_echo listening on %d\n", NODE_UDP_PORT );

    for( ; ; )
    {
        xFromLen = sizeof( xFrom );
        int32_t n = FreeRTOS_recvfrom( xSock, ucBuf, sizeof( ucBuf ), 0, &xFrom, &xFromLen );
        if( n > 0 )
        {
            ulRxCount++;
            if( FreeRTOS_sendto( xSock, ucBuf, ( size_t ) n, 0, &xFrom, xFromLen ) == n ) { ulTxCount++; }
        }
        if( ( int32_t ) ( xTaskGetTickCount() - xNextReport ) >= 0 )
        {
            xNextReport += pdMS_TO_TICKS( 5000 );
            printf( "udp_echo rx=%lu tx=%lu\n", ( unsigned long ) ulRxCount, ( unsigned long ) ulTxCount );
        }
    }
}

void udp_echo_start( void )
{
    /* Privileged: sockets, queues and network buffers live in the kernel heap. */
    xTaskCreate( prvUdpEchoTask, "udp_echo", configMINIMAL_STACK_SIZE * 2, NULL,
                 ( tskIDLE_PRIORITY + 2 ) | portPRIVILEGE_BIT, NULL );
}
