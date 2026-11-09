cmake_minimum_required(VERSION 3.20)
project(motion_node C ASM)

# Static addressing of the node (see docs/sil_environment.md section 3).
set(NODE_IP      "10,0,2,15"      CACHE STRING "Node IPv4, comma separated")
set(NODE_NETMASK "255,255,255,0"  CACHE STRING "Netmask, comma separated")
set(NODE_GATEWAY "10,0,2,2"       CACHE STRING "Gateway, comma separated")

set(KERNEL ${CMAKE_CURRENT_SOURCE_DIR}/third_party/FreeRTOS-Kernel)
set(TCP    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/FreeRTOS-Plus-TCP)
set(NI     ${TCP}/source/portable/NetworkInterface/MPS2_AN385)

file(GLOB TCP_SOURCES ${TCP}/source/*.c)

# Third-party code: built, but not held to the project's warning level.
add_library(freertos_third_party OBJECT
    ${KERNEL}/tasks.c ${KERNEL}/list.c ${KERNEL}/queue.c ${KERNEL}/timers.c
    ${KERNEL}/event_groups.c ${KERNEL}/stream_buffer.c
    ${KERNEL}/portable/GCC/ARM_CM3_MPU/port.c
    ${KERNEL}/portable/GCC/ARM_CM3_MPU/mpu_wrappers_v2_asm.c
    ${KERNEL}/portable/Common/mpu_wrappers.c
    ${KERNEL}/portable/Common/mpu_wrappers_v2.c
    ${KERNEL}/portable/MemMang/heap_4.c
    ${TCP_SOURCES}
    ${TCP}/source/portable/BufferManagement/BufferAllocation_2.c
    ${NI}/NetworkInterface.c
    ${NI}/ether_lan9118/smsc9220_eth_drv.c)

set(COMMON_INCLUDES
    app port port/CMSIS
    ${KERNEL}/include ${KERNEL}/portable/GCC/ARM_CM3_MPU
    ${TCP}/source/include ${TCP}/source/portable/Compiler/GCC
    ${NI}/ether_lan9118)
set(COMMON_FLAGS -mthumb -mcpu=cortex-m3 -Os -g -ffunction-sections -fdata-sections)

target_include_directories(freertos_third_party PUBLIC ${COMMON_INCLUDES})
target_compile_options(freertos_third_party PRIVATE ${COMMON_FLAGS} -w)

add_executable(node.elf
    port/startup.c port/syscall.c
    app/main.c app/hooks.c app/net.c app/udp_echo.c app/mpu_selftest.c
    $<TARGET_OBJECTS:freertos_third_party>)
target_include_directories(node.elf PRIVATE ${COMMON_INCLUDES})
target_compile_options(node.elf PRIVATE ${COMMON_FLAGS} -Wall -Wextra -Wno-unused-parameter)
foreach(v NODE_IP NODE_NETMASK NODE_GATEWAY)
    string(TOUPPER ${v} V)
endforeach()
target_compile_definitions(freertos_third_party PRIVATE NODE_IP_ADDR=${NODE_IP} NODE_NETMASK=${NODE_NETMASK} NODE_GATEWAY=${NODE_GATEWAY})
target_compile_definitions(node.elf PRIVATE NODE_IP_ADDR=${NODE_IP} NODE_NETMASK=${NODE_NETMASK} NODE_GATEWAY=${NODE_GATEWAY})
target_link_options(node.elf PRIVATE -mthumb -mcpu=cortex-m3 -nostartfiles --specs=nano.specs --specs=nosys.specs
    -T${CMAKE_CURRENT_SOURCE_DIR}/port/mps2_m3.ld -Wl,-Map=node.map -Wl,--gc-sections)
add_custom_command(TARGET node.elf POST_BUILD COMMAND arm-none-eabi-size node.elf)
