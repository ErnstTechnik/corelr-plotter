/*
 * SDReader.cpp
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

/* SD variant only (the file's own documentation is inside the condition too: the documentation of the serial image does not list an empty file). */
#if CORELR_JOB_SOURCE == CORELR_JOB_SOURCE_SD

/**
 * @file SDReader.cpp
 * @brief See SDReader.h.
 */

/*
 * SdFat's lower layer directly, NOT <SD.h>: the SDClass/File wrapper
 * costs far too much stack and heap for a 2KB target
 * (REQ-CoreLR-D-SDReader-0005).
 *
 * And it is the copy vendored under src/sdfat/, not the one inside the
 * SD library, so that SD.cpp is never compiled at all -- merely having
 * it linked cost 73 bytes of RAM for a global object whose code is
 * entirely unreachable. See src/sdfat/PROVENANCE.md.
 */
#include "sdfat/SdFat.h"
#include <string.h>
#include "SDReader.h"
#include "HwConfig.h"

/*
 * Pin-allocation guard (added 2026-09-10, after a real wiring mistake).
 *
 * The hardware SPI pins are fixed by the silicon: once SPI.begin() sets
 * SPE with MSTR, the SPI unit overrides DDRB for MOSI/MISO/SCK
 * regardless of what any other module writes. So neither the chip
 * select nor a motor wire may be placed on one of them -- a CS there
 * can never be driven (the card is never selected), and a motor wire
 * there is simply dead, which shows up as a motor that buzzes instead
 * of turning rather than as an outright failure. Neither symptom points
 * at the pin map, which is why this is worth catching at compile time.
 *
 * Only meaningful for the SD variant: with CORELR_JOB_SOURCE_SERIAL
 * there is no SPI peripheral in use and D11-D13 are ordinary GPIO.
 *
 * PIN_SPI_* come from the board variant's pins_arduino.h (via
 * Arduino.h, pulled in by Sd2Card.h) and are real preprocessor macros,
 * so they can be tested here -- unlike the Arduino core's MOSI/MISO/SCK,
 * which are `static const uint8_t` and invisible to #if. Undefined
 * identifiers evaluate to 0 in #if, which would silently turn every
 * test below into a comparison against pin 0, so their presence is
 * checked first rather than assumed.
 */
#if !defined(PIN_SPI_MOSI) || !defined(PIN_SPI_MISO) || !defined(PIN_SPI_SCK)
#error "PIN_SPI_MOSI/MISO/SCK not defined -- the pin-allocation guard below would silently compare against pin 0. Check that Arduino.h/pins_arduino.h is reachable for this board."
#endif

/** Is pin `p` one of the hardware SPI pins (MOSI, MISO, SCK)? For the guards below only; undefined again after them. */
#define SDREADER_PIN_IS_SPI(p) (((p) == PIN_SPI_MOSI) || ((p) == PIN_SPI_MISO) || ((p) == PIN_SPI_SCK))

#if SDREADER_PIN_IS_SPI(SDREADER_CS_PIN)
#error "SDREADER_CS_PIN (HwConfig.h) is on a hardware SPI pin (MOSI/MISO/SCK). The SPI unit owns those pins' direction once enabled, so the chip select could never be driven and the card would never be selected. Pick an ordinary GPIO -- see docs/hardware/PinMapping.md."
#endif

#if SDREADER_PIN_IS_SPI(STEPPER_MOTOR_L_PIN0) || SDREADER_PIN_IS_SPI(STEPPER_MOTOR_L_PIN1) \
 || SDREADER_PIN_IS_SPI(STEPPER_MOTOR_L_PIN2) || SDREADER_PIN_IS_SPI(STEPPER_MOTOR_L_PIN3)
#error "A Motor L pin (HwConfig.h) is on a hardware SPI pin (MOSI/MISO/SCK). Writes to it would not reach the pin: that coil wire is dead and the 4-phase pattern is wrong -- the motor buzzes instead of turning. See docs/hardware/PinMapping.md."
#endif

#if SDREADER_PIN_IS_SPI(STEPPER_MOTOR_R_PIN0) || SDREADER_PIN_IS_SPI(STEPPER_MOTOR_R_PIN1) \
 || SDREADER_PIN_IS_SPI(STEPPER_MOTOR_R_PIN2) || SDREADER_PIN_IS_SPI(STEPPER_MOTOR_R_PIN3)
#error "A Motor R pin (HwConfig.h) is on a hardware SPI pin (MOSI/MISO/SCK). Writes to it would not reach the pin: that coil wire is dead and the 4-phase pattern is wrong -- the motor buzzes instead of turning. See docs/hardware/PinMapping.md."
#endif

