# TargetDefs.cmake -- what the compiler is told about the target, in ONE place: the firmware build (cmake/Firmware.cmake) and the API documentation (cmake/Doxygen.cmake) read it, so that
# Doxygen sees the code under the same #if as the compiler does (REQ-CoreLR-D-Build-0013).
#
# Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# the -D of the Nano's build (what the Arduino IDE's builder passed: `arduino-builder -verbose`, fqbn arduino:avr:nano:cpu=atmega328)
set(CORELR_TARGET_DEFS F_CPU=16000000L ARDUINO=10600 ARDUINO_AVR_NANO ARDUINO_ARCH_AVR)

# what avr-gcc defines by itself for -mmcu=atmega328p (CORELR_MCU, cmake/Firmware.cmake): the compiler does not need them in its command line, Doxygen does
set(CORELR_TARGET_COMPILER_DEFS __AVR__ __AVR_ATmega328P__=1)
