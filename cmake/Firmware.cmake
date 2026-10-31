# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# Firmware.cmake -- corelr_add_variant(): one firmware image (core + sketch + sdfat + SPI), with the flags the Arduino IDE's builder used,
# so that the image is the same byte for byte (checked by tools/analysis/compare_with_ide.sh).
#
#   corelr_add_variant(<name> <JOB_SOURCE: SD|SERIAL>)   ->  targets corelr_<name> (the .elf, .hex, .map, size check) and upload_<name>

include(${CMAKE_CURRENT_LIST_DIR}/VariantModules.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/TargetDefs.cmake)

# --- the flags, from `arduino-builder -verbose` of the Nano (fqbn arduino:avr:nano:cpu=atmega328), recorded in REQ-CoreLR-D-Build
set(CORELR_MCU atmega328p)
set(CORELR_C_FLAGS   -g -Os -w -std=gnu11 -ffunction-sections -fdata-sections -flto -fno-fat-lto-objects)
set(CORELR_CXX_FLAGS -g -Os -w -std=gnu++11 -fpermissive -fno-exceptions -ffunction-sections -fdata-sections -fno-threadsafe-statics
                     -Wno-error=narrowing -flto)
set(CORELR_LINK_FLAGS -w -Os -g -flto -fuse-linker-plugin -Wl,--gc-sections)
# The size flags (cmake/size_flags.txt, REQ-CoreLR-D-Build-0012): the lines marked `on`, and `sd` / `serial` for that variant only, with CORELR_SIZE_FLAGS; the `short_enums` line also
# with CORELR_SHORT_ENUMS. The two variants do not get the same flags (a flag can pay in one and hurt in the other): _corelr_size_flags() works out one variant's.
file(STRINGS ${PROJECT_SOURCE_DIR}/cmake/size_flags.txt _size_lines REGEX "^[^#].*\\|")
function(_corelr_size_flags VARIANT OUT_C OUT_LINK)
    set(_c_all "")
    set(_l_all "")
    foreach(_l IN LISTS _size_lines)
        string(REPLACE "|" ";" _f "${_l}")
        list(GET _f 0 _name)
        list(GET _f 3 _status)
        set(_use OFF)
        if(CORELR_SIZE_FLAGS AND (_status STREQUAL "on" OR _status STREQUAL VARIANT))
            set(_use ON)
        endif()
        if(_name STREQUAL "short_enums" AND CORELR_SHORT_ENUMS)
            set(_use ON)
        endif()
        if(_use)
            list(GET _f 1 _c)
            list(GET _f 2 _lk)
            separate_arguments(_c)
            separate_arguments(_lk)
            list(APPEND _c_all ${_c})
            list(APPEND _l_all ${_lk})
        endif()
    endforeach()
    set(${OUT_C} ${_c_all} PARENT_SCOPE)
    set(${OUT_LINK} ${_l_all} PARENT_SCOPE)
endfunction()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/cmake/size_flags.txt)
# experiments only (tools/analysis/flag_sweep.sh): empty by default, and then the flags are exactly the IDE's
list(APPEND CORELR_C_FLAGS ${CORELR_EXTRA_COMPILE_FLAGS})
list(APPEND CORELR_CXX_FLAGS ${CORELR_EXTRA_COMPILE_FLAGS})
list(APPEND CORELR_LINK_FLAGS ${CORELR_EXTRA_LINK_FLAGS})

find_program(CORELR_AVRDUDE avrdude HINTS "${AVR_BIN_DIR}")
if(AVRDUDE_CONF)
    set(CORELR_AVRDUDE_CONF "${AVRDUDE_CONF}")
else()
    find_file(CORELR_AVRDUDE_CONF avrdude.conf HINTS "${AVR_BIN_DIR}/../etc" "${AVR_BIN_DIR}/../../etc" "/etc")
endif()

# The sketch: grbl_corelr.ino is compiled as C++ with <Arduino.h> in front, which is what the IDE does (without its prototype generator,
# unneeded here: the file defines before it uses).
set(CORELR_INO ${PROJECT_SOURCE_DIR}/firmware/app/grbl_corelr.ino)
set(CORELR_INO_CPP ${CMAKE_BINARY_DIR}/generated/grbl_corelr.ino.cpp)
file(WRITE ${CORELR_INO_CPP}.tmp "#include <Arduino.h>\n#line 1 \"firmware/app/grbl_corelr.ino\"\n#include \"${CORELR_INO}\"\n")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CORELR_INO_CPP}.tmp ${CORELR_INO_CPP})
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CORELR_INO})

