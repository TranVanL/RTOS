#ifndef APP_H
#define APP_H

void app_net_init( void );
void udp_echo_start( void );
void udp_probe_start( void );
void mpu_selftest_start( void );
void control_task_start( void );    /* M1: 1 kHz Restricted control task + drain task */
void periph_test_start( void );  /* selected by -DPERIPH_TEST=<id> in CMake; no-op for none */

/* M4: integrated node. Comm + E-stop tasks are privileged; the Restricted control task consumes
 * their mailbox (see control_task.c). */
#include <stdint.h>
typedef struct
{
    uint32_t ulTick;
    int32_t lCount;       /* encoder counts */
    int32_t lVel_mrads;
    int32_t lCur_mA;
    uint32_t ulNodeInfo;  /* node_state | enable << 8 | cause << 16 */
    uint32_t ulMissed;
    uint32_t ulEstopPostTick;   /* control tick current when the E-stop task posted */
    uint32_t ulLastCmdTick;     /* control tick that consumed the last CMD */
    uint32_t ulNodeTick;        /* control tick of the last node state change */
} control_snapshot_t;

void control_post_cmd( uint8_t ucMode, int32_t lTarget, uint16_t usVelRpm );
void control_post_estop( void );
void control_get_snapshot( control_snapshot_t * pxOut );
void comm_task_start( void );
void estop_task_start( void );

#endif
