/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
/* Host test of TextFmt_Micrometres (REQ-CoreLR-D-SerialLink-0005) against the C library's own printf, the reference the firmware
 * may not link. The real TextFmt.c is compiled in.
 *
 *   gcc -m32 -fpack-struct=1 -O1 -Wall -Ishim -I../../firmware/src test_textfmt.c ../../firmware/src/TextFmt.c -o out/test_textfmt
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "TextFmt.h"

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; if (failures < 25) { printf("FAIL line %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

static void ref(char *out, long um)
{
   long a = um < 0 ? -um : um;
   sprintf(out, "%s%ld.%03ld", um < 0 ? "-" : "", a / 1000, a % 1000);
}

int main(void)
{
   char got[32], want[32];
   long checked = 0, v;

   /* every value of the machine's whole range, in steps that visit every residue of 1000, plus the neighbourhood of every sign change and
    * of every power of ten, and the extremes */
   for (v = -4300000L; v <= 4300000L; v += 997L)
   {
      TextFmt_Micrometres(got, 0, sizeof got, (int32_t)v); ref(want, v); checked++;
      CHECK(strcmp(got, want) == 0, "%ld um: '%s', expected '%s'", v, got, want);
   }
   for (v = -3000L; v <= 3000L; v++)
   {
      TextFmt_Micrometres(got, 0, sizeof got, (int32_t)v); ref(want, v); checked++;
      CHECK(strcmp(got, want) == 0, "%ld um: '%s', expected '%s'", v, got, want);
   }
   { long p; for (p = 1; p <= 65000000L; p *= 10) { long d; for (d = -2; d <= 2; d++) { v = p + d; TextFmt_Micrometres(got, 0, sizeof got, (int32_t)v); ref(want, v); checked++; CHECK(strcmp(got, want) == 0, "%ld um: '%s' vs '%s'", v, got, want); v = -(p + d); TextFmt_Micrometres(got, 0, sizeof got, (int32_t)v); ref(want, v); checked++; CHECK(strcmp(got, want) == 0, "%ld um: '%s' vs '%s'", v, got, want); } } }
   printf("TextFmt_Micrometres: %ld values, identical to printf\n", checked);

   TextFmt_Micrometres(got, 0, sizeof got, 0);       CHECK(strcmp(got, "0.000") == 0, "zero: '%s'", got);
   TextFmt_Micrometres(got, 0, sizeof got, -1);      CHECK(strcmp(got, "-0.001") == 0, "-1 um: '%s'", got);
   TextFmt_Micrometres(got, 0, sizeof got, 2097151); CHECK(strcmp(got, "2097.151") == 0, "the reach limit: '%s'", got);
   TextFmt_Micrometres(got, 0, sizeof got, -2097151);CHECK(strcmp(got, "-2097.151") == 0, "the reach limit: '%s'", got);
   TextFmt_Micrometres(got, 0, sizeof got, 2147483647L);  CHECK(strcmp(got, "65535.999") == 0, "INT32_MAX saturates: '%s'", got);
   TextFmt_Micrometres(got, 0, sizeof got, -2147483647L - 1); CHECK(strcmp(got, "-65535.999") == 0, "INT32_MIN saturates: '%s'", got);
   TextFmt_Micrometres(got, 0, sizeof got, 65535999L);CHECK(strcmp(got, "65535.999") == 0, "the largest exact value: '%s'", got);
   TextFmt_Micrometres(got, 0, sizeof got, 65536000L);CHECK(strcmp(got, "65535.999") == 0, "one past it: '%s'", got);

   /* the chain: it appends where it is told, and never writes past the capacity */
   { char b[16]; uint8_t at;
     memset(b, 'x', sizeof b);
     at = TextFmt_Str(b, 0, sizeof b, "MPos:");
     at = TextFmt_Micrometres(b, at, sizeof b, -12345);
     CHECK(strcmp(b, "MPos:-12.345") == 0 && at == 12, "chained: '%s' at %u", b, at);
     memset(b, 'x', sizeof b); b[15] = 0;
     at = TextFmt_Micrometres(b, 12, 16, -2097151);           /* 10 characters at position 12 of 16: must clip, stay terminated */
     CHECK(at <= 15 && b[at] == 0 && strncmp(b + 12, "-20", 3) == 0, "clipped: '%s' at %u", b, at);
     { int i; memset(b, 'x', sizeof b); TextFmt_Micrometres(b, 0, 4, 123456); CHECK(strcmp(b, "123") == 0, "capacity 4: '%s'", b); for (i = 4; i < 16; i++) CHECK(b[i] == 'x', "wrote past the capacity at %d", i); } }

   printf(failures ? "\n%d FAILURE(S)\n" : "\nall checks passed\n", failures);
   return failures != 0;
}
