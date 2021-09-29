#ifndef COROUTINE_NONE_CONTEXT_H
#define COROUTINE_NONE_CONTEXT_H

#include <stddef.h>

#define COROUTINE __attribute__((noreturn)) void

struct coroutine_context;

typedef COROUTINE(* coroutine_start)(struct coroutine_context *from, struct coroutine_context *self);

struct coroutine_context
{
    struct coroutine_shared * shared;

    coroutine_start start;
    void *argument;

    void *stack;
    size_t size;

    struct coroutine_context * from;
};

COROUTINE coroutine_trampoline(void * _context);

#define MAIN_ASYNCIFY_STACK_SIZE 65536
static inline void coroutine_initialize_main(struct coroutine_context * context) {
}
#undef MAIN_ASYNCIFY_STACK_SIZE

static inline void coroutine_initialize(
    struct coroutine_context *context,
    coroutine_start start,
    void *stack,
    size_t size
) {
}

static inline struct coroutine_context * coroutine_transfer(struct coroutine_context * current, struct coroutine_context * target)
{
    return target;
}

static inline void coroutine_destroy(struct coroutine_context * context)
{
}

#endif /* COROUTINE_NONE_CONTEXT_H */
