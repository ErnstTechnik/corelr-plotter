/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Host stand-in for <avr/io.h>: the registers Stepper.c touches, as plain variables.
 * The test defines them and owns the timer model. */
#pragma once
#include <stdint.h>
#define F_CPU 16000000UL
extern volatile uint8_t PORTB, PORTC, PORTD, DDRB, DDRC, DDRD, TCCR1A, TCCR1B, TIMSK1, TIFR1, SREG, TIMSK2, TIFR2, OCR2B;
extern volatile uint16_t TCNT1, OCR1A;
#define CS11   1
#define OCIE1A 1
#define OCF1A  1
#define OCIE2B 2
#define OCF2B  2
#define __AVR_ATmega328P__ 1
/* USART0, for Uart.c (a test defines the variables). The bit numbers are the ATmega328P's. */
extern volatile uint8_t UCSR0A, UCSR0B, UCSR0C, UDR0;
extern volatile uint16_t UBRR0;
#define RXC0 7
#define UDRE0 5
#define FE0 4
#define DOR0 3
#define U2X0 1
#define RXCIE0 7
#define UDRIE0 5
#define RXEN0 4
#define TXEN0 3
#define UCSZ01 2
#define UCSZ00 1
