#ifndef RUBY_COMPILE_INTERN_H
#define RUBY_COMPILE_INTERN_H
/**********************************************************************

  compile_intern.h - declarations shared by the iseq compilers

  Copyright (C) 2004-2007 Koichi Sasada

  The instruction list (LINK_ANCHOR) representation and the helpers
  shared by compile.c (the node tree compiler) and prism_compile.c
  (the prism compiler).  This is not a public header.

**********************************************************************/

#include "ruby/internal/config.h"
#include "internal.h"
#include "internal/object.h"
#include "iseq.h"
#include "vm_core.h"
#include "vm_callinfo.h"
#include "insns.inc"

typedef struct iseq_link_element {
    enum {
        ISEQ_ELEMENT_ANCHOR,
        ISEQ_ELEMENT_LABEL,
        ISEQ_ELEMENT_INSN,
        ISEQ_ELEMENT_ADJUST,
        ISEQ_ELEMENT_TRACE,
    } type;
    struct iseq_link_element *next;
    struct iseq_link_element *prev;
} LINK_ELEMENT;

typedef struct iseq_link_anchor {
    LINK_ELEMENT anchor;
    LINK_ELEMENT *last;
} LINK_ANCHOR;

typedef enum {
    LABEL_RESCUE_NONE,
    LABEL_RESCUE_BEG,
    LABEL_RESCUE_END,
    LABEL_RESCUE_TYPE_MAX
} LABEL_RESCUE_TYPE;

typedef struct iseq_label_data {
    LINK_ELEMENT link;
    int label_no;
    int position;
    int sc_state;
    int sp;
    int refcnt;
    unsigned int set: 1;
    unsigned int rescued: 2;
    unsigned int unremovable: 1;
} LABEL;

typedef struct iseq_insn_data {
    LINK_ELEMENT link;
    enum ruby_vminsn_type insn_id;
    int operand_size;
    int sc_state;
    VALUE *operands;
    struct {
        int line_no;
        int node_id;
        rb_event_flag_t events;
    } insn_info;
} INSN;

typedef struct iseq_adjust_data {
    LINK_ELEMENT link;
    LABEL *label;
    int line_no;
} ADJUST;

typedef struct iseq_trace_data {
    LINK_ELEMENT link;
    rb_event_flag_t event;
    long data;
} TRACE;

struct ensure_range {
    LABEL *begin;
    LABEL *end;
    struct ensure_range *next;
};

struct iseq_compile_data_ensure_node_stack {
    const void *ensure_node;
    struct iseq_compile_data_ensure_node_stack *prev;
    struct ensure_range *erange;
};

/**
 * debug function(macro) interface depend on CPDEBUG
 * if it is less than 0, runtime option is in effect.
 *
 * debug level:
 *  0: no debug output
 *  1: show node type
 *  2: show node important parameters
 *  ...
 *  5: show other parameters
 * 10: show every AST array
 */

#ifndef CPDEBUG
#define CPDEBUG 0
#endif

#if CPDEBUG >= 0
#define compile_debug CPDEBUG
#else
#define compile_debug ISEQ_COMPILE_DATA(iseq)->option->debug_level
#endif

#if CPDEBUG

#define compile_debug_print_indent(level) \
    ruby_debug_print_indent((level), compile_debug, gl_node_level * 2)

#define debugp(header, value) (void) \
  (compile_debug_print_indent(1) && \
   ruby_debug_print_value(1, compile_debug, (header), (value)))

#define debugi(header, id)  (void) \
  (compile_debug_print_indent(1) && \
   ruby_debug_print_id(1, compile_debug, (header), (id)))

#define debugp_param(header, value)  (void) \
  (compile_debug_print_indent(1) && \
   ruby_debug_print_value(1, compile_debug, (header), (value)))

#define debugp_verbose(header, value)  (void) \
  (compile_debug_print_indent(2) && \
   ruby_debug_print_value(2, compile_debug, (header), (value)))

#define debugp_verbose_node(header, value)  (void) \
  (compile_debug_print_indent(10) && \
   ruby_debug_print_value(10, compile_debug, (header), (value)))

