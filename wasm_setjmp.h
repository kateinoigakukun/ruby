#ifndef RUBY_WASM_SETJMP
#define RUBY_WASM_SETJMP 1
typedef void *jmp_buf;
void longjmp(void *, int);
int setjmp(void *);
#endif