# The compile settings of an image, in the caller's scope: `defs` (definitions for EVERY file, core and libraries included -- what the IDE's build.extra_flags
# did), `common_opts` (-mmcu, -mcall-prologues) and `lang_opts` (the per-language flags). A macro, so that the variables land where they are used.
macro(_corelr_settings JOBSRC)
    set(defs ${CORELR_TARGET_DEFS}
             CORELR_JOB_SOURCE=CORELR_JOB_SOURCE_${JOBSRC} CORELR_MATH=CORELR_MATH_${CORELR_MATH})
    if(CORELR_DEBUG_MISS_COUNT)
        list(APPEND defs CORELR_DEBUG_MISS_COUNT=1)
    endif()
    if(CORELR_DEBUG_JOBSTATE)
        list(APPEND defs CORELR_DEBUG_JOBSTATE=1)
    endif()
    foreach(d IN LISTS CORELR_EXTRA_DEFS)
        list(APPEND defs ${d})
    endforeach()
    set(common_opts -mmcu=${CORELR_MCU})
    if(CORELR_CALL_PROLOGUES)
        list(APPEND common_opts -mcall-prologues)
        list(APPEND defs CORELR_CALL_PROLOGUES=1)   # tells the .ino it was asked for: avr-gcc defines no macro for the flag
    endif()
    string(TOLOWER "${JOBSRC}" _variant)
    _corelr_size_flags(${_variant} _size_c _size_link)
    set(link_opts ${CORELR_LINK_FLAGS} ${_size_link})
    set(lang_opts $<$<COMPILE_LANGUAGE:C>:${CORELR_C_FLAGS};${_size_c}> $<$<COMPILE_LANGUAGE:CXX>:${CORELR_CXX_FLAGS};${_size_c}>
                  $<$<COMPILE_LANGUAGE:ASM>:-g$<SEMICOLON>-x$<SEMICOLON>assembler-with-cpp$<SEMICOLON>-flto>)
endmacro()

