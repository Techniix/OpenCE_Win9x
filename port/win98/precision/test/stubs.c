static unsigned char heap[1 << 22]; static unsigned long heap_used;
void *debug_malloc(unsigned long size, int clear, const char *file, long line) { unsigned char *p = heap + heap_used, *q; unsigned long n = size; heap_used += (size + 15) & ~15UL; for (q = p; n--; ) *q++ = 0; return p; }
void debug_free(void *p, const char *file, long line) {}
void *memset(void *d, int c, unsigned long n) { unsigned char *p = d; while (n--) *p++ = (unsigned char)c; return d; }
void *csmemset(void *d, long c, unsigned long n) { return memset(d, (int)c, n); }
void *memcpy(void *d, const void *s, unsigned long n) { unsigned char *p = d; const unsigned char *q = s; while (n--) *p++ = *q++; return d; }
void *csmemcpy(void *d, const void *s, unsigned long n) { return memcpy(d, s, n); }
int display_assert(const char *a, const char *b, long c, int d) { return 0; }
void error(int a, const char *b, ...) {}
char *csprintf(char *d, const char *f, ...) { if (d) d[0] = 0; return d; }
void system_exit(int c) { __asm__ volatile("int $0x80" : : "a"(1), "b"(c)); }
char temporary[4096];
int game_engine_running(void) { return 0; }
static unsigned long r = 1; int rand(void) { r = r * 1103515245 + 12345; return (int)((r >> 16) & 0x7fff); }
unsigned long system_milliseconds(void) { return 0; }
double system_seconds(void) { return 0; }
void *geosphere_new(void) { return 0; }
void geosphere_dispose(void *p) {}
unsigned int _control87(unsigned int a, unsigned int b) { return 0; }

/* exact operations, the same on both builds (x87 instructions are exact here) */
float rintf(float x) { float r; __asm__("frndint" : "=t"(r) : "0"(x)); return r; }
double floor(double x) { unsigned short cw, down; double r; __asm__ volatile("fnstcw %0" : "=m"(cw)); down = (cw & ~0x0C00) | 0x0400; __asm__ volatile("fldcw %1; frndint; fldcw %2" : "=t"(r) : "m"(down), "m"(cw), "0"(x)); return r; }
double scalbn(double x, int n) { double r; double fn = n; __asm__("fscale; fstp %%st(1)" : "=t"(r) : "0"(x), "u"(fn)); return r; }
double fmod(double x, double y) { double r; __asm__("1: fprem; fnstsw %%ax; sahf; jp 1b; fstp %%st(1)" : "=t"(r) : "0"(x), "u"(y) : "ax", "cc"); return r; }
#ifdef __SSE2__
float sqrtf(float x) { float r; __asm__("sqrtss %1, %0" : "=x"(r) : "x"(x)); return r; }
double sqrt(double x) { double r; __asm__("sqrtsd %1, %0" : "=x"(r) : "x"(x)); return r; }
#else
float sqrtf(float x) { float r; __asm__("fsqrt" : "=t"(r) : "0"(x)); return r; }
double sqrt(double x) { double r; __asm__("fsqrt" : "=t"(r) : "0"(x)); return r; }
#endif
