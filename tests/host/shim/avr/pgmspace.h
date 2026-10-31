/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#define PROGMEM
#define pgm_read_byte(p) (*(const uint8_t *)(p))
#define pgm_read_dword(p) (*(const uint32_t *)(p))
#ifndef PSTR
#define PSTR(s) (s)
#endif
#ifndef pgm_read_word
#define pgm_read_word(p) (*(const uint16_t *)(p))
#endif
