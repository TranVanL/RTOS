/* M0 firmware: ARM_CM3_MPU + FreeRTOS+TCP on QEMU mps2-an385.
 * Application = UDP echo (port NODE_UDP_PORT) + MPU self-test. */
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "app.h"

int main( void )
{
    setvbuf( stdout, NULL, _IONBF, 0 ); /* _isatty() is 0 -> newlib would buffer fully */
    printf( "\nM0 node boot: FreeRTOS %s, MPU port, FreeRTOS+TCP\n", tskKERNEL_VERSION_NUMBER );
    app_net_init();      /* creates the IP task; udp_echo is started from the network-up hook */
    mpu_selftest_start();
    periph_test_start();
    vTaskStartScheduler();
    printf( "vTaskStartScheduler returned\n" );
    for( ; ; ) { }
    return 0;
}
