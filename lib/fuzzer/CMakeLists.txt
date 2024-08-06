if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CARGO_CMD cargo build)
    set(TARGET_DIR "debug")
else ()
    set(CARGO_CMD cargo build --release)
    set(TARGET_DIR "release")
endif ()

set(LibkleeFuzzer "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_DIR}/libkleeFuzzer.a")

add_custom_target(kleeFuzzerBuild ALL
    COMMENT "Compiling kleeFuzzer"
    COMMAND CARGO_TARGET_DIR=${CMAKE_CURRENT_BINARY_DIR} ${CARGO_CMD} 
    COMMAND cp ${LibkleeFuzzer} ${CMAKE_CURRENT_BINARY_DIR}/../
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

add_library(kleeFuzzer STATIC IMPORTED GLOBAL)
set_property(TARGET kleeFuzzer PROPERTY
             IMPORTED_LOCATION ${CMAKE_CURRENT_BINARY_DIR}/../libkleeFuzzer.a)

add_dependencies(kleeFuzzer kleeFuzzerBuild)
