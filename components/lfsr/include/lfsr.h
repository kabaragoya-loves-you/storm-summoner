#ifndef LFSR_H
#define LFSR_H

#include <stdint.h>

// 8-bit maximal LFSR (polynomial x^8 + x^6 + x^5 + x^4 + 1)
// Period: 255 steps before repeating
static inline uint8_t lfsr8_step(uint8_t x) {
  uint8_t lsb = x & 1;
  x >>= 1;
  if (lsb) x ^= 0xB8;  // 0b10111000 taps
  return x ? x : 0xA5; // Avoid zero lockup
}

#endif // LFSR_H
