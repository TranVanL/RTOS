cmake_minimum_required(VERSION 3.20)
project(m3_safety_host_tests LANGUAGES C)

include(CTest)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(COMMON_SAFETY_DIR "${REPO_ROOT}/common/safety")

add_library(m3_safety STATIC ${COMMON_SAFETY_DIR}/safety_fsm.c)
target_include_directories(m3_safety PUBLIC ${COMMON_SAFETY_DIR})

add_executable(test_safety_fsm test_safety_fsm.c)
target_link_libraries(test_safety_fsm PRIVATE m3_safety)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(m3_safety PRIVATE -Wall -Wextra -Werror)
    target_compile_options(test_safety_fsm PRIVATE -Wall -Wextra -Werror)
endif()

add_test(NAME m3_safety_fsm COMMAND test_safety_fsm)