#if (STEPPER_MOTOR_L_PIN0 == SDREADER_CS_PIN) || (STEPPER_MOTOR_L_PIN1 == SDREADER_CS_PIN) \
 || (STEPPER_MOTOR_L_PIN2 == SDREADER_CS_PIN) || (STEPPER_MOTOR_L_PIN3 == SDREADER_CS_PIN) \
 || (STEPPER_MOTOR_R_PIN0 == SDREADER_CS_PIN) || (STEPPER_MOTOR_R_PIN1 == SDREADER_CS_PIN) \
 || (STEPPER_MOTOR_R_PIN2 == SDREADER_CS_PIN) || (STEPPER_MOTOR_R_PIN3 == SDREADER_CS_PIN)
#error "A motor pin (HwConfig.h) is the same pin as SDREADER_CS_PIN. Both drive it as an output and would fight each other. See docs/hardware/PinMapping.md."
#endif

#undef SDREADER_PIN_IS_SPI

/** Queue indices (write/read). */
typedef struct
{
   uint8_t WriteIndex;
   uint8_t ReadIndex;
} QueueIndices_t;

/** Full GCode line queue. */
typedef struct
{
   QueueIndices_t Indices;                         /**< where the next line is written and where the next is read */
   SDReader_Line_t Lines[SDREADER_QUEUE_SIZE];     /**< the slots: raw GCode lines, comments stripped */
} LineQueue_t;

/**
 * Forward declarations for every file-local helper, right after the
 * includes -- confirmed 2026-09-07 as a standing convention: function
 * bodies can then be ordered however reads best.
 */
static uint8_t StripComments(const char *rawLine, char *outLine, uint8_t outCapacity);
static uint8_t RawExtIs(const uint8_t *rawExt, const char *dotted);
static uint8_t HasGCodeExtension(const uint8_t *rawName);
static uint8_t HasParentDirectory(void);
static uint8_t EntryMatches(const dir_t *entry, uint8_t wantDirectory);
static uint8_t CountEntries(uint8_t wantDirectory);
static void NoteCardError(void);
static void InvalidateListing(void);
static uint8_t FindEntryAtRank(uint8_t wantDirectory, uint8_t rank, uint8_t *outRawName);
static void FormatRawName(const uint8_t *rawName, char *outName);
static uint8_t ResolveBrowserEntry(uint8_t index, char *outName, uint8_t outNameSize, uint8_t *outIsDirectory);
static uint8_t OpenDirByPath(const char *path);
static uint8_t PathHasRoomFor(const char *name);
static void AppendPathComponent(const char *name);

/** The look-ahead of GCode lines read from the job file, filled by SDReader_Task(), emptied by SDReader_Dequeue() (one slot is kept empty to tell full from empty). */
static LineQueue_t s_queue;

/**
 * The SdFat stack, owned by this module (@req{REQ-CoreLR-D-SDReader-0005}).
 * SDClass used to own an equivalent trio privately; instantiating them
 * here is what lets us bypass it. SdVolume's 512-byte block cache is a
 * *static* member of the class, so it is not duplicated by having our
 * own SdVolume -- only the ~30 bytes of per-volume geometry are.
 */
static Sd2Card  s_card;
static SdVolume s_volume;   /**< the FAT volume of the card: its geometry (the 512-byte block cache is shared, see above) */

/**
 * The browser's current directory, held **open** across calls. This is
 * the whole point of the SdFile rewrite: listing an entry no longer
 * re-opens the directory by path (SDClass::open, 120-byte frame) nor
 * opens each entry as a file just to read its name
 * (File::openNextFile, 113 bytes + two heap blocks) -- it rewinds this
 * handle and walks raw directory entries.
 */
static SdFile s_dir;
/** The job file being streamed, open from SDReader_OpenJob() until the job ends or fails. */
static SdFile s_jobFile;

/**
 * Current SD browser directory as text (@req{REQ-CoreLR-D-LcdJoystickUI-0008});
 * "/" = card root. Kept in step with s_dir, and needed for exactly one
 * thing s_dir cannot do: find the *parent* on "go up". FAT stores a
 * ".." entry, but SdFile::open() rejects the name (make83Name() allows
 * a single '.'), and opening it by directory index would mishandle the
 * cluster-0 "parent is root" convention -- so going up re-walks this
 * path from the root instead. That happens once per navigation, never
 * while listing.
 */
static char s_currentPath[SDREADER_PATH_SIZE] = "/";

