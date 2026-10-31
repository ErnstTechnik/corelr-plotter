/*
 * Uart.h
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

/* This module belongs to the serial image only (CORELR_JOB_SOURCE_SERIAL): in the other image this header is empty, so nothing can use the module by mistake,
 * and the documentation of that image does not list it. */
#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SERIAL

/**
 * @file Uart.h
 * @brief The serial port: a receive ring filled by interrupt, with the real-time bytes
 * taken out of the stream, and a transmit ring emptied by interrupt.
 *
 * Realizes: docs/protocol/SerialProtocol.md @proto{FW-02} (@req{REQ-CoreLR-F-SerialControl-0004}..).
 * Design:   @req{REQ-CoreLR-D-Uart-0001}..
 *
 * =====================================================================
 * THIS REPLACES THE ARDUINO `Serial` -- callers point at this module.
 * =====================================================================
 * `HardwareSerial` costs 732 bytes of flash and 175 of RAM (measured when it was unlinked from
 * the SD build), brings `Print` and `Stream`, and only reads bytes in `loop()`, so a `!` sent
 * while a line is held back would wait behind it, and be dropped if the ring were full. This
 * driver is smaller, and takes the real-time bytes out of the stream in the interrupt.
 * Nothing in the serial build may include `<HardwareSerial.h>` or call `Serial`: both define
 * the same two interrupt vectors, and the link would fail.
 *
 * Context: the two ISRs fill and empty the rings; everything else is called from loop().
 * Each ring has one writer and one reader that are never the same context, and indices are
 * free-running bytes, so no critical section is needed to read or write one: the one
 * exception is Uart_RxFlush(), which moves the *reader's* index.
 */

#ifndef CORELR_UART_H
#define CORELR_UART_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Real-time bytes (docs/protocol/SerialProtocol.md @protosec{4-2,4.2}), as the flags Uart_TakeRealtime() returns. Several of the same kind
 *  received before loop() looks count once: two `?` give one report. */
#define UART_RT_STATUS  0x01u  /**< `?` */
#define UART_RT_HOLD    0x02u  /**< `!` */
#define UART_RT_RESUME  0x04u  /**< `~` */
#define UART_RT_CANCEL  0x08u  /**< Ctrl-X (0x18) */
#define UART_RT_JOGCANCEL 0x10u /**< 0x85, grbl's jog cancel (docs/protocol/SerialProtocol.md @protosec{4-2,4.2}, @proto{P2}) */

/** @brief Configure the port (UART_BAUD, 8N1), empty both rings, enable the receiver and its interrupt. */
void Uart_Init(void);

/** @return how many received bytes wait in the ring (0..UART_RX_SIZE). */
uint8_t Uart_RxCount(void);

/** @return the free space of the receive ring: what the status report's `Bf:` says. */
uint8_t Uart_RxFree(void);

/** @brief Take the oldest received byte. Call only when Uart_RxCount() > 0. */
uint8_t Uart_RxPop(void);

/** @brief Discard everything received and not yet taken (Ctrl-X). */
void Uart_RxFlush(void);

/** @return the real-time flags received since the last call, and clear them. */
uint8_t Uart_TakeRealtime(void);

/**
 * @return 1, once, if the byte Uart_RxPop() would return next is the first one received after data was lost -- the ring
 * was full, or the hardware reported a framing error or an overrun. The caller (line assembly) then knows that the line
 * it is reading is damaged, and refuses it (docs/protocol/SerialProtocol.md @protosec{4-13,4.13}): a host that counts never causes it. Call it before
 * each Uart_RxPop(). A second loss before the first has been read past is folded into it.
 */
uint8_t Uart_RxAtGap(void);

/** @return the free space of the transmit ring. */
uint8_t Uart_TxFree(void);

/** @brief Queue bytes for sending. The caller has checked Uart_TxFree(): a byte that does not fit is dropped, never waited for
 *  -- nothing here blocks. */
void Uart_TxWrite(const char *data, uint8_t length);

/** @brief The same for a NUL-terminated string in flash (avr/pgmspace.h). */
void Uart_TxWriteP(const char *flashString);

#ifdef __cplusplus
}
#endif

#endif /* CORELR_UART_H */

#endif /* CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SERIAL */
