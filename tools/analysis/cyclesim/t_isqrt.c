/* Copyright (C) 2026 ErnstTechnik and the CoreLR contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include <avr/io.h>
#include <stdint.h>
extern uint32_t Motion_ISqrt(uint64_t);
#define OUT(v) (GPIOR0 = (uint8_t)(v))
#define MARK(v) (GPIOR2 = (uint8_t)(v))
static const uint64_t vals[] = { 0ULL, 1ULL, 2ULL, 3ULL, 4ULL, 15ULL, 16ULL, 99999ULL,
  1000000000000ULL, 614000000000ULL*3, 0xFFFFFFFFFFFFFFFFULL, 123456789012345678ULL };
int main(void){
  for (uint8_t i=0;i<sizeof(vals)/sizeof(vals[0]);i++){
    volatile uint64_t v = vals[i];
    MARK(1);
    uint32_t r = Motion_ISqrt(v);
    MARK(2);
    OUT(r); OUT(r>>8); OUT(r>>16); OUT(r>>24);
  }
  GPIOR1 = 1;
  for(;;);
}
