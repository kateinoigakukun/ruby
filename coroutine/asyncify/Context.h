#ifndef COROUTINE_ASYNCIFY_CONTEXT_H
#define COROUTINE_ASYNCIFY_CONTEXT_H

#include <stddef.h>
#include <rb-wasm-support/asyncify.h>
#include <rb-wasm-support/machine.h>
#include <rb-wasm-support/fiber.h>

#define COROUTINE void

struct coroutine_context;

typedef COROUTINE(* coroutine_start)(struct coroutine_context *from, struct coroutine_context *self);

struct coroutine_context
{
    rb_wasm_fiber_context fc;
    void *argument;
    struct coroutine_context * from;

    void *current_sp;
    void *stack_base;
    size_t size;
};

COROUTINE coroutine_trampoline(void * _start, void * _context);

static inline void coroutine_initialize_main(struct coroutine_context * context) {
    rb_wasm_init_context(&context->fc);
    context->fc.is_started = true;
}

static inline void coroutine_initialize(
    struct coroutine_context *context,
    coroutine_start start,
    void *stack,
    size_t size
) {
    rb_wasm_init_context(&context->fc);
    rb_wasm_makecontext(&context->fc, coroutine_trampoline, start, context);
    context->current_sp = (char *)stack + size;
    context->stack_base = stack;
    context->size = size;
}

static inline struct coroutine_context * coroutine_transfer(struct coroutine_context * current, struct coroutine_context * target)
{
    struct coroutine_context * previous = target->from;

    target->from = current;

    rb_wasm_swapcontext(&current->fc, &target->fc);
    rb_wasm_set_stack_pointer(current->current_sp);

    target->from = previous;

    return target;
}

static inline void coroutine_destroy(struct coroutine_context * context)
{
}

#endif /* COROUTINE_ASYNCIFY_CONTEXT_H */