/** Which bring-up stage last failed (@req{REQ-CoreLR-D-SDReader-0007}).
 * Stored as uint8_t rather than the enum: one byte either way on AVR,
 * and it keeps the accessor's cast explicit. */
static uint8_t s_cardStatus = (uint8_t)SDREADER_CARD_ABSENT;

/** Set when a read of the card failed (@req{REQ-CoreLR-D-SDReader-0009}): the card was pulled, or pulled and put back (it then has to be initialised again). The scans only
 *  NOTE it -- they sit on the deepest call chain, where a remount (its own frames) must not run; SDReader_BrowserRecover() acts on it from a shallow caller. */
static uint8_t s_cardLost = 0;

/** 1 when the last SDReader_BrowserEnter() on a directory was refused for want of room in the path buffer (@req{REQ-CoreLR-D-SDReader-0011}): the UI says so. */
static uint8_t s_enterRefused = 0;

/** 1 when reading the job file failed on the card itself (pulled, not the end of the file): the job cannot go on (@req{REQ-CoreLR-D-SDReader-0012}). Cleared by SDReader_OpenJob(). */
static uint8_t s_jobFailed = 0;

void SDReader_Init(void)
{
   s_queue.Indices.WriteIndex = 0;
   s_queue.Indices.ReadIndex = 0;
   (void)SDReader_Mount();
}

uint8_t SDReader_Mount(void)
{
   /* What SDClass::begin() used to do for us, minus the wrapper -- but
    * recording which stage failed (REQ-CoreLR-D-SDReader-0007) instead
    * of discarding it, so the UI can distinguish an absent card from an
    * unreadable one from an empty one. */
   s_dir.close();
   s_jobFile.close();
   InvalidateListing();
   s_currentPath[0] = '/';
   s_currentPath[1] = '\0';

   if (!s_card.init(SPI_HALF_SPEED, SDREADER_CS_PIN))
   {
      s_cardStatus = SDREADER_CARD_ABSENT;
      return 0;
   }
   if (!s_volume.init(&s_card))
   {
      s_cardStatus = SDREADER_CARD_NO_VOLUME;
      return 0;
   }
   if (!s_dir.openRoot(&s_volume))
   {
      s_cardStatus = SDREADER_CARD_NO_ROOT;
      return 0;
   }
   s_cardStatus = SDREADER_CARD_OK;
   return 1;
}

SDReader_CardStatus_t SDReader_CardStatus(void)
{
   return (SDReader_CardStatus_t)s_cardStatus;
}

/* CORELR_NOINLINE: keeps this out of main()'s permanent frame -- see SwConfig.h */
CORELR_NOINLINE
void SDReader_Task(void)
{
   uint8_t writeIndex = s_queue.Indices.WriteIndex;
   uint8_t nextWriteIndex = (uint8_t)((writeIndex + 1u) % SDREADER_QUEUE_SIZE);
   char *slot = s_queue.Lines[writeIndex];
   uint8_t rawLen = 0;
   uint8_t outLen;

   if (nextWriteIndex == s_queue.Indices.ReadIndex)
   {
      return; /* queue full: try again next call (REQ-CoreLR-D-SDReader-0001) */
   }
   if (s_jobFailed || !s_jobFile.isOpen() || (s_jobFile.curPosition() >= s_jobFile.fileSize()))
   {
      return; /* no job open, or nothing left to read */
   }

   /*
    * Read one line by hand: SdFile derives from Print, not Stream, so
    * there is no readBytesUntil() at this layer. No loss of speed --
    * File::read() went through the same per-byte SdFile::read(), just
    * with an extra virtual call -- and it fixes a rough edge of the
    * old code: a line longer than the buffer used to leave its tail to
    * be picked up as a second, bogus line. Here the overflow is
    * consumed and discarded up to the newline, so the next line read
    * is a real line.
    */
   for (;;)
   {
      int16_t c = s_jobFile.read();

      if ((c < 0) || (c == '\n'))
      {
         break; /* end of line, end of file, or read error */
      }
      if (rawLen < (uint8_t)(SDREADER_LINE_SIZE - 1u))
      {
         slot[rawLen] = (char)c;
         rawLen++;
      }
   }
   slot[rawLen] = '\0';
   if (s_card.errorCode() != 0u)
   {
      s_jobFailed = 1;   /* the read failed on the card, it is not the end of the file: the job stops (REQ-CoreLR-D-SDReader-0012); the browser remounts on its next refresh */
      s_cardLost = 1;
      return;            /* the half line just read is not published */
   }

   /*
    * Stripped **in place**, inside the queue slot: no separate raw-line
    * buffer, which took 48 bytes of loop()'s frame -- and loop() sits
    * under the deepest call chain in the firmware
    * (REQ-CoreLR-D-SDReader-0005). Safe because StripComments() is a
    * single left-to-right pass whose output index never overtakes its
    * input index (each iteration advances the input by at least as much
    * as the output, and the character is read before being written).
    * The slot is not published until WriteIndex advances below, so a
    * line that strips to nothing simply never becomes visible.
    */
   outLen = StripComments(slot, slot, (uint8_t)SDREADER_LINE_SIZE);

   if (outLen > 0u)
   {
      s_queue.Indices.WriteIndex = nextWriteIndex; /* else: blank/comment-only line, slot just gets overwritten next call */
   }
}

