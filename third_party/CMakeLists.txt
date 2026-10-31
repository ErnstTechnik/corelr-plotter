# The vendored sources, as ordered lists the firmware targets use (see cmake/Firmware.cmake). Nothing is built here: each variant builds
# its own copy with its own flags (-mcall-prologues changes the core's code too, exactly as the Arduino IDE applied it to the core).
set(_core ${CMAKE_CURRENT_SOURCE_DIR}/arduino-core)

# Same order as the Arduino IDE's core archive, so the link order -- and with it the layout of the image -- is the same.
set(CORELR_ARDUINO_CORE_SOURCES
    wiring_pulse.S WInterrupts.c hooks.c wiring.c wiring_analog.c wiring_digital.c wiring_pulse.c wiring_shift.c
    CDC.cpp HardwareSerial.cpp HardwareSerial0.cpp HardwareSerial1.cpp HardwareSerial2.cpp HardwareSerial3.cpp
    IPAddress.cpp PluggableUSB.cpp Print.cpp Stream.cpp Tone.cpp USBCore.cpp WMath.cpp WString.cpp abi.cpp main.cpp new.cpp)
list(TRANSFORM CORELR_ARDUINO_CORE_SOURCES PREPEND ${_core}/cores/arduino/)

set(CORELR_ARDUINO_INCLUDES ${_core}/cores/arduino ${_core}/variants/eightanaloginputs)
set(CORELR_SPI_SOURCES  ${_core}/libraries/SPI/src/SPI.cpp)
set(CORELR_SPI_INCLUDES ${_core}/libraries/SPI/src)

set(CORELR_SDFAT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/sdfat/Sd2Card.cpp ${CMAKE_CURRENT_SOURCE_DIR}/sdfat/SdFile.cpp ${CMAKE_CURRENT_SOURCE_DIR}/sdfat/SdVolume.cpp)
set(CORELR_THIRD_PARTY_INCLUDE_ROOT ${CMAKE_CURRENT_SOURCE_DIR})   # firmware/src/SDReader.cpp includes "sdfat/SdFat.h"

foreach(_v CORELR_ARDUINO_CORE_SOURCES CORELR_ARDUINO_INCLUDES CORELR_SPI_SOURCES CORELR_SPI_INCLUDES CORELR_SDFAT_SOURCES CORELR_THIRD_PARTY_INCLUDE_ROOT)
    set(${_v} ${${_v}} PARENT_SCOPE)
endforeach()
