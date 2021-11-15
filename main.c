/**********************************************************************

  main.c -

  $Author$
  created at: Fri Aug 19 13:19:58 JST 1994

  Copyright (C) 1993-2007 Yukihiro Matsumoto

**********************************************************************/

/*!
 * \mainpage Developers' documentation for Ruby
 *
 * This documentation is produced by applying Doxygen to
 * <a href="https://github.com/ruby/ruby">Ruby's source code</a>.
 * It is still under construction (and even not well-maintained).
 * If you are familiar with Ruby's source code, please improve the doc.
 */
#undef RUBY_EXPORT
#include "ruby.h"
#include "vm_debug.h"
#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif
#if RUBY_DEVEL && !defined RUBY_DEBUG_ENV
# define RUBY_DEBUG_ENV 1
#endif
#if defined RUBY_DEBUG_ENV && !RUBY_DEBUG_ENV
# undef RUBY_DEBUG_ENV
#endif

#if defined(__wasm__) && !defined(__EMSCRIPTEN__)
# include <rb-wasm-support/asyncify.h>
# include <rb-wasm-support/setjmp.h>
# include <rb-wasm-support/fiber.h>
#endif

#if defined(__wasm__) && !defined(__EMSCRIPTEN__)
__attribute__((noinline))
#endif
int
rb_main(int argc, char **argv)
{
#ifdef RUBY_DEBUG_ENV
    ruby_set_debug_option(getenv("RUBY_DEBUG"));
#endif
#ifdef HAVE_LOCALE_H
    setlocale(LC_CTYPE, "");
#endif

    ruby_sysinit(&argc, &argv);
    {
	RUBY_INIT_STACK;
	ruby_init();
	return ruby_run_node(ruby_options(argc, argv));
    }
}

int main(int argc, char **argv) {
#if defined(__wasm__) && !defined(__EMSCRIPTEN__)
  int result;
  void *asyncify_buf;

  void *arg0 = NULL, *arg1 = NULL;
  void (*fiber_entry_point)(void *, void *) = NULL;
  void (*old_entry_point)(void *, void *);

  while (1) {
    if (fiber_entry_point) {
      fiber_entry_point(arg0, arg1);
    } else {
      result = rb_main(argc, argv);
    }

    // NOTE: it's important to call 'asyncify_stop_unwind' here instead in rb_wasm_handle_jmp_unwind
    // because unless that, Asyncify inserts another unwind check here and it unwinds to the root frame.
    asyncify_stop_unwind();
    if ((asyncify_buf = rb_wasm_handle_jmp_unwind()) != NULL) {
      asyncify_start_rewind(asyncify_buf);
      continue;
    }
    if ((asyncify_buf = rb_asyncify_get_active_buf()) != NULL) {
      asyncify_start_rewind(asyncify_buf);
      continue;
    }

    old_entry_point = fiber_entry_point;
    asyncify_buf = rb_wasm_handle_fiber_unwind(&fiber_entry_point, &arg0, &arg1);
    if (asyncify_buf) {
      asyncify_start_rewind(asyncify_buf);
      continue;
    } else if (old_entry_point != fiber_entry_point) {
      continue;
    }

    break;
  }
  return result;
#else
  return rb_main(argc, argv);
#endif
}
