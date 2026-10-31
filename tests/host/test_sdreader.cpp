/* Host test of the SD browser in the real SDReader.cpp, against a fake of sdfatlib (shim_sd/sdfat/SdFat.h): the card is a tree in memory, in unsorted order, that can
 * be pulled and put back. What it proves: the listing order (directories first, then the files with a G-code extension, each alphabetical, on the raw 8.3 names),
 * entering and leaving directories, the depth bound of SDREADER_PATH_SIZE (a path that would not fit is refused, never truncated), the job file's lines, and the recovery
 * from a card that was pulled and put back during a navigation (REQ-CoreLR-D-SDReader-0009): the browser is at the root again, the UI's index is reset.
 * Prints 'all checks passed' or the failing case. Copyright (C) 2026 ErnstTechnik and the CoreLR contributors; GPLv3. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <algorithm>
#include "sdfat/SdFat.h"
#include "SDReader.h"

std::vector<FakeNode> g_fs; bool g_cardPresent = true, g_cardNeedsInit = false; int g_cardErr = 0; long g_readDirCalls, g_rewinds, g_opens; long g_failReadsAfter = -1;

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; if (failures < 30) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

/* ---- the fake card ---- */
static int add(int parent, const char *name83, bool dir, const char *data = "")
{
   FakeNode n; memset(n.name, ' ', 11); n.dir = dir; n.parent = parent; n.data = data;
   const char *dot = strchr(name83, '.'); int base = dot ? (int)(dot - name83) : (int)strlen(name83);
   for (int i = 0; i < base && i < 8; i++) n.name[i] = name83[i];
   if (dot) for (int i = 0; i < 3 && dot[1 + i]; i++) n.name[8 + i] = dot[1 + i];
   g_fs.push_back(n); int id = (int)g_fs.size() - 1; g_fs[parent].kids.push_back(id); return id;
}
static void reset_card(void) { g_fs.clear(); FakeNode r; memset(r.name, ' ', 11); r.dir = true; r.parent = -1; g_fs.push_back(r); g_cardPresent = true; g_cardNeedsInit = false; g_cardErr = 0; }

/* ---- what the UI does (LcdJoystickUI.cpp, RefreshBrowserEntry) ---- */
struct Ui { uint8_t index, total; char name[13]; uint8_t isDir; };
static void refresh(Ui *u)
{
   for (uint8_t pass = 0; pass < 2u; pass++)
   {
      u->total = SDReader_BrowserEntryCount();
      if (u->total > 0u) { SDReader_BrowserGetEntryName(u->index, u->name, sizeof u->name); u->isDir = SDReader_BrowserIsDirectory(u->index); }
      else { u->name[0] = 0; u->isDir = 0; }
      if (!SDReader_BrowserRecover()) break;
      u->index = 0;
   }
}
static std::string listing(void)
{
   std::string s; uint8_t n = SDReader_BrowserEntryCount();
   for (uint8_t i = 0; i < n; i++) { char b[13]; SDReader_BrowserGetEntryName(i, b, sizeof b); s += b; s += ' '; }
   return s;
}
static int indexOf(const char *name)
{
   uint8_t n = SDReader_BrowserEntryCount();
   for (uint8_t i = 0; i < n; i++) { char b[13]; SDReader_BrowserGetEntryName(i, b, sizeof b); if (strcmp(b, name) == 0) return i; }
   return -1;
}

