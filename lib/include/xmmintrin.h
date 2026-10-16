#ifndef _XMMINTRIN_H_INCLUDED
#define _XMMINTRIN_H_INCLUDED

enum _mm_hint { _MM_HINT_NTA = 0, _MM_HINT_T2 = 1, _MM_HINT_T1 = 2, _MM_HINT_T0 = 3 };

static __inline__ void _mm_pause(void) {
  __asm__ __volatile__("pause");
}

static __inline__ void _mm_sfence(void) {
  __asm__ __volatile__("sfence" : : : "memory");
}

static __inline__ unsigned int _mm_getcsr(void) {
  unsigned int __csr;
  __asm__ __volatile__("stmxcsr %0" : "=m"(__csr));
  return __csr;
}

static __inline__ void _mm_setcsr(unsigned int __csr) {
  __asm__ __volatile__("ldmxcsr %0" : : "m"(__csr));
}

static __inline__ void _mm_prefetch(char const *__p, int __hint) {
  switch (__hint) {
  case _MM_HINT_T0:
    __asm__ __volatile__("prefetcht0 %0" : : "m"(*__p));
    break;
  case _MM_HINT_T1:
    __asm__ __volatile__("prefetcht1 %0" : : "m"(*__p));
    break;
  case _MM_HINT_T2:
    __asm__ __volatile__("prefetcht2 %0" : : "m"(*__p));
    break;
  default:
    __asm__ __volatile__("prefetchnta %0" : : "m"(*__p));
    break;
  }
}

#endif
