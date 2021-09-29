#include "Context.h"

void coroutine_trampoline(void * _context)
{
  // FIXME(katei)
}

int setjmp(void *env) { return 0; }
void longjmp(void *env, int val) {}

