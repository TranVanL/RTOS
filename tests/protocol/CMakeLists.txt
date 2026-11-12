cmake_minimum_required(VERSION 3.20)
project(m2_protocol_host_tests LANGUAGES C)

include(CTest)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(COMMON_FRAME_DIR "${REPO_ROOT}/common/frame")
set(COMMON_E2E_DIR "${REPO_ROOT}/common/e2e")
set(COMMON_GENERATED_DIR "${REPO_ROOT}/common/generated")

add_library(m2_protocol STATIC
    ${COMMON_FRAME_DIR}/can_frame.c
    ${COMMON_E2E_DIR}/crc32p4.c
    ${COMMON_E2E_DIR}/e2e.c
    ${COMMON_GENERATED_DIR}/can_matrix.c
    ${COMMON_GENERATED_DIR}/can_matrix_meta.c
)
target_include_directories(m2_protocol PUBLIC
    ${COMMON_FRAME_DIR}
    ${COMMON_E2E_DIR}
    ${COMMON_GENERATED_DIR}
)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(m2_protocol PRIVATE -Wall -Wextra -Werror)
endif()

add_executable(test_can_frame test_can_frame.c)
target_link_libraries(test_can_frame PRIVATE m2_protocol)

add_executable(test_e2e test_e2e.c)
target_link_libraries(test_e2e PRIVATE m2_protocol)

add_executable(test_dbc_generated test_dbc_generated.c)
target_link_libraries(test_dbc_generated PRIVATE m2_protocol)

add_executable(protocol_cli protocol_cli.c)
target_link_libraries(protocol_cli PRIVATE m2_protocol)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(test_can_frame PRIVATE -Wall -Wextra -Werror)
    target_compile_options(test_e2e PRIVATE -Wall -Wextra -Werror)
    target_compile_options(test_dbc_generated PRIVATE -Wall -Wextra -Werror)
    target_compile_options(protocol_cli PRIVATE -Wall -Wextra -Werror)
endif()

add_test(NAME m2_can_frame COMMAND test_can_frame)
add_test(NAME m2_e2e COMMAND test_e2e)
add_test(NAME m2_dbc_generated COMMAND test_dbc_generated)
