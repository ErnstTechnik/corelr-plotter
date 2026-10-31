/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <stdint.h>
#include <string.h>
#define PROGMEM
#define PSTR(s) (s)
#define PGM_P const char *
#define pgm_read_byte(p) (*(const uint8_t *)(p))
#define pgm_read_word(p) ((uintptr_t)(*(const void * const *)(p)))  /* the UI reads POINTERS with it; an AVR word is a pointer, a host pointer is wider */
#define pgm_read_dword(p) (*(const uint32_t *)(p))
#define pgm_read_ptr(p) (*(void * const *)(p))
#define strncpy_P strncpy
#define strlen_P strlen
#define memcpy_P memcpy
#define strcpy_P strcpy
