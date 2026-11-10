/* Network bring-up: single LAN9118 interface, one static IPv4 end-point. */
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "NetworkInterface.h"
#include "app.h"
#include "CMSDK_CM3.h"

static const uint8_t ucIPAddress[ 4 ]  = { NODE_IP_ADDR };
static const uint8_t ucNetMask[ 4 ]    = { NODE_NETMASK };
static const uint8_t ucGateway[ 4 ]    = { NODE_GATEWAY };
static const uint8_t ucDNSServer[ 4 ]  = { NODE_GATEWAY };
static const uint8_t ucMACAddress[ 6 ] = { NODE_MAC_ADDR };

static NetworkInterface_t xInterface;
static NetworkEndPoint_t xEndPoint;

extern NetworkInterface_t * pxMPS2_FillInterfaceDescriptor( BaseType_t xEMACIndex, NetworkInterface_t * pxInterface );

void app_net_init( void )
{
    NVIC_SetPriority( ETHERNET_IRQn, configMAC_INTERRUPT_PRIORITY );
    pxMPS2_FillInterfaceDescriptor( 0, &xInterface );
    FreeRTOS_FillEndPoint( &xInterface, &xEndPoint, ucIPAddress, ucNetMask, ucGateway, ucDNSServer, ucMACAddress );
    configASSERT( FreeRTOS_IPInit_Multi() == pdTRUE );
}

void vApplicationIPNetworkEventHook_Multi( eIPCallbackEvent_t eNetworkEvent, struct xNetworkEndPoint * pxEndPoint )
{
    static BaseType_t xStarted = pdFALSE;
    ( void ) pxEndPoint;

    if( ( eNetworkEvent == eNetworkUp ) && ( xStarted == pdFALSE ) )
    {
        xStarted = pdTRUE;
        printf( "network up, ip %u.%u.%u.%u\n", ucIPAddress[ 0 ], ucIPAddress[ 1 ], ucIPAddress[ 2 ], ucIPAddress[ 3 ] );
        udp_echo_start();
#ifdef NODE_PROBE
        udp_probe_start();
#endif
    }
}

/* Plus-TCP random number hooks. Not cryptographic: SIL only. */
static uint32_t ulRandState = 0x1234ABCDUL;
static uint32_t prvRand( void )
{
    ulRandState = ( ulRandState * 1664525UL ) + 1013904223UL;
    return ulRandState;
}

BaseType_t xApplicationGetRandomNumber( uint32_t * pulNumber ) { *pulNumber = prvRand(); return pdTRUE; }

uint32_t ulApplicationGetNextSequenceNumber( uint32_t a, uint16_t b, uint32_t c, uint16_t d )
{
    ( void ) a; ( void ) b; ( void ) c; ( void ) d;
    return prvRand();
}
