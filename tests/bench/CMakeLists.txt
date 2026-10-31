# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later
# The bring-up sketches: one small image each, proving as little as possible so that a failure points somewhere specific (README.md). Not part of `all`:
#   cmake --build build/avr-default --target bench                  all of them
#   cmake --build build/avr-default --target upload_bench_T05_PhaseCheck      flash one (resets the board: ask first)
# Built with the SD variant's settings; they use `Serial` (the SD variant leaves it free). REQ-CoreLR-D-Build-0011.
corelr_bench_libraries()
corelr_add_bench(T01_LcdJoystick ${CMAKE_CURRENT_LIST_DIR}/T01_LcdJoystick/T01_LcdJoystick.ino)
corelr_add_bench(T02_Startup     ${CMAKE_CURRENT_LIST_DIR}/T02_Startup/T02_Startup.ino)
# T03 runs the real firmware itself plus a stack painter and a UART report (~270 bytes). It fitted the SD image after the size flags (REQ-CoreLR-D-Build-0012), but the SD image has
# grown since (30 524 of 30 720 bytes: 196 free) and T03 is now 82 bytes TOO BIG for the Nano (found by the first CI run, 2026-10-26): STANDALONE keeps it out of `bench`, built by naming
# it, until the image or the painter shrinks. Meanwhile the SD stack is measured statically (tools/analysis/stack_depth.py).
corelr_add_bench(T03_StackWatch  ${CMAKE_CURRENT_LIST_DIR}/T03_StackWatch/T03_StackWatch.ino STANDALONE)
corelr_add_bench(T05_PhaseCheck  ${CMAKE_CURRENT_LIST_DIR}/T05_PhaseCheck/T05_PhaseCheck.ino)
corelr_add_bench(T09_LcdRom      ${CMAKE_CURRENT_LIST_DIR}/T09_LcdRom/T09_LcdRom.ino)