/**
 * @brief Strip ';' (to end of line) and parenthesised comments
 * (@req{REQ-CoreLR-D-SDReader-0002}, same convention as grbl), then trim
 * trailing whitespace back to the last meaningful character. Multiple
 * parenthesised comments on one line are each stripped independently;
 * an unclosed '(' drops everything from it to the end of the line.
 * @return the stripped line's length (0 if it became empty).
 */
static uint8_t StripComments(const char *rawLine, char *outLine, uint8_t outCapacity)
{
   uint8_t inIndex = 0;
   uint8_t outIndex = 0;

   while (rawLine[inIndex] != '\0' && outIndex < (uint8_t)(outCapacity - 1u))
   {
      char c = rawLine[inIndex];

      if (c == ';')
      {
         break;
      }
      if (c == '(')
      {
         inIndex++;
         while (rawLine[inIndex] != '\0' && rawLine[inIndex] != ')')
         {
            inIndex++;
         }
         if (rawLine[inIndex] == ')')
         {
            inIndex++;
         }
         continue;
      }
      outLine[outIndex] = c;
      outIndex++;
      inIndex++;
   }

   while (outIndex > 0u
       && (outLine[outIndex - 1u] == ' ' || outLine[outIndex - 1u] == '\t'
        || outLine[outIndex - 1u] == '\v' || outLine[outIndex - 1u] == '\r'))
   {
      outIndex--;
   }

   outLine[outIndex] = '\0';
   return outIndex;
}

uint8_t SDReader_JobIsExhausted(void)
{
   /* Both halves matter: the file being at EOF is not enough while
    * lines already read are still waiting in the queue. */
   uint8_t queueEmpty = (uint8_t)(s_queue.Indices.WriteIndex == s_queue.Indices.ReadIndex);
   uint8_t fileDone = (uint8_t)(!s_jobFile.isOpen()
                                || (s_jobFile.curPosition() >= s_jobFile.fileSize()));

   return (uint8_t)(queueEmpty && fileDone);
}

uint8_t SDReader_Dequeue(SDReader_Line_t outLine)
{
   uint8_t writeIndex = s_queue.Indices.WriteIndex;
   uint8_t readIndex = s_queue.Indices.ReadIndex;

   if (writeIndex == readIndex)
   {
      return 0; /* empty */
   }
   memcpy(outLine, s_queue.Lines[readIndex], sizeof(SDReader_Line_t));
   s_queue.Indices.ReadIndex = (uint8_t)((readIndex + 1u) % SDREADER_QUEUE_SIZE);
   return 1;
}

uint8_t SDReader_OpenJob(const char *name)
{
   s_queue.Indices.WriteIndex = 0;
   s_queue.Indices.ReadIndex = 0;
   uint8_t opened;

   s_jobFailed = 0;
   s_jobFile.close(); /* SdFile::open() refuses an already-open handle */
   opened = (uint8_t)(s_jobFile.open(&s_dir, name, O_READ) ? 1u : 0u);
   NoteCardError();   /* a failed open on a card that was pulled and put back: the browser recovers on its next refresh */
   return opened;
}

/** @brief Case-insensitive suffix match against SDREADER_BROWSER_EXT1/2/3. */
/**
 * @brief Does the raw 3-character FAT extension field match a ".xyz"
 * literal from SwConfig.h?
 *
 * Works on the directory entry's own bytes rather than on a formatted
 * name, so the browser never has to build one while scanning
 * (@req{REQ-CoreLR-D-SDReader-0008}). The field is fixed-width, upper case
 * and space-padded, so ".gc" is matched as 'G','C',' '.
 *
 * The tunables stay written the familiar way (".gc", ".nc") rather than
 * as raw fields, and the padding is applied here. A consequence falls
 * out for free and is worth stating: an extension longer than three
 * characters can never match, which is exactly why ".gcode" had to
 * become ".gco" (REQ-CoreLR-D-SDReader's open items).
 */
