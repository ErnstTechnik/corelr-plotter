/*
 * Uart.c
 * Part of the CoreLR wall-plotter firmware project.
 *
 * Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "SwConfig.h"

/* Serial variant only: in the SD build this file is empty, so that its two interrupt vectors, its rings (192 bytes of RAM) and
 * its code are not linked there (REQ-CoreLR-F-GCodeInterpreter-0013). The file's own documentation is inside the condition too, so that
 * the documentation of the SD image does not list an empty file. */
#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SERIAL

/**
 * @file Uart.c
 * @brief See Uart.h.
 */

#include "Uart.h"

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>

#if !(defined(__AVR_ATmega328P__) || defined(__AVR_ATmega328__))
#error "Uart.c assumes the ATmega328(P) USART0 register set (Arduino Uno/Nano)"
#endif

/*
 * Baud rate generator, double-speed mode (U2X0 = 1): UBRR = F_CPU / (8 * baud) - 1, rounded.
 * At 16 MHz and 115 200 that is 16 -- an actual 117 647 baud, +2.1 %, which is what the Arduino
 * core also does; without the double-speed mode it would be 111 111, -3.5 %. A bridge and a UART
 * tolerate a few percent between them (8N1 resynchronises on every start bit), but a different
 * clock or rate must not silently land somewhere worse, so the error is checked.
 */
#define UART_UBRR (((F_CPU + (4UL * UART_BAUD)) / (8UL * UART_BAUD)) - 1UL)   /**< the baud rate register value for UART_BAUD in double-speed mode, rounded */
#define UART_ACTUAL_BAUD (F_CPU / (8UL * (UART_UBRR + 1UL)))   /**< the baud rate that value really gives; checked to be within 2.5 % of UART_BAUD */
#if (UART_ACTUAL_BAUD * 1000UL > UART_BAUD * 1025UL) || (UART_ACTUAL_BAUD * 1000UL < UART_BAUD * 975UL)
#error "UART_BAUD cannot be generated within 2.5 % at this F_CPU -- pick another rate"
#endif

#if (UART_RX_SIZE & (UART_RX_SIZE - 1u)) != 0u || (UART_TX_SIZE & (UART_TX_SIZE - 1u)) != 0u
#error "UART_RX_SIZE and UART_TX_SIZE must be powers of two (the ring indices are masked)"
#endif
#if UART_RX_SIZE > 128u || UART_TX_SIZE > 128u
#error "a ring larger than 128 bytes would make 'full' ambiguous with the free-running 8-bit indices"
#endif

/*
 * Rings. The indices are free-running bytes (0..255, wrapping), the slot is `index & (SIZE - 1)`, and the fill is the
 * difference of the two: it can reach SIZE, where an index that wrapped with the slot number could only reach SIZE - 1.
 * The receive ring really holds 128 bytes, as the senders' character counting assumes (UART_RX_SIZE).
 *
 * Single writer, single reader per index, never the same context: the receive head is written by the ISR only, its tail
 * by loop() only; for the transmit ring the other way round. A one-byte read or write is atomic on AVR, so neither side
 * needs a critical section to use them, except Uart_RxFlush(), which moves the *reader's* index from outside.
 */
static volatile uint8_t s_rxHead;   /**< receive ring, write index (the receive ISR only) */
static volatile uint8_t s_rxTail;   /**< receive ring, read index (loop() only) */
static volatile uint8_t s_txHead;   /**< transmit ring, write index (loop() only) */
static volatile uint8_t s_txTail;   /**< transmit ring, read index (the data-register-empty ISR only) */
static volatile uint8_t s_realtime; /**< the UART_RT_* flags of the real-time bytes received and not yet taken by Uart_TakeRealtime() */
static volatile uint8_t s_gapAt;    /**< the receive index at which data was lost (valid while s_gapSet) */
static volatile uint8_t s_gapSet;   /**< 1 from a loss (MarkGap()) until Uart_RxAtGap() has reported it */
static uint8_t s_rx[UART_RX_SIZE];  /**< the receive ring's storage */
static uint8_t s_tx[UART_TX_SIZE];  /**< the transmit ring's storage */

void Uart_Init(void)
{
   UBRR0 = (uint16_t)UART_UBRR;
   UCSR0A = (uint8_t)(1u << U2X0);
   UCSR0C = (uint8_t)((1u << UCSZ01) | (1u << UCSZ00)); /* 8 data bits, no parity, 1 stop bit */
   while ((UCSR0A & (1u << RXC0)) != 0u)
   {
      (void)UDR0;                                        /* what the bootloader left in the receiver */
   }
   s_rxHead = 0;
   s_rxTail = 0;
   s_txHead = 0;
   s_txTail = 0;
   s_realtime = 0;
   s_gapSet = 0;
   UCSR0B = (uint8_t)((1u << RXEN0) | (1u << TXEN0) | (1u << RXCIE0));
}

/** Note that data was lost at the ring's head. Only the first gap is kept until it has been read past: a second one
 *  before that folds into it (a host that overruns twice in a ring's length is past saving one line).
 *  Inlined on purpose: an ISR that *calls* a function must save every call-clobbered register on entry (measured: 17 pushes and
 *  17 pops in the receive handler, 196 bytes) even though the call is on the rare path; without the call it saves the 8 it uses. */
