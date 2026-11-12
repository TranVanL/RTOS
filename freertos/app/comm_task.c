/* M4 node communication: CMD in, TELEM + HEARTBEAT out (UDP 5000) and the E-stop channel (UDP 5001).
 *
 * Both tasks are privileged (sockets/queues live in the kernel heap; project risk table: "IP task
 * privileged, isolate only control/comm tasks" fallback). They never touch plant state directly:
 * they exchange data with the Restricted control task only through the mailbox/snapshot API in
 * control_task.c.
 *
 *   comm   : priority 5, polls every tick (1 ms); sends TELEM + HB every 10 ticks, to the peer
 *            (NODE_PEER_IP_ADDR : 5000), starting at network-up (no CMD needed: QNX needs the
 *            first heartbeat to leave INIT).
 *   e-stop : priority configMAX_PRIORITIES - 1 (highest), blocks on its own socket (5001).
 *
 * All counters are ticks/frames. Times printed are FreeRTOS ticks (QEMU virtual time with -icount,
 * otherwise host-paced): SIL, not hard real-time.
 */
#include <stdio.h>
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "app.h"
#include "can_matrix.h"
#include "node_fsm.h"
#include "proto_link.h"

#define prvQuick( ... ) FreeRTOS_inet_addr_quick( __VA_ARGS__ ) /* expands NODE_PEER_IP_ADDR (a,b,c,d) first */
#define COMM_PERIOD_TICKS      10U
#define CMD_RESYNC_AFTER       5U     /* consecutive WRONG_SEQ CMDs before a (disabled-output) resync */
#define REPORT_PERIOD_TICKS    5000U

static volatile uint32_t ulCmdOk, ulCmdBad, ulCmdResync, ulTelemTx, ulHbTx, ulEstopOk, ulEstopBad;

static Socket_t prvBind( uint16_t usPort )
{
    struct freertos_sockaddr xBind;
    Socket_t xSock = FreeRTOS_socket( FREERTOS_AF_INET, FREERTOS_SOCK_DGRAM, FREERTOS_IPPROTO_UDP );

    configASSERT( xSock != FREERTOS_INVALID_SOCKET );
    memset( &xBind, 0, sizeof( xBind ) );
    xBind.sin_family = FREERTOS_AF_INET;
    xBind.sin_port = FreeRTOS_htons( usPort );
    configASSERT( FreeRTOS_bind( xSock, &xBind, sizeof( xBind ) ) == 0 );
    return xSock;
}

static void prvSend( Socket_t xSock, const struct freertos_sockaddr * pxPeer, uint16_t usId, const uint8_t * pucPayload,
                     size_t xLen, uint16_t * pusCounter )
{
    uint8_t ucDg[ PROTO_DATAGRAM_MAX ];
    size_t xN = 0;

    if( proto_encode( usId, pucPayload, xLen, ( *pusCounter )++, ucDg, sizeof( ucDg ), &xN ) == PROTO_OK )
    {
        ( void ) FreeRTOS_sendto( xSock, ucDg, xN, 0, pxPeer, sizeof( *pxPeer ) );
    }
}