static uint8_t RawExtIs(const uint8_t *rawExt, const char *dotted)
{
   uint8_t i;

   dotted++; /* skip the leading '.' */
   for (i = 0; i < 3u; i++)
   {
      char want;

      if (*dotted != '\0')
      {
         want = ((*dotted >= 'a') && (*dotted <= 'z')) ? (char)(*dotted - 'a' + 'A') : *dotted;
         dotted++;
      }
      else
      {
         want = ' '; /* FAT pads the field with spaces */
      }
      if ((char)rawExt[i] != want)
      {
         return 0;
      }
   }
   return (uint8_t)(*dotted == '\0');
}

/** @brief Filter on the raw 11-byte name field of a directory entry. */
static uint8_t HasGCodeExtension(const uint8_t *rawName)
{
   return (uint8_t)(RawExtIs(&rawName[8], SDREADER_BROWSER_EXT1)
                 || RawExtIs(&rawName[8], SDREADER_BROWSER_EXT2)
                 || RawExtIs(&rawName[8], SDREADER_BROWSER_EXT3));
}

/** Is the browser below the root, i.e. is there a parent to go up to? (The current path is anything but "/".) */
static uint8_t HasParentDirectory(void)
{
   return (uint8_t)(strcmp(s_currentPath, "/") != 0);
}

/**
 * @brief Does raw directory entry `entry` belong to the group the
 * browser is currently listing? Writes its formatted 8.3 name into
 * `outName` (at least 13 bytes) either way, since the caller needs it
 * for the extension test and for ordering.
 *
 * Only 8.3 short names are visible at this layer, which is a property
 * of the SD library as a whole, not of this rewrite -- File::name()
 * called the same SdFile::dirName() formatter. One consequence worth
 * knowing: an extension longer than three characters (".gcode") can never
 * match, because a short name's extension field is 3 characters wide --
 * which is why SDREADER_BROWSER_EXT2 is ".gco". A file saved as
 * "PLOT.GCODE" appears here under its generated short name
 * ("PLOT~1.GCO" typically), so it is the *card's* naming that decides,
 * not our filter list. Left as-is rather than silently dropped: the
 * filter is harmless and long-name support is a separate question.
 */
static uint8_t EntryMatches(const dir_t *entry, uint8_t wantDirectory)
{
   uint8_t isDirectory = (uint8_t)(DIR_IS_SUBDIR(entry) ? 1u : 0u);

   if (isDirectory != wantDirectory)
   {
      return 0;
   }
   return (uint8_t)(wantDirectory ? 1u : HasGCodeExtension(entry->name));
}

/** @brief A failed card read leaves a code in the Sd2Card (cleared by its init()): remember it, so that the browser can start again from the root. */
static void NoteCardError(void)
{
   if (s_card.errorCode() != 0u)
   {
      s_cardLost = 1;
   }
}

/**
 * The listing of the current directory is never held in RAM (@req{REQ-CoreLR-D-SDReader-0005}), so what the browser keeps between two calls is small: the number of directories
 * and of files (found in ONE scan of the directory, and not again until the directory changes), and the last entry it was asked for (its rank and its raw name).
 * The UI moves by one entry at a time, so the next or the previous entry is one scan away from that one -- the smallest name above it, the largest below it -- where the
 * first version rescanned the directory once per entry from the start (k + 1 scans for the entry of rank k: the display slowed down along a list of 40 files,
 * bench 2026-10-13; @req{REQ-CoreLR-D-SDReader-0010}). Both are dropped whenever the directory (or the card) changes.
 */
static uint8_t s_countsValid = 0;
static uint8_t s_dirCount = 0;      /**< directories of the current directory (saturated at 255), valid while s_countsValid */
static uint8_t s_fileCount = 0;     /**< G-code files of the current directory (saturated at 255), valid while s_countsValid */
static uint8_t s_rcValid = 0;       /**< the "last entry asked for" below is valid: its kind, rank and raw 8.3 name are the next scan's starting point */
static uint8_t s_rcKind = 0;        /**< the last entry asked for: 1 = a directory, 0 = a file */
static uint8_t s_rcRank = 0;        /**< its rank within its kind (0 = first in name order) */
static uint8_t s_rcName[11];        /**< its raw 8.3 name, as stored in the directory entry */

/** Forget the counts and the last entry: the directory (or the card) has changed, the next call scans again. */
static void InvalidateListing(void)
{
   s_countsValid = 0;
   s_rcValid = 0;
}