static inline __attribute__((always_inline)) void MarkGap(void)
{
   if (s_gapSet == 0u)
   {
      s_gapAt = s_rxHead;
      __asm__ __volatile__("" ::: "memory");
      s_gapSet = 1u;
   }
}

/**
 * One received byte. Real-time bytes (docs/protocol/SerialProtocol.md @protosec{4-2,4.2}) become a flag and are not stored; bytes of 0x7F and
 * above are dropped (0x80..0xFF are grbl's override commands, not supported); everything else goes in the ring.
 * A framing error means the byte is garbage and is dropped; an overrun means an earlier byte was lost, and this one
 * is good. Both, and a full ring, mark a *gap* at the ring's head: the byte that will be stored next follows lost data
 * (Uart_RxAtGap()).
 */
ISR(USART_RX_vect)
{
   uint8_t status = UCSR0A;
   uint8_t data = UDR0;
   uint8_t head;

   if ((status & ((1u << FE0) | (1u << DOR0))) != 0u)
   {
      MarkGap();
      if ((status & (1u << FE0)) != 0u)
      {
         return;
      }
   }
   if (data == (uint8_t)'?')
   {
      s_realtime |= UART_RT_STATUS;
   }
   else if (data == (uint8_t)'!')
   {
      s_realtime |= UART_RT_HOLD;
   }
   else if (data == (uint8_t)'~')
   {
      s_realtime |= UART_RT_RESUME;
   }
   else if (data == 0x18u)
   {
      s_realtime |= UART_RT_CANCEL;
   }
   else if (data == 0x85u)
   {
      s_realtime |= UART_RT_JOGCANCEL;   /* the one grbl override byte we act on; the rest of 0x80..0xFF is dropped */
   }
   else if (data < 0x7Fu)
   {
      head = s_rxHead;
      if ((uint8_t)(head - s_rxTail) >= UART_RX_SIZE)
      {
         MarkGap();
         return;
      }
      s_rx[head & (UART_RX_SIZE - 1u)] = data;
      s_rxHead = (uint8_t)(head + 1u);
   }
   else
   {
      /* 0x7F..0xFF except 0x85: dropped */
   }
}

/** Data register empty: send the next byte, and switch this interrupt off after the last one. */
ISR(USART_UDRE_vect)
{
   uint8_t tail = s_txTail;

   UDR0 = s_tx[tail & (UART_TX_SIZE - 1u)];
   tail = (uint8_t)(tail + 1u);
   s_txTail = tail;
   if (tail == s_txHead)
   {
      UCSR0B = (uint8_t)(UCSR0B & (uint8_t)~(1u << UDRIE0));
   }
}

uint8_t Uart_RxCount(void)
{
   return (uint8_t)(s_rxHead - s_rxTail);
}

uint8_t Uart_RxFree(void)
{
   return (uint8_t)(UART_RX_SIZE - (uint8_t)(s_rxHead - s_rxTail));
}

uint8_t Uart_RxPop(void)
{
   uint8_t tail = s_rxTail;
   uint8_t data = s_rx[tail & (UART_RX_SIZE - 1u)];

   s_rxTail = (uint8_t)(tail + 1u);
   return data;
}

void Uart_RxFlush(void)
{
   uint8_t sreg = SREG;

   cli();
   s_rxTail = s_rxHead;
   s_gapSet = 0;
   SREG = sreg;
}

uint8_t Uart_TakeRealtime(void)
{
   uint8_t sreg = SREG;
   uint8_t flags;

   cli();
   flags = s_realtime;
   s_realtime = 0;
   SREG = sreg;
   return flags;
}

uint8_t Uart_RxAtGap(void)
{
   if ((s_gapSet != 0u) && (s_gapAt == s_rxTail))
   {
      s_gapSet = 0;
      return 1u;
   }
   return 0u;
}

uint8_t Uart_TxFree(void)
{
   return (uint8_t)(UART_TX_SIZE - (uint8_t)(s_txHead - s_txTail));
}

/** Put one byte in the transmit ring; 0 if it was full. Does not start the transmission. */
static uint8_t PutByte(uint8_t data)
{
   uint8_t head = s_txHead;

   if ((uint8_t)(head - s_txTail) >= UART_TX_SIZE)
   {
      return 0;
   }
   s_tx[head & (UART_TX_SIZE - 1u)] = data;
   __asm__ __volatile__("" ::: "memory");   /* the byte is in the ring before the head says so */
   s_txHead = (uint8_t)(head + 1u);
   return 1;
}

/** Let the UDRE interrupt run: it sends from the ring and switches itself off when it is empty. */
static void StartTransmission(void)
{
   uint8_t sreg = SREG;

   cli();                                        /* the interrupt also rewrites UCSR0B; do not interleave the two */
   UCSR0B = (uint8_t)(UCSR0B | (1u << UDRIE0));
   SREG = sreg;
}

void Uart_TxWrite(const char *data, uint8_t length)
{
   uint8_t i;

   for (i = 0; i < length; i++)
   {
      if (!PutByte((uint8_t)data[i]))
      {
         break;
      }
   }
   StartTransmission();
}

void Uart_TxWriteP(const char *flashString)
{
   uint8_t c = pgm_read_byte(flashString);

   while ((c != 0u) && PutByte(c))
   {
      flashString++;
      c = pgm_read_byte(flashString);
   }
   StartTransmission();
}

#endif /* CORELR_JOB_SOURCE_SERIAL */
