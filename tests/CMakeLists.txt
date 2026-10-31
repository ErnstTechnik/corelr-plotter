# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# Host tests: the real firmware modules compiled with the NATIVE compiler against stubs (tests/host/README.md). CTest runs the
# existing scripts, so a test is a script in one place; both arithmetics must pass.
cmake_minimum_required(VERSION 3.20)
foreach(m 48 64)
    add_test(NAME host_math${m} COMMAND ${CMAKE_CURRENT_LIST_DIR}/host/run_all.sh CORELR_MATH_${m})
endforeach()
add_test(NAME cmake_size_check COMMAND ${CMAKE_CURRENT_LIST_DIR}/cmake/test_check_size.sh)
add_test(NAME tension_chart    COMMAND ${CMAKE_CURRENT_LIST_DIR}/tools/test_tension_chart.py)
add_test(NAME flag_truth_table COMMAND ${CMAKE_CURRENT_LIST_DIR}/tools/test_flag_truth_table.py)
add_test(NAME mutants_union       COMMAND ${CMAKE_CURRENT_LIST_DIR}/tools/test_check_mutants_union.py)
add_test(NAME mutant_batch_checker COMMAND ${CMAKE_CURRENT_LIST_DIR}/tools/test_mutant_batch_checker.py)
add_test(NAME mutant_batches      COMMAND ${CMAKE_CURRENT_LIST_DIR}/tools/test_mutant_batches.py)   # every mutant of every batch can run (known defects: tests/mutation/stale_mutants.txt)
add_test(NAME tools_have_help COMMAND ${CMAKE_CURRENT_LIST_DIR}/../tools/check_help.sh)
add_test(NAME doc_links       COMMAND ${CMAKE_CURRENT_LIST_DIR}/../tools/check_links.py)

# The `doc` target (cmake/Doxygen.cmake, REQ-CoreLR-D-Build-0013). The self-test (the classifier of the Doxygen log and the output checker, on fabricated cases) needs
# nothing; the other two run the real thing and need Doxygen, so they are registered only when it was found at configure time (the Doxygen graphs need `dot`, optional).
add_test(NAME variant_modules COMMAND ${CMAKE_CURRENT_LIST_DIR}/tools/test_variant_modules.py)   # cmake/variant_modules.txt against the #if guards of firmware/src (REQ-CoreLR-D-Build-0014)
add_test(NAME image_symbols COMMAND ${CMAKE_CURRENT_LIST_DIR}/tools/test_image_symbols.py)   # the symbol check of the linked images, on fabricated cases (and on the images of a build, if there is one) (REQ-CoreLR-D-Build-0015)
add_test(NAME version COMMAND ${CMAKE_CURRENT_LIST_DIR}/tools/test_check_version.py --cmake-version ${CORELR_VERSION})   # firmware/config/Version.h: well formed, written once, read alike by CMake (REQ-CoreLR-D-Build-0020)
add_test(NAME req_refs COMMAND ${CMAKE_CURRENT_LIST_DIR}/tools/test_req_refs.py)   # the requirement anchors and the @req{} citations of the code and the pages (REQ-CoreLR-D-Build-0019)
add_test(NAME gcode_page COMMAND ${CMAKE_CURRENT_LIST_DIR}/tools/test_gcode_page.py)   # docs/doxygen/gcode.md against the real parser of each image (REQ-CoreLR-D-Build-0018)
add_test(NAME doxygen_selftest COMMAND ${CMAKE_CURRENT_LIST_DIR}/tools/test_doxygen.py)
if(CORELR_DOXYGEN_FOUND)
    add_test(NAME doxygen_build  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target doc)
    # index.html, no markup warning, both variants, no absolute path, the gaps of docs/doxygen/gaps_*.txt (compared only for the Doxygen version that wrote them)
    add_test(NAME doxygen_output COMMAND ${CMAKE_CURRENT_LIST_DIR}/tools/test_doxygen.py --doc-dir ${CORELR_DOC_DIR} --doxygen-version ${DOXYGEN_VERSION})
    add_test(NAME doxygen_package_build COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target doc_package)
    add_test(NAME doxygen_package COMMAND ${CMAKE_CURRENT_LIST_DIR}/tools/test_doxygen.py --doc-dir ${CORELR_DOC_DIR} --doxygen-version ${DOXYGEN_VERSION}
             --package-dir ${CORELR_DOC_DIR}/dist --package-version ${CORELR_VERSION})   # the release zips (REQ-CoreLR-D-Build-0016)
    set_tests_properties(doxygen_build  PROPERTIES FIXTURES_SETUP    corelr_doc)
    set_tests_properties(doxygen_output PROPERTIES FIXTURES_REQUIRED corelr_doc)
    set_tests_properties(doxygen_package_build PROPERTIES FIXTURES_REQUIRED corelr_doc FIXTURES_SETUP corelr_doc_package)
    set_tests_properties(doxygen_package PROPERTIES FIXTURES_REQUIRED "corelr_doc;corelr_doc_package")
else()
    message(STATUS "Doxygen not found: the tests doxygen_build, doxygen_output, doxygen_package_build and doxygen_package are not registered")
endif()
