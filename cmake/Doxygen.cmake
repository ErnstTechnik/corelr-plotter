# Doxygen.cmake -- the `doc` target: the API documentation of the firmware (design: requirements/design/REQ-CoreLR-D-Build.md, REQ-CoreLR-D-Build-0013).
#
# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
#   cmake --build --preset host --target doc        (or avr-default: the same output, no AVR toolchain is used)
#
# Not part of `all`: building the firmware neither runs Doxygen nor depends on it, and no image changes by a byte (the sources are only READ).
# One documentation per firmware image, like the firmware: <CORELR_DOC_DIR>/serial/html/index.html and <CORELR_DOC_DIR>/sd/html/index.html (default build/doxygen/,
# git-ignored, whichever preset configured it), each with its warning log, and <CORELR_DOC_DIR>/index.html linking both. Targets: doc (both), doc_serial, doc_sd.
# Without Doxygen the configuration succeeds, says so, and the target stops with the way to get it. `dot` (Graphviz) is optional: without it the
# include graphs are simply left out.
#
# Included from CMakeLists.txt, in both configurations (the firmware's and the host tests'); it needs no compiler. Sets CORELR_DOXYGEN_FOUND for the tests.

set(CORELR_DOC_DIR "${CMAKE_SOURCE_DIR}/build/doxygen" CACHE PATH "Where the target `doc` writes: serial/ and sd/ (html/ and doxygen-warnings.log each) and index.html")
set(CORELR_DOXYGEN_FOUND OFF)

include(${CMAKE_CURRENT_LIST_DIR}/VariantModules.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/TargetDefs.cmake)

# the compiler's definitions of the target (F_CPU, ARDUINO..., and what avr-gcc defines for the MCU), as the build passes them: one list, cmake/TargetDefs.cmake
string(JOIN " " CORELR_DOC_TARGET_DEFS ${CORELR_TARGET_DEFS} ${CORELR_TARGET_COMPILER_DEFS})

find_package(Doxygen QUIET OPTIONAL_COMPONENTS dot)
if(NOT DOXYGEN_FOUND)
    message(STATUS "Doxygen not found: the target `doc` is not available (install doxygen, and graphviz for the graphs)")
    add_custom_target(doc
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_LIST_DIR}/DocUnavailable.cmake
        COMMENT "doc: Doxygen is not installed"
        VERBATIM)
    return()
endif()
set(CORELR_DOXYGEN_FOUND ON)

if(Doxygen_dot_FOUND OR DOXYGEN_DOT_FOUND)   # the first since CMake 3.20, the second is what older ones set
    set(CORELR_DOC_HAVE_DOT YES)
else()
    set(CORELR_DOC_HAVE_DOT NO)
    message(STATUS "dot (Graphviz) not found: the documentation is generated without its diagrams (state machines, include graphs); install graphviz to get them")
endif()
set(CORELR_DOC_VERSION "${CORELR_VERSION}")

