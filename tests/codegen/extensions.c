#include <anon.h>
#include <emmintrin.h>
#include <stdio.h>
#include <x86intrin.h>
#include <xmmintrin.h>

static struct pair shared = {7, {.whole = 0x0000000500000003LL}};

static int add_named(int a, int b) {
  __asm__("addl %[b], %[a]" : [a] "+r"(a) : [b] "r"(b));
  return a;
}

static unsigned low_word(unsigned long long v) {
  unsigned out;
  __asm__("movl %k[in], %[out]" : [out] "=r"(out) : [in] "r"(v));
  return out;
}

int main(void) {
  struct pair p = {1, {.whole = -1}};
  struct pair *q = &p;
  printf("pair %d %u %d %lld\n", p.tag, p.low, q->high, q->whole);
  q->high = 2;
  p.low = 9;
  printf("pair %lld %u %d size %u\n", p.whole, shared.low, shared.high, (unsigned)sizeof p);
  struct pair list = {.low = 4, .high = 5, .tag = 6};
  printf("list %d %u %d\n", list.tag, list.low, list.high);

  int counter = 10;
  int r1 = __sync_fetch_and_add(&counter, 5);
  printf("fetch_and_add %d now %d\n", r1, counter);
  r1 = __sync_add_and_fetch(&counter, 1);
  printf("add_and_fetch %d\n", r1);
  r1 = __sync_fetch_and_sub(&counter, 2);
  int r2 = __sync_sub_and_fetch(&counter, 2);
  printf("fetch_and_sub %d sub_and_fetch %d\n", r1, r2);
  unsigned char bits = 0xF0;
  unsigned b1 = __sync_or_and_fetch(&bits, 0x0F);
  unsigned b2 = __sync_and_and_fetch(&bits, 0x3C);
  unsigned b3 = __sync_xor_and_fetch(&bits, 0xFF);
  unsigned b4 = __sync_nand_and_fetch(&bits, 0x0F);
  printf("or %u and %u xor %u nand %u\n", b1, b2, b3, b4);
  b1 = __sync_fetch_and_or(&bits, 1);
  b2 = __sync_fetch_and_and(&bits, 7);
  b3 = __sync_fetch_and_xor(&bits, 2);
  b4 = __sync_fetch_and_nand(&bits, 6);
  printf("fetch or %u and %u xor %u nand %u now %u\n", b1, b2, b3, b4, bits);
  long long wide = 100;
  int c1 = __sync_bool_compare_and_swap(&wide, 100, 200);
  int c2 = __sync_bool_compare_and_swap(&wide, 100, 300);
  printf("cas %d %d value %lld\n", c1, c2, wide);
  long long v1 = __sync_val_compare_and_swap(&wide, 200, 400);
  long long v2 = __sync_val_compare_and_swap(&wide, 1, 2);
  printf("val cas %lld %lld value %lld\n", v1, v2, wide);
  short lock = 0;
  int t1 = __sync_lock_test_and_set(&lock, 1);
  int t2 = __sync_lock_test_and_set(&lock, 1);
  printf("tas %d %d\n", t1, t2);
  __sync_lock_release(&lock);
  char *where = "x";
  char *seen = __sync_val_compare_and_swap(&where, 0, where);
  printf("pointer cas %d lock %d\n", seen == where, lock);
  __sync_synchronize();

  _mm_sfence();
  _mm_lfence();
  _mm_mfence();
  _mm_pause();
  _mm_clflush(&counter);
  _mm_prefetch((const char *)&counter, _MM_HINT_T0);
  unsigned csr = _mm_getcsr();
  _mm_setcsr(csr | 0x8000);
  printf("csr %x %x\n", csr & 0xFFC0, _mm_getcsr() & 0xFFC0);
  _mm_setcsr(csr);

  printf("asm %d %u\n", add_named(40, 2), low_word(0x123456789ULL));
  return 0;
}
