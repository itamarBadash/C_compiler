int printf(const char *format, ...);

static int counter;

int main(void) {
  int r, x = 41;
  __asm__("movl %1, %0\n\taddl $1, %0" : "=r"(r) : "r"(x));
  int y = 10;
  __asm__ volatile("addl $5, %0" : "+r"(y));
  int m;
  __asm__("movl $7, %0" : "=m"(m) : : "memory");
  int lo, hi;
  __asm__("movl %2, %0\n\tmovl %3, %1" : "=&r"(lo), "=r"(hi) : "g"(x), "r"(y));
  long long q = 5;
  __asm__("incq %0" : "+m"(q));
  int c = 0;
  __asm__("movl $3, %%eax\n\tmovl %%eax, %0" : "=r"(c) : : "eax");
  unsigned a = 6, d = 9;
  __asm__("xchgl %0, %1" : "+a"(a), "+d"(d));
  unsigned char ch;
  __asm__("movb $65, %0" : "=q"(ch));
  int from_memory;
  __asm__("movl %1, %0" : "=r"(from_memory) : "m"(x));
  int tied = 0;
  __asm__("addl %2, %0" : "=r"(tied) : "0"(100), "i"(23));
  int jumped = 0;
  __asm__("jmp .Lskip%=\n\tmovl $1, %0\n.Lskip%=:" : "+r"(jumped));
  int bytes[2] = {1, 2};
  int *p;
  __asm__("leaq %1, %0" : "=r"(p) : "m"(bytes[1]));
  __asm__("nop");
  __asm__ volatile("movl %eax, %eax");
  __asm__("incl %0" : "+m"(counter));
  long long wide;
  __asm__("movq $-1, %q0" : "=r"(wide));
  printf("%d %d %d %d %d %lld %d\n", r, y, m, lo, hi, q, c);
  printf("%u %u %c %d %d %d %d %d %lld\n", a, d, ch, from_memory, tied, jumped, *p, counter, wide);
  return 0;
}