# What the output depends on: re-run only when a source, the Doxyfile template or a page changes.
file(GLOB _doc_inputs CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/firmware/src/*" "${CMAKE_SOURCE_DIR}/firmware/config/*" "${CMAKE_SOURCE_DIR}/firmware/app/*"
    "${CMAKE_SOURCE_DIR}/third_party/sdfat/*" "${CMAKE_SOURCE_DIR}/third_party/arduino-core/cores/arduino/*"
    "${CMAKE_SOURCE_DIR}/third_party/arduino-core/variants/eightanaloginputs/*" "${CMAKE_SOURCE_DIR}/third_party/arduino-core/libraries/SPI/src/*"
    "${CMAKE_SOURCE_DIR}/docs/doxygen/*.md" "${CMAKE_SOURCE_DIR}/docs/doxygen/*.dox"
    "${CMAKE_SOURCE_DIR}/docs/hardware/images/*.svg" "${CMAKE_SOURCE_DIR}/docs/hardware/images/wiring/*.svg"   # the pictures of the hardware chapter
    "${CMAKE_SOURCE_DIR}/requirements/functional/*.md" "${CMAKE_SOURCE_DIR}/requirements/design/*.md" "${CMAKE_SOURCE_DIR}/docs/protocol/*.md")   # the requirements chapter and the protocol documents
list(APPEND _doc_inputs "${CMAKE_SOURCE_DIR}/docs/doxygen/Doxyfile.in" "${CMAKE_SOURCE_DIR}/tools/analysis/req_refs.py")   # the filter that the requirement files go through

find_program(CORELR_PYTHON NAMES python3 python)

# One documentation per image, as the compiler sees it (REQ-CoreLR-D-Build-0013). Two things differ between the two Doxyfiles:
#  - the job source, written as cmake/Firmware.cmake gives it to the compiler (CORELR_JOB_SOURCE=CORELR_JOB_SOURCE_<SERIAL|SD>); the values behind the names are
#    those of SwConfig.h, Doxygen reads them there;
#  - the vendored code the image is built with (third_party/, read as it is, grouped by docs/doxygen/third_party.dox): the Arduino core in both, what
#    cmake/variant_modules.txt gives to one image only (the SPI library and sdfat, to sd) in that image.
# The modules that belong to one image only are empty in the other one (their headers and sources are guarded by `#if CORELR_JOB_SOURCE ==`), so Doxygen
# needs no list of them; tests/tools/test_doxygen.py checks that each output holds its own code and none of the other's.
set(_doc_jobsrc_serial SERIAL)
set(_doc_jobsrc_sd     SD)
# every vendored directory the documentation may read; what cmake/variant_modules.txt gives to the other image is dropped (the SPI library and sdfat belong to sd)
set(_doc_third_party_all third_party/arduino-core/cores/arduino third_party/arduino-core/variants/eightanaloginputs
                         third_party/arduino-core/libraries/SPI/src third_party/sdfat)
set(_doc_outputs)
foreach(_v serial sd)
    set(CORELR_DOC_VARIANT "${_v}")
    set(CORELR_DOC_JOBSRC "${_doc_jobsrc_${_v}}")
    corelr_variant_exclusions("${CMAKE_SOURCE_DIR}" ${_v} _doc_other_image)
    set(_doc_third_party "")
    foreach(_d IN LISTS _doc_third_party_all)
        set(_doc_dir "${_d}")
        set(_doc_abs "${CMAKE_SOURCE_DIR}/${_d}")
        corelr_variant_filter(_doc_abs "${_doc_other_image}")
        if(_doc_abs)
            list(APPEND _doc_third_party "${_doc_dir}")
        endif()
    endforeach()
    if("${CMAKE_SOURCE_DIR}/third_party/sdfat" IN_LIST _doc_other_image)
        set(_doc_group_files "")
    else()
        set(_doc_group_files docs/doxygen/third_party_sd.dox)   # the groups of sdfat and SPI
    endif()
    string(JOIN " " CORELR_DOC_THIRD_PARTY ${_doc_third_party} ${_doc_group_files})
    # the files of the other image's modules (empty under their guards): left out, or the HTML run (EXTRACT_ALL = YES) would make an empty page of each
    set(_doc_exclude "")
    foreach(_e IN LISTS _doc_other_image)
        string(FIND "${_e}" "${CMAKE_SOURCE_DIR}/firmware/src/" _doc_at)
        if(_doc_at EQUAL 0)
            file(RELATIVE_PATH _doc_rel "${CMAKE_SOURCE_DIR}" "${_e}")
            list(APPEND _doc_exclude "${_doc_rel}")
        endif()
    endforeach()
    string(JOIN " " CORELR_DOC_EXCLUDE ${_doc_exclude})
    # Doxygen runs from the source root, so every path of the Doxyfile is relative to it.
    # Two runs of the one template for each image: the HTML run documents every member (EXTRACT_ALL = YES: the callers that have no comment yet are drawn in the caller graphs), the
    # inventory run (EXTRACT_ALL = NO, XML only) is the one that says what has no comment: its log, doxygen-warnings.log, is what tools/analysis/doc_warnings.py and the tests read.
    set(CORELR_DOC_OUTDIR "${CORELR_DOC_DIR}/${_v}")
    set(CORELR_DOC_EXTRACT_ALL YES)
    set(CORELR_DOC_GENERATE_HTML YES)
    set(CORELR_DOC_GENERATE_XML NO)
    set(CORELR_DOC_LOG "${CORELR_DOC_DIR}/${_v}/doxygen-html.log")
    configure_file("${CMAKE_SOURCE_DIR}/docs/doxygen/Doxyfile.in" "${CMAKE_BINARY_DIR}/Doxyfile_${_v}" @ONLY)
    set(CORELR_DOC_OUTDIR "${CORELR_DOC_DIR}/${_v}/inventory")
    set(CORELR_DOC_EXTRACT_ALL NO)
    set(CORELR_DOC_GENERATE_HTML NO)
    set(CORELR_DOC_GENERATE_XML YES)
    set(CORELR_DOC_LOG "${CORELR_DOC_DIR}/${_v}/doxygen-warnings.log")
    configure_file("${CMAKE_SOURCE_DIR}/docs/doxygen/Doxyfile.in" "${CMAKE_BINARY_DIR}/Doxyfile_${_v}_inventory" @ONLY)
    set(_summary "")
    if(CORELR_PYTHON)
        set(_summary
            COMMAND ${CMAKE_COMMAND} -E echo "Doxygen warnings, ${_v} variant:"
            COMMAND ${CORELR_PYTHON} ${CMAKE_SOURCE_DIR}/tools/analysis/doc_warnings.py ${CORELR_DOC_DIR}/${_v}/doxygen-warnings.log)
    endif()
    add_custom_command(
        OUTPUT "${CORELR_DOC_DIR}/${_v}/html/index.html"
        # a file that no longer exists in the sources must not survive in the output
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${CORELR_DOC_DIR}/${_v}/html" "${CORELR_DOC_DIR}/${_v}/inventory"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CORELR_DOC_DIR}/${_v}"
        COMMAND ${DOXYGEN_EXECUTABLE} "${CMAKE_BINARY_DIR}/Doxyfile_${_v}_inventory"
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${CORELR_DOC_DIR}/${_v}/inventory"
        ${_summary}
        COMMAND ${DOXYGEN_EXECUTABLE} "${CMAKE_BINARY_DIR}/Doxyfile_${_v}"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS ${_doc_inputs} "${CMAKE_BINARY_DIR}/Doxyfile_${_v}" "${CMAKE_BINARY_DIR}/Doxyfile_${_v}_inventory"
        COMMENT "Doxygen: ${_v} variant -> ${CORELR_DOC_DIR}/${_v}/html/index.html"
        VERBATIM)
    add_custom_target(doc_${_v} DEPENDS "${CORELR_DOC_DIR}/${_v}/html/index.html")
    list(APPEND _doc_outputs "${CORELR_DOC_DIR}/${_v}/html/index.html")
endforeach()

add_custom_command(
    OUTPUT "${CORELR_DOC_DIR}/index.html"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/docs/doxygen/index.html" "${CORELR_DOC_DIR}/index.html"
    DEPENDS "${CMAKE_SOURCE_DIR}/docs/doxygen/index.html" ${_doc_outputs}
    VERBATIM)
# `doc` makes both; doc_serial and doc_sd make one
add_custom_target(doc DEPENDS "${CORELR_DOC_DIR}/index.html")

# `doc_package`: one zip per image + SHA256SUMS in <CORELR_DOC_DIR>/dist/, to attach to a release (cmake/DocPackage.cmake, REQ-CoreLR-D-Build-0016). The version in the names is
# CORELR_VERSION: the one of the release's tag.
add_custom_command(
    OUTPUT "${CORELR_DOC_DIR}/dist/SHA256SUMS"
    COMMAND ${CMAKE_COMMAND} -DDOC_DIR=${CORELR_DOC_DIR} -DDIST_DIR=${CORELR_DOC_DIR}/dist -DVERSION=${CORELR_VERSION} -P ${CMAKE_CURRENT_LIST_DIR}/DocPackage.cmake
    DEPENDS ${_doc_outputs} ${CMAKE_CURRENT_LIST_DIR}/DocPackage.cmake
    COMMENT "Doxygen: packaging the documentation of both images -> ${CORELR_DOC_DIR}/dist"
    VERBATIM)
add_custom_target(doc_package DEPENDS "${CORELR_DOC_DIR}/dist/SHA256SUMS")
