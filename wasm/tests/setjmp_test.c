#include "wasm/setjmp.h"
#include "wasm/asyncify.h"
#include "wasm/machine.h"
#include <stdio.h>
#include <assert.h>

void check_direct(void) {
  rb_wasm_jmp_buf buf;
  int val;
  printf("[%s] start\n", __func__);
  printf("[%s] call rb_wasm_setjmp\n", __func__);
  if ((val = rb_wasm_setjmp(buf)) == 0) {
    printf("[%s] rb_wasm_setjmp(buf) == 0\n", __func__);
    printf("[%s] call rb_wasm_longjmp(buf, 2)\n", __func__);
    rb_wasm_longjmp(buf, 2);
    assert(0 && "unreachable after longjmp");
  } else {
    printf("[%s] rb_wasm_setjmp(buf) == %d\n", __func__, val);
    printf("[%s] sp = %p\n", __func__, rb_wasm_get_stack_pointer());
    assert(val == 2 && "unexpected returned value");
  }
  printf("[%s] end\n", __func__);
}

void jump_to_dst(rb_wasm_jmp_buf *dst) {
  rb_wasm_jmp_buf buf;
  printf("[%s] start sp = %p\n", __func__, rb_wasm_get_stack_pointer());
  printf("[%s] call rb_wasm_setjmp\n", __func__);
  if (rb_wasm_setjmp(buf) == 0) {
    printf("[%s] rb_wasm_setjmp(buf) == 0\n", __func__);
    printf("[%s] call rb_wasm_longjmp(dst, 4)\n", __func__);
    rb_wasm_longjmp(*dst, 4);
    assert(0 && "unreachable after longjmp");
  } else {
    assert(0 && "unreachable");
  }
  printf("[%s] end\n", __func__);
}

void check_jump_two_level(void) {
  rb_wasm_jmp_buf buf;
  int val;
  printf("[%s] start\n", __func__);
  printf("[%s] call rb_wasm_setjmp\n", __func__);
  if ((val = rb_wasm_setjmp(buf)) == 0) {
    printf("[%s] rb_wasm_setjmp(buf) == 0\n", __func__);
    printf("[%s] call jump_to_dst(&buf)\n", __func__);
    jump_to_dst(&buf);
    assert(0 && "unreachable after longjmp");
  } else {
    printf("[%s] rb_wasm_setjmp(buf) == %d\n", __func__, val);
    assert(val == 4 && "unexpected returned value");
  }
  printf("[%s] end\n", __func__);
}

void check_reuse(void) {
  rb_wasm_jmp_buf buf;
  int val;
  printf("[%s] start\n", __func__);
  printf("[%s] call rb_wasm_setjmp\n", __func__);
  if ((val = rb_wasm_setjmp(buf)) == 0) {
    printf("[%s] rb_wasm_setjmp(buf) == 0\n", __func__);
    printf("[%s] call rb_wasm_longjmp(buf, 2)\n", __func__);
    rb_wasm_longjmp(buf, 2);
    assert(0 && "unreachable after longjmp");
  } else {
    printf("[%s] rb_wasm_setjmp(buf) == %d\n", __func__, val);
    if (val < 5) {
      printf("[%s] re-call rb_wasm_longjmp(buf, %d)\n", __func__, val + 1);
      rb_wasm_longjmp(buf, val + 1);
    }
  }
  printf("[%s] end\n", __func__);
}

void check_stack_ptr(void) {
  static void *normal_sp;
  rb_wasm_jmp_buf buf;
  normal_sp = rb_wasm_get_stack_pointer();

  printf("[%s] start sp = %p\n", __func__, normal_sp);
  printf("[%s] call rb_wasm_setjmp\n", __func__);
  if (rb_wasm_setjmp(buf) == 0) {
    printf("[%s] call jump_to_dst(&buf)\n", __func__);
    jump_to_dst(&buf);
    assert(0 && "unreachable after longjmp");
  } else {
    printf("[%s] sp = %p\n", __func__, rb_wasm_get_stack_pointer());
    assert(rb_wasm_get_stack_pointer() == normal_sp);
  }
  printf("[%s] end\n", __func__);
}

static rb_wasm_jmp_buf upperbuf;
static rb_wasm_jmp_buf bottombuf1;
static rb_wasm_jmp_buf bottombuf2;
static rb_wasm_jmp_buf bottombuf3;

enum exec_core_behavior {
    NOOP                    = 0,
    LONGJMP_TO_UPPER        = 1,
    LONGJMP_TO_TAG          = 2,
    SETJMP                  = 3,
    SETJMP_AND_LONGJMP      = 4,
    RESCUE_LONGJMP_TO_TAG   = 5,
    RESCUE_LONGJMP_TO_UPPER = 6,
    RESCUE_SETJMP           = 7,
};

static enum exec_core_behavior behavior = NOOP;

extern rb_wasm_jmp_buf *_rb_wasm_active_jmpbuf;

