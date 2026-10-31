/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Host stand-in for <avr/io.h>, for the LcdI2c test only: the TWI registers are OBJECTS, so that writing one
 * can have the effect it has on the chip (writing TWCR with TWINT set starts a bus operation), and PORTC. The
 * test defines the model that these call into. Compiled as C++ (the driver is plain C and compiles as C++). */
#pragma once
#include <stdint.h>
#define F_CPU 16000000UL
#define _BV(b) (1u << (b))
#define TWIE 0
#define TWEN 2
#define TWSTO 4
#define TWSTA 5
#define TWEA 6
#define TWINT 7
#define PC4 4
#define PC5 5
struct Reg
{
   int id;
   Reg &operator=(uint8_t v);
   operator uint8_t() const;
   Reg &operator|=(uint8_t v) { return *this = (uint8_t)((uint8_t)*this | v); }
};
extern Reg TWCR, TWSR, TWDR, TWBR, PORTC;
