# DocPackage.cmake -- one zip of the API documentation per image, for a release (REQ-CoreLR-D-Build-0016). Run by the target `doc_package` (cmake/Doxygen.cmake), as a script:
#
#   cmake -DDOC_DIR=<build/doxygen> -DDIST_DIR=<where the zips go> -DVERSION=<x.y.z> -P cmake/DocPackage.cmake
#
# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Writes, in DIST_DIR:   corelr-<VERSION>-serial-api.zip   corelr-<VERSION>-sd-api.zip   SHA256SUMS   (`sha256sum -c SHA256SUMS` checks them)
# Each zip holds one directory, corelr-<VERSION>-<variant>-api/, which is the image's html/ (open its index.html); the warning log, which names the build machine's paths, is not in it.
# The entries are in alphabetical order. The zip is NOT byte-for-byte reproducible (measured: the format records the access and change times of the staged copies, and
# `cmake -E tar` cannot fix them): the checksum is that of the file that is published, not something to recompute from the sources.
# The names follow the firmware's (corelr-<version>-<variant>.hex), so a release's SHA256SUMS can list them all.

foreach(_v DOC_DIR DIST_DIR VERSION)
    if(NOT ${_v})
        message(FATAL_ERROR "DocPackage.cmake: -D${_v}=... is required")
    endif()
endforeach()

set(_sums "")
file(MAKE_DIRECTORY "${DIST_DIR}")
foreach(_v serial sd)
    set(_html "${DOC_DIR}/${_v}/html")
    if(NOT EXISTS "${_html}/index.html")
        message(FATAL_ERROR "DocPackage.cmake: ${_html}/index.html is missing: build the target `doc` first")
    endif()
    set(_top "corelr-${VERSION}-${_v}-api")
    set(_stage "${DIST_DIR}/stage_${_v}")
    file(REMOVE_RECURSE "${_stage}")
    file(MAKE_DIRECTORY "${_stage}/${_top}")
    file(COPY "${_html}/" DESTINATION "${_stage}/${_top}")
    file(GLOB_RECURSE _files RELATIVE "${_stage}" "${_stage}/${_top}/*")
    list(SORT _files)
    string(JOIN "\n" _list ${_files})
    file(WRITE "${_stage}/files.txt" "${_list}\n")
    file(REMOVE "${DIST_DIR}/${_top}.zip")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E tar cf "${DIST_DIR}/${_top}.zip" --format=zip --files-from=files.txt
        WORKING_DIRECTORY "${_stage}"
        RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "DocPackage.cmake: cannot write ${_top}.zip")
    endif()
    file(REMOVE_RECURSE "${_stage}")
    file(SHA256 "${DIST_DIR}/${_top}.zip" _hash)
    string(APPEND _sums "${_hash}  ${_top}.zip\n")
endforeach()
file(WRITE "${DIST_DIR}/SHA256SUMS" "${_sums}")
message(STATUS "API documentation packages: ${DIST_DIR}")