static void prvCommTask( void * pvParameters )
{
    struct freertos_sockaddr xPeer, xFrom;
    socklen_t xFromLen;
    e2e_rx_state_t xCmdRx;
    uint16_t usTelemCtr = 0, usHbCtr = 0;
    uint8_t ucAlive = 0, ucPrevState = 0xFF;
    uint32_t ulWrongSeqRun = 0;
    TickType_t xNoWait = 0, xWake = xTaskGetTickCount();
    Socket_t xSock = prvBind( NODE_UDP_PORT );

    ( void ) pvParameters;
    FreeRTOS_setsockopt( xSock, 0, FREERTOS_SO_RCVTIMEO, &xNoWait, sizeof( xNoWait ) );
    memset( &xPeer, 0, sizeof( xPeer ) );
    xPeer.sin_family = FREERTOS_AF_INET;
    xPeer.sin_port = FreeRTOS_htons( NODE_UDP_PORT );
    xPeer.sin_address.ulIP_IPv4 = prvQuick( NODE_PEER_IP_ADDR );
    e2e_rx_reset( &xCmdRx, xTaskGetTickCount() );
    printf( "comm: UDP %d (CMD in, TELEM/HB out)\n", NODE_UDP_PORT );

    for( ; ; )
    {
        uint8_t ucDg[ 128 ], ucPl[ PROTO_PAYLOAD_MAX ];
        int32_t lN;
        control_snapshot_t xSnap;
        const uint32_t ulNow = xTaskGetTickCount();

        control_get_snapshot( &xSnap );

        for( ; ; )
        {
            uint16_t usId = 0;
            size_t xPlLen = 0;

            xFromLen = sizeof( xFrom );
            lN = FreeRTOS_recvfrom( xSock, ucDg, sizeof( ucDg ), 0, &xFrom, &xFromLen );
            if( lN <= 0 ) { break; }
            ( void ) xFromLen;
            if( !proto_peek_can_id( ucDg, ( size_t ) lN, &usId ) || usId != 0x100 ) { ulCmdBad++; continue; }
            const proto_result_t xR = proto_decode( ucDg, ( size_t ) lN, &xCmdRx, ulNow, &usId, ucPl, &xPlLen );
            if( xR == PROTO_OK )
            {
                struct can_matrix_cmd_t xCmd;
                ulWrongSeqRun = 0;
                if( can_matrix_cmd_unpack( &xCmd, ucPl, xPlLen ) == 0 )
                {
                    control_post_cmd( xCmd.mode, xCmd.target_pos, xCmd.vel_limit );
                    ulCmdOk++;
                }
                else { ulCmdBad++; }
            }
            else
            {
                ulCmdBad++;
                /* Resync policy: a peer restart (counter back to 0) must not lock the node out forever.
                 * Only allowed while the output is disabled; in RUN/DEGRADED the node times out instead. */
                const uint32_t ulSt = xSnap.ulNodeInfo & 0xFFU;
                if( xR == PROTO_ERR_E2E_SEQ && ulSt != NODE_RUN && ulSt != NODE_DEGRADED )
                {
                    if( ++ulWrongSeqRun >= CMD_RESYNC_AFTER )
                    {
                        e2e_rx_reset( &xCmdRx, ulNow );
                        ulWrongSeqRun = 0;
                        ulCmdResync++;
                    }
                }
            }
        }

        if( ( ulNow % COMM_PERIOD_TICKS ) == 0U )
        {
            struct can_matrix_telem_t xT = { 0 };
            struct can_matrix_heartbeat_t xH = { 0 };
            uint8_t ucRaw[ 16 ];
            int32_t lRpm = ( xSnap.lVel_mrads * 60 ) / 6283;      /* mrad/s -> rpm */
            int32_t lCur = xSnap.lCur_mA;

            xT.position = xSnap.lCount;
            xT.velocity = ( int16_t ) ( lRpm > 32767 ? 32767 : ( lRpm < -32768 ? -32768 : lRpm ) );
            xT.current = ( int16_t ) ( lCur > 32767 ? 32767 : ( lCur < -32768 ? -32768 : lCur ) );
            xT.output_state = ( xSnap.ulNodeInfo >> 8 ) & 1U;
            xT.cmd_echo = xCmdRx.has_last_counter ? xCmdRx.last_counter : 0U;
            ( void ) can_matrix_telem_pack( ucRaw, &xT, sizeof( ucRaw ) );
            prvSend( xSock, &xPeer, 0x180, ucRaw, CAN_MATRIX_TELEM_LENGTH, &usTelemCtr );
            ulTelemTx++;

            xH.node_state = ( uint8_t ) ( xSnap.ulNodeInfo & 0xFFU );
            xH.alive_counter = ucAlive++;
            xH.cpu_load = 0;                    /* not measured in M4 (idle-hook load is M5/M6 scope) */
            xH.fault_code = 0;                  /* reserved for real faults (watchdog/MPU): M5 */
            ( void ) can_matrix_heartbeat_pack( ucRaw, &xH, sizeof( ucRaw ) );
            prvSend( xSock, &xPeer, 0x1F0, ucRaw, CAN_MATRIX_HEARTBEAT_LENGTH, &usHbCtr );
            ulHbTx++;
        }

        if( ( xSnap.ulNodeInfo & 0xFFU ) != ucPrevState )
        {
            ucPrevState = ( uint8_t ) ( xSnap.ulNodeInfo & 0xFFU );
            /* ctl_tick = control tick of the transition (g_shared.ulNodeTicks would be exact; the snapshot tick can be
             * later by the comm poll delay, so the transition tick is printed separately). */
            printf( "NODE_STATE %s ctl_tick=%lu cause=%lu output=%lu last_cmd_tick=%lu estop_post_tick=%lu\n",
                    node_state_name( ( node_state_t ) ucPrevState ), ( unsigned long ) xSnap.ulNodeTick,
                    ( unsigned long ) ( xSnap.ulNodeInfo >> 16 ), ( unsigned long ) ( ( xSnap.ulNodeInfo >> 8 ) & 1U ),
                    ( unsigned long ) xSnap.ulLastCmdTick, ( unsigned long ) xSnap.ulEstopPostTick );
        }
        if( ( ulNow % REPORT_PERIOD_TICKS ) == 0U )
        {
            printf( "NODE_STATS ctl_tick=%lu missed=%lu cmd_ok=%lu cmd_bad=%lu resync=%lu telem_tx=%lu hb_tx=%lu estop_ok=%lu estop_bad=%lu\n",
                    ( unsigned long ) xSnap.ulTick, ( unsigned long ) xSnap.ulMissed, ( unsigned long ) ulCmdOk,
                    ( unsigned long ) ulCmdBad, ( unsigned long ) ulCmdResync, ( unsigned long ) ulTelemTx,
                    ( unsigned long ) ulHbTx, ( unsigned long ) ulEstopOk, ( unsigned long ) ulEstopBad );
        }
        vTaskDelayUntil( &xWake, 1 );
    }
}

