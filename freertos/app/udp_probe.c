/* M0 direction node -> QNX: send numbered datagrams to PEER (QNX) on NODE_PROBE_PORT every 10 ticks;
 * the peer echoes them back. Node counts sent/echoed; the peer counts gaps in seq (its own PASS criterion). */
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "app.h"

#define PROBE_MAGIC 0x4D305052UL /* "M0PR" */
#define prvQuick( ... ) FreeRTOS_inet_addr_quick( __VA_ARGS__ ) /* expands NODE_PEER_IP_ADDR (a,b,c,d) first */

static void prvProbeTask( void * pvParameters )
{
    struct freertos_sockaddr xPeer, xBind, xFrom;
    socklen_t xFromLen;
    uint32_t ulPkt[ 8 ], ulSent = 0, ulEcho = 0;
    TickType_t xWake = xTaskGetTickCount(), xNextReport = xWake + pdMS_TO_TICKS( 5000 );
    TickType_t xNoWait = 0;
    Socket_t xSock = FreeRTOS_socket( FREERTOS_AF_INET, FREERTOS_SOCK_DGRAM, FREERTOS_IPPROTO_UDP );

    ( void ) pvParameters;
    configASSERT( xSock != FREERTOS_INVALID_SOCKET );
    memset( &xBind, 0, sizeof( xBind ) );
    xBind.sin_family = FREERTOS_AF_INET;
    xBind.sin_port = FreeRTOS_htons( NODE_PROBE_PORT );
    configASSERT( FreeRTOS_bind( xSock, &xBind, sizeof( xBind ) ) == 0 );
    FreeRTOS_setsockopt( xSock, 0, FREERTOS_SO_RCVTIMEO, &xNoWait, sizeof( xNoWait ) );
    memset( &xPeer, 0, sizeof( xPeer ) );
    xPeer.sin_family = FREERTOS_AF_INET;
    xPeer.sin_port = FreeRTOS_htons( NODE_PROBE_PORT );
    xPeer.sin_address.ulIP_IPv4 = prvQuick( NODE_PEER_IP_ADDR );
    printf( "udp_probe -> peer port %d\n", NODE_PROBE_PORT );

    for( ; ; )
    {
        memset( ulPkt, 0, sizeof( ulPkt ) );
        ulPkt[ 0 ] = PROBE_MAGIC;
        ulPkt[ 1 ] = ulSent;
        if( FreeRTOS_sendto( xSock, ulPkt, sizeof( ulPkt ), 0, &xPeer, sizeof( xPeer ) ) == ( int32_t ) sizeof( ulPkt ) ) { ulSent++; }
        for( ; ; )
        {
            xFromLen = sizeof( xFrom );
            int32_t n = FreeRTOS_recvfrom( xSock, ulPkt, sizeof( ulPkt ), 0, &xFrom, &xFromLen );
            if( n <= 0 ) { break; }
            if( ulPkt[ 0 ] == PROBE_MAGIC ) { ulEcho++; }
        }
        if( ( int32_t ) ( xTaskGetTickCount() - xNextReport ) >= 0 )
        {
            xNextReport += pdMS_TO_TICKS( 5000 );
            printf( "udp_probe sent=%lu echoed=%lu\n", ( unsigned long ) ulSent, ( unsigned long ) ulEcho );
        }
        vTaskDelayUntil( &xWake, pdMS_TO_TICKS( 10 ) );
    }
}

void udp_probe_start( void )
{
    xTaskCreate( prvProbeTask, "udp_probe", configMINIMAL_STACK_SIZE * 2, NULL,
                 ( tskIDLE_PRIORITY + 2 ) | portPRIVILEGE_BIT, NULL );
}