/** @brief Count the directories and the G-code files of the current directory, once per directory: one scan. */
static uint8_t CountEntries(uint8_t wantDirectory)
{
   dir_t entry;

   if (!s_dir.isOpen())
   {
      return 0;
   }
   if (!s_countsValid)
   {
      uint8_t dirs = 0;
      uint8_t files = 0;

      s_dir.rewind();
      while (s_dir.readDir(&entry) > 0)
      {
         if (EntryMatches(&entry, 1u))
         {
            dirs = (uint8_t)(dirs + (dirs < 255u));      /* saturating: a directory of more than 255 entries shows its first 255, not a wrapped count */
         }
         else if (EntryMatches(&entry, 0u))
         {
            files = (uint8_t)(files + (files < 255u));
         }
      }
      NoteCardError();
      s_dirCount = dirs;
      s_fileCount = files;
      s_countsValid = (uint8_t)(s_cardLost ? 0u : 1u);
   }
   return wantDirectory ? s_dirCount : s_fileCount;
}

/**
 * @brief One scan of the current directory: among the entries of kind `wantDirectory`, the smallest raw name above `above` (when given) or -- with `wantLargest` -- the largest
 * below `below` (when given; neither: the smallest, or the largest, of all). Ordering is on the **raw 11-byte FAT name field**, not on the formatted "NAME.EXT" string
 * (@req{REQ-CoreLR-D-SDReader-0008}): the two orderings are identical -- the raw field is fixed-width and space-padded, and a space (0x20) sorts below every character valid in a short
 * name, exactly as the formatted string's terminator and its '.' (0x2E) do (verified exhaustively on the host over 576 ordered pairs, zero disagreements). The point is
 * stack: this sits on the deepest call chain of the firmware, and formatting during the scan cost three 13-byte buffers.
 * @return 1 and the name in `best`, or 0 (no such entry, or the card is gone: s_cardLost).
 */
static uint8_t ScanPass(uint8_t wantDirectory, const uint8_t *above, const uint8_t *below, uint8_t wantLargest, uint8_t *best)
{
   dir_t entry;
   uint8_t haveBest = 0;

   s_dir.rewind();
   while (s_dir.readDir(&entry) > 0)
   {
      int8_t order;

      if (!EntryMatches(&entry, wantDirectory))
      {
         continue;
      }
      if ((above != 0) && (memcmp(entry.name, above, 11u) <= 0))
      {
         continue; /* at or below the previous pick */
      }
      if ((below != 0) && (memcmp(entry.name, below, 11u) >= 0))
      {
         continue; /* at or above it */
      }
      if (haveBest)
      {
         order = (int8_t)((memcmp(entry.name, best, 11u) < 0) ? -1 : 1);
         if ((wantLargest != 0u) ? (order < 0) : (order > 0))
         {
            continue; /* not better than the best so far */
         }
      }
      memcpy(best, entry.name, 11u);
      haveBest = 1;
   }
   NoteCardError();
   return (uint8_t)(haveBest && !s_cardLost);
}

/**
 * @brief Find the `rank`-th (0-based) name, alphabetically, among entries matching `wantDirectory` in the current directory --
 * @req{REQ-CoreLR-D-LcdJoystickUI-0008}'s directories-first-then-alphabetical ordering, without ever holding the listing in RAM.
 * One scan when the rank is that of the last entry asked for plus or minus one (the UI's only move), or the last of its kind (the wrap-around of the list), or the same;
 * otherwise (a jump, the first call in a directory) a chain of rank + 1 scans, each from the previous pick, as before (@req{REQ-CoreLR-D-SDReader-0010}).
 */