#define debug_node_start(node)  ((void) \
  (compile_debug_print_indent(1) && \
   (ruby_debug_print_node(1, CPDEBUG, "", (const NODE *)(node)), gl_node_level)), \
   gl_node_level++)

#define debug_node_end()  gl_node_level --

#else

#define debugi(header, id)                 ((void)0)
#define debugp(header, value)              ((void)0)
#define debugp_verbose(header, value)      ((void)0)
#define debugp_verbose_node(header, value) ((void)0)
#define debugp_param(header, value)        ((void)0)
#define debug_node_start(node)             ((void)0)
#define debug_node_end()                   ((void)0)
#endif

#if CPDEBUG > 1 || CPDEBUG < 0
#undef printf
#define printf ruby_debug_printf
#define debugs if (compile_debug_print_indent(1)) ruby_debug_printf
#define debug_compile(msg, v) ((void)(compile_debug_print_indent(1) && fputs((msg), stderr)), (v))
#else
#define debugs                             if(0)printf
#define debug_compile(msg, v) (v)
#endif

#define LVAR_ERRINFO (1)

/* create new label */
#define NEW_LABEL(l) new_label_body(iseq, (l))
#define LABEL_FORMAT "<L%03d>"

#define NEW_ISEQ(node, name, type, line_no) \
  new_child_iseq(iseq, (node), rb_fstring(name), 0, (type), (line_no))

#define NEW_CHILD_ISEQ(node, name, type, line_no) \
  new_child_iseq(iseq, (node), rb_fstring(name), iseq, (type), (line_no))

#define NEW_CHILD_ISEQ_WITH_CALLBACK(callback_func, name, type, line_no) \
  new_child_iseq_with_callback(iseq, (callback_func), (name), iseq, (type), (line_no))

/* add instructions */
#define ADD_SEQ(seq1, seq2) \
  APPEND_LIST((seq1), (seq2))

/* add an instruction */
#define ADD_INSN(seq, line_node, insn) \
  ADD_ELEM((seq), (LINK_ELEMENT *) new_insn_body(iseq, nd_line(line_node), nd_node_id(line_node), BIN(insn), 0))

/* add an instruction with the given line number and node id */
#define ADD_SYNTHETIC_INSN(seq, line_no, node_id, insn) \
  ADD_ELEM((seq), (LINK_ELEMENT *) new_insn_body(iseq, (line_no), (node_id), BIN(insn), 0))

/* insert an instruction before next */
#define INSERT_BEFORE_INSN(next, line_no, node_id, insn) \
  ELEM_INSERT_PREV(&(next)->link, (LINK_ELEMENT *) new_insn_body(iseq, line_no, node_id, BIN(insn), 0))

/* insert an instruction after prev */
#define INSERT_AFTER_INSN(prev, line_no, node_id, insn) \
  ELEM_INSERT_NEXT(&(prev)->link, (LINK_ELEMENT *) new_insn_body(iseq, line_no, node_id, BIN(insn), 0))

/* add an instruction with some operands (1, 2, 3, 5) */
#define ADD_INSN1(seq, line_node, insn, op1) \
  ADD_ELEM((seq), (LINK_ELEMENT *) \
           new_insn_body(iseq, nd_line(line_node), nd_node_id(line_node), BIN(insn), 1, (VALUE)(op1)))

/* insert an instruction with some operands (1, 2, 3, 5) before next */
#define INSERT_BEFORE_INSN1(next, line_no, node_id, insn, op1) \
  ELEM_INSERT_PREV(&(next)->link, (LINK_ELEMENT *) \
           new_insn_body(iseq, line_no, node_id, BIN(insn), 1, (VALUE)(op1)))

/* insert an instruction with some operands (1, 2, 3, 5) after prev */
#define INSERT_AFTER_INSN1(prev, line_no, node_id, insn, op1) \
  ELEM_INSERT_NEXT(&(prev)->link, (LINK_ELEMENT *) \
           new_insn_body(iseq, line_no, node_id, BIN(insn), 1, (VALUE)(op1)))

#define LABEL_REF(label) ((label)->refcnt++)

/* add an instruction with label operand (alias of ADD_INSN1) */
#define ADD_INSNL(seq, line_node, insn, label) (ADD_INSN1(seq, line_node, insn, label), LABEL_REF(label))

