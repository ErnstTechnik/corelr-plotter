/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define A0 14
#define A1 15
#define A2 16
#define A3 17
#define A4 18
#define A5 19
#define A6 20
#define A7 21
#define HIGH 1
#define LOW 0
void pinMode(uint8_t, uint8_t);
int analogRead(uint8_t);
int digitalRead(uint8_t);
void digitalWrite(uint8_t, uint8_t);
unsigned long millis(void);
