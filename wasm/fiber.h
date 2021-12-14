#ifndef RB_WASM_SUPPORT_FIBER_H
#define RB_WASM_SUPPORT_FIBER_H

#include <stdbool.h>

#ifndef WASM_FIBER_STACK_BUFFER_SIZE
# define WASM_FIBER_STACK_BUFFER_SIZE 6144
#endif

struct __rb_wasm_asyncify_fiber_ctx {
  void* top;
  void* end;
  char buffer[WASM_FIBER_STACK_BUFFER_SIZE];
};

typedef struct {
  /// null if the entry point is main
  void (*entry_point)(void *, void *);
  void *arg0, *arg1;
  struct __rb_wasm_asyncify_fiber_ctx asyncify_buf;
  bool is_rewinding;
  bool is_started;
} rb_wasm_fiber_context;

void rb_wasm_init_context(rb_wasm_fiber_context *fcp);

void rb_wasm_makecontext(rb_wasm_fiber_context *fcp, void (*func)(void *, void *), void *arg0, void *arg1);

void rb_wasm_swapcontext(rb_wasm_fiber_context *ofcp, rb_wasm_fiber_context *fcp);

void *rb_wasm_handle_fiber_unwind(void (**new_fiber_entry)(void *, void *),
                                  void **arg0, void **arg1, bool *is_new_fiber_started);

#endif