static void prvEstopTask( void * pvParameters )
{
    struct freertos_sockaddr xFrom;
    socklen_t xFromLen;
    e2e_rx_state_t xRx;
    TickType_t xTo = pdMS_TO_TICKS( 1000 );
    Socket_t xSock = prvBind( NODE_ESTOP_PORT );

    ( void ) pvParameters;
    FreeRTOS_setsockopt( xSock, 0, FREERTOS_SO_RCVTIMEO, &xTo, sizeof( xTo ) );
    e2e_rx_reset( &xRx, xTaskGetTickCount() );
    printf( "estop: UDP %d\n", NODE_ESTOP_PORT );

    for( ; ; )
    {
        uint8_t ucDg[ 128 ], ucPl[ PROTO_PAYLOAD_MAX ];
        uint16_t usId = 0;
        size_t xPlLen = 0;

        xFromLen = sizeof( xFrom );
        const int32_t lN = FreeRTOS_recvfrom( xSock, ucDg, sizeof( ucDg ), 0, &xFrom, &xFromLen );
        if( lN <= 0 ) { continue; }
        uint16_t usPeek = 0;
        const bool bPeek = proto_peek_can_id( ucDg, ( size_t ) lN, &usPeek ) && ( usPeek == 0x010 );
        const proto_result_t xR = proto_decode( ucDg, ( size_t ) lN, &xRx, xTaskGetTickCount(), &usId, ucPl, &xPlLen );
        /* Fail-safe bias: the E-stop action is idempotent and safe, so a frame that passed length/ID/CRC but
         * has a stale or restarted counter (REPEATED / WRONG_SEQ, reported as PROTO_ERR_E2E_SEQ after the CRC
         * check) is still honoured and re-baselines the counter. CRC/length/ID errors are rejected. */
        if( bPeek && ( xR == PROTO_ERR_E2E_SEQ ) )
        {
            control_post_estop();
            ulEstopOk++;
            e2e_rx_reset( &xRx, xTaskGetTickCount() );
        }
        else if( bPeek && ( xR == PROTO_OK ) )
        {
            struct can_matrix_estop_t xE;
            if( ( can_matrix_estop_unpack( &xE, ucPl, xPlLen ) == 0 ) && ( xE.command == 1U ) )
            {
                control_post_estop();
                ulEstopOk++;
            }
            else { ulEstopBad++; }
        }
        else
        {
            ulEstopBad++;
        }
    }
}

void comm_task_start( void )
{
    xTaskCreate( prvCommTask, "comm", configMINIMAL_STACK_SIZE * 6, NULL, ( tskIDLE_PRIORITY + 5 ) | portPRIVILEGE_BIT, NULL );
}

void estop_task_start( void )
{
    xTaskCreate( prvEstopTask, "estop", configMINIMAL_STACK_SIZE * 3, NULL, ( configMAX_PRIORITIES - 1 ) | portPRIVILEGE_BIT, NULL );
}
