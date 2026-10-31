/*
 * shim_sd/sdfat/SdFat.h -- a fake of the vendored sdfatlib for the host test of SDReader.cpp (test_sdreader.cpp).
 *
 * Part of the CoreLR wall-plotter firmware project.
 * Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * GPLv3, see LICENSE.
 *
 * Only what SDReader.cpp uses: Sd2Card (init, errorCode), SdVolume (init), SdFile (openRoot, open, close, isOpen, rewind, readDir, dirName, read, curPosition, fileSize).
 * The card is a tree in memory, in the order the "files were written" (not sorted: the firmware sorts). The card can be pulled and put back: after that every read fails
 * (error code set, -1 from readDir) until Sd2Card::init() has run again, as on a real card. Counters let the tests count the work (readDir calls, rewinds, card reads).
 */
#ifndef CORELR_SHIM_SDFAT_H
#define CORELR_SHIM_SDFAT_H

#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>
#include "sdfat/FatStructs.h"          /* the real dir_t and DIR_IS_SUBDIR (third_party) */

#define PIN_SPI_MOSI 11
#define PIN_SPI_MISO 12
#define PIN_SPI_SCK  13
#define SPI_HALF_SPEED 1
#define O_READ 1

struct FakeNode { char name[11]; bool dir; int parent; std::vector<int> kids; std::string data; };
extern std::vector<FakeNode> g_fs;           /* node 0 is the root */
extern bool g_cardPresent, g_cardNeedsInit;
extern int g_cardErr;                        /* the Sd2Card's error code */
extern long g_readDirCalls, g_rewinds, g_opens;
extern long g_failReadsAfter;                /* >= 0: the card is pulled after that many more SdFile::read() bytes (mid-line); -1: never */

class Sd2Card
{
 public:
   uint8_t init(uint8_t speed, uint8_t cs) { (void)speed; (void)cs; g_cardErr = 0; if (!g_cardPresent) { g_cardErr = 1; return 0; } g_cardNeedsInit = false; return 1; }
   uint8_t errorCode(void) const { return (uint8_t)g_cardErr; }
};

class SdVolume
{
 public:
   uint8_t init(Sd2Card *c) { (void)c; return g_cardPresent && !g_cardNeedsInit; }
};

class SdFile
{
 public:
   SdFile() : node_(-1), pos_(0), fpos_(0) {}
   uint8_t isOpen(void) const { return node_ >= 0; }
   void close(void) { node_ = -1; }
   uint8_t openRoot(SdVolume *v) { (void)v; if (!fail()) { node_ = 0; pos_ = 0; return 1; } return 0; }
   uint8_t open(SdFile *dir, const char *name, uint8_t flags)
   {
      (void)flags; g_opens++;
      if (node_ >= 0 || dir->node_ < 0 || fail()) return 0;
      for (int k : g_fs[dir->node_].kids) { char n[13]; dir_t e; memcpy(e.name, g_fs[k].name, 11); dirName(e, n); if (strcasecmp(n, name) == 0) { node_ = k; pos_ = 0; fpos_ = 0; return 1; } }
      return 0;
   }
   void rewind(void) { pos_ = 0; g_rewinds++; }
   int8_t readDir(dir_t *d)
   {
      g_readDirCalls++;
      if (fail()) return -1;
      if (node_ < 0 || pos_ >= (int)g_fs[node_].kids.size()) return 0;
      const FakeNode &n = g_fs[g_fs[node_].kids[pos_++]];
      memset(d, 0, sizeof *d); memcpy(d->name, n.name, 11); d->attributes = n.dir ? DIR_ATT_DIRECTORY : 0x20;
      return 1;
   }
   static void dirName(const dir_t &d, char *name)
   {
      uint8_t j = 0;
      for (uint8_t i = 0; i < 11; i++) { if (d.name[i] == ' ') continue; if (i == 8) name[j++] = '.'; name[j++] = (char)d.name[i]; }
      name[j] = 0;
   }
   int16_t read(void) { if (g_failReadsAfter == 0) { g_failReadsAfter = -1; g_cardNeedsInit = true; } else if (g_failReadsAfter > 0) g_failReadsAfter--; if (node_ < 0 || fail() || fpos_ >= g_fs[node_].data.size()) return -1; return (uint8_t)g_fs[node_].data[fpos_++]; }
   uint32_t curPosition(void) const { return fpos_; }
   uint32_t fileSize(void) const { return node_ >= 0 ? (uint32_t)g_fs[node_].data.size() : 0; }
 private:
   static bool fail(void) { if (!g_cardPresent || g_cardNeedsInit) { g_cardErr = 7; return true; } return false; }
   int node_; int pos_; uint32_t fpos_;
};

#endif