# The sources of an image, in the caller's scope: `c_src`, `cpp_src` (firmware/src), `sdfat_src`, `spi_src` and `spi_inc`, without what belongs to the other image
# (cmake/variant_modules.txt, REQ-CoreLR-D-Build-0014): a module or a library the image does not use is not compiled at all. A macro, so that the variables land where they are used.
macro(_corelr_variant_sources JOBSRC)
    string(TOLOWER "${JOBSRC}" _variant_name)
    corelr_variant_exclusions(${PROJECT_SOURCE_DIR} ${_variant_name} _other_image)
    file(GLOB c_src   CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/firmware/src/*.c)
    file(GLOB cpp_src CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/firmware/src/*.cpp)
    list(SORT c_src)
    list(SORT cpp_src)
    set(sdfat_src ${CORELR_SDFAT_SOURCES})
    set(spi_src ${CORELR_SPI_SOURCES})
    corelr_variant_filter(c_src "${_other_image}")
    corelr_variant_filter(cpp_src "${_other_image}")
    corelr_variant_filter(sdfat_src "${_other_image}")
    corelr_variant_filter(spi_src "${_other_image}")
    set(spi_inc "")
    if(spi_src)
        set(spi_inc ${CORELR_SPI_INCLUDES})
    endif()
endmacro()

function(corelr_add_variant NAME JOBSRC)
    set(T corelr_${NAME})
    _corelr_settings(${JOBSRC})
    _corelr_variant_sources(${JOBSRC})

    # ---- the Arduino core, as an archive (only the members the program refers to are linked)
    add_library(${T}_core STATIC ${CORELR_ARDUINO_CORE_SOURCES})
    set_target_properties(${T}_core PROPERTIES LINKER_LANGUAGE C)
    target_compile_definitions(${T}_core PRIVATE ${defs})
    target_compile_options(${T}_core PRIVATE ${common_opts} ${lang_opts})
    target_include_directories(${T}_core PRIVATE ${CORELR_ARDUINO_INCLUDES})

    # ---- the image: sketch modules, the .ino, vendored sdfat, SPI, then the core archive and libm (the IDE's link order)
    add_executable(${T} ${c_src} ${cpp_src} ${CORELR_INO_CPP} ${sdfat_src} ${spi_src})
    set_target_properties(${T} PROPERTIES SUFFIX ".elf" LINKER_LANGUAGE C)
    if(CORELR_WARNINGS)
        set_source_files_properties(${c_src} ${cpp_src} PROPERTIES COMPILE_OPTIONS "-Wall;-Wextra")
    endif()
    target_compile_definitions(${T} PRIVATE ${defs})
    target_compile_options(${T} PRIVATE ${common_opts} ${lang_opts})
    target_include_directories(${T} PRIVATE
        ${PROJECT_SOURCE_DIR}/firmware/src ${PROJECT_SOURCE_DIR}/firmware/config ${CORELR_THIRD_PARTY_INCLUDE_ROOT}
        ${CORELR_ARDUINO_INCLUDES} ${spi_inc})
    target_link_options(${T} PRIVATE ${link_opts} -mmcu=${CORELR_MCU} -Wl,-Map=${CMAKE_BINARY_DIR}/${T}.map)
    target_link_libraries(${T} PRIVATE ${T}_core m)

    # ---- outputs: .hex (flash image, never the .eep: it would erase the calibration), size check
    add_custom_command(TARGET ${T} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O ihex -R .eeprom $<TARGET_FILE:${T}> ${CMAKE_BINARY_DIR}/${T}.hex
        COMMAND ${CMAKE_COMMAND} -DSIZE_TOOL=${CMAKE_SIZE} -DELF=$<TARGET_FILE:${T}> -DNAME=${T}
                -DFLASH_MAX=${CORELR_FLASH_BYTES} -DRAM_MAX=${CORELR_RAM_BYTES} -P ${PROJECT_SOURCE_DIR}/cmake/CheckSize.cmake
        BYPRODUCTS ${CMAKE_BINARY_DIR}/${T}.hex ${CMAKE_BINARY_DIR}/${T}.map
        COMMENT "hex + size check: ${T}" VERBATIM)

    # ---- upload: never part of `all`; the board is reset, and a reset before the position is stored loses the calibration
    if(CORELR_AVRDUDE AND CORELR_AVRDUDE_CONF)
        add_custom_target(upload_${NAME}
            COMMAND ${CORELR_AVRDUDE} -C ${CORELR_AVRDUDE_CONF} -p m328p -c arduino -P ${CORELR_PORT} -b ${CORELR_BAUD} -D
                    -U flash:w:${CMAKE_BINARY_DIR}/${T}.hex:i
            DEPENDS ${T} USES_TERMINAL COMMENT "flashing ${T} to ${CORELR_PORT}")
    endif()
endfunction()

# ---- the IDE export: flat sketch folders under <build>/ide_sketch/<variant>/ (generated, in build/, never committed)
foreach(_v serial sd)
    add_custom_target(export_ino_${_v}
        COMMAND ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR} -DOUT=${CMAKE_BINARY_DIR}/ide_sketch/${_v} -DVARIANT=${_v} -P ${PROJECT_SOURCE_DIR}/cmake/ExportIno.cmake
        COMMENT "exporting the ${_v} sketch for the Arduino IDE" VERBATIM)
endforeach()
add_custom_target(export_ino DEPENDS export_ino_serial export_ino_sd)

# ---- the bring-up sketches (tests/bench): small images that exercise one thing on the real board, built with the SD variant's settings.
#   corelr_add_bench(<name> <sketch.ino>)  ->  targets bench_<name> (.elf, .hex, size check) and upload_bench_<name>; all of them under the target `bench`.
# The firmware's modules are compiled ONCE for all of them, as an object library whose objects are all linked (a vector defined in a module that nothing
# else refers to must not be dropped by an archive); the Arduino core as an archive, as for the images.
function(corelr_bench_libraries)
    _corelr_settings(SD)
    _corelr_variant_sources(SD)
    add_library(corelr_bench_modules OBJECT EXCLUDE_FROM_ALL ${c_src} ${cpp_src} ${sdfat_src} ${spi_src})
    target_compile_definitions(corelr_bench_modules PRIVATE ${defs})
    target_compile_options(corelr_bench_modules PRIVATE ${common_opts} ${lang_opts})
    target_include_directories(corelr_bench_modules PRIVATE ${PROJECT_SOURCE_DIR}/firmware/src ${PROJECT_SOURCE_DIR}/firmware/config
        ${CORELR_THIRD_PARTY_INCLUDE_ROOT} ${CORELR_ARDUINO_INCLUDES} ${spi_inc})
    add_library(corelr_bench_core STATIC EXCLUDE_FROM_ALL ${CORELR_ARDUINO_CORE_SOURCES})
    set_target_properties(corelr_bench_core PROPERTIES LINKER_LANGUAGE C)
    target_compile_definitions(corelr_bench_core PRIVATE ${defs})
    target_compile_options(corelr_bench_core PRIVATE ${common_opts} ${lang_opts})
    target_include_directories(corelr_bench_core PRIVATE ${CORELR_ARDUINO_INCLUDES})
    add_custom_target(bench)
endfunction()

function(corelr_add_bench NAME SKETCH)   # an optional third argument, STANDALONE, keeps the sketch out of the `bench` target (it is built by naming it)
    _corelr_settings(SD)
    set(T bench_${NAME})
    get_filename_component(sketch_abs ${SKETCH} ABSOLUTE)
    set(wrapper ${CMAKE_BINARY_DIR}/generated/bench/${NAME}.ino.cpp)
    file(WRITE ${wrapper}.tmp "#include <Arduino.h>\n#line 1 \"${SKETCH}\"\n#include \"${sketch_abs}\"\n")
    execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different ${wrapper}.tmp ${wrapper})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${sketch_abs})
    add_executable(${T} EXCLUDE_FROM_ALL ${wrapper} $<TARGET_OBJECTS:corelr_bench_modules>)
    set_target_properties(${T} PROPERTIES SUFFIX ".elf" LINKER_LANGUAGE C)
    target_compile_definitions(${T} PRIVATE ${defs})
    target_compile_options(${T} PRIVATE ${common_opts} ${lang_opts})
    get_filename_component(sketch_dir ${sketch_abs} DIRECTORY)
    target_include_directories(${T} PRIVATE ${PROJECT_SOURCE_DIR}/firmware/src ${PROJECT_SOURCE_DIR}/firmware/config ${PROJECT_SOURCE_DIR}/firmware/app
        ${CORELR_THIRD_PARTY_INCLUDE_ROOT} ${CORELR_ARDUINO_INCLUDES} ${CORELR_SPI_INCLUDES} ${sketch_dir})
    target_link_options(${T} PRIVATE ${link_opts} -mmcu=${CORELR_MCU} -Wl,-Map=${CMAKE_BINARY_DIR}/${T}.map)
    target_link_libraries(${T} PRIVATE corelr_bench_core m)
    add_custom_command(TARGET ${T} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -O ihex -R .eeprom $<TARGET_FILE:${T}> ${CMAKE_BINARY_DIR}/${T}.hex
        COMMAND ${CMAKE_COMMAND} -DSIZE_TOOL=${CMAKE_SIZE} -DELF=$<TARGET_FILE:${T}> -DNAME=${T}
                -DFLASH_MAX=${CORELR_FLASH_BYTES} -DRAM_MAX=${CORELR_RAM_BYTES} -P ${PROJECT_SOURCE_DIR}/cmake/CheckSize.cmake
        BYPRODUCTS ${CMAKE_BINARY_DIR}/${T}.hex ${CMAKE_BINARY_DIR}/${T}.map COMMENT "hex + size check: ${T}" VERBATIM)
    if(NOT "${ARGV2}" STREQUAL "STANDALONE")
        add_dependencies(bench ${T})
    endif()
    if(CORELR_AVRDUDE AND CORELR_AVRDUDE_CONF)
        add_custom_target(upload_${T}
            COMMAND ${CORELR_AVRDUDE} -C ${CORELR_AVRDUDE_CONF} -p m328p -c arduino -P ${CORELR_PORT} -b ${CORELR_BAUD} -D -U flash:w:${CMAKE_BINARY_DIR}/${T}.hex:i
            DEPENDS ${T} USES_TERMINAL COMMENT "flashing ${T} to ${CORELR_PORT}")
    endif()
endfunction()
