cmake_minimum_required(VERSION 3.20)
project(m3_safety_host_tests LANGUAGES C)

include(CTest)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(COMMON_SAFETY_DIR "${REPO_ROOT}/common/safety")

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_library(m3_safety STATIC
    ${COMMON_SAFETY_DIR}/safety_fsm.c
    ${REPO_ROOT}/common/shm/motion_shm.c
    ${REPO_ROOT}/common/ipc/motion_ipc.c
)
target_include_directories(m3_safety PUBLIC
    ${COMMON_SAFETY_DIR} ${REPO_ROOT}/common/shm ${REPO_ROOT}/common/ipc)

foreach(t test_safety_fsm test_motion_shm test_motion_ipc)
    add_executable(${t} ${t}.c)
    target_link_libraries(${t} PRIVATE m3_safety Threads::Threads)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${t} PRIVATE -Wall -Wextra -Werror)
    endif()
endforeach()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(m3_safety PRIVATE -Wall -Wextra -Werror)
endif()

add_test(NAME m3_safety_fsm COMMAND test_safety_fsm)
add_test(NAME m3_motion_shm COMMAND test_motion_shm)
add_test(NAME m3_motion_ipc COMMAND test_motion_ipc)