#define ADD_INSN2(seq, line_node, insn, op1, op2) \
  ADD_ELEM((seq), (LINK_ELEMENT *) \
           new_insn_body(iseq, nd_line(line_node), nd_node_id(line_node), BIN(insn), 2, (VALUE)(op1), (VALUE)(op2)))

#define ADD_INSN3(seq, line_node, insn, op1, op2, op3) \
  ADD_ELEM((seq), (LINK_ELEMENT *) \
           new_insn_body(iseq, nd_line(line_node), nd_node_id(line_node), BIN(insn), 3, (VALUE)(op1), (VALUE)(op2), (VALUE)(op3)))

/* Specific Insn factory */
#define ADD_SEND(seq, line_node, id, argc) \
  ADD_SEND_R((seq), (line_node), (id), (argc), NULL, (VALUE)INT2FIX(0), NULL)

#define ADD_SEND_WITH_FLAG(seq, line_node, id, argc, flag) \
  ADD_SEND_R((seq), (line_node), (id), (argc), NULL, (VALUE)(flag), NULL)

#define ADD_SEND_WITH_BLOCK(seq, line_node, id, argc, block) \
  ADD_SEND_R((seq), (line_node), (id), (argc), (block), (VALUE)INT2FIX(0), NULL)

#define ADD_CALL_RECEIVER(seq, line_node) \
  ADD_INSN((seq), (line_node), putself)

#define ADD_CALL(seq, line_node, id, argc) \
  ADD_SEND_R((seq), (line_node), (id), (argc), NULL, (VALUE)INT2FIX(VM_CALL_FCALL), NULL)

#define ADD_CALL_WITH_BLOCK(seq, line_node, id, argc, block) \
  ADD_SEND_R((seq), (line_node), (id), (argc), (block), (VALUE)INT2FIX(VM_CALL_FCALL), NULL)

#define ADD_SEND_R(seq, line_node, id, argc, block, flag, keywords) \
  ADD_ELEM((seq), (LINK_ELEMENT *) new_insn_send(iseq, nd_line(line_node), nd_node_id(line_node), (id), (VALUE)(argc), (block), (VALUE)(flag), (keywords)))

#define ADD_TRACE(seq, event) \
  ADD_ELEM((seq), (LINK_ELEMENT *)new_trace_body(iseq, (event), 0))
#define ADD_TRACE_WITH_DATA(seq, event, data) \
  ADD_ELEM((seq), (LINK_ELEMENT *)new_trace_body(iseq, (event), (data)))

/* add label */
#define ADD_LABEL(seq, label) \
  ADD_ELEM((seq), (LINK_ELEMENT *) (label))

#define APPEND_LABEL(seq, before, label) \
  APPEND_ELEM((seq), (before), (LINK_ELEMENT *) (label))

#define ADD_ADJUST(seq, line_node, label) \
  ADD_ELEM((seq), (LINK_ELEMENT *) new_adjust_body(iseq, (label), nd_line(line_node)))

#define ADD_ADJUST_RESTORE(seq, label) \
  ADD_ELEM((seq), (LINK_ELEMENT *) new_adjust_body(iseq, (label), -1))

#define LABEL_UNREMOVABLE(label) \
    ((label) ? (LABEL_REF(label), (label)->unremovable=1) : 0)
#define ADD_CATCH_ENTRY(type, ls, le, iseqv, lc) do {				\
    VALUE _e = rb_ary_new3(5, (type),						\
                           (VALUE)(ls) | 1, (VALUE)(le) | 1,			\
                           (VALUE)(iseqv), (VALUE)(lc) | 1);			\
    LABEL_UNREMOVABLE(ls);							\
    LABEL_REF(le);								\
    LABEL_REF(lc);								\
    if (NIL_P(ISEQ_COMPILE_DATA(iseq)->catch_table_ary)) \
        RB_OBJ_WRITE(iseq, &ISEQ_COMPILE_DATA(iseq)->catch_table_ary, rb_ary_hidden_new(3)); \
    rb_ary_push(ISEQ_COMPILE_DATA(iseq)->catch_table_ary, freeze_hide_obj(_e));	\
} while (0)

