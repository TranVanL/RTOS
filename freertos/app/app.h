#ifndef APP_H
#define APP_H

void app_net_init( void );
void udp_echo_start( void );
void mpu_selftest_start( void );
void periph_test_start( void );  /* selected by -DPERIPH_TEST=<id> in CMake; no-op for none */

#endif