static uint8_t FindEntryAtRank(uint8_t wantDirectory, uint8_t rank, uint8_t *outRawName)
{
   uint8_t *name = outRawName;
   uint8_t found = 0;

   if (!s_dir.isOpen())
   {
      return 0;
   }
   if (s_rcValid && (s_rcKind == wantDirectory) && (s_rcRank == rank))
   {
      memcpy(name, s_rcName, sizeof(s_rcName));
      found = 1;
   }
   else if (s_rcValid && (s_rcKind == wantDirectory) && ((uint8_t)(s_rcRank + 1u) == rank))
   {
      found = ScanPass(wantDirectory, s_rcName, 0, 0, name);
   }
   else if (s_rcValid && (s_rcKind == wantDirectory) && ((uint8_t)(rank + 1u) == s_rcRank))
   {
      found = ScanPass(wantDirectory, 0, s_rcName, 1, name);
   }
   else if (((uint8_t)(rank + 1u) == CountEntries(wantDirectory)) && (CountEntries(wantDirectory) < 255u))
   {
      found = ScanPass(wantDirectory, 0, 0, 1, name); /* the last of its kind (not when the count saturated: the last one is then not the 255th) */
   }
   else
   {
      uint8_t pass;

      /* each pick is the floor of the next scan; s_rcName holds it (the scan must not compare against the buffer it writes the best name to) */
      s_rcValid = 0;
      for (pass = 0; pass <= rank; pass++)
      {
         found = ScanPass(wantDirectory, (pass != 0u) ? s_rcName : 0, 0, 0, name);
         if (!found)
         {
            break; /* fewer matching entries than the requested rank (or the card is gone) */
         }
         memcpy(s_rcName, name, sizeof(s_rcName));
      }
   }
   if (!found)
   {
      return 0;
   }
   s_rcValid = 1;
   s_rcKind = wantDirectory;
   s_rcRank = rank;
   memcpy(s_rcName, name, sizeof(s_rcName));
   return 1;
}

/** @brief "NAME.EXT" from a raw 11-byte name, by the library's own formatter (the convention stays in one place). A function of its own, called after the search has returned:
 *  its directory-entry buffer (32 bytes) must not sit on the deepest call chain under the search's. */
CORELR_NOINLINE
static void FormatRawName(const uint8_t *rawName, char *outName)
{
   dir_t entry;

   memcpy(entry.name, rawName, 11u);
   SdFile::dirName(entry, outName);
}

/**
 * @brief Resolve a browser index (@req{REQ-CoreLR-D-LcdJoystickUI-0008}: an
 * optional ".." first, then directories, then filtered files, each
 * group alphabetical) to a raw 8.3 name and its directory/file kind.
 */
static uint8_t ResolveBrowserEntry(uint8_t index, char *outName, uint8_t outNameSize, uint8_t *outIsDirectory)
{
   uint8_t hasParent = HasParentDirectory();
   uint8_t realIndex = index;
   uint8_t dirCount;

   if (hasParent)
   {
      if (index == 0u)
      {
         strncpy(outName, "..", (size_t)(outNameSize - 1u));
         outName[outNameSize - 1u] = '\0';
         *outIsDirectory = 1;
         return 1;
      }
      realIndex = (uint8_t)(index - 1u);
   }

   if (outNameSize < 13u)
   {
      outName[0] = '\0';
      return 0; /* the caller's buffer cannot hold "NAME.EXT\0" */
   }
   dirCount = CountEntries(1u);
   *outIsDirectory = (uint8_t)((realIndex < dirCount) ? 1u : 0u);
   /* The raw 11-byte name is found into the caller's own buffer (13 bytes at least) and formatted in place: no buffer of ours on the deepest chain. */
   if (!(*outIsDirectory ? FindEntryAtRank(1u, realIndex, (uint8_t *)outName) : FindEntryAtRank(0u, (uint8_t)(realIndex - dirCount), (uint8_t *)outName)))
   {
      outName[0] = '\0';
      return 0;
   }
   FormatRawName((const uint8_t *)outName, outName);
   return 1;
}

/** @brief Can s_currentPath still hold one more "/name" component? */
static uint8_t PathHasRoomFor(const char *name)
{
   uint8_t len = (uint8_t)strlen(s_currentPath);
   uint8_t need = (uint8_t)(strlen(name) + 1u); /* separator + name */

   return (uint8_t)(((uint16_t)len + need) < SDREADER_PATH_SIZE);
}

/**
 * @brief Append one component to s_currentPath, in place.
 * Caller must have checked PathHasRoomFor() first. In place rather
 * than via a build-then-copy helper so no second PATH_SIZE buffer ever
 * lands on the stack.
 */
static void AppendPathComponent(const char *name)
{
   uint8_t len = (uint8_t)strlen(s_currentPath);

   if ((len > 0u) && (s_currentPath[len - 1u] != '/'))
   {
      s_currentPath[len] = '/';
      len++;
   }
   strcpy(&s_currentPath[len], name);
}

/**
 * @brief Re-open s_dir at `path`, walking components down from the
 * card root. Used only on "go up" (and at init) -- never while
 * listing, which is the point: this is the one function that still
 * costs a path walk, and it runs once per navigation action.
 *
 * On failure s_dir is left closed, and every browser entry point
 * treats that as an empty directory.
 */
