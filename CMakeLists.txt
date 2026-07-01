cmake_minimum_required(VERSION 3.16)

project(mod_ble_host_demo C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wpedantic)

set(PROJECT_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)

set(MOD_BLE_HEADERS
    ${PROJECT_INCLUDE_DIR}/mod_ble_types.h
    ${PROJECT_INCLUDE_DIR}/mod_ble_log.h
    ${PROJECT_INCLUDE_DIR}/ble_link.h
    ${PROJECT_INCLUDE_DIR}/protocol.h
    ${PROJECT_INCLUDE_DIR}/demo_cli.h
    ${PROJECT_INCLUDE_DIR}/mod_ble.h
)

set(MOD_BLE_SOURCES
    ${PROJECT_SOURCE_DIR}/src/mod_ble_log.c
    ${PROJECT_SOURCE_DIR}/src/ble_link.c
    ${PROJECT_SOURCE_DIR}/src/protocol.c
    ${PROJECT_SOURCE_DIR}/src/mod_ble.c
    ${PROJECT_SOURCE_DIR}/src/demo_cli.c
)

add_library(mod_ble STATIC ${MOD_BLE_HEADERS} ${MOD_BLE_SOURCES})
target_include_directories(mod_ble PUBLIC ${PROJECT_INCLUDE_DIR})

if(UNIX AND NOT WIN32)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(GLIB2 QUIET glib-2.0 gio-2.0)
    endif()

    if(GLIB2_FOUND)
        target_compile_definitions(mod_ble PRIVATE MOD_BLE_HAVE_GIO=1)
        target_include_directories(mod_ble PRIVATE ${GLIB2_INCLUDE_DIRS})
        target_link_directories(mod_ble PRIVATE ${GLIB2_LIBRARY_DIRS})
        target_link_libraries(mod_ble PRIVATE ${GLIB2_LIBRARIES})
        target_compile_options(mod_ble PRIVATE ${GLIB2_CFLAGS_OTHER})
    else()
        message(WARNING "glib-2.0/gio-2.0 not found; mod_ble will build with the stub transport")
    endif()
endif()

add_executable(mod_ble_demo ${PROJECT_SOURCE_DIR}/src/main.c)
target_link_libraries(mod_ble_demo PRIVATE mod_ble)
