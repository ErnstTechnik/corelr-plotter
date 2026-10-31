/*
 * SDReader.h
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

/* This module belongs to the SD card image only (CORELR_JOB_SOURCE_SD): in the other image this header is empty, so nothing can use the module by mistake,
 * and the documentation of that image does not list it. */
#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SD

/**
 * @file SDReader.h
 * @brief Background line reading from the SD card into a queue for the
 * GCode interpreter, plus the directory-browsing functions used by the
 * LCD/joystick SD file browser.
 *
 * Realizes: @req{REQ-CoreLR-F-JobStorage-0001}..@req{REQ-CoreLR-F-JobStorage-0002}, @req{REQ-CoreLR-F-UserInterface-0008}
 * Design:   @req{REQ-CoreLR-D-SDReader-0001}..@req{REQ-CoreLR-D-SDReader-0004}
 * See:      requirements/design/REQ-CoreLR-D-SDReader.md
 *
 * Implemented in a .cpp file (not .c) because it drives the Arduino SD
 * library, which is C++-only — permitted for imported libraries by
 * docs/standards/STD-FileOrganization.md. The public API below stays a plain C
 * surface (extern "C").
 */

#ifndef CORELR_SDREADER_H
#define CORELR_SDREADER_H

#include <stdint.h>
#include "SwConfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief One queued, comment-stripped, NUL-terminated GCode line. */
typedef char SDReader_Line_t[SDREADER_LINE_SIZE];

/*
 * The line queue's own types (indices, the ring buffer) are NOT
 * declared here: nothing outside this module names them and no public
 * signature mentions them, so they live in SDReader.cpp
 * (docs/standards/STD-FileOrganization.md, "Expose only what
 * callers need"). SDReader_Line_t above stays public -- callers do hold
 * a line buffer of their own.
 */


/** @brief Initialize the SD card (SDREADER_CS_PIN) and the line queue. */
/**
 * How far card bring-up got (@req{REQ-CoreLR-D-SDReader-0007}). Ordered by
 * the stage that failed, so the UI can say *what* is wrong instead of
 * showing the same "nothing here" for an absent card, an unformatted
 * one, and an empty one. The stages of SDReader_Mount():
 *
 * \dot
 * digraph SdMount {
 *    rankdir=LR;
 *    node [shape=box, style=rounded, fontname="Helvetica", fontsize=10];
 *    edge [fontname="Helvetica", fontsize=9];
 *    card [label="card init\n(SPI, chip select)"];
 *    vol  [label="FAT volume init"];
 *    root [label="open the root\ndirectory"];
 *    ok   [label="SDREADER_CARD_OK", style="rounded,bold"];
 *    card -> vol  [label="answers"];
 *    vol  -> root [label="usable volume"];
 *    root -> ok   [label="opens"];
 *    card -> ABSENT    [label="no answer"];
 *    vol  -> NO_VOLUME [label="none"];
 *    root -> NO_ROOT   [label="fails"];
 *    ABSENT    [label="SDREADER_CARD_ABSENT", style="rounded,dashed"];
 *    NO_VOLUME [label="SDREADER_CARD_NO_VOLUME", style="rounded,dashed"];
 *    NO_ROOT   [label="SDREADER_CARD_NO_ROOT", style="rounded,dashed"];
 * }
 * \enddot
 */
typedef enum
{
   SDREADER_CARD_OK = 0,     /**< mounted, root directory open */
   SDREADER_CARD_ABSENT,     /**< no card responded (Sd2Card::init failed) */
   SDREADER_CARD_NO_VOLUME,  /**< card responds but carries no usable FAT volume */
   SDREADER_CARD_NO_ROOT     /**< volume mounted but its root directory would not open */
} SDReader_CardStatus_t;

/** @brief Empty the line queue and mount the card (SDReader_Mount()): called once at start-up, whatever the card's state (the result is in SDReader_CardStatus()). */
void SDReader_Init(void);

/**
 * @brief (Re)mount the card: initialise the SPI card, the FAT volume
 * and the root directory, and reset the browser to the root.
 * @return 1 on success.
 *
 * SDReader_Init() calls this once at start-up. Calling it again is how
 * a card inserted after power-on becomes usable without a reset.
 *
 * **Blocks for up to ~2 s on failure** (sdfatlib's `SD_INIT_TIMEOUT`
 * while waiting for a card that never answers), so it must only ever
 * be called deliberately -- from start-up, or on an explicit user
 * request. Never poll it: a job cannot be running when the browser is
 * reachable (@req{REQ-CoreLR-D-LcdJoystickUI-0012}), but a 2 s stall in
 * loop() would still freeze the UI and starve the step queue.
 */
uint8_t SDReader_Mount(void);

/** @return how far the last SDReader_Mount() got. */
SDReader_CardStatus_t SDReader_CardStatus(void);