__attribute__((noinline))
void rb_vm_exec_core(rb_wasm_jmp_buf *tag) {
  printf("[%s] start\n", __func__);
  switch (behavior) {
  case NOOP: {
    break;
  }
  case LONGJMP_TO_UPPER: {
    rb_wasm_longjmp(upperbuf, 42);
    break;
  }
  case LONGJMP_TO_TAG:
  case RESCUE_LONGJMP_TO_TAG:
  case RESCUE_LONGJMP_TO_UPPER:
  case RESCUE_SETJMP: {
    rb_wasm_longjmp(*tag, 24);
    break;
  }
  case SETJMP: {
    if (rb_wasm_setjmp(bottombuf1) == 0) {
      printf("[%s] normal path in setjmp\n", __func__);
    } else {
      printf("[%s] returned path in setjmp\n", __func__);
    }
    break;
  }
  case SETJMP_AND_LONGJMP: {
    if (rb_wasm_setjmp(bottombuf2) == 0) {
      printf("[%s] normal path in setjmp\n", __func__);
      rb_wasm_longjmp(bottombuf2, 32);
    } else {
      printf("[%s] returned path in setjmp\n", __func__);
    }
    break;
  }
  }
  printf("[%s] end\n", __func__);
}

static int rescued = 0;
__attribute__((noinline))
static void rescue_block(rb_wasm_jmp_buf *tag) {
  printf("[%s] start\n", __func__);
  if (rescued) return;
  rescued = 1;
  switch (behavior) {
  case RESCUE_LONGJMP_TO_TAG: {
    rb_wasm_longjmp(*tag, 24);
    break;
  }
  case RESCUE_LONGJMP_TO_UPPER: {
    rb_wasm_longjmp(upperbuf, 42);
    break;
  }
  case RESCUE_SETJMP: {
    if (rb_wasm_setjmp(bottombuf3) == 0) {
      printf("[%s] normal path in setjmp\n", __func__);
    } else {
      printf("[%s] returned path in setjmp\n", __func__);
    }
    break;
  }
  default: break;
  }
}

enum rb_vm_exec_middle_phase {
  PHASE_MAIN   = 0,
  PHASE_RESCUE = 1,
};

__attribute__((noinline))
void rb_vm_exec_middle(rb_wasm_jmp_buf *tag, enum rb_vm_exec_middle_phase *phase) {
  if (!_rb_wasm_active_jmpbuf /* unrelated rewind */) {
    printf("[%s] start\n", __func__);
  }
  switch (*phase) {
  case PHASE_MAIN: {
    rb_vm_exec_core(tag);
    break;
  }
  case PHASE_RESCUE: {
    rescue_block(tag);
    break;
  }
  }
  do {
    if (_rb_wasm_active_jmpbuf == tag) {
      asyncify_stop_rewind();
      _rb_wasm_active_jmpbuf = NULL;
      *phase = PHASE_RESCUE;
      // reset jmpbuf state
      tag->state = 2; // JMP_BUF_STATE_CAPTURED
      rescue_block(tag);
      continue;
    } else if (_rb_wasm_active_jmpbuf /* unrelated unwind */) {
        return;
    }
    break;
  } while (1);
  printf("[%s] end\n", __func__);
}

__attribute__((noinline))
void rb_vm_exec() {
  printf("[%s] start\n", __func__);
  rb_wasm_jmp_buf new_tag;
  enum rb_vm_exec_middle_phase phase = PHASE_MAIN;
  new_tag.state = 2; // JMP_BUF_STATE_CAPTURED
  rb_vm_exec_middle(&new_tag, &phase);
  printf("[%s] end\n", __func__);
}

int _check_lightweight_sjlj(enum exec_core_behavior b) {
  behavior = b;
  rescued = 0;
  if (rb_wasm_setjmp(upperbuf) == 0) {
    rb_vm_exec();
  } else {
    printf("[%s] caught a longjmp\n", __func__);
  }
}

#define do_check_lightweight_sjlj(b) do { \
  printf("check sjlj: " #b "\n");          \
  _check_lightweight_sjlj(b);             \
} while(0)

int check_lightweight_sjlj() {
  do_check_lightweight_sjlj(NOOP);
  do_check_lightweight_sjlj(LONGJMP_TO_UPPER);
  do_check_lightweight_sjlj(LONGJMP_TO_TAG);
  do_check_lightweight_sjlj(SETJMP);
  do_check_lightweight_sjlj(SETJMP_AND_LONGJMP);
  do_check_lightweight_sjlj(RESCUE_LONGJMP_TO_TAG);
  do_check_lightweight_sjlj(RESCUE_LONGJMP_TO_UPPER);
  do_check_lightweight_sjlj(RESCUE_SETJMP);
}

// top level function should not be inlined to stop unwinding immediately after this function returns
__attribute__((noinline))
int start(int argc, char **argv) {
  check_direct();
  check_jump_two_level();
  check_reuse();
  check_stack_ptr();
  check_lightweight_sjlj();
  return 0;
}

int main(int argc, char **argv) {
  extern int rb_wasm_rt_start(int (main)(int argc, char **argv), int argc, char **argv);
  return rb_wasm_rt_start(start, argc, argv);
}
