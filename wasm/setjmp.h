#ifndef RB_WASM_SUPPORT_SETJMP_H
#define RB_WASM_SUPPORT_SETJMP_H

#include "wasm/config.h"
#include <stdbool.h>

struct __rb_wasm_asyncify_jmp_buf {
  void* top;
  void* end;
  char buffer[RB_WASM_SUPPORT_FRAME_BUFFER_SIZE];
};

typedef struct {
  struct __rb_wasm_asyncify_jmp_buf setjmp_buf;
  struct __rb_wasm_asyncify_jmp_buf longjmp_buf;
  void *dst_buf_top;
  int val;
  int state;
} rb_wasm_jmp_buf;

__attribute__((noinline))
int _rb_wasm_setjmp(rb_wasm_jmp_buf *env);

__attribute__((noinline))
void _rb_wasm_longjmp(rb_wasm_jmp_buf *env, int val);

#define rb_wasm_setjmp(env) ((env).state = 0, _rb_wasm_setjmp(&(env)))

// NOTE: Why is `_rb_wasm_longjmp` not `noreturn`? Why put `unreachable` in the call site?
// Asyncify expects that `_rb_wasm_longjmp` returns its control, and Asyncify inserts a return
// for unwinding after the call. This means that "`_rb_wasm_longjmp` returns its control but the
// next line in the caller (C level) won't be executed".
// On the other hand, `noreturn` means the callee won't return its control to the caller,
// so compiler can assume that a function with the attribute won't reach the end of the function.
// Therefore `_rb_wasm_longjmp`'s semantics is not exactly same as `noreturn`.
#define rb_wasm_longjmp(env, val) (_rb_wasm_longjmp(&env, val), __builtin_unreachable())

void *rb_wasm_handle_jmp_unwind(void);


typedef rb_wasm_jmp_buf jmp_buf;

#define setjmp(env) rb_wasm_setjmp(env)
#define longjmp(env, val) rb_wasm_longjmp(env, val)

#endif
