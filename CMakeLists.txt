# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# corelr-plotter -- firmware for the CoreLR cable-driven wall plotter (ATmega328P, Arduino Nano).
#
#   cmake --preset serial && cmake --build --preset serial        (or: sd)
#   ctest --preset host                                          (the host tests, native compiler)
#   cmake --build --preset host --target doc                      (the API documentation of both images, build/doxygen/{serial,sd}/html; needs Doxygen, not the AVR toolchain)
#
# One firmware image per job source (REQ-CoreLR-F-SerialControl-0003): `corelr_serial` and `corelr_sd`, both built by `all`.
# Design of this build: requirements/design/REQ-CoreLR-D-Build.md.
cmake_minimum_required(VERSION 3.20)

# The software version is written once, in firmware/config/Version.h (REQ-CoreLR-D-Build-0020); read here as text.
#   CORELR_VERSION_NUMBER  <x.y.z>     (what project(VERSION) takes)
#   CORELR_VERSION         <x.y.z>[-dev] (the names of the release files, the generated documentation; a release has no suffix, and its tag is v + this)
file(STRINGS "${CMAKE_CURRENT_LIST_DIR}/firmware/config/Version.h" _corelr_version_lines REGEX "^#define CORELR_VERSION_(MAJOR|MINOR|PATCH|SUFFIX) ")
foreach(_part MAJOR MINOR PATCH)
    set(_corelr_${_part} "")
    foreach(_line IN LISTS _corelr_version_lines)
        if(_line MATCHES "^#define CORELR_VERSION_${_part} +([0-9]+)")
            set(_corelr_${_part} "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    if(_corelr_${_part} STREQUAL "")
        message(FATAL_ERROR "firmware/config/Version.h: no '#define CORELR_VERSION_${_part} <number>' line")
    endif()
endforeach()
set(_corelr_SUFFIX "")
foreach(_line IN LISTS _corelr_version_lines)
    if(_line MATCHES "^#define CORELR_VERSION_SUFFIX +\"([^\"]*)\"")
        set(_corelr_SUFFIX "${CMAKE_MATCH_1}")
    endif()
endforeach()
set(CORELR_VERSION_NUMBER "${_corelr_MAJOR}.${_corelr_MINOR}.${_corelr_PATCH}")
set(CORELR_VERSION "${CORELR_VERSION_NUMBER}${_corelr_SUFFIX}")

# Which build this is: the AVR firmware (the default, needs the toolchain file) or the host tests only (-DCORELR_HOST_TESTS_ONLY=ON).
option(CORELR_HOST_TESTS_ONLY "Configure only the native host tests (no AVR toolchain needed)" OFF)

if(CORELR_HOST_TESTS_ONLY)
    project(corelr_host_tests LANGUAGES NONE)
    enable_testing()
    include(cmake/Doxygen.cmake)   # the target `doc` (no AVR toolchain needed); the tests that check its output are registered by tests/
    add_subdirectory(tests)
    return()
endif()

project(corelr_plotter VERSION ${CORELR_VERSION_NUMBER} LANGUAGES C CXX ASM)

# ---------------------------------------------------------------- options
set(CORELR_MATH "48" CACHE STRING "Arithmetic width (REQ-CoreLR-D-Motion-0024): 48 (nothing wider than 32 bits, fits the Nano with arcs) or 64")
set_property(CACHE CORELR_MATH PROPERTY STRINGS 48 64)
option(CORELR_CALL_PROLOGUES   "-mcall-prologues: shared register save/restore (-1396 B of flash; the SD variant does not link without it)" ON)
option(CORELR_WARNINGS         "-Wall -Wextra on the project's own sources (not on third_party/); changes no code" ON)
option(CORELR_SIZE_FLAGS       "the size flags of cmake/size_flags.txt marked `on` (-mrelax -fno-gcse ...: -1236 B of flash on the SD variant), see REQ-CoreLR-D-Build-0012" ON)
option(CORELR_SHORT_ENUMS      "also apply -fshort-enums when it is `off` in cmake/size_flags.txt (it is `on` since 2026-10-12: -374 B SD, -240 B serial); changes the size of every enum" OFF)
option(CORELR_DEBUG_MISS_COUNT "CORELR_DEBUG_MISS_COUNT=1: the jog's 'miss:' counter (+50 B of flash)" OFF)
option(CORELR_DEBUG_JOBSTATE   "CORELR_DEBUG_JOBSTATE=1: the |D:...> field of the status report (serial variant, for tools/bench/serial_probe.py)" OFF)
set(CORELR_EXTRA_DEFS "" CACHE STRING "More compile definitions for every file, a ;-separated list of NAME[=VALUE] (every selector in SwConfig.h is #ifndef-guarded), for experiments")
set(CORELR_EXTRA_COMPILE_FLAGS "" CACHE STRING "More compiler flags for every C and C++ file (a ;-separated list), for experiments: tools/analysis/flag_sweep.sh. Empty = the IDE's flags, byte for byte")
set(CORELR_EXTRA_LINK_FLAGS "" CACHE STRING "More linker/driver flags (a ;-separated list), for experiments")
set(CORELR_EXPECTED_GCC "7.3.0" CACHE STRING "The avr-gcc version the documented sizes were measured with")

set(CORELR_FLASH_BYTES 30720 CACHE STRING "Flash available to the sketch (Nano bootloader: 30720, NOT the Uno's 32256)")
set(CORELR_RAM_BYTES   2048  CACHE STRING "Static RAM")
if(NOT CORELR_PORT)
    set(CORELR_PORT "/dev/ttyUSB0")
endif()
if(NOT CORELR_BAUD)
    set(CORELR_BAUD 115200)
endif()
set(CORELR_PORT "${CORELR_PORT}" CACHE STRING "Serial port of the board, for the upload_* targets")
set(CORELR_BAUD "${CORELR_BAUD}" CACHE STRING "Bootloader speed (fqbn arduino:avr:nano:cpu=atmega328 is 115200; the 'old bootloader' Nanos are 57600)")

if(NOT CMAKE_C_COMPILER_VERSION VERSION_EQUAL CORELR_EXPECTED_GCC)
    message(WARNING "avr-gcc ${CMAKE_C_COMPILER_VERSION} found, the documented sizes are those of ${CORELR_EXPECTED_GCC}: measure again before trusting a budget.")
endif()

include(cmake/Firmware.cmake)

add_subdirectory(third_party)
add_subdirectory(firmware)
add_subdirectory(tests/bench)   # the bring-up sketches: `cmake --build <dir> --target bench`; not part of `all`
include(cmake/Doxygen.cmake)    # the target `doc`: the API documentation (Doxygen); not part of `all`, builds nothing for the AVR
# `check_image_symbols`: does each image hold anything that belongs to the OTHER one? (REQ-CoreLR-D-Build-0015). Not part of `all`: it builds both images, then looks at their symbols and map files.
add_custom_target(check_image_symbols
    COMMAND python3 ${CMAKE_CURRENT_LIST_DIR}/tools/analysis/check_image_symbols.py --build-dir ${CMAKE_BINARY_DIR}
    DEPENDS corelr_sd corelr_serial
    COMMENT "Checking that each image holds nothing of the other (cmake/variant_modules.txt)" VERBATIM)

# ---------------------------------------------------------------- host tests (native), reachable from the same tree
# They are configured by their own project because they need the host compiler: see CMakePresets.json, preset "host".
