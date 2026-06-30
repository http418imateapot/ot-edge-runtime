cmake_minimum_required(VERSION 3.12)
project(robust-binary-config
    VERSION 1.0.0
    DESCRIPTION "Lightweight embedded Key-Value config store for Linux edge devices"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# ------------------------------------------------------------------ #
# Options                                                             #
# ------------------------------------------------------------------ #

option(BUILD_TESTING    "Build test executables"   ON)
option(BUILD_SHARED_LIBS "Build shared library"    OFF)

# ------------------------------------------------------------------ #
# Library: librobustcfg                                               #
# ------------------------------------------------------------------ #

add_library(robustcfg src/robust_cfg.c)

target_include_directories(robustcfg
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(robustcfg PRIVATE -Wall -Wextra -Wpedantic)

find_package(Threads REQUIRED)
target_link_libraries(robustcfg PUBLIC Threads::Threads)

set_target_properties(robustcfg PROPERTIES
    VERSION   ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    PUBLIC_HEADER include/robust_cfg.h
)

# ------------------------------------------------------------------ #
# CLI tool: robust_cfg_tool                                           #
# ------------------------------------------------------------------ #

add_executable(robust_cfg_tool src/cli.c)
target_link_libraries(robust_cfg_tool PRIVATE robustcfg)
target_compile_options(robust_cfg_tool PRIVATE -Wall -Wextra -Wpedantic)

# ------------------------------------------------------------------ #
# Tests                                                               #
# ------------------------------------------------------------------ #

if(BUILD_TESTING)
    enable_testing()

    set(_tests test_read_write test_concurrent test_fault_inject)

    foreach(_t IN LISTS _tests)
        add_executable(${_t} tests/${_t}.c)
        target_link_libraries(${_t} PRIVATE robustcfg)
        target_include_directories(${_t} PRIVATE tests)
        target_compile_options(${_t} PRIVATE -Wall -Wextra)
        add_test(NAME ${_t} COMMAND ${_t})
    endforeach()
endif()

# ------------------------------------------------------------------ #
# Installation                                                        #
# ------------------------------------------------------------------ #

include(GNUInstallDirs)

install(TARGETS robustcfg robust_cfg_tool
    EXPORT   robustcfgTargets
    LIBRARY  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE  DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME  DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT robustcfgTargets
    FILE      robustcfgTargets.cmake
    NAMESPACE robustcfg::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/robustcfg
)

# ------------------------------------------------------------------ #
# pkg-config                                                          #
# ------------------------------------------------------------------ #

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/robustcfg.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/robustcfg.pc
    @ONLY
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/robustcfg.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

# ------------------------------------------------------------------ #
# Summary                                                             #
# ------------------------------------------------------------------ #

message(STATUS "robust-binary-config ${PROJECT_VERSION}")
message(STATUS "  BUILD_TESTING    = ${BUILD_TESTING}")
message(STATUS "  BUILD_SHARED_LIBS= ${BUILD_SHARED_LIBS}")
message(STATUS "  Install prefix   = ${CMAKE_INSTALL_PREFIX}")
