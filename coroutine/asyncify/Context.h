#ifndef COROUTINE_ASYNCIFY_CONTEXT_H
#define COROUTINE_ASYNCIFY_CONTEXT_H

#include <stddef.h>
#include <stdio.h>
#include <rb-wasm-support/asyncify.h>
#include <rb-wasm-support/machine.h>
#include <rb-wasm-support/fiber.h>

#define COROUTINE void

static const int ASYNCIFY_CORO_DEBUG = 0;

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
    if (ASYNCIFY_CORO_DEBUG) fprintf(stderr, "[%s] entry (context = %p)\n", __func__, context);
    rb_wasm_init_context(&context->fc);
    context->fc.is_started = true;
}

static inline void coroutine_initialize(
    struct coroutine_context *context,
    coroutine_start start,
    void *stack,
    size_t size
) {
    if (ASYNCIFY_CORO_DEBUG) fprintf(stderr, "[%s] entry (context = %p, stack = %p ... %p)\n", __func__, context, stack, (char *)stack + size);
    rb_wasm_init_context(&context->fc);
    rb_wasm_makecontext(&context->fc, coroutine_trampoline, start, context);
    context->current_sp = (char *)stack + size;
    context->stack_base = stack;
    context->size = size;
}

static inline struct coroutine_context * coroutine_transfer(struct coroutine_context * current, struct coroutine_context * target)
{
    if (ASYNCIFY_CORO_DEBUG) fprintf(stderr, "[%s] entry (current = %p, target = %p)\n", __func__, current, target);
    struct coroutine_context * previous = target->from;

    target->from = current;
    if (ASYNCIFY_CORO_DEBUG) fprintf(stderr, "[%s] current->current_sp = %p -> %p\n", __func__, current->current_sp, rb_wasm_get_stack_pointer());
    current->current_sp = rb_wasm_get_stack_pointer();

    rb_wasm_swapcontext(&current->fc, &target->fc);
    rb_wasm_set_stack_pointer(current->current_sp);

    target->from = previous;

    return target;
}

static inline void coroutine_destroy(struct coroutine_context * context)
{
    if (ASYNCIFY_CORO_DEBUG) fprintf(stderr, "[%s] entry (context = %p)\n", __func__, context);
    context->stack_base = NULL;
    context->size = 0;
    context->from = NULL;
}

#endif /* COROUTINE_ASYNCIFY_CONTEXT_H */