#define OPERAND_AT(insn, idx) \
  (((INSN*)(insn))->operands[(idx)])

#define INSN_OF(insn) \
  (((INSN*)(insn))->insn_id)

#define IS_INSN(link) ((link)->type == ISEQ_ELEMENT_INSN)
#define IS_LABEL(link) ((link)->type == ISEQ_ELEMENT_LABEL)
#define IS_ADJUST(link) ((link)->type == ISEQ_ELEMENT_ADJUST)
#define IS_TRACE(link) ((link)->type == ISEQ_ELEMENT_TRACE)
#define IS_INSN_ID(iobj, insn) (INSN_OF(iobj) == BIN(insn))
#define IS_NEXT_INSN_ID(link, insn) \
    ((link)->next && IS_INSN((link)->next) && IS_INSN_ID((link)->next, insn))

static inline bool
IS_INDEPENDENT_INSN(LINK_ELEMENT *link)
{
    if (!IS_INSN(link)) {
        return false;
    }

    enum ruby_vminsn_type type = INSN_OF(link);

    return (
        type == BIN(putobject) ||
        type == BIN(putspecialobject) ||
        type == BIN(putnil) ||
        type == BIN(putself) ||
        type == BIN(duphash) ||
        type == BIN(getinstancevariable) ||
        type == BIN(getlocal) ||
        type == BIN(getlocal_WC_0) ||
        type == BIN(getlocal_WC_1) ||
        type == BIN(putobject_INT2FIX_0_) ||
        type == BIN(putobject_INT2FIX_1_) ||
        type == BIN(opt_getconstant_path)
    );
}

#define COMPILE_ERROR append_compile_error

#define ERROR_ARGS_AT(n) iseq, nd_line(n),
#define ERROR_ARGS ERROR_ARGS_AT(node)

#define EXPECT_NODE(prefix, node, ndtype, errval) \
do { \
    const NODE *error_node = (node); \
    enum node_type error_type = nd_type(error_node); \
    if (error_type != (ndtype)) { \
        COMPILE_ERROR(ERROR_ARGS_AT(error_node) \
                      prefix ": " #ndtype " is expected, but %s", \
                      ruby_node_name(error_type)); \
        return errval; \
    } \
} while (0)

