#ifndef _EMMINTRIN_H_INCLUDED
#define _EMMINTRIN_H_INCLUDED

#include <xmmintrin.h>

static __inline__ void _mm_lfence(void) {
  __asm__ __volatile__("lfence" : : : "memory");
}

static __inline__ void _mm_mfence(void) {
  __asm__ __volatile__("mfence" : : : "memory");
}

static __inline__ void _mm_clflush(void const *__p) {
  __asm__ __volatile__("clflush %0" : : "m"(*(const char *)__p));
}

#endif
