/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Cost of the UART interrupts, on the AVR simulator (REQ-CoreLR-D-Uart, request M-7 of docs/protocol/SerialProtocol.md). Called directly: a
 * call+RETI costs the same 4+4 cycles as the hardware's response+RETI; the 3-cycle JMP in the vector table is not counted.
 * Registers are plain memory in the simulator, so the status and data registers are set by hand before each call.
 *   marks 1/2: receive, an ordinary data byte         marks 3/4: receive, a real-time byte ('?')
 *   marks 5/6: receive, a byte that overflows the ring (the rare path: a gap is noted)
 *   marks 7/8: receive, a framing error                marks 9/10: transmit, a byte that leaves more to send
 *   marks 11/12: transmit, the last byte (switches the interrupt off)
 * build:  ./build.sh t_uart.c t_uart ../../../firmware/src/Uart.c -DCORELR_JOB_SOURCE=CORELR_JOB_SOURCE_SERIAL && python3 run_uart.py out/t_uart.bin */
#include <avr/io.h>
#include <stdint.h>
#include "Uart.h"
#define MARK(v) (GPIOR2 = (uint8_t)(v))
extern void __vector_18(void); extern void __vector_19(void);

int main(void)
{
   uint8_t i;
   Uart_Init();
   for (i = 0; i < 8; i++) { UCSR0A = 0; UDR0 = (uint8_t)('A' + i); MARK(1); __vector_18(); MARK(2); }
   UCSR0A = 0; UDR0 = '?'; MARK(3); __vector_18(); MARK(4);
   Uart_RxFlush();
   for (i = 0; i < 128; i++) { UCSR0A = 0; UDR0 = 'x'; __vector_18(); }          /* the ring is full */
   UCSR0A = 0; UDR0 = 'y'; MARK(5); __vector_18(); MARK(6);
   Uart_RxFlush();
   UCSR0A = (uint8_t)(1u << 4); UDR0 = 'z'; MARK(7); __vector_18(); MARK(8);      /* FE0 */
   Uart_TxWrite("ok\r\n", 4);
   MARK(9); __vector_19(); MARK(10); __vector_19(); __vector_19();
   MARK(11); __vector_19(); MARK(12);
   GPIOR1 = 1; for (;;) {}
}
