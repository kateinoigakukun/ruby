/* -*-c-*- */
/*
 * from eval.c
 */

#include "eval_intern.h"

/* exit */

void
rb_call_end_proc(VALUE data)
{
    rb_proc_call(data, rb_ary_new());
}

/*
 *  call-seq:
 *     at_exit { block } -> proc
 *
 *  Converts _block_ to a +Proc+ object (and therefore
 *  binds it at the point of call) and registers it for execution when
 *  the program exits. If multiple handlers are registered, they are
 *  executed in reverse order of registration.
 *
 *     def do_at_exit(str1)
 *       at_exit { print str1 }
 *     end
 *     at_exit { puts "cruel world" }
 *     do_at_exit("goodbye ")
 *     exit
 *
 *  <em>produces:</em>
 *
 *     goodbye cruel world
 */

static VALUE
rb_f_at_exit(VALUE _)
{
    VALUE proc;

    if (!rb_block_given_p()) {
	rb_raise(rb_eArgError, "called without a block");
    }
    proc = rb_block_proc();
    rb_set_end_proc(rb_call_end_proc, proc);
    return proc;
}

struct end_proc_data {
    void (*func) (VALUE);
    VALUE data;
    struct end_proc_data *next;
};

static struct end_proc_data *end_procs, *ephemeral_end_procs;

void
rb_set_end_proc(void (*func)(VALUE), VALUE data)
{
    struct end_proc_data *link = ALLOC(struct end_proc_data);
    struct end_proc_data **list;
    rb_thread_t *th = GET_THREAD();

    if (th->top_wrapper) {
	list = &ephemeral_end_procs;
    }
    else {
	list = &end_procs;
    }
    link->next = *list;
    link->func = func;
    link->data = data;
    *list = link;
}

void
rb_mark_end_proc(void)
{
    struct end_proc_data *link;

    link = end_procs;
    while (link) {
	rb_gc_mark(link->data);
	link = link->next;
    }
    link = ephemeral_end_procs;
    while (link) {
	rb_gc_mark(link->data);
	link = link->next;
    }
}

static void
exec_end_procs_chain(struct end_proc_data *volatile *procs, VALUE *errp)
{
    struct end_proc_data volatile endproc;
    struct end_proc_data *link;
    VALUE errinfo = *errp;

    while ((link = *procs) != 0) {
	*procs = link->next;
	endproc = *link;
	xfree(link);
	(*endproc.func) (endproc.data);
	*errp = errinfo;
    }
}

// FIXME(katei): Please place it in a suitable file!!!
enum ruby_tag_type
rb_try_catch(rb_execution_context_t *ec,
             void (* b_proc) (VALUE), VALUE data1,
             enum ruby_tag_type (* r_proc) (VALUE, enum ruby_tag_type), VALUE data2);

struct  rb_ec_exec_end_proc_context {
    rb_execution_context_t * ec;
    volatile VALUE errinfo;
};

static void rb_ec_exec_end_proc_main(VALUE v)
{
    struct rb_ec_exec_end_proc_context *ctx = (struct rb_ec_exec_end_proc_context *)v;
    exec_end_procs_chain(&ephemeral_end_procs, &ctx->ec->errinfo);
    exec_end_procs_chain(&end_procs, &ctx->ec->errinfo);
}

static enum ruby_tag_type rb_ec_exec_end_proc_rescue(VALUE v, enum ruby_tag_type state)
{
    struct rb_ec_exec_end_proc_context *ctx = (struct rb_ec_exec_end_proc_context *)v;
    rb_execution_context_t *ec = ctx->ec;

    // FIXME(katei): Should encapsulate _ec and _tag variables?
    rb_execution_context_t *_ec = ec;
    struct rb_vm_tag _tag = *ec->tag;

	EC_TMPPOP_TAG();
    error_handle(ec, state);
	if (!NIL_P(ec->errinfo)) {
        ctx->errinfo = ec->errinfo;
    }
	EC_REPUSH_TAG();
    return state;
}

static void
rb_ec_exec_end_proc(rb_execution_context_t * ec)
{
    enum ruby_tag_type state;
    struct rb_ec_exec_end_proc_context ctx = {
        .ec = ec, .errinfo = ec->errinfo
    };

    do {
        state = rb_try_catch(ec, rb_ec_exec_end_proc_main, (VALUE)&ctx, rb_ec_exec_end_proc_rescue, (VALUE)&ctx);
    } while (state != TAG_NONE);

    ec->errinfo = ctx.errinfo;
}

void
Init_jump(void)
{
    rb_define_global_function("at_exit", rb_f_at_exit, 0);
}