#define EXPECT_NODE_NONULL(prefix, parent, ndtype, errval) \
do { \
    COMPILE_ERROR(ERROR_ARGS_AT(parent) \
                  prefix ": must be " #ndtype ", but 0"); \
    return errval; \
} while (0)

#define UNKNOWN_NODE(prefix, node, errval) \
do { \
    const NODE *error_node = (node); \
    COMPILE_ERROR(ERROR_ARGS_AT(error_node) prefix ": unknown node (%s)", \
                  ruby_node_name(nd_type(error_node))); \
    return errval; \
} while (0)

#define COMPILE_OK 1
#define COMPILE_NG 0

#define CHECK(sub) if (!(sub)) {BEFORE_RETURN;return COMPILE_NG;}
#define NO_CHECK(sub) (void)(sub)
#define BEFORE_RETURN

#define DECL_ANCHOR(name) \
    LINK_ANCHOR name[1] = {{{ISEQ_ELEMENT_ANCHOR,},&name[0].anchor}}
#define INIT_ANCHOR(name) \
    ((name->last = &name->anchor)->next = NULL) /* re-initialize */

static inline VALUE
freeze_hide_obj(VALUE obj)
{
    OBJ_FREEZE(obj);
    RBASIC_CLEAR_CLASS(obj);
    return obj;
}

/* for debug */
#if CPDEBUG < 0
#define ISEQ_ARG iseq,
#define ISEQ_ARG_DECLARE rb_iseq_t *iseq,
#else
#define ISEQ_ARG
#define ISEQ_ARG_DECLARE
#endif

#if CPDEBUG
#define gl_node_level ISEQ_COMPILE_DATA(iseq)->node_level
#endif

/*
 * To make Array to LinkedList, use link_anchor
 */

static inline void
verify_list(ISEQ_ARG_DECLARE const char *info, LINK_ANCHOR *const anchor)
{
#if CPDEBUG
    int flag = 0;
    LINK_ELEMENT *list, *plist;

    if (!compile_debug) return;

    list = anchor->anchor.next;
    plist = &anchor->anchor;
    while (list) {
        if (plist != list->prev) {
            flag += 1;
        }
        plist = list;
        list = list->next;
    }

    if (anchor->last != plist && anchor->last != 0) {
        flag |= 0x70000;
    }

    if (flag != 0) {
        rb_bug("list verify error: %08x (%s)", flag, info);
    }
#endif
}
#if CPDEBUG < 0
#define verify_list(info, anchor) verify_list(iseq, (info), (anchor))
#endif

/*
 * elem1, elem2 => elem1, elem2, elem
 */
static inline void
ADD_ELEM(ISEQ_ARG_DECLARE LINK_ANCHOR *const anchor, LINK_ELEMENT *elem)
{
    elem->prev = anchor->last;
    anchor->last->next = elem;
    anchor->last = elem;
    verify_list("add", anchor);
}

/*
 * elem1, before, elem2 => elem1, before, elem, elem2
 */
static inline void
APPEND_ELEM(ISEQ_ARG_DECLARE LINK_ANCHOR *const anchor, LINK_ELEMENT *before, LINK_ELEMENT *elem)
{
    elem->prev = before;
    elem->next = before->next;
    elem->next->prev = elem;
    before->next = elem;
    if (before == anchor->last) anchor->last = elem;
    verify_list("add", anchor);
}
#if CPDEBUG < 0
#define ADD_ELEM(anchor, elem) ADD_ELEM(iseq, (anchor), (elem))
#define APPEND_ELEM(anchor, before, elem) APPEND_ELEM(iseq, (anchor), (before), (elem))
#endif

#define ALIGNMENT_SIZE_OF(type) alignment_size_assert(RUBY_ALIGNOF(type), #type)

static inline size_t
alignment_size_assert(size_t align, const char *type)
{
    RUBY_ASSERT((align & (align - 1)) == 0,
                "ALIGNMENT_SIZE_OF(%s):%zd == (2 ** N) is expected", type, align);
    return align;
}

#define compile_data_alloc2_type(iseq, type, num) \
    (type *)compile_data_alloc2(iseq, sizeof(type), num, ALIGNMENT_SIZE_OF(type))

/*
 * elem1, elemX => elem1, elem2, elemX
 */
static inline void
ELEM_INSERT_NEXT(LINK_ELEMENT *elem1, LINK_ELEMENT *elem2)
{
    elem2->next = elem1->next;
    elem2->prev = elem1;
    elem1->next = elem2;
    if (elem2->next) {
        elem2->next->prev = elem2;
    }
}

/*
 * elem1, elemX => elemX, elem2, elem1
 */
static inline void
ELEM_INSERT_PREV(LINK_ELEMENT *elem1, LINK_ELEMENT *elem2)
{
    elem2->prev = elem1->prev;
    elem2->next = elem1;
    elem1->prev = elem2;
    if (elem2->prev) {
        elem2->prev->next = elem2;
    }
}

static inline LINK_ELEMENT *
FIRST_ELEMENT(const LINK_ANCHOR *const anchor)
{
    return anchor->anchor.next;
}

static inline LINK_ELEMENT *
LAST_ELEMENT(LINK_ANCHOR *const anchor)
{
    return anchor->last;
}

static inline LINK_ELEMENT *
ELEM_FIRST_INSN(LINK_ELEMENT *elem)
{
    while (elem) {
        switch (elem->type) {
          case ISEQ_ELEMENT_INSN:
          case ISEQ_ELEMENT_ADJUST:
            return elem;
          default:
            elem = elem->next;
        }
    }
    return NULL;
}

static inline int
LIST_INSN_SIZE_ONE(const LINK_ANCHOR *const anchor)
{
    LINK_ELEMENT *first_insn = ELEM_FIRST_INSN(FIRST_ELEMENT(anchor));
    if (first_insn != NULL &&
        ELEM_FIRST_INSN(first_insn->next) == NULL) {
        return TRUE;
    }
    else {
        return FALSE;
    }
}

static inline int
LIST_INSN_SIZE_ZERO(const LINK_ANCHOR *const anchor)
{
    if (ELEM_FIRST_INSN(FIRST_ELEMENT(anchor)) == NULL) {
        return TRUE;
    }
    else {
        return FALSE;
    }
}

/*
 * anc1: e1, e2, e3
 * anc2: e4, e5
 *#=>
 * anc1: e1, e2, e3, e4, e5
 * anc2: e4, e5 (broken)
 */
static inline void
APPEND_LIST(ISEQ_ARG_DECLARE LINK_ANCHOR *const anc1, LINK_ANCHOR *const anc2)
{
    if (anc2->anchor.next) {
        /* LINK_ANCHOR must not loop */
        RUBY_ASSERT(anc2->last != &anc2->anchor);
        anc1->last->next = anc2->anchor.next;
        anc2->anchor.next->prev = anc1->last;
        anc1->last = anc2->last;
    }
    else {
        RUBY_ASSERT(anc2->last == &anc2->anchor);
    }
    verify_list("append", anc1);
}
#if CPDEBUG < 0
#define APPEND_LIST(anc1, anc2) APPEND_LIST(iseq, (anc1), (anc2))
#endif

/*
 * Helpers defined in compile.c and used by other compilation units.
 * They are not static, so they carry the rb_iseq_ prefix; the aliases
 * keep their short names at the call sites.
 */
#define access_outer_variables rb_iseq_access_outer_variables
#define add_ensure_range rb_iseq_add_ensure_range
#define add_trace_branch_coverage rb_iseq_add_trace_branch_coverage
#define append_compile_error rb_iseq_append_compile_error
#define build_defined_rescue_iseq rb_iseq_build_defined_rescue_iseq
#define can_add_ensure_iseq rb_iseq_can_add_ensure_iseq
#define cdhash_aset_if_missing rb_iseq_cdhash_aset_if_missing
#define cdhash_new rb_iseq_cdhash_new
#define compile_builtin_attr_symbol rb_iseq_compile_builtin_attr_symbol
#define compile_data_alloc2 rb_iseq_compile_data_alloc2
#define decl_branch_base rb_iseq_decl_branch_base
#define delegate_call_p rb_iseq_delegate_call_p
#define get_cvar_ic_value rb_iseq_get_cvar_ic_value
#define get_ivar_ic_value rb_iseq_get_ivar_ic_value
#define get_local_var_idx rb_iseq_get_local_var_idx
#define get_lvar_level rb_iseq_get_lvar_level
#define get_next_insn rb_iseq_get_next_insn
#define get_prev_insn rb_iseq_get_prev_insn
#define iseq_block_param_id_p rb_iseq_block_param_id_p
#define iseq_builtin_function_lookup rb_iseq_builtin_function_lookup
#define iseq_calc_param_size rb_iseq_calc_param_size
#define iseq_has_builtin_function_table rb_iseq_has_builtin_function_table
#define iseq_local_block_param_p rb_iseq_local_block_param_p
#define iseq_lvar_id rb_iseq_lvar_id
#define iseq_set_exception_local_table rb_iseq_set_exception_local_table
#define iseq_set_local_table rb_iseq_set_local_table
#define iseq_set_parameters_lvar_state rb_iseq_set_parameters_lvar_state
#define iseq_set_use_block rb_iseq_set_use_block
#define iseq_setup rb_iseq_setup
#define iseq_setup_insn rb_iseq_setup_insn
#define make_name_for_block rb_iseq_make_name_for_block
#define new_adjust_body rb_iseq_new_adjust_body
#define new_callinfo rb_iseq_new_callinfo
#define new_child_iseq_with_callback rb_iseq_new_child_iseq_with_callback
#define new_insn_body rb_iseq_new_insn_body
#define new_insn_send rb_iseq_new_insn_send
#define new_label_body rb_iseq_new_label_body
#define new_trace_body rb_iseq_new_trace_body
#define push_ensure_entry rb_iseq_push_ensure_entry
#define update_lvar_state rb_iseq_update_lvar_state

void access_outer_variables(const rb_iseq_t *iseq, int level, ID id, bool write);
void add_ensure_range(rb_iseq_t *iseq, struct ensure_range *erange, LABEL *lstart, LABEL *lend);
void add_trace_branch_coverage(rb_iseq_t *iseq, LINK_ANCHOR *const seq, const rb_code_location_t *loc, int node_id, int branch_id, const char *type, VALUE branches);
#if CPDEBUG > 0
RBIMPL_ATTR_NORETURN()
#endif
RBIMPL_ATTR_FORMAT(RBIMPL_PRINTF_FORMAT, 3, 4)
void append_compile_error(const rb_iseq_t *iseq, int line, const char *fmt, ...);
void build_defined_rescue_iseq(rb_iseq_t *iseq, LINK_ANCHOR *const ret, const void *unused);
bool can_add_ensure_iseq(const rb_iseq_t *iseq);
void cdhash_aset_if_missing(VALUE cdhash, VALUE key, VALUE val);
VALUE cdhash_new(size_t size);
int compile_builtin_attr_symbol(rb_iseq_t *iseq, VALUE symbol);
void *compile_data_alloc2(rb_iseq_t *iseq, size_t elsize, size_t num, size_t align);
VALUE decl_branch_base(rb_iseq_t *iseq, int node_id, const rb_code_location_t *loc, const char *type);
int delegate_call_p(const rb_iseq_t *iseq, unsigned int argc, const LINK_ANCHOR *args, unsigned int *pstart_index);
VALUE get_cvar_ic_value(rb_iseq_t *iseq,ID id);
VALUE get_ivar_ic_value(rb_iseq_t *iseq,ID id);
int get_local_var_idx(const rb_iseq_t *iseq, ID id);
int get_lvar_level(const rb_iseq_t *iseq);
LINK_ELEMENT *get_next_insn(INSN *iobj);
LINK_ELEMENT *get_prev_insn(INSN *iobj);
int iseq_block_param_id_p(const rb_iseq_t *iseq, ID id, int *pidx, int *plevel);
const struct rb_builtin_function *iseq_builtin_function_lookup(const rb_iseq_t *iseq, const char *name);
void iseq_calc_param_size(rb_iseq_t *iseq);
int iseq_has_builtin_function_table(const rb_iseq_t *iseq);
int iseq_local_block_param_p(const rb_iseq_t *iseq, unsigned int idx, unsigned int level);
ID iseq_lvar_id(const rb_iseq_t *iseq, int idx, int level);
int iseq_set_exception_local_table(rb_iseq_t *iseq);
int iseq_set_local_table(rb_iseq_t *iseq, const rb_ast_id_table_t *tbl, const NODE *const node_args);
int iseq_set_parameters_lvar_state(const rb_iseq_t *iseq);
void iseq_set_use_block(rb_iseq_t *iseq);
int iseq_setup(rb_iseq_t *iseq, LINK_ANCHOR *const anchor);
int iseq_setup_insn(rb_iseq_t *iseq, LINK_ANCHOR *const anchor);
VALUE make_name_for_block(const rb_iseq_t *orig_iseq);
ADJUST *new_adjust_body(rb_iseq_t *iseq, LABEL *label, int line);
const struct rb_callinfo *new_callinfo(rb_iseq_t *iseq, ID mid, int argc, unsigned int flag, struct rb_callinfo_kwarg *kw_arg, int has_blockiseq);
rb_iseq_t *new_child_iseq_with_callback(rb_iseq_t *iseq, const struct rb_iseq_new_with_callback_callback_func *ifunc, VALUE name, const rb_iseq_t *parent, enum rb_iseq_type type, int line_no);
INSN *new_insn_body(rb_iseq_t *iseq, int line_no, int node_id, enum ruby_vminsn_type insn_id, int argc, ...);
INSN *new_insn_send(rb_iseq_t *iseq, int line_no, int node_id, ID id, VALUE argc, const rb_iseq_t *blockiseq, VALUE flag, struct rb_callinfo_kwarg *keywords);
LABEL *new_label_body(rb_iseq_t *iseq, long line);
TRACE *new_trace_body(rb_iseq_t *iseq, rb_event_flag_t event, long data);
void push_ensure_entry(rb_iseq_t *iseq, struct iseq_compile_data_ensure_node_stack *enl, struct ensure_range *er, const void *const node);
void update_lvar_state(const rb_iseq_t *iseq, int level, int idx);

#endif /* RUBY_COMPILE_INTERN_H */