int main(void)
{
   printf("1. Listing: directories first, then the G-code files, each alphabetical; other files hidden (BIG.GCODE shows as its short name BIG.GCO)\n");
   reset_card();
   add(0, "ZETA.GC", false); add(0, "ALPHA.GC", false); int sub1 = add(0, "SUB1", true); add(0, "NOTES.TXT", false); add(0, "MID.GCO", false); add(0, "AAA", true); add(0, "PART.NC", false); add(0, "BIG.GCODE", false);
   add(sub1, "IN1.GC", false); add(sub1, "DEEP", true);
   SDReader_Init();
   CHECK(SDReader_CardStatus() == SDREADER_CARD_OK, "status %d", (int)SDReader_CardStatus());
   CHECK(SDReader_BrowserAtRoot(), "not at the root after init");
   { std::string l = listing(); CHECK(l == "AAA/ SUB1/ ALPHA.GC BIG.GCO MID.GCO PART.NC ZETA.GC ", "root listing: '%s'", l.c_str()); }

   printf("2. Entering and leaving a directory\n");
   CHECK(SDReader_BrowserEnter((uint8_t)indexOf("SUB1/")) == 0, "entering a directory reported a job");
   CHECK(!SDReader_BrowserAtRoot(), "still at the root after entering SUB1");
   { std::string l = listing(); CHECK(l == ".. DEEP/ IN1.GC ", "SUB1 listing: '%s'", l.c_str()); }
   SDReader_BrowserGoUp();
   CHECK(SDReader_BrowserAtRoot(), "go up did not reach the root");
   { std::string l = listing(); CHECK(l == "AAA/ SUB1/ ALPHA.GC BIG.GCO MID.GCO PART.NC ZETA.GC ", "root listing after going up: '%s'", l.c_str()); }
   CHECK(SDReader_BrowserEnter((uint8_t)indexOf("ALPHA.GC")) == 1, "a file was not opened as a job");

   printf("3. Many files in a shuffled order: the listing is sorted\n");
   reset_card(); { int m = add(0, "MANY", true); std::vector<int> ids; for (int i = 1; i <= 40; i++) ids.push_back(i); std::random_shuffle(ids.begin(), ids.end(), [](int n) { return (n * 7 + 3) % n; });
      for (int i : ids) { char b[16]; sprintf(b, "F%02d.GC", i); add(m, b, false); } }
   SDReader_Init(); SDReader_BrowserEnter(0);
   { std::string want = ".. "; for (int i = 1; i <= 40; i++) { char b[16]; sprintf(b, "F%02d.GC ", i); want += b; } std::string l = listing(); CHECK(l == want, "40 shuffled files: '%s'", l.c_str()); }

   printf("3b. The work per step (REQ-CoreLR-D-SDReader-0010): the next or the previous entry is one scan away, not k + 1; the answers stay right for any order of requests\n");
   reset_card(); { int m = add(0, "MANY", true); std::vector<int> ids; for (int i = 1; i <= 40; i++) ids.push_back(i); std::random_shuffle(ids.begin(), ids.end(), [](int n) { return (n * 5 + 1) % n; });
      for (int i : ids) { char b[16]; sprintf(b, "F%02d.GC", i); add(m, b, false); } add(m, "S1", true); add(m, "S2", true); add(m, "S3", true); }
   SDReader_Init(); SDReader_BrowserEnter(0);
   {  /* entries: "..", S1/ S2/ S3/, F01.GC .. F40.GC */
      std::vector<std::string> ref; ref.push_back(".."); ref.push_back("S1/"); ref.push_back("S2/"); ref.push_back("S3/"); for (int i = 1; i <= 40; i++) { char b[16]; sprintf(b, "F%02d.GC", i); ref.push_back(b); }
      auto at = [&](int i, bool asUi) { char b[13]; SDReader_BrowserGetEntryName((uint8_t)i, b, sizeof b); if (asUi) (void)SDReader_BrowserIsDirectory((uint8_t)i); return std::string(b); };
      CHECK(SDReader_BrowserEntryCount() == 44, "44 entries expected, %u", SDReader_BrowserEntryCount());
      long r0 = g_rewinds; for (int i = 0; i < 44; i++) { std::string n = at(i, true); CHECK(n == ref[i], "forward, entry %d: '%s' expected '%s'", i, n.c_str(), ref[i].c_str()); }
      long forward = g_rewinds - r0; printf("   forward through 44 entries (name + kind, as the UI asks): %ld scans (the first version: about 1 900)\n", forward); CHECK(forward <= 2 * 44, "forward: %ld scans", forward);
      r0 = g_rewinds; for (int i = 43; i >= 0; i--) { std::string n = at(i, true); CHECK(n == ref[i], "backward, entry %d: '%s'", i, n.c_str()); }
      long backward = g_rewinds - r0; printf("   backward: %ld scans\n", backward); CHECK(backward <= 2 * 44, "backward: %ld scans", backward);
      at(4, true); r0 = g_rewinds; (void)at(43, true); CHECK(g_rewinds - r0 <= 1, "the wrap-around to the last entry cost %ld scans", g_rewinds - r0);
      r0 = g_rewinds; (void)at(43, true); CHECK(g_rewinds - r0 == 0, "asking again for the same entry rescanned (%ld)", g_rewinds - r0);
      r0 = g_rewinds; SDReader_BrowserEntryCount(); SDReader_BrowserEntryCount(); CHECK(g_rewinds - r0 == 0, "the counts are rescanned (%ld)", g_rewinds - r0);
      unsigned seed = 12345; for (int k = 0; k < 400; k++)
      {  seed = seed * 1664525u + 1013904223u; int i = (int)((seed >> 8) % 44u); std::string n = at(i, (seed & 1) != 0); CHECK(n == ref[i], "random access %d: entry %d '%s' expected '%s'", k, i, n.c_str(), ref[i].c_str()); }
      CHECK(at(200, false) == "" && at(200, false) == "" && at(44, false) == "", "an index beyond the list answered something");
      CHECK(at(7, false) == ref[7] && at(8, false) == ref[8], "entries after a request beyond the list");
      /* a directory change drops what was remembered: the same indexes in another directory */
      SDReader_BrowserGoUp(); CHECK(listing() == "MANY/ ", "after going up: '%s'", listing().c_str());
      SDReader_BrowserEnter(0); CHECK(at(10, false) == ref[10] && at(3, false) == ref[3] && at(4, false) == ref[4], "the same directory entered again");
   }
   reset_card(); { int a = add(0, "AAA", true); add(a, "ONLY.GC", false); add(0, "ZZZ", true); add(0, "BBB.GC", false); }
   SDReader_Init(); CHECK(listing() == "AAA/ ZZZ/ BBB.GC ", "root: '%s'", listing().c_str());
   { char b[13]; SDReader_BrowserGetEntryName(1, b, sizeof b); SDReader_BrowserEnter(0); CHECK(listing() == ".. ONLY.GC ", "entering after a cached lookup in the parent: '%s'", listing().c_str()); }

   printf("3c. A directory of more than 255 entries shows its first 255 (the counts saturate, they do not wrap)\n");
   reset_card(); { int m = add(0, "BIGDIR", true); for (int i = 0; i < 300; i++) { char b[16]; sprintf(b, "F%03d.GC", i); add(m, b, false); } }
   SDReader_Init(); SDReader_BrowserEnter(0);
   { CHECK(SDReader_BrowserEntryCount() == 255, "300 files: count %u", SDReader_BrowserEntryCount());
     char b[13]; SDReader_BrowserGetEntryName(1, b, sizeof b); CHECK(strcmp(b, "F000.GC") == 0, "first of the 300: '%s'", b);
     SDReader_BrowserGetEntryName(254, b, sizeof b); CHECK(strcmp(b, "F253.GC") == 0, "last shown: '%s'", b); }
   reset_card(); { for (int i = 0; i < 300; i++) { char b[16]; sprintf(b, "D%03d", i); add(0, b, true); } add(0, "Z.GC", false); }
   SDReader_Init(); { char b[13]; CHECK(SDReader_BrowserEntryCount() == 255, "300 directories: count %u", SDReader_BrowserEntryCount()); SDReader_BrowserGetEntryName(254, b, sizeof b); CHECK(strcmp(b, "D254/") == 0, "last directory shown: '%s'", b); }

   printf("4. The depth bound: a path that would not fit is refused, not truncated\n");
   reset_card(); { int parent = 0; const char *names[9] = { "D2345678", "D3456789", "D4567890", "D5678901", "D6789012", "D7890123", "D8901234", "D9012345", "D0123456" };
      for (int i = 0; i < 9; i++) { parent = add(parent, names[i], true); char f[16]; sprintf(f, "L%d.GC", i + 1); add(parent, f, false); } }
   SDReader_Init();
   { int reached = 0; for (int level = 1; level <= 9; level++)
      { SDReader_BrowserEnter((uint8_t)indexOf(level == 1 ? "D2345678/" : (std::string(level == 2 ? "D3456789" : level == 3 ? "D4567890" : level == 4 ? "D5678901" : level == 5 ? "D6789012" : level == 6 ? "D7890123" : level == 7 ? "D8901234" : level == 8 ? "D9012345" : "D0123456") + "/").c_str()));
        char f[16]; sprintf(f, "L%d.GC", level); if (listing().find(f) != std::string::npos) reached = level; else break; }
     /* each name of 8 characters costs 9 bytes: the longest path is 7 x 9 = 63 < SDREADER_PATH_SIZE (64) */
     CHECK(reached == (int)((SDREADER_PATH_SIZE - 1u) / 9u), "%d levels reached, expected %u for a path buffer of %u", reached, (unsigned)((SDREADER_PATH_SIZE - 1u) / 9u), (unsigned)SDREADER_PATH_SIZE);
     std::string at = listing(); SDReader_BrowserEnter((uint8_t)indexOf("D9012345/") < 0 ? 1 : (uint8_t)indexOf("D9012345/")); CHECK(listing() == at, "the refused level changed the listing");
     CHECK(SDReader_BrowserEnterRefused() == 1, "the refused descent was not reported");
     SDReader_BrowserGoUp(); SDReader_BrowserGoUp(); SDReader_BrowserEnter(0); CHECK(SDReader_BrowserEnterRefused() == 0, "the report outlived the next descent");
     for (int i = 0; i < 20; i++) SDReader_BrowserGoUp();
     CHECK(SDReader_BrowserAtRoot() && listing() == "D2345678/ ", "going up from the deepest level did not land at the root: '%s'", listing().c_str()); }

   printf("4b. The bound to the byte: a path of exactly SDREADER_PATH_SIZE (the terminator would not fit) is refused, one byte shorter is accepted\n");
   reset_card(); { int parent = 0; const char *names[6] = { "D2345678", "D3456789", "D4567890", "D5678901", "D6789012", "D7890123" };
      for (int i = 0; i < 6; i++) parent = add(parent, names[i], true);
      add(parent, "ABCDEFG.H", true); add(parent, "ABCDEFGH", true); }                       /* below 6 x 9 = 54: "/ABCDEFG.H" makes 64, "/ABCDEFGH" 63 */
   SDReader_Init();
   { const char *down[6] = { "D2345678/", "D3456789/", "D4567890/", "D5678901/", "D6789012/", "D7890123/" }; for (int level = 0; level < 6; level++) SDReader_BrowserEnter((uint8_t)indexOf(down[level])); }
   { std::string at = listing(); CHECK(at.find("ABCDEFGH/") != std::string::npos, "not at the 6th level: '%s'", at.c_str());
     SDReader_BrowserEnter((uint8_t)indexOf("ABCDEFG.H/")); CHECK(listing() == at, "a path of exactly %u bytes was accepted", (unsigned)SDREADER_PATH_SIZE);
     SDReader_BrowserEnter((uint8_t)indexOf("ABCDEFGH/")); CHECK(listing() != at && listing().find("..") == 0, "a path of %u bytes was refused", (unsigned)SDREADER_PATH_SIZE - 1u); }

   printf("5. A job file: the lines come out without their comments\n");
   reset_card(); add(0, "JOB.GC", false, "G0 X1 Y2 ; to the start\n(a comment)\nG1 X5\n\n  ; only a comment\nM30"); SDReader_Init();
   CHECK(SDReader_BrowserEnter(0) == 1, "job not opened");
   { SDReader_Line_t l; std::vector<std::string> got; for (int i = 0; i < 40; i++) { SDReader_Task(); while (SDReader_Dequeue(l)) got.push_back(l); }
     CHECK(got.size() == 3 && got[0] == "G0 X1 Y2" && got[1] == "G1 X5" && got[2] == "M30", "%zu lines, first '%s'", got.size(), got.empty() ? "" : got[0].c_str());
     CHECK(SDReader_JobIsExhausted(), "the job is not exhausted"); CHECK(!SDReader_JobFailed(), "a job that ended at the end of its file is reported as failed"); }

   printf("5b. The card pulled during a job: the job is reported failed (REQ-CoreLR-D-SDReader-0012), not finished\n");
   reset_card(); { std::string d; for (int i = 1; i <= 30; i++) { char b[24]; sprintf(b, "G1 X%d Y%d\n", i, i); d += b; } add(0, "LONG.GC", false, d.c_str()); } SDReader_Init();
   CHECK(SDReader_BrowserEnter(0) == 1, "job not opened");
   { SDReader_Line_t l; int got = 0; for (int i = 0; i < 3; i++) SDReader_Task(); while (SDReader_Dequeue(l)) got++;
     CHECK(got >= 1 && !SDReader_JobFailed(), "before the pull: %d lines, failed %d", got, SDReader_JobFailed());
     g_cardNeedsInit = true;                                    /* pulled and put back */
     for (int i = 0; i < 3; i++) { SDReader_Task(); }
     CHECK(SDReader_JobFailed() == 1, "the failed read was taken for the end of the file");
     int after = 0; for (int i = 0; i < 40; i++) { SDReader_Task(); while (SDReader_Dequeue(l)) { after++; CHECK(strncmp(l, "G1 X", 4) == 0 && strchr(l, 'Y') != NULL, "a half line was published: '%s'", l); } }
     CHECK(after <= (int)SDREADER_QUEUE_SIZE, "%d lines came out after the pull", after);
     CHECK(SDReader_BrowserRecover() == 1, "the browser was not told to remount");
     CHECK(SDReader_BrowserEnter(0) == 1 && !SDReader_JobFailed(), "the next job did not open clean: failed %d", SDReader_JobFailed()); }
   /* the card pulled in the MIDDLE of a line: the half line must not reach the parser */
   reset_card(); { std::string d; for (int i = 1; i <= 30; i++) { char b[24]; sprintf(b, "G1 X%d Y%d\n", i, i); d += b; } add(0, "LONG.GC", false, d.c_str()); } SDReader_Init();
   SDReader_BrowserEnter(0); g_failReadsAfter = 14;                 /* "G1 X1 Y1\n" is 9 bytes: the second line is cut after 5 */
   { SDReader_Line_t l; std::vector<std::string> got; for (int i = 0; i < 10; i++) { SDReader_Task(); while (SDReader_Dequeue(l)) got.push_back(l); }
     CHECK(SDReader_JobFailed() == 1, "a pull in the middle of a line was not noted");
     for (size_t i = 0; i < got.size(); i++) CHECK(got[i].find(" Y") != std::string::npos, "a half line was published: '%s'", got[i].c_str());
     CHECK(got.size() == 1, "%zu lines published, expected the one complete line", got.size()); }

   printf("6. The card pulled and put back during a navigation: the browser is back at the root (REQ-CoreLR-D-SDReader-0009)\n");
   reset_card(); { int a = add(0, "AAA", true); add(a, "X.GC", false); add(0, "ROOT.GC", false); } SDReader_Init();
   { Ui u = { 0, 0, "", 0 }; refresh(&u); CHECK(u.total == 2, "root total %u", u.total);
     SDReader_BrowserEnter(0); u.index = 0; refresh(&u); CHECK(u.total == 2 && !SDReader_BrowserAtRoot(), "inside AAA: total %u", u.total);   /* "..", X.GC */
     CHECK(SDReader_BrowserRecover() == 0, "recover reported a lost card with nothing wrong");
     g_cardNeedsInit = true;                                    /* pulled and put back: the card forgot its initialisation */
     u.index = 1; refresh(&u);
     CHECK(SDReader_BrowserAtRoot(), "after the card was pulled and put back the browser is not at the root");
     CHECK(u.index == 0 && u.total == 2, "index %u, total %u: expected 0 and the 2 entries of the root", u.index, u.total);
     CHECK(strcmp(u.name, "AAA/") == 0, "first entry '%s'", u.name);
     CHECK(SDReader_CardStatus() == SDREADER_CARD_OK, "card status %d after the remount", (int)SDReader_CardStatus());
     CHECK(SDReader_BrowserRecover() == 0, "recover fired twice for one loss");
     /* the card removed and not put back */
     SDReader_BrowserEnter(0); g_cardPresent = false; g_cardNeedsInit = true; u.index = 0; refresh(&u);
     CHECK(u.total == 0 && SDReader_CardStatus() == SDREADER_CARD_ABSENT, "no card: total %u, status %d", u.total, (int)SDReader_CardStatus());
     /* and back: the explicit retry (the UI's short press) mounts it */
     g_cardPresent = true; CHECK(SDReader_Mount() == 1, "the retry did not mount"); u.index = 0; refresh(&u); CHECK(u.total == 2 && SDReader_BrowserAtRoot(), "after the retry: total %u", u.total); }
   /* a job opened by name on a card that was pulled: the failed open is itself noted */
   reset_card(); add(0, "A.GC", false); SDReader_Init(); g_cardNeedsInit = true;
   CHECK(SDReader_OpenJob("A.GC") == 0, "a job opened on a dead card"); CHECK(SDReader_BrowserRecover() == 1, "the failed open was not noted");
   /* a job opened on a card that was pulled: the open fails, and the browser recovers on its next refresh */
   reset_card(); add(0, "A.GC", false); SDReader_Init();
   { Ui u = { 0, 0, "", 0 }; refresh(&u); g_cardNeedsInit = true; CHECK(SDReader_BrowserEnter(0) == 0, "a job opened on a dead card"); refresh(&u);
     CHECK(u.total == 1 && SDReader_BrowserAtRoot() && SDReader_CardStatus() == SDREADER_CARD_OK, "no recovery after a failed open: total %u", u.total); }

   printf(failures ? "\n%d FAILURE(S)\n" : "\nall checks passed\n", failures);
   return failures != 0;
}