/**
 * @brief Background task: read one more line from the currently open
 * job file into the queue, if there is room (@req{REQ-CoreLR-D-SDReader-0001}).
 * Strips ';' and parenthesised comments and trailing whitespace
 * (@req{REQ-CoreLR-D-SDReader-0002}), NUL-terminates into a fixed-size buffer
 * (-0003). A line that becomes empty once stripped (blank or
 * comment-only) is not enqueued. Call every loop() iteration -- a
 * bounded, single-line unit of work per call, same pattern as every
 * other module's *_Task().
 */
void SDReader_Task(void);

/**
 * @brief Dequeue the next line for the interpreter.
 * @return 1 if a line was dequeued into `outLine`, 0 if the queue is empty.
 */
uint8_t SDReader_Dequeue(SDReader_Line_t outLine);

/**
 * @return 1 when no further job line will ever come: the file is at
 * end-of-file (or none is open) **and** the line queue is empty.
 *
 * The job-completion test needs both halves, plus Motion_IsIdle() and
 * Stepper_IsEmpty() on top -- lines can be exhausted while a couple of
 * hundred milliseconds of motion is still queued.
 */
uint8_t SDReader_JobIsExhausted(void);

/**
 * @brief Open a job file and reset the line queue/reading state.
 *
 * `name` is an 8.3 file name **in the browser's current directory**,
 * not an absolute path (@req{REQ-CoreLR-D-SDReader-0005}). That is what the
 * only caller has -- the user picks a file in the browser -- and it
 * means the file is opened straight out of the already-open directory
 * handle, with no path walk and no PATH_SIZE buffer on the stack.
 *
 * @return 1 on success.
 */
uint8_t SDReader_OpenJob(const char *name);

/**
 * @brief SD file browser: list the current directory's entries
 * (@req{REQ-CoreLR-D-LcdJoystickUI-0008}), filtered to SDREADER_BROWSER_EXT1/2/3
 * plus subdirectories, sorted directories-first then files,
 * alphabetically, with a ".." entry first when not at the SD root.
 * @return the number of listed entries (including "..", if present).
 */
uint8_t SDReader_BrowserEntryCount(void);

/**
 * @brief Fetch the entry name at `index` (directories get a trailing
 * '/'; ".." is returned as-is). Truncated to `outNameSize` if needed.
 */
void SDReader_BrowserGetEntryName(uint8_t index, char *outName, uint8_t outNameSize);

/** @return 1 if the entry at index is a directory (or ".."). */
uint8_t SDReader_BrowserIsDirectory(uint8_t index);

/**
 * @brief Enter the directory at index (or go up, for ".."), resetting
 * the current directory; if the entry is a file, open it as the job
 * (@req{REQ-CoreLR-D-LcdJoystickUI-0008}: "load it and start the job") --
 * check SDReader_BrowserIsDirectory() first if the caller needs to
 * know which one happened.
 *
 * Descending is **refused** (the listing simply does not change) when
 * the resulting path would not fit SDREADER_PATH_SIZE. See
 * @req{REQ-CoreLR-D-SDReader-0006} for why refusing beats truncating -- the
 * path is what SDReader_BrowserGoUp() re-walks -- and for the depth
 * that buys: at 32 bytes, 3 levels below the root for 8-character
 * directory names, 2 for maximal 8.3 names.
 */
uint8_t SDReader_BrowserEnter(uint8_t index);

/** @brief Go up to the parent directory (no-op at SD root). */
void SDReader_BrowserGoUp(void);

/** @return 1 if currently browsing the SD card root (nothing to go up to). */
uint8_t SDReader_BrowserAtRoot(void);

/**
 * @brief If a read of the card has failed since the last call (the card was pulled, or pulled and put back: it then has to be initialised again), remount it -- the browser is
 * back at the root, and SDReader_CardStatus() says whether the card answered -- and return 1; else 0. The UI calls it after each refresh of the browser and, on 1, resets its
 * index and refreshes again (@req{REQ-CoreLR-D-SDReader-0009}). May block up to ~2 s when there is no card (sdfatlib's init timeout), like the explicit retry.
 */
uint8_t SDReader_BrowserRecover(void);

/** @return 1 if the last SDReader_BrowserEnter() was on a directory whose name would not fit the path buffer: the descent was refused, the browser stayed where it was
 *  (@req{REQ-CoreLR-D-SDReader-0006}); 0 otherwise. The UI shows "Path too long". */
uint8_t SDReader_BrowserEnterRefused(void);

/** @return 1 if reading the job file failed on the card itself -- the card was pulled -- and not at the end of the file (@req{REQ-CoreLR-D-SDReader-0012}): the job cannot go on, the
 *  state machine stops it. Cleared when the next job is opened. */
uint8_t SDReader_JobFailed(void);

#ifdef __cplusplus
}
#endif

#endif /* CORELR_SDREADER_H */

#endif /* CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SD */
