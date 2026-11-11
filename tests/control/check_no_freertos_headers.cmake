if(NOT DEFINED CORE_DIR OR NOT IS_DIRECTORY "${CORE_DIR}")
    message(FATAL_ERROR "CORE_DIR must name common/control")
endif()

file(GLOB_RECURSE core_files
    LIST_DIRECTORIES FALSE
    "${CORE_DIR}/*.c"
    "${CORE_DIR}/*.h"
)

if(NOT core_files)
    message(STATUS
        "common/control is empty; future source/header files will be checked automatically"
    )
    return()
endif()

set(freertos_include_pattern
    "#[ \t]*include[ \t]*[<\"][^>\"]*(freertos\\.h|task\\.h|queue\\.h|semphr\\.h|timers\\.h|event_groups\\.h|stream_buffer\\.h|message_buffer\\.h|list\\.h|portable\\.h|projdefs\\.h|mpu_wrappers[^>\"]*|stackmacros\\.h)[>\"]"
)

foreach(source IN LISTS core_files)
    file(READ "${source}" contents)
    string(TOLOWER "${contents}" contents_lower)

    if(contents_lower MATCHES "${freertos_include_pattern}")
        message(FATAL_ERROR
            "FreeRTOS header dependency found in OS-free core: ${source}"
        )
    endif()
endforeach()

message(STATUS
    "Checked ${core_files} control-core file(s): no FreeRTOS headers found"
)