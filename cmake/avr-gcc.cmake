# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# avr-gcc.cmake -- toolchain file for the ATmega328P (Arduino Nano).
#
# Use it with  -DCMAKE_TOOLCHAIN_FILE=cmake/avr-gcc.cmake  (the presets in CMakePresets.json do).
# The toolchain is looked for, in this order:
#   1. AVR_BIN: the directory that holds avr-gcc, from -DAVR_BIN=..., the environment or local.conf (cmake/LocalConf.cmake, local.conf.example);
#   2. the PATH;
#   3. the toolchain of an Arduino IDE 1.8.x installation (snap or ~/.arduino15).
# The measured sizes of this project (docs/bringup/Bringup-Status.md) are those of avr-gcc 7.3.0 (Atmel/Arduino build 7.3.0-atmel3.6.1-arduino7):
# another version may change them, which is why CORELR_EXPECTED_GCC (see CMakeLists.txt) warns when the compiler differs.

set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR avr)

# the machine's own paths: local.conf (AVR_BIN), the environment, -DAVR_BIN=...
include(${CMAKE_CURRENT_LIST_DIR}/LocalConf.cmake)

set(_avr_hints
    "/snap/arduino/current/hardware/tools/avr/bin"
    "$ENV{HOME}/snap/arduino/current/.arduino15/packages/arduino/tools/avr-gcc/7.3.0-atmel3.6.1-arduino7/bin"
    "$ENV{HOME}/.arduino15/packages/arduino/tools/avr-gcc/7.3.0-atmel3.6.1-arduino7/bin")

if(AVR_BIN)
    find_program(AVR_GCC avr-gcc PATHS "${AVR_BIN}" NO_DEFAULT_PATH)
else()
    find_program(AVR_GCC avr-gcc HINTS ${_avr_hints})
endif()
if(NOT AVR_GCC)
    message(FATAL_ERROR "avr-gcc not found. Install it, or give its directory: -DAVR_BIN=/path/to/bin, or AVR_BIN=... in local.conf (copy local.conf.example).")
endif()
get_filename_component(AVR_BIN_DIR "${AVR_GCC}" DIRECTORY)

set(CMAKE_C_COMPILER   "${AVR_BIN_DIR}/avr-gcc")
set(CMAKE_CXX_COMPILER "${AVR_BIN_DIR}/avr-g++")
set(CMAKE_ASM_COMPILER "${AVR_BIN_DIR}/avr-gcc")
# the gcc-ar wrapper loads the LTO plugin; a plain ar cannot index LTO objects
set(CMAKE_AR     "${AVR_BIN_DIR}/avr-gcc-ar")
set(CMAKE_RANLIB "${AVR_BIN_DIR}/avr-gcc-ranlib")
set(CMAKE_OBJCOPY "${AVR_BIN_DIR}/avr-objcopy" CACHE FILEPATH "")
set(CMAKE_SIZE    "${AVR_BIN_DIR}/avr-size"    CACHE FILEPATH "")

# no host system to probe: configure-time checks build a static library only
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)

# the flags are the project's own (CMakeLists.txt); CMake's per-build-type defaults must not add -O3, -DNDEBUG...
foreach(_l C CXX ASM)
    set(CMAKE_${_l}_FLAGS_INIT "")
    foreach(_t DEBUG RELEASE MINSIZEREL RELWITHDEBINFO)
        set(CMAKE_${_l}_FLAGS_${_t}_INIT "")
    endforeach()
endforeach()
