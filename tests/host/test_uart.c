/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of the UART driver (REQ-CoreLR-D-Uart): the real Uart.c is #included, the USART0 registers are plain variables,
 * and the two interrupt handlers are called by the test exactly where the hardware would raise them.
 *
 *   gcc -m32 -fpack-struct=1 -O1 -Wall -DCORELR_JOB_SOURCE=CORELR_JOB_SOURCE_SERIAL -Ishim -I../../firmware/src test_uart.c -o out/test_uart
 */
#include <stdio.h>
#include <string.h>
#include <avr/io.h>

/* UCSR0A and UDR0 are accessors, not plain variables: the receiver can hold bytes that nobody has read yet (rxPending), as the bootloader may leave it.
 * RXC0 reads as set while there are some, and each read of UDR0 takes one. In every other test rxPending is 0 and they behave as plain variables. */
static volatile uint8_t ucsr0a_reg, udr0_reg;
static int rxPending;
static volatile uint8_t *ucsr0a_p(void) { if (rxPending > 0) ucsr0a_reg |= (uint8_t)(1u << RXC0); else ucsr0a_reg &= (uint8_t)~(1u << RXC0); return &ucsr0a_reg; }
static volatile uint8_t *udr0_p(void) { if (rxPending > 0) rxPending--; return &udr0_reg; }
#define UCSR0A (*ucsr0a_p())
#define UDR0   (*udr0_p())
#include "../../firmware/src/Uart.c"

volatile uint8_t UCSR0B, UCSR0C, SREG;
volatile uint16_t UBRR0;

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; if (failures < 25) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

/* one byte arrives, with the status the hardware would show */
static void rx(uint8_t b, uint8_t status) { UCSR0A = status; UDR0 = b; ISR_USART_RX_vect(); UCSR0A = 0; }
static void rxs(const char *s) { while (*s) rx((uint8_t)*s++, 0); }
/* the transmitter: every time the data register is empty and the interrupt is enabled, the handler runs */
static int sentN; static uint8_t sent[400];
static void txRun(int maxIsrs)
{
   while (maxIsrs-- > 0 && (UCSR0B & (1u << UDRIE0)))
   {
      ISR_USART_UDRE_vect();
      if (sentN < 400) sent[sentN++] = UDR0;
   }
}
static void reset(void) { UCSR0B = 0; Uart_Init(); sentN = 0; }

