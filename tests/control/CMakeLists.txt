cmake_minimum_required(VERSION 3.20)
project(m1_control_host_tests LANGUAGES C)

include(CTest)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(test_control_smoke
    test_control_smoke.c
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_control_smoke PRIVATE
        -Wall
        -Wextra
        -Werror
    )
endif()

add_test(
    NAME control_host_smoke
    COMMAND test_control_smoke
)

get_filename_component(
    CONTROL_CORE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../common/control"
    ABSOLUTE
)

add_library(control_core STATIC
    ${CONTROL_CORE_DIR}/plant.c
    ${CONTROL_CORE_DIR}/encoder.c
    ${CONTROL_CORE_DIR}/limits.c
    ${CONTROL_CORE_DIR}/pid.c
)
target_include_directories(control_core PUBLIC ${CONTROL_CORE_DIR})
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(control_core PRIVATE
        -Wall
        -Wextra
        -Werror
    )
endif()

add_executable(test_plant
    test_plant.c
)
target_link_libraries(test_plant PRIVATE control_core)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_plant PRIVATE
        -Wall
        -Wextra
        -Werror
    )
    target_link_libraries(test_plant PRIVATE m)
endif()

add_test(
    NAME control_plant
    COMMAND test_plant
)

add_executable(test_encoder
    test_encoder.c
)
target_link_libraries(test_encoder PRIVATE control_core)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_encoder PRIVATE
        -Wall
        -Wextra
        -Werror
    )
    target_link_libraries(test_encoder PRIVATE m)
endif()

add_test(
    NAME control_encoder
    COMMAND test_encoder
)

add_library(sim_loop STATIC sim_loop.c)
target_link_libraries(sim_loop PUBLIC control_core m)
target_include_directories(sim_loop PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_compile_options(sim_loop PRIVATE -Wall -Wextra -Werror)

add_executable(sim_closed_loop sim_closed_loop.c)
target_link_libraries(sim_closed_loop PRIVATE sim_loop)
target_compile_options(sim_closed_loop PRIVATE -Wall -Wextra -Werror)

add_executable(test_pid
    test_pid.c
)
target_link_libraries(test_pid PRIVATE control_core)
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_pid PRIVATE -Wall -Wextra -Werror)
    target_link_libraries(test_pid PRIVATE m)
endif()

add_test(
    NAME control_pid
    COMMAND test_pid
)

add_executable(test_step_response test_step_response.c)
target_link_libraries(test_step_response PRIVATE sim_loop)
target_compile_options(test_step_response PRIVATE -Wall -Wextra -Werror)
add_test(NAME control_step_response COMMAND test_step_response)

add_test(
    NAME control_core_no_freertos_headers
    COMMAND "${CMAKE_COMMAND}"
        "-DCORE_DIR=${CONTROL_CORE_DIR}"
        -P "${CMAKE_CURRENT_LIST_DIR}/check_no_freertos_headers.cmake"
)

set_tests_properties(
    control_core_no_freertos_headers
    PROPERTIES LABELS "architecture"
)