static uint8_t OpenDirByPath(const char *path)
{
   SdFile child;
   const char *cursor = path;

   s_dir.close();
   if (!s_dir.openRoot(&s_volume))
   {
      return 0;
   }
   while (*cursor != '\0')
   {
      char component[13];
      uint8_t n = 0;

      while (*cursor == '/')
      {
         cursor++;
      }
      if (*cursor == '\0')
      {
         break;
      }
      while ((*cursor != '\0') && (*cursor != '/') && (n < (uint8_t)(sizeof(component) - 1u)))
      {
         component[n] = *cursor;
         n++;
         cursor++;
      }
      component[n] = '\0';
      while ((*cursor != '\0') && (*cursor != '/'))
      {
         cursor++; /* discard the tail of an over-long component */
      }

      child.close(); /* reused across iterations; open() refuses an open handle */
      if (!child.open(&s_dir, component, O_READ))
      {
         s_dir.close();
         return 0;
      }
      s_dir.close();
      s_dir = child; /* memberwise copy: child's slot is reused, s_dir keeps the open file */
   }
   return 1;
}

uint8_t SDReader_BrowserEntryCount(void)
{
   uint16_t count = (uint16_t)((uint16_t)CountEntries(1u) + CountEntries(0u) + (HasParentDirectory() ? 1u : 0u));

   return (uint8_t)((count > 255u) ? 255u : count);   /* the UI's index is a byte: the first 255 entries of a longer directory */
}

void SDReader_BrowserGetEntryName(uint8_t index, char *outName, uint8_t outNameSize)
{
   uint8_t isDirectory;

   if (outNameSize == 0u)
   {
      return;
   }
   if (!ResolveBrowserEntry(index, outName, outNameSize, &isDirectory))
   {
      outName[0] = '\0';
      return;
   }
   if (isDirectory && strcmp(outName, "..") != 0)
   {
      uint8_t nameLen = (uint8_t)strlen(outName);

      if (nameLen < (uint8_t)(outNameSize - 1u))
      {
         outName[nameLen] = '/';
         outName[nameLen + 1u] = '\0';
      }
   }
}

uint8_t SDReader_BrowserIsDirectory(uint8_t index)
{
   char scratchName[13];
   uint8_t isDirectory = 0;

   ResolveBrowserEntry(index, scratchName, sizeof(scratchName), &isDirectory);
   return isDirectory;
}

uint8_t SDReader_BrowserEnter(uint8_t index)
{
   char name[13];
   uint8_t isDirectory;

   s_enterRefused = 0;
   if (!ResolveBrowserEntry(index, name, sizeof(name), &isDirectory))
   {
      return 0;
   }
   if (!isDirectory)
   {
      /* Reports success so the caller only starts a job that really
       * opened -- the return value used to be discarded, so a failed
       * open (card pulled between listing and selecting) silently
       * looked like a started job. */
      return SDReader_OpenJob(name);
   }
   if (strcmp(name, "..") == 0)
   {
      SDReader_BrowserGoUp();
      return 0;
   }

   /*
    * Descending is the fast case: the child is opened straight out of
    * the already-open parent handle, so no path is walked and no
    * PATH_SIZE buffer is needed. The text path is only updated to keep
    * "go up" and BrowserAtRoot() working.
    */
   if (PathHasRoomFor(name))
   {
      SdFile child;

      if (child.open(&s_dir, name, O_READ))
      {
         s_dir.close();
         s_dir = child;
         AppendPathComponent(name);
         InvalidateListing();
      }
      else
      {
         NoteCardError();
      }
   }
   else
   {
      s_enterRefused = 1;
   }
   return 0;
}

void SDReader_BrowserGoUp(void)
{
   char *lastSlash = strrchr(s_currentPath, '/');

   if (!lastSlash || (lastSlash == s_currentPath))
   {
      s_currentPath[0] = '/';
      s_currentPath[1] = '\0';
   }
   else
   {
      *lastSlash = '\0';
   }
   InvalidateListing();
   (void)OpenDirByPath(s_currentPath); /* the one remaining path walk */
}

uint8_t SDReader_JobFailed(void)
{
   return s_jobFailed;
}

uint8_t SDReader_BrowserEnterRefused(void)
{
   return s_enterRefused;
}

uint8_t SDReader_BrowserRecover(void)
{
   if (!s_cardLost)
   {
      return 0;
   }
   s_cardLost = 0;
   (void)SDReader_Mount();   /* back to the root; the status says whether the card answered (absent: "No SD card") */
   return 1;
}

uint8_t SDReader_BrowserAtRoot(void)
{
   return (uint8_t)(!HasParentDirectory());
}

#endif /* CORELR_JOB_SOURCE_SD */