int main(void)
{
   printf("1. Configuration\n");
   reset();
   CHECK(UBRR0 == 16 && (UCSR0A & (1u << U2X0)), "UBRR0 = %u, U2X0 %d: expected 16 with the double-speed mode", UBRR0, (UCSR0A >> U2X0) & 1);
   CHECK(UCSR0B == ((1u << RXEN0) | (1u << TXEN0) | (1u << RXCIE0)), "UCSR0B = 0x%02X: receiver, transmitter and the receive interrupt only", UCSR0B);
   CHECK(UCSR0C == ((1u << UCSZ01) | (1u << UCSZ00)), "UCSR0C = 0x%02X: 8N1", UCSR0C);
   printf("   UBRR0 %u (U2X0): %lu baud actual, %+.1f %% from %lu\n", UBRR0, (unsigned long)UART_ACTUAL_BAUD, 100.0 * ((double)UART_ACTUAL_BAUD / (double)UART_BAUD - 1.0), (unsigned long)UART_BAUD);

   printf("1b. Init reads out what the bootloader left in the receiver\n");
   rxPending = 3; reset();
   CHECK(rxPending == 0, "%d byte(s) still in the receiver after Uart_Init: the bootloader's leftovers would be taken for the first bytes", rxPending);
   rxPending = 1; reset();
   CHECK(rxPending == 0, "a single leftover byte was not read out");
   CHECK(Uart_RxCount() == 0 && !Uart_RxAtGap(), "the leftovers must not reach the ring");
   printf("2. Data goes in the ring, in order; real-time bytes do not\n");
   reset();
   rxs("G1 X10\n");
   CHECK(Uart_RxCount() == 7 && Uart_RxFree() == 121, "count %u free %u", Uart_RxCount(), Uart_RxFree());
   { char got[8]; int i; for (i = 0; i < 7; i++) got[i] = (char)Uart_RxPop(); got[7] = 0; CHECK(strcmp(got, "G1 X10\n") == 0, "got '%s'", got); }
   CHECK(Uart_RxCount() == 0, "ring not empty after taking everything");
   rx('?', 0); rx('!', 0); rx('~', 0); rx(0x18, 0); rx(0x85, 0);
   CHECK(Uart_RxCount() == 0, "a real-time byte went into the ring");
   CHECK(Uart_TakeRealtime() == (UART_RT_STATUS | UART_RT_HOLD | UART_RT_RESUME | UART_RT_CANCEL | UART_RT_JOGCANCEL), "real-time flags");
   CHECK(Uart_TakeRealtime() == 0, "the flags were not cleared by taking them");
   rx('?', 0); rx('?', 0); rx('?', 0);
   CHECK(Uart_TakeRealtime() == UART_RT_STATUS, "three '?' give one flag");
   rxs("G1"); rx('?', 0); rxs("X5"); rx('!', 0); rxs("\n");     /* in the middle of a line */
   CHECK(Uart_RxCount() == 5, "real-time bytes inside a line were stored: %u bytes", Uart_RxCount());
   { char got[6]; int i; for (i = 0; i < 5; i++) got[i] = (char)Uart_RxPop(); got[5] = 0; CHECK(strcmp(got, "G1X5\n") == 0, "the line was not rejoined: '%s'", got); }
   CHECK(Uart_TakeRealtime() == (UART_RT_STATUS | UART_RT_HOLD), "flags from inside a line");

   reset(); rx(0x84, 0); rx(0x86, 0); rx(0x80, 0); rx(0xFF, 0);
   CHECK(Uart_TakeRealtime() == 0 && Uart_RxCount() == 0, "the other grbl override bytes (0x84, 0x86...) must still be dropped, flag-free");
   printf("3. What is dropped, what is kept\n");
   reset();
   { int b, kept = 0; for (b = 0; b < 256; b++) { rx((uint8_t)b, 0); }
     CHECK(Uart_TakeRealtime() == (UART_RT_STATUS | UART_RT_HOLD | UART_RT_RESUME | UART_RT_CANCEL | UART_RT_JOGCANCEL), "of all 256 byte values exactly the five real-time ones raise a flag");
     while (Uart_RxCount()) { uint8_t v = Uart_RxPop(); kept++; CHECK(v != '?' && v != '!' && v != '~' && v != 0x18 && v < 0x7F, "0x%02X kept", v); }
     CHECK(kept == 0x7F - 4, "%d bytes kept of 256: expected 123 (0x00..0x7E minus the four real-time bytes)", kept);
     printf("   of the 256 byte values, %d reach the ring (0x00..0x7E but the real-time four)\n", kept); }
   CHECK(!Uart_RxAtGap(), "dropping 0x7F..0xFF is not a loss of data (it is a decision)");
   reset(); rx(0x0A, 0); rx(0x0D, 0); rx(0x01, 0); rx(' ', 0); rx('(', 0); rx(';', 0);
   CHECK(Uart_RxCount() == 6, "control characters, spaces and comment marks are line data for the line assembler, not for the driver: %u", Uart_RxCount());

   printf("4. The ring holds 128 bytes, exactly\n");
   reset();
   { int i; for (i = 0; i < 128; i++) rx((uint8_t)('A' + i % 26), 0);
     CHECK(Uart_RxCount() == 128 && Uart_RxFree() == 0, "after 128 bytes: count %u free %u", Uart_RxCount(), Uart_RxFree());
     rx('Z', 0);
     CHECK(Uart_RxCount() == 128, "the 129th byte changed the count: %u", Uart_RxCount());
     for (i = 0; i < 128; i++) { CHECK(Uart_RxAtGap() == 0, "128 bytes is not an overflow: a gap was reported before byte %d (a host that counts to 128 must never cause one)", i); { uint8_t v = Uart_RxPop(); CHECK(v == (uint8_t)('A' + i % 26), "byte %d is %c: the overflow damaged the ring", i, v); } }
     CHECK(Uart_RxCount() == 0 && Uart_RxFree() == 128, "after reading all: count %u free %u", Uart_RxCount(), Uart_RxFree());
     rx('Y', 0);
     CHECK(Uart_RxAtGap() == 1, "the byte after the lost one was not reported as following a gap");
     CHECK(Uart_RxPop() == 'Y' && Uart_RxAtGap() == 0, "the gap is reported once only"); }

   printf("5. The 8-bit indices wrap: 100 000 bytes in bursts of random length, order kept\n");
   reset();
   { unsigned long in = 0, out = 0; unsigned seed = 7; int errs = 0;
     while (in < 100000UL)
     {
        unsigned n; seed = seed * 1664525u + 1013904223u; n = (seed >> 16) % 129u;
        while (n-- && Uart_RxFree() > 0) { rx((uint8_t)('a' + in % 26), 0); in++; }
        seed = seed * 1664525u + 1013904223u; n = (seed >> 16) % 129u;
        while (n-- && Uart_RxCount() > 0) { if (Uart_RxPop() != (uint8_t)('a' + out % 26)) errs++; out++; }
     }
     while (Uart_RxCount() > 0) { if (Uart_RxPop() != (uint8_t)('a' + out % 26)) errs++; out++; }
     CHECK(errs == 0 && in == out, "%d bytes out of order, in %lu out %lu", errs, in, out);
     CHECK(!Uart_RxAtGap(), "a gap was reported although the ring was never overfilled"); }

   printf("6. Hardware errors\n");
   reset();
   rxs("AB"); rx('X', 1u << FE0); rxs("CD");
   CHECK(Uart_RxCount() == 4, "a framing error: the byte must be dropped (%u bytes stored, expected 4)", Uart_RxCount());
   { char order[5] = ""; int i, gapAt = -1;
     for (i = 0; i < 4; i++) { if (Uart_RxAtGap()) gapAt = i; order[i] = (char)Uart_RxPop(); }
     CHECK(strcmp(order, "ABCD") == 0 && gapAt == 2, "framing error: '%s', gap before byte %d (expected ABCD and 2: C follows the lost byte)", order, gapAt); }
   rxs("AB"); rx('C', 1u << DOR0); rxs("D");
   CHECK(Uart_RxCount() == 4, "an overrun: the byte in the register is good (%u stored, expected 4)", Uart_RxCount());
   { char order[5] = ""; int i, gapAt = -1;
     for (i = 0; i < 4; i++) { if (Uart_RxAtGap()) gapAt = i; order[i] = (char)Uart_RxPop(); }
     CHECK(strcmp(order, "ABCD") == 0 && gapAt == 2, "overrun: '%s', gap before byte %d (expected ABCD and 2: the lost byte came before C)", order, gapAt); }
   rx('Q', 1u << FE0);                                    /* a loss with the ring EMPTY: the next byte is the one after the gap */
   rx('R', 0);
   CHECK(Uart_RxAtGap() == 1 && Uart_RxPop() == 'R', "a loss on an empty ring: the next byte was not marked");
   rxs("AB"); rx('x', 1u << FE0); rxs("C"); rx('y', 1u << FE0); rxs("D");   /* a second loss before the first is read past folds into it */
   { int i, gaps = 0, at = -1; for (i = 0; i < 4; i++) { if (Uart_RxAtGap()) { gaps++; at = i; } (void)Uart_RxPop(); }
     CHECK(gaps == 1 && at == 2, "two losses: %d gaps reported, at byte %d (expected the first only, before byte 2)", gaps, at); }
   rxs("AB"); rx('x', 1u << FE0); Uart_RxFlush(); rxs("CD");
   CHECK(Uart_RxAtGap() == 0, "a flush must forget a pending gap");
   Uart_RxFlush();

   printf("7. Flush\n");
   reset(); rxs("G1 X10 Y10\n"); rx('?', 0);
   Uart_RxFlush();
   CHECK(Uart_RxCount() == 0 && Uart_RxFree() == 128, "after the flush: count %u", Uart_RxCount());
   CHECK(Uart_TakeRealtime() == UART_RT_STATUS, "the flush touched the real-time flags");
   rxs("OK"); CHECK(Uart_RxCount() == 2 && Uart_RxPop() == 'O', "the ring does not work after a flush");

   printf("8. Transmission\n");
   reset();
   CHECK(Uart_TxFree() == UART_TX_SIZE, "tx free %u", Uart_TxFree());
   Uart_TxWrite("ok\r\n", 4);
   CHECK((UCSR0B & (1u << UDRIE0)) != 0 && Uart_TxFree() == UART_TX_SIZE - 4, "after queueing 4 bytes: UDRIE0 %d, free %u", (UCSR0B >> UDRIE0) & 1, Uart_TxFree());
   txRun(10);
   CHECK(sentN == 4 && memcmp(sent, "ok\r\n", 4) == 0, "sent %d bytes: '%.*s'", sentN, sentN, sent);
   CHECK((UCSR0B & (1u << UDRIE0)) == 0, "the data-register-empty interrupt was left on with nothing to send");
   CHECK(Uart_TxFree() == UART_TX_SIZE, "tx free %u after sending", Uart_TxFree());
   Uart_TxWriteP("[MSG:x]\r\n");
   txRun(20);
   CHECK(sentN == 4 + 9 && memcmp(sent + 4, "[MSG:x]\r\n", 9) == 0, "flash string not sent as is");
   CHECK((UCSR0B & ((1u << RXEN0) | (1u << TXEN0) | (1u << RXCIE0))) == ((1u << RXEN0) | (1u << TXEN0) | (1u << RXCIE0)), "starting and stopping the transmit interrupt disturbed the other bits of UCSR0B");

   printf("9. A full transmit ring drops, never blocks, never overwrites\n");
   reset();
   { char big[UART_TX_SIZE + 72]; int i; for (i = 0; i < (int)sizeof(big); i++) big[i] = (char)('a' + i % 26);
     Uart_TxWrite(big, sizeof(big));
     CHECK(Uart_TxFree() == 0, "free %u after offering more than the ring", Uart_TxFree());
     txRun(200);
     CHECK(sentN == UART_TX_SIZE, "%d bytes sent: expected the first %u only", sentN, (unsigned)UART_TX_SIZE);
     for (i = 0; i < (int)UART_TX_SIZE; i++) CHECK(sent[i] == (uint8_t)('a' + i % 26), "byte %d of the full ring is wrong", i); }

   printf("10. Interleaved writing and sending, 100 000 bytes through the wrapping ring\n");
   reset();
   { unsigned long in = 0, out = 0; unsigned seed = 11; int errs = 0;
     while (in < 100000UL)
     {
        unsigned n; seed = seed * 1664525u + 1013904223u; n = (seed >> 16) % 40u;
        while (n-- && Uart_TxFree() > 0) { char c = (char)('0' + in % 60); Uart_TxWrite(&c, 1); in++; }
        seed = seed * 1664525u + 1013904223u; n = (seed >> 16) % 40u;
        sentN = 0; txRun((int)n);
        { int i; for (i = 0; i < sentN; i++) { if (sent[i] != (uint8_t)('0' + out % 60)) errs++; out++; } }
     }
     sentN = 0; txRun(200); { int i; for (i = 0; i < sentN; i++) { if (sent[i] != (uint8_t)('0' + out % 60)) errs++; out++; } }
     CHECK(errs == 0 && in == out, "%d bytes out of order, in %lu out %lu", errs, in, out); }

   printf(failures ? "\n%d FAILURE(S)\n" : "\nall checks passed\n", failures);
   return failures != 0;
}
