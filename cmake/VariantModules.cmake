# VariantModules.cmake -- reads cmake/variant_modules.txt (REQ-CoreLR-D-Build-0014). Usable from the build and from a `cmake -P` script (no project variable is used).
#
# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   corelr_variant_exclusions(<root> <variant: sd|serial> <out>)   <out> = the absolute paths of what belongs to the OTHER image
#   corelr_variant_filter(<list variable> "<exclusions>")          removes from the list the files that are an exclusion or lie under a directory that is
include_guard(GLOBAL)
if(NOT CMAKE_SCRIPT_MODE_FILE)   # in a build: a change of the list configures again
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/variant_modules.txt")
endif()

function(corelr_variant_exclusions ROOT VARIANT OUT)
    file(STRINGS "${ROOT}/cmake/variant_modules.txt" _lines REGEX "^[^#].*\\|")
    set(_other "")
    foreach(_l IN LISTS _lines)
        string(REPLACE "|" ";" _f "${_l}")
        list(GET _f 0 _path)
        list(GET _f 1 _image)
        string(STRIP "${_path}" _path)
        string(STRIP "${_image}" _image)
        if(NOT _image STREQUAL "sd" AND NOT _image STREQUAL "serial")
            message(FATAL_ERROR "cmake/variant_modules.txt: the image of '${_path}' must be sd or serial, not '${_image}'")
        endif()
        if(NOT _image STREQUAL "${VARIANT}")
            list(APPEND _other "${ROOT}/${_path}")
        endif()
    endforeach()
    set(${OUT} "${_other}" PARENT_SCOPE)
endfunction()

function(corelr_variant_filter VAR EXCLUSIONS)
    set(_keep "")
    foreach(_f IN LISTS ${VAR})
        set(_drop OFF)
        foreach(_e IN LISTS EXCLUSIONS)
            if(_f STREQUAL _e)
                set(_drop ON)
            else()
                string(FIND "${_f}" "${_e}/" _pos)
                if(_pos EQUAL 0)
                    set(_drop ON)
                endif()
            endif()
        endforeach()
        if(NOT _drop)
            list(APPEND _keep "${_f}")
        endif()
    endforeach()
    set(${VAR} "${_keep}" PARENT_SCOPE)
endfunction()
