#ifndef COROUTINE_NONE_CONTEXT_H
#define COROUTINE_NONE_CONTEXT_H 1

#pragma once

#include <stddef.h>
#include <errno.h>

#define COROUTINE void
#define COROUTINE_NONE_CONTEXT

struct coroutine_context
{
    struct coroutine_context *from;
    void *argument;
    void *stack_base;
    size_t stack_size;
};

typedef COROUTINE(* coroutine_start)(struct coroutine_context *from, struct coroutine_context *self);

COROUTINE coroutine_trampoline(void * _start, void * _context);

static inline void
coroutine_initialize_main(struct coroutine_context *context)
{
    context->from = NULL;
    context->argument = NULL;
    context->stack_base = NULL;
    context->stack_size = 0;
}

static inline void
coroutine_initialize(struct coroutine_context *context, coroutine_start start, void *stack, size_t size)
{
    (void)start;
    context->from = NULL;
    context->stack_base = stack;
    context->stack_size = size;
}

static inline struct coroutine_context *
coroutine_transfer(struct coroutine_context *current, struct coroutine_context *target)
{
    (void)current;
    (void)target;
    errno = ENOTSUP;
    return NULL;
}

static inline void
coroutine_destroy(struct coroutine_context *context)
{
    context->from = NULL;
}

#endif
