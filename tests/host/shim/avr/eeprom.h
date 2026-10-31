/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Host stand-in for <avr/eeprom.h>: a byte array that can lose power. The
 * functions are defined in the test, which owns the array and the cut logic. */
#pragma once
#include <stdint.h>
#include <stddef.h>
uint8_t eeprom_read_byte(const uint8_t *addr);
void eeprom_read_block(void *dst, const void *src, size_t n);
void eeprom_update_block(const void *src, void *dst, size_t n);
void eeprom_update_byte(uint8_t *addr, uint8_t value);
#define eeprom_busy_wait() ((void)0)
