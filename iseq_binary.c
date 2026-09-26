/**********************************************************************

  iseq_binary.c - ISeq binary format (RubyVM::InstructionSequence#to_binary
                  and .load_from_binary)

  Copyright (C) 2004-2007 Koichi Sasada

**********************************************************************/

#include "ruby/internal/config.h"

#include "internal.h"
#include "internal/encoding.h"
#include "internal/error.h"
#include "internal/hash.h"
#include "internal/re.h"
#include "internal/ruby_parser.h"
#include "internal/symbol.h"
#include "ruby/util.h"
#include "builtin.h"
#include "compile_intern.h"
#include "insns_info.inc"

/* ISeq binary format */

#ifndef IBF_ISEQ_DEBUG
#define IBF_ISEQ_DEBUG 0
#endif

#ifndef IBF_ISEQ_ENABLE_LOCAL_BUFFER
#define IBF_ISEQ_ENABLE_LOCAL_BUFFER 0
#endif

typedef uint32_t ibf_offset_t;
#define IBF_OFFSET(ptr) ((ibf_offset_t)(VALUE)(ptr))

#define IBF_MAJOR_VERSION ISEQ_MAJOR_VERSION
#ifdef RUBY_DEVEL
#define IBF_DEVEL_VERSION 8
#define IBF_MINOR_VERSION (ISEQ_MINOR_VERSION * 10000 + IBF_DEVEL_VERSION)
#else
#define IBF_MINOR_VERSION ISEQ_MINOR_VERSION
#endif

static const char IBF_ENDIAN_MARK =
#ifdef WORDS_BIGENDIAN
    'b'
#else
    'l'
#endif
    ;

struct ibf_header {
    char magic[4]; /* YARB */
    uint32_t major_version;
    uint32_t minor_version;
    uint32_t size;
    uint32_t extra_size;

    uint32_t iseq_list_size;
    uint32_t global_object_list_size;
    ibf_offset_t iseq_list_offset;
    ibf_offset_t global_object_list_offset;
    uint8_t endian;
    uint8_t wordsize;           /* assume no 2048-bit CPU */
};

struct ibf_dump_buffer {
    VALUE str;
    st_table *obj_table; /* obj -> obj number */
};

struct ibf_dump {
    st_table *iseq_table; /* iseq -> iseq number */
    struct ibf_dump_buffer global_buffer;
    struct ibf_dump_buffer *current_buffer;
};

struct ibf_load_buffer {
    const char *buff;
    ibf_offset_t size;

    VALUE obj_list; /* [obj0, ...] */
    unsigned int obj_list_size;
    ibf_offset_t obj_list_offset;
};

struct ibf_load {
    const struct ibf_header *header;
    VALUE iseq_list;       /* [iseq0, ...] */
    struct ibf_load_buffer global_buffer;
    VALUE loader_obj;
    rb_iseq_t *iseq;
    VALUE str;
    struct ibf_load_buffer *current_buffer;
};

struct pinned_list {
    long size;
    VALUE buffer[1];
};

static void
pinned_list_mark(void *ptr)
{
    long i;
    struct pinned_list *list = (struct pinned_list *)ptr;
    for (i = 0; i < list->size; i++) {
        if (list->buffer[i]) {
            rb_gc_mark(list->buffer[i]);
        }
    }
}

static const rb_data_type_t pinned_list_type = {
    "pinned_list",
    {
        pinned_list_mark,
        RUBY_DEFAULT_FREE,
        NULL, // No external memory to report,
    },
    0, 0, RUBY_TYPED_WB_PROTECTED | RUBY_TYPED_THREAD_SAFE_FREE | RUBY_TYPED_EMBEDDABLE
};

static VALUE
pinned_list_fetch(VALUE list, long offset)
{
    struct pinned_list * ptr;

    TypedData_Get_Struct(list, struct pinned_list, &pinned_list_type, ptr);

    if (offset < 0 || offset >= ptr->size) {
        rb_raise(rb_eIndexError, "object index out of range: %ld", offset);
    }

    return ptr->buffer[offset];
}

static void
pinned_list_store(VALUE list, long offset, VALUE object)
{
    struct pinned_list * ptr;

    TypedData_Get_Struct(list, struct pinned_list, &pinned_list_type, ptr);

    if (offset < 0 || offset >= ptr->size) {
        rb_raise(rb_eIndexError, "object index out of range: %ld", offset);
    }

    RB_OBJ_WRITE(list, &ptr->buffer[offset], object);
}

static VALUE
pinned_list_new(long size)
{
    size_t memsize = offsetof(struct pinned_list, buffer) + size * sizeof(VALUE);
    VALUE obj_list = rb_data_typed_object_zalloc(0, memsize, &pinned_list_type);
    struct pinned_list * ptr = RTYPEDDATA_GET_DATA(obj_list);
    ptr->size = size;
    return obj_list;
}

static ibf_offset_t
ibf_dump_pos(struct ibf_dump *dump)
{
    long pos = RSTRING_LEN(dump->current_buffer->str);
#if SIZEOF_LONG > SIZEOF_INT
    if (pos >= UINT_MAX) {
        rb_raise(rb_eRuntimeError, "dump size exceeds");
    }
#endif
    return (unsigned int)pos;
}

static void
ibf_dump_align(struct ibf_dump *dump, size_t align)
{
    ibf_offset_t pos = ibf_dump_pos(dump);
    if (pos % align) {
        static const char padding[sizeof(VALUE)];
        size_t size = align - ((size_t)pos % align);
#if SIZEOF_LONG > SIZEOF_INT
        if (pos + size >= UINT_MAX) {
            rb_raise(rb_eRuntimeError, "dump size exceeds");
        }
#endif
        for (; size > sizeof(padding); size -= sizeof(padding)) {
            rb_str_cat(dump->current_buffer->str, padding, sizeof(padding));
        }
        rb_str_cat(dump->current_buffer->str, padding, size);
    }
}

static ibf_offset_t
ibf_dump_write(struct ibf_dump *dump, const void *buff, unsigned long size)
{
    ibf_offset_t pos = ibf_dump_pos(dump);
#if SIZEOF_LONG > SIZEOF_INT
    /* ensure the resulting dump does not exceed UINT_MAX */
    if (size >= UINT_MAX || pos + size >= UINT_MAX) {
        rb_raise(rb_eRuntimeError, "dump size exceeds");
    }
#endif
    rb_str_cat(dump->current_buffer->str, (const char *)buff, size);
    return pos;
}

static ibf_offset_t
ibf_dump_write_byte(struct ibf_dump *dump, unsigned char byte)
{
    return ibf_dump_write(dump, &byte, sizeof(unsigned char));
}

static void
ibf_dump_overwrite(struct ibf_dump *dump, void *buff, unsigned int size, long offset)
{
    VALUE str = dump->current_buffer->str;
    char *ptr = RSTRING_PTR(str);
    if ((unsigned long)(size + offset) > (unsigned long)RSTRING_LEN(str))
        rb_bug("ibf_dump_overwrite: overflow");
    memcpy(ptr + offset, buff, size);
}

static const void *
ibf_load_ptr(const struct ibf_load *load, ibf_offset_t *offset, int size)
{
    ibf_offset_t beg = *offset;
    *offset += size;
    return load->current_buffer->buff + beg;
}

static void *
ibf_load_alloc(const struct ibf_load *load, ibf_offset_t offset, size_t x, size_t y)
{
    void *buff = ruby_xmalloc2(x, y);
    size_t size = x * y;
    memcpy(buff, load->current_buffer->buff + offset, size);
    return buff;
}

#define IBF_W_ALIGN(type) (RUBY_ALIGNOF(type) > 1 ? ibf_dump_align(dump, RUBY_ALIGNOF(type)) : (void)0)

#define IBF_W(b, type, n) (IBF_W_ALIGN(type), (type *)(VALUE)IBF_WP(b, type, n))
#define IBF_WV(variable)   ibf_dump_write(dump, &(variable), sizeof(variable))
#define IBF_WP(b, type, n) ibf_dump_write(dump, (b), sizeof(type) * (n))
#define IBF_R(val, type, n) (type *)ibf_load_alloc(load, IBF_OFFSET(val), sizeof(type), (n))
#define IBF_ZERO(variable) memset(&(variable), 0, sizeof(variable))

static int
ibf_table_lookup(struct st_table *table, st_data_t key)
{
    st_data_t val;

    if (st_lookup(table, key, &val)) {
        return (int)val;
    }
    else {
        return -1;
    }
}

static int
ibf_table_find_or_insert(struct st_table *table, st_data_t key)
{
    int index = ibf_table_lookup(table, key);

    if (index < 0) { /* not found */
        index = (int)table->num_entries;
        st_insert(table, key, (st_data_t)index);
    }

    return index;
}

/* dump/load generic */

static void ibf_dump_object_list(struct ibf_dump *dump, ibf_offset_t *obj_list_offset, unsigned int *obj_list_size);

static VALUE ibf_load_object(const struct ibf_load *load, VALUE object_index);
static rb_iseq_t *ibf_load_iseq(const struct ibf_load *load, const rb_iseq_t *index_iseq);

static st_table *
ibf_dump_object_table_new(void)
{
    st_table *obj_table = st_init_numtable(); /* need free */
    st_insert(obj_table, (st_data_t)Qnil, (st_data_t)0); /* 0th is nil */

    return obj_table;
}

static VALUE
ibf_dump_object(struct ibf_dump *dump, VALUE obj)
{
    return ibf_table_find_or_insert(dump->current_buffer->obj_table, (st_data_t)obj);
}

static VALUE
ibf_dump_id(struct ibf_dump *dump, ID id)
{
    if (id == 0 || rb_id2name(id) == NULL) {
        return 0;
    }
    return ibf_dump_object(dump, rb_id2sym(id));
}

static ID
ibf_load_id(const struct ibf_load *load, const ID id_index)
{
    if (id_index == 0) {
        return 0;
    }
    VALUE sym = ibf_load_object(load, id_index);
    if (rb_integer_type_p(sym)) {
        /* Load hidden local variables as indexes */
        return NUM2ULONG(sym);
    }
    return rb_sym2id(sym);
}

/* dump/load: code */

static ibf_offset_t ibf_dump_iseq_each(struct ibf_dump *dump, const rb_iseq_t *iseq);

static int
ibf_dump_iseq(struct ibf_dump *dump, const rb_iseq_t *iseq)
{
    if (iseq == NULL) {
        return -1;
    }
    else {
        return ibf_table_find_or_insert(dump->iseq_table, (st_data_t)iseq);
    }
}

static unsigned char
ibf_load_byte(const struct ibf_load *load, ibf_offset_t *offset)
{
    if (*offset >= load->current_buffer->size) { rb_raise(rb_eRuntimeError, "invalid bytecode"); }
    return (unsigned char)load->current_buffer->buff[(*offset)++];
}

/*
 * Small uint serialization
 * 0x00000000_00000000 - 0x00000000_0000007f: 1byte | XXXX XXX1 |
 * 0x00000000_00000080 - 0x00000000_00003fff: 2byte | XXXX XX10 | XXXX XXXX |
 * 0x00000000_00004000 - 0x00000000_001fffff: 3byte | XXXX X100 | XXXX XXXX | XXXX XXXX |
 * 0x00000000_00020000 - 0x00000000_0fffffff: 4byte | XXXX 1000 | XXXX XXXX | XXXX XXXX | XXXX XXXX |
 * ...
 * 0x00010000_00000000 - 0x00ffffff_ffffffff: 8byte | 1000 0000 | XXXX XXXX | XXXX XXXX | XXXX XXXX | XXXX XXXX | XXXX XXXX | XXXX XXXX | XXXX XXXX |
 * 0x01000000_00000000 - 0xffffffff_ffffffff: 9byte | 0000 0000 | XXXX XXXX | XXXX XXXX | XXXX XXXX | XXXX XXXX | XXXX XXXX | XXXX XXXX | XXXX XXXX | XXXX XXXX |
 */
static void
ibf_dump_write_small_value(struct ibf_dump *dump, VALUE x)
{
    if (sizeof(VALUE) > 8 || CHAR_BIT != 8) {
        ibf_dump_write(dump, &x, sizeof(VALUE));
        return;
    }

    enum { max_byte_length = sizeof(VALUE) + 1 };

    unsigned char bytes[max_byte_length];
    ibf_offset_t n;

    for (n = 0; n < sizeof(VALUE) && (x >> (7 - n)); n++, x >>= 8) {
        bytes[max_byte_length - 1 - n] = (unsigned char)x;
    }

    x <<= 1;
    x |= 1;
    x <<= n;
    bytes[max_byte_length - 1 - n] = (unsigned char)x;
    n++;

    ibf_dump_write(dump, bytes + max_byte_length - n, n);
}

static VALUE
ibf_load_small_value(const struct ibf_load *load, ibf_offset_t *offset)
{
    if (sizeof(VALUE) > 8 || CHAR_BIT != 8) {
        union { char s[sizeof(VALUE)]; VALUE v; } x;

        memcpy(x.s, load->current_buffer->buff + *offset, sizeof(VALUE));
        *offset += sizeof(VALUE);

        return x.v;
    }

    enum { max_byte_length = sizeof(VALUE) + 1 };

    const unsigned char *buffer = (const unsigned char *)load->current_buffer->buff;
    const unsigned char c = buffer[*offset];

    ibf_offset_t n =
        c & 1 ? 1 :
        c == 0 ? 9 : ntz_int32(c) + 1;
    VALUE x = (VALUE)c >> n;

    if (*offset + n > load->current_buffer->size) {
        rb_raise(rb_eRuntimeError, "invalid byte sequence");
    }

    ibf_offset_t i;
    for (i = 1; i < n; i++) {
        x <<= 8;
        x |= (VALUE)buffer[*offset + i];
    }

    *offset += n;
    return x;
}

static void
ibf_dump_builtin(struct ibf_dump *dump, const struct rb_builtin_function *bf)
{
    // short: index
    // short: name.length
    // bytes: name
    // // omit argc (only verify with name)
    ibf_dump_write_small_value(dump, (VALUE)bf->index);

    size_t len = strlen(bf->name);
    ibf_dump_write_small_value(dump, (VALUE)len);
    ibf_dump_write(dump, bf->name, len);
}

static const struct rb_builtin_function *
ibf_load_builtin(const struct ibf_load *load, ibf_offset_t *offset)
{
    int i = (int)ibf_load_small_value(load, offset);
    int len = (int)ibf_load_small_value(load, offset);
    const char *name = (char *)ibf_load_ptr(load, offset, len);

    if (0) {
        fprintf(stderr, "%.*s!!\n", len, name);
    }

    const struct rb_builtin_function *table = GET_VM()->builtin_function_table;
    if (table == NULL) rb_raise(rb_eArgError, "builtin function table is not provided");
    if (strncmp(table[i].name, name, len) != 0) {
        rb_raise(rb_eArgError, "builtin function index (%d) mismatch (expect %.*s but %s)",
                 i, len, name, table[i].name);
    }
    // fprintf(stderr, "load-builtin: name:%s(%d)\n", table[i].name, table[i].argc);

    return &table[i];
}

static ibf_offset_t
ibf_dump_code(struct ibf_dump *dump, const rb_iseq_t *iseq)
{
    const struct rb_iseq_constant_body *const body = ISEQ_BODY(iseq);
    const int iseq_size = body->iseq_size;
    int code_index;
    const VALUE *orig_code = rb_iseq_original_iseq(iseq);

    ibf_offset_t offset = ibf_dump_pos(dump);

    for (code_index=0; code_index<iseq_size;) {
        const VALUE insn = orig_code[code_index++];
        const char *types = insn_op_types(insn);
        int op_index;

        /* opcode */
        if (insn >= 0x100) { rb_raise(rb_eRuntimeError, "invalid instruction"); }
        ibf_dump_write_small_value(dump, insn);

        /* operands */
        for (op_index=0; types[op_index]; op_index++, code_index++) {
            VALUE op = orig_code[code_index];
            VALUE wv;

            switch (types[op_index]) {
              case TS_CDHASH:
              case TS_VALUE:
                wv = ibf_dump_object(dump, op);
                break;
              case TS_ISEQ:
                wv = (VALUE)ibf_dump_iseq(dump, (const rb_iseq_t *)op);
                break;
              case TS_IC:
                {
                    IC ic = (IC)op;
                    VALUE arr = idlist_to_array(ic->segments);
                    wv = ibf_dump_object(dump, arr);
                }
                break;
              case TS_ISE:
              case TS_IVC:
              case TS_ICVARC:
                {
                    union iseq_inline_storage_entry *is = (union iseq_inline_storage_entry *)op;
                    wv = is - ISEQ_IS_ENTRY_START(body, types[op_index]);
                }
                break;
              case TS_CALLDATA:
                {
                    goto skip_wv;
                }
              case TS_ID:
                wv = ibf_dump_id(dump, (ID)op);
                break;
              case TS_FUNCPTR:
                rb_raise(rb_eRuntimeError, "TS_FUNCPTR is not supported");
                goto skip_wv;
              case TS_BUILTIN:
                ibf_dump_builtin(dump, (const struct rb_builtin_function *)op);
                goto skip_wv;
              default:
                wv = op;
                break;
            }
            ibf_dump_write_small_value(dump, wv);
          skip_wv:;
        }
        RUBY_ASSERT(insn_len(insn) == op_index+1);
    }

    return offset;
}

static VALUE *
ibf_load_code(const struct ibf_load *load, rb_iseq_t *iseq, ibf_offset_t bytecode_offset, ibf_offset_t bytecode_size, unsigned int iseq_size)
{
    VALUE iseqv = (VALUE)iseq;
    unsigned int code_index;
    ibf_offset_t reading_pos = bytecode_offset;
    VALUE *code = ZALLOC_N(VALUE, iseq_size);

    struct rb_iseq_constant_body *load_body = ISEQ_BODY(iseq);
    struct rb_call_data *cd_entries = load_body->call_data;
    int ic_index = 0;

    load_body->iseq_encoded = code;
    load_body->iseq_size = iseq_size;

    iseq_bits_t * mark_offset_bits;
    if (ISEQ_MBITS_BUFLEN(iseq_size) == 1) {
        load_body->mark_bits.single = 0;
        mark_offset_bits = &load_body->mark_bits.single;
    }
    else {
        load_body->mark_bits.list = ZALLOC_N(iseq_bits_t, ISEQ_MBITS_BUFLEN(iseq_size));
        mark_offset_bits = load_body->mark_bits.list;
    }
    bool needs_bitmap = false;

    for (code_index=0; code_index<iseq_size;) {
        /* opcode */
        const VALUE insn = code[code_index] = ibf_load_small_value(load, &reading_pos);
        const char *types = insn_op_types(insn);
        int op_index;

        code_index++;

        /* operands */
        for (op_index=0; types[op_index]; op_index++, code_index++) {
            const char operand_type = types[op_index];
            switch (operand_type) {
              case TS_VALUE:
                {
                    VALUE op = ibf_load_small_value(load, &reading_pos);
                    VALUE v = ibf_load_object(load, op);
                    code[code_index] = v;
                    if (!SPECIAL_CONST_P(v)) {
                        RB_OBJ_WRITTEN(iseqv, Qundef, v);
                        ISEQ_MBITS_SET(mark_offset_bits, code_index);
                        needs_bitmap = true;
                    }
                    break;
                }
              case TS_CDHASH:
                {
                    VALUE op = ibf_load_small_value(load, &reading_pos);
                    VALUE v = ibf_load_object(load, op);

                    // Overwrite the existing hash in the object list.  This
                    // is to keep the object alive during load time.
                    // [Bug #17984] [ruby-core:104259]
                    pinned_list_store(load->current_buffer->obj_list, (long)op, v);

                    code[code_index] = v;
                    ISEQ_MBITS_SET(mark_offset_bits, code_index);
                    RB_OBJ_WRITTEN(iseqv, Qundef, v);
                    needs_bitmap = true;
                    break;
                }
              case TS_ISEQ:
                {
                    VALUE op = (VALUE)ibf_load_small_value(load, &reading_pos);
                    VALUE v = (VALUE)ibf_load_iseq(load, (const rb_iseq_t *)op);
                    code[code_index] = v;
                    if (!SPECIAL_CONST_P(v)) {
                        RB_OBJ_WRITTEN(iseqv, Qundef, v);
                        ISEQ_MBITS_SET(mark_offset_bits, code_index);
                        needs_bitmap = true;
                    }
                    break;
                }
              case TS_IC:
                {
                    VALUE op = ibf_load_small_value(load, &reading_pos);
                    VALUE arr = ibf_load_object(load, op);

                    IC ic = &ISEQ_IS_IC_ENTRY(load_body, ic_index++);
                    ic->segments = array_to_idlist(arr);

                    code[code_index] = (VALUE)ic;
                }
                break;
              case TS_ISE:
              case TS_ICVARC:
              case TS_IVC:
                {
                    unsigned int op = (unsigned int)ibf_load_small_value(load, &reading_pos);

                    ISE ic = ISEQ_IS_ENTRY_START(load_body, operand_type) + op;
                    code[code_index] = (VALUE)ic;

                    if (operand_type == TS_IVC) {
                        IVC cache = (IVC)ic;

                        if (insn == BIN(setinstancevariable)) {
                            ID iv_name = (ID)code[code_index - 1];
                            cache->iv_set_name = iv_name;
                            cache->value = IVAR_CACHE_INIT;
                        }
                        else {
                            cache->iv_set_name = 0;
                            cache->value = rb_getivar_cache_pack(ROOT_SHAPE_ID, ATTR_INDEX_NOT_SET);
                        }
                    }

                }
                break;
              case TS_CALLDATA:
                {
                    code[code_index] = (VALUE)cd_entries++;
                }
                break;
              case TS_ID:
                {
                    VALUE op = ibf_load_small_value(load, &reading_pos);
                    code[code_index] = ibf_load_id(load, (ID)(VALUE)op);
                }
                break;
              case TS_FUNCPTR:
                rb_raise(rb_eRuntimeError, "TS_FUNCPTR is not supported");
                break;
              case TS_BUILTIN:
                code[code_index] = (VALUE)ibf_load_builtin(load, &reading_pos);
                break;
              default:
                code[code_index] = ibf_load_small_value(load, &reading_pos);
                continue;
            }
        }
        if (insn_len(insn) != op_index+1) {
            rb_raise(rb_eRuntimeError, "operand size mismatch");
        }
    }

    if (!needs_bitmap) {
        SIZED_FREE_N(load_body->mark_bits.list, ISEQ_MBITS_BUFLEN(iseq_size));
        load_body->mark_bits.list = NULL;
    }

    RUBY_ASSERT(code_index == iseq_size);
    RUBY_ASSERT(reading_pos == bytecode_offset + bytecode_size);
    return code;
}

static ibf_offset_t
ibf_dump_param_opt_table(struct ibf_dump *dump, const rb_iseq_t *iseq)
{
    int opt_num = ISEQ_BODY(iseq)->param.opt_num;

    if (opt_num > 0) {
        IBF_W_ALIGN(VALUE);
        return ibf_dump_write(dump, ISEQ_BODY(iseq)->param.opt_table, sizeof(VALUE) * (opt_num + 1));
    }
    else {
        return ibf_dump_pos(dump);
    }
}

static VALUE *
ibf_load_param_opt_table(const struct ibf_load *load, ibf_offset_t opt_table_offset, int opt_num)
{
    if (opt_num > 0) {
        VALUE *table = ALLOC_N(VALUE, opt_num+1);
        MEMCPY(table, load->current_buffer->buff + opt_table_offset, VALUE, opt_num+1);
        return table;
    }
    else {
        return NULL;
    }
}

static ibf_offset_t
ibf_dump_param_keyword(struct ibf_dump *dump, const rb_iseq_t *iseq)
{
    const struct rb_iseq_param_keyword *kw = ISEQ_BODY(iseq)->param.keyword;

    if (kw) {
        struct rb_iseq_param_keyword dump_kw = *kw;
        int dv_num = kw->num - kw->required_num;
        ID *ids = kw->num > 0 ? ALLOCA_N(ID, kw->num) : NULL;
        VALUE *dvs = dv_num > 0 ? ALLOCA_N(VALUE, dv_num) : NULL;
        int i;

        for (i=0; i<kw->num; i++) ids[i] = (ID)ibf_dump_id(dump, kw->table[i]);
        for (i=0; i<dv_num; i++) dvs[i] = (VALUE)ibf_dump_object(dump, kw->default_values[i]);

        dump_kw.table = IBF_W(ids, ID, kw->num);
        dump_kw.default_values = IBF_W(dvs, VALUE, dv_num);
        IBF_W_ALIGN(struct rb_iseq_param_keyword);
        return ibf_dump_write(dump, &dump_kw, sizeof(struct rb_iseq_param_keyword) * 1);
    }
    else {
        return 0;
    }
}

static const struct rb_iseq_param_keyword *
ibf_load_param_keyword(const struct ibf_load *load, ibf_offset_t param_keyword_offset)
{
    if (param_keyword_offset) {
        struct rb_iseq_param_keyword *kw = IBF_R(param_keyword_offset, struct rb_iseq_param_keyword, 1);
        int dv_num = kw->num - kw->required_num;
        VALUE *dvs = dv_num ? IBF_R(kw->default_values, VALUE, dv_num) : NULL;

        int i;
        for (i=0; i<dv_num; i++) {
            dvs[i] = ibf_load_object(load, dvs[i]);
        }

        // Will be set once the local table is loaded.
        kw->table = NULL;

        kw->default_values = dvs;
        return kw;
    }
    else {
        return NULL;
    }
}

static ibf_offset_t
ibf_dump_insns_info_body(struct ibf_dump *dump, const rb_iseq_t *iseq)
{
    ibf_offset_t offset = ibf_dump_pos(dump);
    const struct iseq_insn_info_entry *entries = ISEQ_BODY(iseq)->insns_info.body;

    unsigned int i;
    for (i = 0; i < ISEQ_BODY(iseq)->insns_info.size; i++) {
        ibf_dump_write_small_value(dump, entries[i].line_no);
#ifdef USE_ISEQ_NODE_ID
        ibf_dump_write_small_value(dump, entries[i].node_id);
#endif
        ibf_dump_write_small_value(dump, entries[i].events);
    }

    return offset;
}

static struct iseq_insn_info_entry *
ibf_load_insns_info_body(const struct ibf_load *load, ibf_offset_t body_offset, unsigned int size)
{
    ibf_offset_t reading_pos = body_offset;
    struct iseq_insn_info_entry *entries = ALLOC_N(struct iseq_insn_info_entry, size);

    unsigned int i;
    for (i = 0; i < size; i++) {
        entries[i].line_no = (int)ibf_load_small_value(load, &reading_pos);
#ifdef USE_ISEQ_NODE_ID
        entries[i].node_id = (int)ibf_load_small_value(load, &reading_pos);
#endif
        entries[i].events = (rb_event_flag_t)ibf_load_small_value(load, &reading_pos);
    }

    return entries;
}

static ibf_offset_t
ibf_dump_insns_info_positions(struct ibf_dump *dump, const unsigned int *positions, unsigned int size)
{
    ibf_offset_t offset = ibf_dump_pos(dump);

    unsigned int last = 0;
    unsigned int i;
    for (i = 0; i < size; i++) {
        ibf_dump_write_small_value(dump, positions[i] - last);
        last = positions[i];
    }

    return offset;
}

static unsigned int *
ibf_load_insns_info_positions(const struct ibf_load *load, ibf_offset_t positions_offset, unsigned int size)
{
    ibf_offset_t reading_pos = positions_offset;
    unsigned int *positions = ALLOC_N(unsigned int, size);

    unsigned int last = 0;
    unsigned int i;
    for (i = 0; i < size; i++) {
        positions[i] = last + (unsigned int)ibf_load_small_value(load, &reading_pos);
        last = positions[i];
    }

    return positions;
}

static ibf_offset_t
ibf_dump_local_table(struct ibf_dump *dump, const rb_iseq_t *iseq)
{
    const struct rb_iseq_constant_body *const body = ISEQ_BODY(iseq);
    const int size = body->local_table_size;
    ID *table = ALLOCA_N(ID, size);
    int i;

    for (i=0; i<size; i++) {
        VALUE v = ibf_dump_id(dump, body->local_table[i]);
        if (v == 0) {
            /* Dump hidden local variables as indexes, so load_from_binary will work with them */
            v = ibf_dump_object(dump, ULONG2NUM(body->local_table[i]));
        }
        table[i] = v;
    }

    IBF_W_ALIGN(ID);
    return ibf_dump_write(dump, table, sizeof(ID) * size);
}

static const ID *
ibf_load_local_table(const struct ibf_load *load, ibf_offset_t local_table_offset, int size)
{
    if (size > 0) {
        ID *table = IBF_R(local_table_offset, ID, size);
        int i;

        for (i=0; i<size; i++) {
            table[i] = ibf_load_id(load, table[i]);
        }

        if (size == 1 && table[0] == idERROR_INFO) {
            ruby_xfree_sized(table, sizeof(ID) * size);
            return rb_iseq_shared_exc_local_tbl;
        }
        else {
            return table;
        }
    }
    else {
        return NULL;
    }
}

static ibf_offset_t
ibf_dump_lvar_states(struct ibf_dump *dump, const rb_iseq_t *iseq)
{
    const struct rb_iseq_constant_body *const body = ISEQ_BODY(iseq);
    const int size = ISEQ_LVAR_STATES_BUFLEN(body->local_table_size);
    IBF_W_ALIGN(uint8_t);
    if (iseq_has_lvar_states_p(body)) {
        return ibf_dump_write(dump, iseq_lvar_states(body), sizeof(uint8_t) * size);
    }
    else {
        return ibf_dump_write(dump, NULL, 0);
    }
}

static void
ibf_load_lvar_states(const struct ibf_load *load, struct rb_iseq_constant_body *load_body, ibf_offset_t lvar_states_offset, int size, const ID *local_table)
{
    if (local_table == rb_iseq_shared_exc_local_tbl ||
        size <= 0) {
        load_body->lvar_states.list = NULL;
    }
    else if (ISEQ_LVAR_STATES_EMBED_P((unsigned int)size)) {
        ibf_offset_t pos = lvar_states_offset;
        const int len = sizeof(uint8_t) * ISEQ_LVAR_STATES_BUFLEN(size);
        memcpy(load_body->lvar_states.single, ibf_load_ptr(load, &pos, len), len);
    }
    else {
        load_body->lvar_states.list = IBF_R(lvar_states_offset, uint8_t, ISEQ_LVAR_STATES_BUFLEN(size));
    }
}

static ibf_offset_t
ibf_dump_catch_table(struct ibf_dump *dump, const rb_iseq_t *iseq)
{
    const struct iseq_catch_table *table = ISEQ_BODY(iseq)->catch_table;

    if (table) {
        int *iseq_indices = ALLOCA_N(int, table->size);
        unsigned int i;

        for (i=0; i<table->size; i++) {
            iseq_indices[i] = ibf_dump_iseq(dump, table->entries[i].iseq);
        }

        const ibf_offset_t offset = ibf_dump_pos(dump);

        for (i=0; i<table->size; i++) {
            ibf_dump_write_small_value(dump, iseq_indices[i]);
            ibf_dump_write_small_value(dump, table->entries[i].type);
            ibf_dump_write_small_value(dump, table->entries[i].start);
            ibf_dump_write_small_value(dump, table->entries[i].end);
            ibf_dump_write_small_value(dump, table->entries[i].cont);
            ibf_dump_write_small_value(dump, table->entries[i].sp);
        }
        return offset;
    }
    else {
        return ibf_dump_pos(dump);
    }
}

static void
ibf_load_catch_table(const struct ibf_load *load, ibf_offset_t catch_table_offset, unsigned int size, const rb_iseq_t *parent_iseq)
{
    if (size) {
        struct iseq_catch_table *table = ruby_xcalloc(1, iseq_catch_table_bytes(size));
        table->size = size;
        ISEQ_BODY(parent_iseq)->catch_table = table;

        ibf_offset_t reading_pos = catch_table_offset;

        unsigned int i;
        for (i=0; i<table->size; i++) {
            int iseq_index = (int)ibf_load_small_value(load, &reading_pos);
            table->entries[i].type = (enum rb_catch_type)ibf_load_small_value(load, &reading_pos);
            table->entries[i].start = (unsigned int)ibf_load_small_value(load, &reading_pos);
            table->entries[i].end = (unsigned int)ibf_load_small_value(load, &reading_pos);
            table->entries[i].cont = (unsigned int)ibf_load_small_value(load, &reading_pos);
            table->entries[i].sp = (unsigned int)ibf_load_small_value(load, &reading_pos);

            rb_iseq_t *catch_iseq = (rb_iseq_t *)ibf_load_iseq(load, (const rb_iseq_t *)(VALUE)iseq_index);
            RB_OBJ_WRITE(parent_iseq, UNALIGNED_MEMBER_PTR(&table->entries[i], iseq), catch_iseq);
        }
    }
    else {
        ISEQ_BODY(parent_iseq)->catch_table = NULL;
    }
}

static ibf_offset_t
ibf_dump_ci_entries(struct ibf_dump *dump, const rb_iseq_t *iseq)
{
    const struct rb_iseq_constant_body *const body = ISEQ_BODY(iseq);
    const unsigned int ci_size = body->ci_size;
    const struct rb_call_data *cds = body->call_data;

    ibf_offset_t offset = ibf_dump_pos(dump);

    unsigned int i;

    for (i = 0; i < ci_size; i++) {
        const struct rb_callinfo *ci = cds[i].ci;
        if (ci != NULL) {
            ibf_dump_write_small_value(dump, ibf_dump_id(dump, vm_ci_mid(ci)));
            ibf_dump_write_small_value(dump, vm_ci_flag(ci));
            ibf_dump_write_small_value(dump, vm_ci_argc(ci));

            const struct rb_callinfo_kwarg *kwarg = vm_ci_kwarg(ci);
            if (kwarg) {
                int len = kwarg->keyword_len;
                ibf_dump_write_small_value(dump, len);
                for (int j=0; j<len; j++) {
                    VALUE keyword = ibf_dump_object(dump, kwarg->keywords[j]);
                    ibf_dump_write_small_value(dump, keyword);
                }
            }
            else {
                ibf_dump_write_small_value(dump, 0);
            }
        }
        else {
            // TODO: truncate NULL ci from call_data.
            ibf_dump_write_small_value(dump, (VALUE)-1);
        }
    }

    return offset;
}

struct outer_variable_pair {
    ID id;
    VALUE name;
    VALUE val;
};

struct outer_variable_list {
    size_t num;
    struct outer_variable_pair pairs[1];
};

static enum rb_id_table_iterator_result
store_outer_variable(ID id, VALUE val, void *dump)
{
    struct outer_variable_list *ovlist = dump;
    struct outer_variable_pair *pair = &ovlist->pairs[ovlist->num++];
    pair->id = id;
    pair->name = rb_id2str(id);
    pair->val = val;
    return ID_TABLE_CONTINUE;
}

static int
outer_variable_cmp(const void *a, const void *b, void *arg)
{
    const struct outer_variable_pair *ap = (const struct outer_variable_pair *)a;
    const struct outer_variable_pair *bp = (const struct outer_variable_pair *)b;

    if (!ap->name) {
        return -1;
    }
    else if (!bp->name) {
        return 1;
    }

    return rb_str_cmp(ap->name, bp->name);
}

static ibf_offset_t
ibf_dump_outer_variables(struct ibf_dump *dump, const rb_iseq_t *iseq)
{
    struct rb_id_table * ovs = ISEQ_BODY(iseq)->outer_variables;

    ibf_offset_t offset = ibf_dump_pos(dump);

    size_t size = ovs ? rb_id_table_size(ovs) : 0;
    ibf_dump_write_small_value(dump, (VALUE)size);
    if (size > 0) {
        VALUE buff;
        size_t buffsize =
            rb_size_mul_add_or_raise(sizeof(struct outer_variable_pair), size,
                                     offsetof(struct outer_variable_list, pairs),
                                     rb_eArgError);
        struct outer_variable_list *ovlist = RB_ALLOCV(buff, buffsize);
        ovlist->num = 0;
        rb_id_table_foreach(ovs, store_outer_variable, ovlist);
        ruby_qsort(ovlist->pairs, size, sizeof(struct outer_variable_pair), outer_variable_cmp, NULL);
        for (size_t i = 0; i < size; ++i) {
            ID id = ovlist->pairs[i].id;
            ID val = ovlist->pairs[i].val;
            ibf_dump_write_small_value(dump, ibf_dump_id(dump, id));
            ibf_dump_write_small_value(dump, val);
        }
    }

    return offset;
}

/* note that we dump out rb_call_info but load back rb_call_data */
static void
ibf_load_ci_entries(const struct ibf_load *load,
                    ibf_offset_t ci_entries_offset,
                    unsigned int ci_size,
                    struct rb_call_data **cd_ptr)
{
    if (!ci_size) {
        *cd_ptr = NULL;
        return;
    }

    ibf_offset_t reading_pos = ci_entries_offset;

    unsigned int i;

    struct rb_call_data *cds = ZALLOC_N(struct rb_call_data, ci_size);
    *cd_ptr = cds;

    for (i = 0; i < ci_size; i++) {
        VALUE mid_index = ibf_load_small_value(load, &reading_pos);
        if (mid_index != (VALUE)-1) {
            ID mid = ibf_load_id(load, mid_index);
            unsigned int flag = (unsigned int)ibf_load_small_value(load, &reading_pos);
            unsigned int argc = (unsigned int)ibf_load_small_value(load, &reading_pos);

            struct rb_callinfo_kwarg *kwarg = NULL;
            int kwlen = (int)ibf_load_small_value(load, &reading_pos);
            if (kwlen > 0) {
                kwarg = rb_xmalloc_mul_add(kwlen, sizeof(VALUE), sizeof(struct rb_callinfo_kwarg));
                kwarg->references = 0;
                kwarg->keyword_len = kwlen;
                for (int j=0; j<kwlen; j++) {
                    VALUE keyword = ibf_load_small_value(load, &reading_pos);
                    kwarg->keywords[j] = ibf_load_object(load, keyword);
                }
            }

            cds[i].ci = vm_ci_new(mid, flag, argc, kwarg);
            RB_OBJ_WRITTEN(load->iseq, Qundef, cds[i].ci);
            cds[i].cc = vm_cc_empty();
        }
        else {
            // NULL ci
            cds[i].ci = NULL;
            cds[i].cc = NULL;
        }
    }
}

static struct rb_id_table *
ibf_load_outer_variables(const struct ibf_load * load, ibf_offset_t outer_variables_offset)
{
    ibf_offset_t reading_pos = outer_variables_offset;

    struct rb_id_table *tbl = NULL;

    size_t table_size = (size_t)ibf_load_small_value(load, &reading_pos);

    if (table_size > 0) {
        tbl = rb_id_table_create(table_size);
    }

    for (size_t i = 0; i < table_size; i++) {
        ID key = ibf_load_id(load, (ID)ibf_load_small_value(load, &reading_pos));
        VALUE value = ibf_load_small_value(load, &reading_pos);
        if (!key) key = rb_make_temporary_id(i);
        rb_id_table_insert(tbl, key, value);
    }

    return tbl;
}

static ibf_offset_t
ibf_dump_iseq_each(struct ibf_dump *dump, const rb_iseq_t *iseq)
{
    RUBY_ASSERT(dump->current_buffer == &dump->global_buffer);

    unsigned int *positions;

    const struct rb_iseq_constant_body *body = ISEQ_BODY(iseq);

    const VALUE location_pathobj_index = ibf_dump_object(dump, body->location.pathobj); /* TODO: freeze */
    const VALUE location_label_index = ibf_dump_object(dump, body->location.label);

#if IBF_ISEQ_ENABLE_LOCAL_BUFFER
    ibf_offset_t iseq_start = ibf_dump_pos(dump);

    struct ibf_dump_buffer *saved_buffer = dump->current_buffer;
    struct ibf_dump_buffer buffer;
    buffer.str = rb_str_new(0, 0);
    buffer.obj_table = ibf_dump_object_table_new();
    dump->current_buffer = &buffer;
#endif

    const ibf_offset_t bytecode_offset =        ibf_dump_code(dump, iseq);
    const ibf_offset_t bytecode_size =          ibf_dump_pos(dump) - bytecode_offset;
    const ibf_offset_t param_opt_table_offset = ibf_dump_param_opt_table(dump, iseq);
    const ibf_offset_t param_keyword_offset =   ibf_dump_param_keyword(dump, iseq);
    const ibf_offset_t insns_info_body_offset = ibf_dump_insns_info_body(dump, iseq);

    positions = rb_iseq_insns_info_decode_positions(ISEQ_BODY(iseq));
    const ibf_offset_t insns_info_positions_offset = ibf_dump_insns_info_positions(dump, positions, body->insns_info.size);
    SIZED_FREE_N(positions, ISEQ_BODY(iseq)->insns_info.size);

    const ibf_offset_t local_table_offset = ibf_dump_local_table(dump, iseq);
    const ibf_offset_t lvar_states_offset = ibf_dump_lvar_states(dump, iseq);
    const unsigned int catch_table_size =   body->catch_table ? body->catch_table->size : 0;
    const ibf_offset_t catch_table_offset = ibf_dump_catch_table(dump, iseq);
    /* a NULL parent (persisted root) and references outside a Proc#refined subtree dump come out absent (-1) */
    const int parent_iseq_index =           ibf_table_lookup(dump->iseq_table, (st_data_t)ISEQ_BODY(iseq)->parent_iseq);
    const int local_iseq_index =            ibf_table_lookup(dump->iseq_table, (st_data_t)ISEQ_BODY(iseq)->local_iseq);
    const int mandatory_only_iseq_index =   ibf_dump_iseq(dump, ISEQ_BODY(iseq)->mandatory_only_iseq);
    const ibf_offset_t ci_entries_offset =  ibf_dump_ci_entries(dump, iseq);
    const ibf_offset_t outer_variables_offset = ibf_dump_outer_variables(dump, iseq);

#if IBF_ISEQ_ENABLE_LOCAL_BUFFER
    ibf_offset_t local_obj_list_offset;
    unsigned int local_obj_list_size;

    ibf_dump_object_list(dump, &local_obj_list_offset, &local_obj_list_size);
#endif

    ibf_offset_t body_offset = ibf_dump_pos(dump);

    /* dump the constant body */
    unsigned int param_flags =
        (body->param.flags.has_lead         << 0) |
        (body->param.flags.has_opt          << 1) |
        (body->param.flags.has_rest         << 2) |
        (body->param.flags.has_post         << 3) |
        (body->param.flags.has_kw           << 4) |
        (body->param.flags.has_kwrest       << 5) |
        (body->param.flags.has_block        << 6) |
        (body->param.flags.ambiguous_param0 << 7) |
        (body->param.flags.accepts_no_kwarg << 8) |
        (body->param.flags.ruby2_keywords   << 9) |
        (body->param.flags.anon_rest        << 10) |
        (body->param.flags.anon_kwrest      << 11) |
        (body->param.flags.use_block        << 12) |
        (body->param.flags.forwardable      << 13) |
        (body->param.flags.accepts_no_block << 14);

#if IBF_ISEQ_ENABLE_LOCAL_BUFFER
#  define IBF_BODY_OFFSET(x) (x)
#else
#  define IBF_BODY_OFFSET(x) (body_offset - (x))
#endif

    ibf_dump_write_small_value(dump, body->type);
    ibf_dump_write_small_value(dump, body->iseq_size);
    ibf_dump_write_small_value(dump, IBF_BODY_OFFSET(bytecode_offset));
    ibf_dump_write_small_value(dump, bytecode_size);
    ibf_dump_write_small_value(dump, param_flags);
    ibf_dump_write_small_value(dump, body->param.size);
    ibf_dump_write_small_value(dump, body->param.lead_num);
    ibf_dump_write_small_value(dump, body->param.opt_num);
    ibf_dump_write_small_value(dump, body->param.rest_start);
    ibf_dump_write_small_value(dump, body->param.post_start);
    ibf_dump_write_small_value(dump, body->param.post_num);
    ibf_dump_write_small_value(dump, body->param.block_start);
    ibf_dump_write_small_value(dump, IBF_BODY_OFFSET(param_opt_table_offset));
    ibf_dump_write_small_value(dump, param_keyword_offset);
    ibf_dump_write_small_value(dump, location_pathobj_index);
    ibf_dump_write_small_value(dump, location_label_index);
    ibf_dump_write_small_value(dump, body->location.first_lineno);
    ibf_dump_write_small_value(dump, body->location.node_id);
    /* Dump the source hash (0 if unavailable) in two 32-bit halves, because
     * VALUE may be 32 bits wide. */
    ibf_dump_write_small_value(dump, (VALUE)(uint32_t)(body->source_hash >> 32));
    ibf_dump_write_small_value(dump, (VALUE)(uint32_t)body->source_hash);
    ibf_dump_write_small_value(dump, body->location.code_location.beg_pos.lineno);
    ibf_dump_write_small_value(dump, body->location.code_location.beg_pos.column);
    ibf_dump_write_small_value(dump, body->location.code_location.end_pos.lineno);
    ibf_dump_write_small_value(dump, body->location.code_location.end_pos.column);
    ibf_dump_write_small_value(dump, IBF_BODY_OFFSET(insns_info_body_offset));
    ibf_dump_write_small_value(dump, IBF_BODY_OFFSET(insns_info_positions_offset));
    ibf_dump_write_small_value(dump, body->insns_info.size);
    ibf_dump_write_small_value(dump, IBF_BODY_OFFSET(local_table_offset));
    ibf_dump_write_small_value(dump, IBF_BODY_OFFSET(lvar_states_offset));
    ibf_dump_write_small_value(dump, catch_table_size);
    ibf_dump_write_small_value(dump, IBF_BODY_OFFSET(catch_table_offset));
    ibf_dump_write_small_value(dump, parent_iseq_index);
    ibf_dump_write_small_value(dump, local_iseq_index);
    ibf_dump_write_small_value(dump, mandatory_only_iseq_index);
    ibf_dump_write_small_value(dump, IBF_BODY_OFFSET(ci_entries_offset));
    ibf_dump_write_small_value(dump, IBF_BODY_OFFSET(outer_variables_offset));
    ibf_dump_write_small_value(dump, ISEQ_FLIP_CNT(iseq));
    ibf_dump_write_small_value(dump, body->local_table_size);
    ibf_dump_write_small_value(dump, body->ivc_size);
    ibf_dump_write_small_value(dump, body->icvarc_size);
    ibf_dump_write_small_value(dump, body->ise_size);
    ibf_dump_write_small_value(dump, body->ic_size);
    ibf_dump_write_small_value(dump, body->ci_size);
    ibf_dump_write_small_value(dump, body->stack_max);
    ibf_dump_write_small_value(dump, body->builtin_attrs);
    ibf_dump_write_small_value(dump, body->prism ? 1 : 0);

#undef IBF_BODY_OFFSET

#if IBF_ISEQ_ENABLE_LOCAL_BUFFER
    ibf_offset_t iseq_length_bytes = ibf_dump_pos(dump);

    dump->current_buffer = saved_buffer;
    ibf_dump_write(dump, RSTRING_PTR(buffer.str), iseq_length_bytes);

    ibf_offset_t offset = ibf_dump_pos(dump);
    ibf_dump_write_small_value(dump, iseq_start);
    ibf_dump_write_small_value(dump, iseq_length_bytes);
    ibf_dump_write_small_value(dump, body_offset);

    ibf_dump_write_small_value(dump, local_obj_list_offset);
    ibf_dump_write_small_value(dump, local_obj_list_size);

    st_free_table(buffer.obj_table); // TODO: this leaks in case of exception

    return offset;
#else
    return body_offset;
#endif
}

static VALUE
ibf_load_location_str(const struct ibf_load *load, VALUE str_index)
{
    VALUE str = ibf_load_object(load, str_index);
    if (str != Qnil) {
        str = rb_fstring(str);
    }
    return str;
}

static void
ibf_load_iseq_each(struct ibf_load *load, rb_iseq_t *iseq, ibf_offset_t offset)
{
    struct rb_iseq_constant_body *load_body = ISEQ_BODY(iseq) = rb_iseq_constant_body_alloc();

    ibf_offset_t reading_pos = offset;

#if IBF_ISEQ_ENABLE_LOCAL_BUFFER
    struct ibf_load_buffer *saved_buffer = load->current_buffer;
    load->current_buffer = &load->global_buffer;

    const ibf_offset_t iseq_start = (ibf_offset_t)ibf_load_small_value(load, &reading_pos);
    const ibf_offset_t iseq_length_bytes = (ibf_offset_t)ibf_load_small_value(load, &reading_pos);
    const ibf_offset_t body_offset = (ibf_offset_t)ibf_load_small_value(load, &reading_pos);

    struct ibf_load_buffer buffer;
    buffer.buff = load->global_buffer.buff + iseq_start;
    buffer.size = iseq_length_bytes;
    buffer.obj_list_offset = (ibf_offset_t)ibf_load_small_value(load, &reading_pos);
    buffer.obj_list_size = (ibf_offset_t)ibf_load_small_value(load, &reading_pos);
    buffer.obj_list = pinned_list_new(buffer.obj_list_size);

    load->current_buffer = &buffer;
    reading_pos = body_offset;
#endif

#if IBF_ISEQ_ENABLE_LOCAL_BUFFER
#  define IBF_BODY_OFFSET(x) (x)
#else
#  define IBF_BODY_OFFSET(x) (offset - (x))
#endif

    const unsigned int type = (unsigned int)ibf_load_small_value(load, &reading_pos);
    const unsigned int iseq_size = (unsigned int)ibf_load_small_value(load, &reading_pos);
    const ibf_offset_t bytecode_offset = (ibf_offset_t)IBF_BODY_OFFSET(ibf_load_small_value(load, &reading_pos));
    const ibf_offset_t bytecode_size = (ibf_offset_t)ibf_load_small_value(load, &reading_pos);
    const unsigned int param_flags = (unsigned int)ibf_load_small_value(load, &reading_pos);
    const unsigned int param_size = (unsigned int)ibf_load_small_value(load, &reading_pos);
    const int param_lead_num = (int)ibf_load_small_value(load, &reading_pos);
    const int param_opt_num = (int)ibf_load_small_value(load, &reading_pos);
    const int param_rest_start = (int)ibf_load_small_value(load, &reading_pos);
    const int param_post_start = (int)ibf_load_small_value(load, &reading_pos);
    const int param_post_num = (int)ibf_load_small_value(load, &reading_pos);
    const int param_block_start = (int)ibf_load_small_value(load, &reading_pos);
    const ibf_offset_t param_opt_table_offset = (ibf_offset_t)IBF_BODY_OFFSET(ibf_load_small_value(load, &reading_pos));
    const ibf_offset_t param_keyword_offset = (ibf_offset_t)ibf_load_small_value(load, &reading_pos);
    const VALUE location_pathobj_index = ibf_load_small_value(load, &reading_pos);
    const VALUE location_label_index = ibf_load_small_value(load, &reading_pos);
    const int location_first_lineno = (int)ibf_load_small_value(load, &reading_pos);
    const int location_node_id = (int)ibf_load_small_value(load, &reading_pos);
    const uint64_t source_hash_hi = (uint64_t)ibf_load_small_value(load, &reading_pos);
    const uint64_t source_hash_lo = (uint64_t)ibf_load_small_value(load, &reading_pos);
    const uint64_t source_hash = (source_hash_hi << 32) | (uint32_t)source_hash_lo;
    const int location_code_location_beg_pos_lineno = (int)ibf_load_small_value(load, &reading_pos);
    const int location_code_location_beg_pos_column = (int)ibf_load_small_value(load, &reading_pos);
    const int location_code_location_end_pos_lineno = (int)ibf_load_small_value(load, &reading_pos);
    const int location_code_location_end_pos_column = (int)ibf_load_small_value(load, &reading_pos);
    const ibf_offset_t insns_info_body_offset = (ibf_offset_t)IBF_BODY_OFFSET(ibf_load_small_value(load, &reading_pos));
    const ibf_offset_t insns_info_positions_offset = (ibf_offset_t)IBF_BODY_OFFSET(ibf_load_small_value(load, &reading_pos));
    const unsigned int insns_info_size = (unsigned int)ibf_load_small_value(load, &reading_pos);
    const ibf_offset_t local_table_offset = (ibf_offset_t)IBF_BODY_OFFSET(ibf_load_small_value(load, &reading_pos));
    const ibf_offset_t lvar_states_offset = (ibf_offset_t)IBF_BODY_OFFSET(ibf_load_small_value(load, &reading_pos));
    const unsigned int catch_table_size = (unsigned int)ibf_load_small_value(load, &reading_pos);
    const ibf_offset_t catch_table_offset = (ibf_offset_t)IBF_BODY_OFFSET(ibf_load_small_value(load, &reading_pos));
    const int parent_iseq_index = (int)ibf_load_small_value(load, &reading_pos);
    const int local_iseq_index = (int)ibf_load_small_value(load, &reading_pos);
    const int mandatory_only_iseq_index = (int)ibf_load_small_value(load, &reading_pos);
    const ibf_offset_t ci_entries_offset = (ibf_offset_t)IBF_BODY_OFFSET(ibf_load_small_value(load, &reading_pos));
    const ibf_offset_t outer_variables_offset = (ibf_offset_t)IBF_BODY_OFFSET(ibf_load_small_value(load, &reading_pos));
    const rb_snum_t variable_flip_count = (rb_snum_t)ibf_load_small_value(load, &reading_pos);
    const unsigned int local_table_size = (unsigned int)ibf_load_small_value(load, &reading_pos);

    const unsigned int ivc_size = (unsigned int)ibf_load_small_value(load, &reading_pos);
    const unsigned int icvarc_size = (unsigned int)ibf_load_small_value(load, &reading_pos);
    const unsigned int ise_size = (unsigned int)ibf_load_small_value(load, &reading_pos);
    const unsigned int ic_size = (unsigned int)ibf_load_small_value(load, &reading_pos);

    const unsigned int ci_size = (unsigned int)ibf_load_small_value(load, &reading_pos);
    const unsigned int stack_max = (unsigned int)ibf_load_small_value(load, &reading_pos);
    const unsigned int builtin_attrs = (unsigned int)ibf_load_small_value(load, &reading_pos);
    const bool prism = (bool)ibf_load_small_value(load, &reading_pos);

    // setup fname and dummy frame
    VALUE path = ibf_load_object(load, location_pathobj_index);
    {
        VALUE realpath = Qnil;

        if (RB_TYPE_P(path, T_STRING)) {
            realpath = path = rb_fstring(path);
        }
        else if (RB_TYPE_P(path, T_ARRAY)) {
            VALUE pathobj = path;
            if (RARRAY_LEN(pathobj) != 2) {
                rb_raise(rb_eRuntimeError, "path object size mismatch");
            }
            path = rb_fstring(RARRAY_AREF(pathobj, 0));
            realpath = RARRAY_AREF(pathobj, 1);
            if (!NIL_P(realpath)) {
                if (!RB_TYPE_P(realpath, T_STRING)) {
                    rb_raise(rb_eArgError, "unexpected realpath %"PRIxVALUE
                             "(%x), path=%+"PRIsVALUE,
                             realpath, TYPE(realpath), path);
                }
                realpath = rb_fstring(realpath);
            }
        }
        else {
            rb_raise(rb_eRuntimeError, "unexpected path object");
        }
        rb_iseq_pathobj_set(iseq, path, realpath);
    }

    // push dummy frame
    rb_execution_context_t *ec = GET_EC();
    VALUE dummy_frame = rb_vm_push_frame_fname(ec, path);

#undef IBF_BODY_OFFSET

    load_body->type = type;
    load_body->stack_max = stack_max;
    load_body->param.flags.has_lead = (param_flags >> 0) & 1;
    load_body->param.flags.has_opt = (param_flags >> 1) & 1;
    load_body->param.flags.has_rest = (param_flags >> 2) & 1;
    load_body->param.flags.has_post = (param_flags >> 3) & 1;
    load_body->param.flags.has_kw = FALSE;
    load_body->param.flags.has_kwrest = (param_flags >> 5) & 1;
    load_body->param.flags.has_block = (param_flags >> 6) & 1;
    load_body->param.flags.ambiguous_param0 = (param_flags >> 7) & 1;
    load_body->param.flags.accepts_no_kwarg = (param_flags >> 8) & 1;
    load_body->param.flags.ruby2_keywords = (param_flags >> 9) & 1;
    load_body->param.flags.anon_rest = (param_flags >> 10) & 1;
    load_body->param.flags.anon_kwrest = (param_flags >> 11) & 1;
    load_body->param.flags.use_block = (param_flags >> 12) & 1;
    load_body->param.flags.forwardable = (param_flags >> 13) & 1;
    load_body->param.flags.accepts_no_block = (param_flags >> 14) & 1;
    load_body->param.size = param_size;
    load_body->param.lead_num = param_lead_num;
    load_body->param.opt_num = param_opt_num;
    load_body->param.rest_start = param_rest_start;
    load_body->param.post_start = param_post_start;
    load_body->param.post_num = param_post_num;
    load_body->param.block_start = param_block_start;
    load_body->local_table_size = local_table_size;
    load_body->ci_size = ci_size;
    load_body->insns_info.size = insns_info_size;

    // variable is NULL from ZALLOC; only allocate if flip_count is non-zero.
    ISEQ_ORIGINAL_ISEQ_CLEAR(iseq);
    if (variable_flip_count) {
        struct rb_iseq_variable *v = rb_iseq_variable_ensure(iseq);
        v->flip_count = variable_flip_count;
    }

    load_body->location.first_lineno = location_first_lineno;
    load_body->location.node_id = location_node_id;
    load_body->source_hash = source_hash;
    load_body->location.code_location.beg_pos.lineno = location_code_location_beg_pos_lineno;
    load_body->location.code_location.beg_pos.column = location_code_location_beg_pos_column;
    load_body->location.code_location.end_pos.lineno = location_code_location_end_pos_lineno;
    load_body->location.code_location.end_pos.column = location_code_location_end_pos_column;
    load_body->builtin_attrs = builtin_attrs;
    load_body->prism = prism;

    load_body->ivc_size             = ivc_size;
    load_body->icvarc_size          = icvarc_size;
    load_body->ise_size             = ise_size;
    load_body->ic_size              = ic_size;

    if (ISEQ_IS_SIZE(load_body)) {
        load_body->is_entries       = ZALLOC_N(union iseq_inline_storage_entry, ISEQ_IS_SIZE(load_body));
    }
    else {
        load_body->is_entries       = NULL;
    }
                                      ibf_load_ci_entries(load, ci_entries_offset, ci_size, &load_body->call_data);
    load_body->outer_variables      = ibf_load_outer_variables(load, outer_variables_offset);
    load_body->param.opt_table      = ibf_load_param_opt_table(load, param_opt_table_offset, param_opt_num);
    load_body->param.keyword        = ibf_load_param_keyword(load, param_keyword_offset);
    load_body->param.flags.has_kw   = (param_flags >> 4) & 1;
    load_body->insns_info.body      = ibf_load_insns_info_body(load, insns_info_body_offset, insns_info_size);
    load_body->insns_info.positions_or_succ_index_table.positions = ibf_load_insns_info_positions(load, insns_info_positions_offset, insns_info_size);
    load_body->local_table          = ibf_load_local_table(load, local_table_offset, local_table_size);
    ibf_load_lvar_states(load, load_body, lvar_states_offset, local_table_size, load_body->local_table);
    ibf_load_catch_table(load, catch_table_offset, catch_table_size, iseq);

    const rb_iseq_t *parent_iseq = ibf_load_iseq(load, (const rb_iseq_t *)(VALUE)parent_iseq_index);
    const rb_iseq_t *local_iseq = ibf_load_iseq(load, (const rb_iseq_t *)(VALUE)local_iseq_index);
    const rb_iseq_t *mandatory_only_iseq = ibf_load_iseq(load, (const rb_iseq_t *)(VALUE)mandatory_only_iseq_index);

    RB_OBJ_WRITE(iseq, &load_body->parent_iseq, parent_iseq);
    RB_OBJ_WRITE(iseq, &load_body->local_iseq, local_iseq);
    RB_OBJ_WRITE(iseq, &load_body->mandatory_only_iseq, mandatory_only_iseq);

    // This must be done after the local table is loaded.
    if (load_body->param.keyword != NULL) {
        RUBY_ASSERT(load_body->local_table);
        struct rb_iseq_param_keyword *keyword = (struct rb_iseq_param_keyword *) load_body->param.keyword;
        keyword->table = &load_body->local_table[keyword->bits_start - keyword->num];
    }

    ibf_load_code(load, iseq, bytecode_offset, bytecode_size, iseq_size);
#if VM_INSN_INFO_TABLE_IMPL == 2
    rb_iseq_insns_info_encode_positions(iseq);
#endif

    rb_iseq_translate_threaded_code(iseq);

#if IBF_ISEQ_ENABLE_LOCAL_BUFFER
    load->current_buffer = &load->global_buffer;
#endif

    RB_OBJ_WRITE(iseq, &load_body->location.label,         ibf_load_location_str(load, location_label_index));

#if IBF_ISEQ_ENABLE_LOCAL_BUFFER
    load->current_buffer = saved_buffer;
#endif
    verify_call_cache(iseq);

    RB_GC_GUARD(dummy_frame);
    rb_vm_pop_frame_no_int(ec);
}

struct ibf_dump_iseq_list_arg
{
    struct ibf_dump *dump;
    VALUE offset_list;
};

static int
ibf_dump_iseq_list_i(st_data_t key, st_data_t val, st_data_t ptr)
{
    const rb_iseq_t *iseq = (const rb_iseq_t *)key;
    struct ibf_dump_iseq_list_arg *args = (struct ibf_dump_iseq_list_arg *)ptr;

    ibf_offset_t offset = ibf_dump_iseq_each(args->dump, iseq);
    rb_ary_push(args->offset_list, UINT2NUM(offset));

    return ST_CONTINUE;
}

static void
ibf_dump_iseq_list(struct ibf_dump *dump, struct ibf_header *header)
{
    VALUE offset_list = rb_ary_hidden_new(dump->iseq_table->num_entries);

    struct ibf_dump_iseq_list_arg args;
    args.dump = dump;
    args.offset_list = offset_list;

    st_foreach(dump->iseq_table, ibf_dump_iseq_list_i, (st_data_t)&args);

    st_index_t i;
    st_index_t size = dump->iseq_table->num_entries;
    ibf_offset_t *offsets = ALLOCA_N(ibf_offset_t, size);

    for (i = 0; i < size; i++) {
        offsets[i] = NUM2UINT(RARRAY_AREF(offset_list, i));
    }

    ibf_dump_align(dump, sizeof(ibf_offset_t));
    header->iseq_list_offset = ibf_dump_write(dump, offsets, sizeof(ibf_offset_t) * size);
    header->iseq_list_size = (unsigned int)size;
}

/*
 * Binary format
 * - ibf_object_header
 * - ibf_object_xxx (xxx is type)
 */

struct ibf_object_header {
    unsigned int type: 5;
    unsigned int special_const: 1;
    unsigned int frozen: 1;
    unsigned int internal: 1;
};

enum ibf_object_class_index {
    IBF_OBJECT_CLASS_OBJECT,
    IBF_OBJECT_CLASS_ARRAY,
    IBF_OBJECT_CLASS_STANDARD_ERROR,
    IBF_OBJECT_CLASS_NO_MATCHING_PATTERN_ERROR,
    IBF_OBJECT_CLASS_TYPE_ERROR,
    IBF_OBJECT_CLASS_NO_MATCHING_PATTERN_KEY_ERROR,
};

struct ibf_object_regexp {
    long srcstr;
    char option;
};

struct ibf_object_hash {
    long len;
    long keyval[FLEX_ARY_LEN];
};

struct ibf_object_struct_range {
    long class_index;
    long len;
    long beg;
    long end;
    int excl;
};

struct ibf_object_bignum {
    ssize_t slen;
    BDIGIT digits[FLEX_ARY_LEN];
};

enum ibf_object_data_type {
    IBF_OBJECT_DATA_ENCODING,
};

struct ibf_object_complex_rational {
    long a, b;
};

struct ibf_object_symbol {
    long str;
};

#define IBF_ALIGNED_OFFSET(align, offset) /* offset > 0 */ \
    ((((offset) - 1) / (align) + 1) * (align))
/* No cast, since it's UB to create an unaligned pointer.
 * Leave as void* for use with memcpy in those cases.
 * We align the offset, but the buffer pointer is only VALUE aligned,
 * so the returned pointer may be unaligned for `type` .*/
#define IBF_OBJBODY(type, offset) \
    ibf_load_check_offset(load, IBF_ALIGNED_OFFSET(RUBY_ALIGNOF(type), offset))

static const void *
ibf_load_check_offset(const struct ibf_load *load, size_t offset)
{
    if (offset >= load->current_buffer->size) {
        rb_raise(rb_eIndexError, "object offset out of range: %"PRIdSIZE, offset);
    }
    return load->current_buffer->buff + offset;
}

NORETURN(static void ibf_dump_object_unsupported(struct ibf_dump *dump, VALUE obj));

static void
ibf_dump_object_unsupported(struct ibf_dump *dump, VALUE obj)
{
    char buff[0x100];
    rb_raw_obj_info(buff, sizeof(buff), obj);
    rb_raise(rb_eNotImpError, "ibf_dump_object_unsupported: %s", buff);
}

NORETURN(static VALUE ibf_load_object_unsupported(const struct ibf_load *load, const struct ibf_object_header *header, ibf_offset_t offset));

static VALUE
ibf_load_object_unsupported(const struct ibf_load *load, const struct ibf_object_header *header, ibf_offset_t offset)
{
    rb_raise(rb_eArgError, "unsupported");
    UNREACHABLE_RETURN(Qnil);
}

static void
ibf_dump_object_class(struct ibf_dump *dump, VALUE obj)
{
    enum ibf_object_class_index cindex;
    if (obj == rb_cObject) {
        cindex = IBF_OBJECT_CLASS_OBJECT;
    }
    else if (obj == rb_cArray) {
        cindex = IBF_OBJECT_CLASS_ARRAY;
    }
    else if (obj == rb_eStandardError) {
        cindex = IBF_OBJECT_CLASS_STANDARD_ERROR;
    }
    else if (obj == rb_eNoMatchingPatternError) {
        cindex = IBF_OBJECT_CLASS_NO_MATCHING_PATTERN_ERROR;
    }
    else if (obj == rb_eTypeError) {
        cindex = IBF_OBJECT_CLASS_TYPE_ERROR;
    }
    else if (obj == rb_eNoMatchingPatternKeyError) {
        cindex = IBF_OBJECT_CLASS_NO_MATCHING_PATTERN_KEY_ERROR;
    }
    else {
        rb_obj_info_dump(obj);
        rb_p(obj);
        rb_bug("unsupported class");
    }
    ibf_dump_write_small_value(dump, (VALUE)cindex);
}

static VALUE
ibf_load_object_class(const struct ibf_load *load, const struct ibf_object_header *header, ibf_offset_t offset)
{
    enum ibf_object_class_index cindex = (enum ibf_object_class_index)ibf_load_small_value(load, &offset);

    switch (cindex) {
      case IBF_OBJECT_CLASS_OBJECT:
        return rb_cObject;
      case IBF_OBJECT_CLASS_ARRAY:
        return rb_cArray;
      case IBF_OBJECT_CLASS_STANDARD_ERROR:
        return rb_eStandardError;
      case IBF_OBJECT_CLASS_NO_MATCHING_PATTERN_ERROR:
        return rb_eNoMatchingPatternError;
      case IBF_OBJECT_CLASS_TYPE_ERROR:
        return rb_eTypeError;
      case IBF_OBJECT_CLASS_NO_MATCHING_PATTERN_KEY_ERROR:
        return rb_eNoMatchingPatternKeyError;
    }

    rb_raise(rb_eArgError, "ibf_load_object_class: unknown class (%d)", (int)cindex);
}


static void
ibf_dump_object_float(struct ibf_dump *dump, VALUE obj)
{
    double dbl = RFLOAT_VALUE(obj);
    (void)IBF_W(&dbl, double, 1);
}

static VALUE
ibf_load_object_float(const struct ibf_load *load, const struct ibf_object_header *header, ibf_offset_t offset)
{
    double d;
    /* Avoid unaligned VFP load on ARMv7; IBF payload may be unaligned (C99 6.3.2.3 p7). */
    memcpy(&d, IBF_OBJBODY(double, offset), sizeof(d));
    VALUE f = DBL2NUM(d);
    if (!FLONUM_P(f)) RB_OBJ_SET_SHAREABLE(f);
    return f;
}

static void
ibf_dump_object_string(struct ibf_dump *dump, VALUE obj)
{
    long encindex = (long)rb_enc_get_index(obj);
    long len = RSTRING_LEN(obj);
    const char *ptr = RSTRING_PTR(obj);

    if (encindex > RUBY_ENCINDEX_BUILTIN_MAX) {
        rb_encoding *enc = rb_enc_from_index((int)encindex);
        const char *enc_name = rb_enc_name(enc);
        encindex = RUBY_ENCINDEX_BUILTIN_MAX + ibf_dump_object(dump, rb_str_new2(enc_name));
    }

    ibf_dump_write_small_value(dump, encindex);
    ibf_dump_write_small_value(dump, len);
    IBF_WP(ptr, char, len);
}

static VALUE
ibf_load_object_string(const struct ibf_load *load, const struct ibf_object_header *header, ibf_offset_t offset)
{
    ibf_offset_t reading_pos = offset;

    int encindex = (int)ibf_load_small_value(load, &reading_pos);
    const long len = (long)ibf_load_small_value(load, &reading_pos);
    const char *ptr = load->current_buffer->buff + reading_pos;

    if (encindex > RUBY_ENCINDEX_BUILTIN_MAX) {
        VALUE enc_name_str = ibf_load_object(load, encindex - RUBY_ENCINDEX_BUILTIN_MAX);
        encindex = rb_enc_find_index(RSTRING_PTR(enc_name_str));
    }

    VALUE str;
    if (header->frozen && !header->internal) {
        str = rb_enc_literal_str(ptr, len, rb_enc_from_index(encindex));
    }
    else {
        str = rb_enc_str_new(ptr, len, rb_enc_from_index(encindex));

        if (header->internal) rb_obj_hide(str);
        if (header->frozen)   str = rb_fstring(str);
    }
    return str;
}

static void
ibf_dump_object_regexp(struct ibf_dump *dump, VALUE obj)
{
    VALUE srcstr = RREGEXP_SRC(obj);
    struct ibf_object_regexp regexp;
    regexp.option = (char)rb_reg_options(obj);
    regexp.srcstr = (long)ibf_dump_object(dump, srcstr);

    ibf_dump_write_byte(dump, (unsigned char)regexp.option);
    ibf_dump_write_small_value(dump, regexp.srcstr);
}

static VALUE
ibf_load_object_regexp(const struct ibf_load *load, const struct ibf_object_header *header, ibf_offset_t offset)
{
    struct ibf_object_regexp regexp;
    regexp.option = ibf_load_byte(load, &offset);
    regexp.srcstr = ibf_load_small_value(load, &offset);

    VALUE srcstr = ibf_load_object(load, regexp.srcstr);
    VALUE reg = rb_reg_compile(srcstr, (int)regexp.option, NULL, 0);

    if (header->internal) rb_obj_hide(reg);
    if (header->frozen) RB_OBJ_SET_SHAREABLE(rb_obj_freeze(reg));

    return reg;
}

static void
ibf_dump_object_array(struct ibf_dump *dump, VALUE obj)
{
    long i, len = RARRAY_LEN(obj);
    ibf_dump_write_small_value(dump, len);
    for (i=0; i<len; i++) {
        long index = (long)ibf_dump_object(dump, RARRAY_AREF(obj, i));
        ibf_dump_write_small_value(dump, index);
    }
}

static VALUE
ibf_load_object_array(const struct ibf_load *load, const struct ibf_object_header *header, ibf_offset_t offset)
{
    ibf_offset_t reading_pos = offset;

    const long len = (long)ibf_load_small_value(load, &reading_pos);

    VALUE ary = header->internal ? rb_ary_hidden_new(len) : rb_ary_new_capa(len);
    int i;

    for (i=0; i<len; i++) {
        const VALUE index = ibf_load_small_value(load, &reading_pos);
        rb_ary_push(ary, ibf_load_object(load, index));
    }

    if (header->frozen) {
        rb_ary_freeze(ary);
        rb_ractor_make_shareable(ary); // TODO: check elements
    }

    return ary;
}

static int
ibf_dump_object_hash_i(st_data_t key, st_data_t val, st_data_t ptr)
{
    struct ibf_dump *dump = (struct ibf_dump *)ptr;

    VALUE key_index = ibf_dump_object(dump, (VALUE)key);
    VALUE val_index = ibf_dump_object(dump, (VALUE)val);

    ibf_dump_write_small_value(dump, key_index);
    ibf_dump_write_small_value(dump, val_index);
    return ST_CONTINUE;
}

static void
ibf_dump_object_hash(struct ibf_dump *dump, VALUE obj)
{
    long len = RHASH_SIZE(obj);
    ibf_dump_write_small_value(dump, (VALUE)len);

    if (len > 0) rb_hash_foreach(obj, ibf_dump_object_hash_i, (VALUE)dump);
}

static VALUE
ibf_load_object_hash(const struct ibf_load *load, const struct ibf_object_header *header, ibf_offset_t offset)
{
    long len = (long)ibf_load_small_value(load, &offset);
    VALUE obj = header->frozen ? rb_hash_alloc_fixed_size(rb_cHash, len) : rb_hash_new_capa(len);
    int i;

    for (i = 0; i < len; i++) {
        VALUE key_index = ibf_load_small_value(load, &offset);
        VALUE val_index = ibf_load_small_value(load, &offset);

        VALUE key = ibf_load_object(load, key_index);
        VALUE val = ibf_load_object(load, val_index);
        rb_hash_aset(obj, key, val);
    }

    if (header->internal) rb_obj_hide(obj);
    if (header->frozen) {
        RB_OBJ_SET_FROZEN_SHAREABLE(obj);
    }

    return obj;
}

static int
ibf_dump_cdhash_i(st_data_t key, st_data_t val, st_data_t ptr)
{
    struct ibf_dump *dump = (struct ibf_dump *)ptr;

    VALUE key_index = ibf_dump_object(dump, (VALUE)key);

    ibf_dump_write_small_value(dump, key_index);
    ibf_dump_write_small_value(dump, val);
    return ST_CONTINUE;
}

static void
ibf_dump_object_imemo(struct ibf_dump *dump, VALUE obj)
{
    switch (imemo_type(obj)) {
      case imemo_cdhash: {
        st_table *tbl = rb_imemo_cdhash_tbl(obj);

        long len = tbl->num_entries;
        ibf_dump_write_small_value(dump, (VALUE)len);
        if (len > 0) st_foreach(tbl, ibf_dump_cdhash_i, (VALUE)dump);
        break;
      }
      default:
        ibf_dump_object_unsupported(dump, obj);
        break;
    }
}

static VALUE
ibf_load_object_imemo(const struct ibf_load *load, const struct ibf_object_header *header, ibf_offset_t offset)
{
    long len = (long)ibf_load_small_value(load, &offset);
    VALUE obj = cdhash_new(len);

    int i;
    for (i = 0; i < len; i++) {
        VALUE key_index = ibf_load_small_value(load, &offset);
        VALUE val = ibf_load_small_value(load, &offset);

        VALUE key = ibf_load_object(load, key_index);
        cdhash_aset(obj, key, val);
    }

    return obj;
}

static void
ibf_dump_object_struct(struct ibf_dump *dump, VALUE obj)
{
    if (rb_obj_is_kind_of(obj, rb_cRange)) {
        struct ibf_object_struct_range range;
        VALUE beg, end;
        IBF_ZERO(range);
        range.len = 3;
        range.class_index = 0;

        rb_range_values(obj, &beg, &end, &range.excl);
        range.beg = (long)ibf_dump_object(dump, beg);
        range.end = (long)ibf_dump_object(dump, end);

        IBF_W_ALIGN(struct ibf_object_struct_range);
        IBF_WV(range);
    }
    else {
        rb_raise(rb_eNotImpError, "ibf_dump_object_struct: unsupported class %"PRIsVALUE,
                 rb_class_name(CLASS_OF(obj)));
    }
}

static VALUE
ibf_load_object_struct(const struct ibf_load *load, const struct ibf_object_header *header, ibf_offset_t offset)
{
    const struct ibf_object_struct_range *range = IBF_OBJBODY(struct ibf_object_struct_range, offset);
    VALUE beg = ibf_load_object(load, range->beg);
    VALUE end = ibf_load_object(load, range->end);
    VALUE obj = rb_range_new(beg, end, range->excl);
    if (header->internal) rb_obj_hide(obj);
    if (header->frozen)   RB_OBJ_SET_FROZEN_SHAREABLE(obj);
    return obj;
}

static void
ibf_dump_object_bignum(struct ibf_dump *dump, VALUE obj)
{
    ssize_t len = BIGNUM_LEN(obj);
    ssize_t slen = BIGNUM_SIGN(obj) > 0 ? len : len * -1;
    BDIGIT *d = BIGNUM_DIGITS(obj);

    (void)IBF_W(&slen, ssize_t, 1);
    IBF_WP(d, BDIGIT, len);
}

static VALUE
ibf_load_object_bignum(const struct ibf_load *load, const struct ibf_object_header *header, ibf_offset_t offset)
{
    const struct ibf_object_bignum *bignum = IBF_OBJBODY(struct ibf_object_bignum, offset);
    int sign = bignum->slen > 0;
    ssize_t len = sign > 0 ? bignum->slen : -1 * bignum->slen;
    const int big_unpack_flags = /* c.f. rb_big_unpack() */
        INTEGER_PACK_LSWORD_FIRST |
        INTEGER_PACK_NATIVE_BYTE_ORDER;
    VALUE obj = rb_integer_unpack(bignum->digits, len, sizeof(BDIGIT), 0,
                                  big_unpack_flags |
                                  (sign == 0 ? INTEGER_PACK_NEGATIVE : 0));
    if (header->internal) rb_obj_hide(obj);
    if (header->frozen)   RB_OBJ_SET_FROZEN_SHAREABLE(obj);
    return obj;
}

static void
ibf_dump_object_data(struct ibf_dump *dump, VALUE obj)
{
    if (rb_data_is_encoding(obj)) {
        rb_encoding *enc = rb_to_encoding(obj);
        const char *name = rb_enc_name(enc);
        long len = strlen(name) + 1;
        long data[2];
        data[0] = IBF_OBJECT_DATA_ENCODING;
        data[1] = len;
        (void)IBF_W(data, long, 2);
        IBF_WP(name, char, len);
    }
    else {
        ibf_dump_object_unsupported(dump, obj);
    }
}

static VALUE
ibf_load_object_data(const struct ibf_load *load, const struct ibf_object_header *header, ibf_offset_t offset)
{
    const long *body = IBF_OBJBODY(long, offset);
    const enum ibf_object_data_type type = (enum ibf_object_data_type)body[0];
    /* const long len = body[1]; */
    const char *data = (const char *)&body[2];

    switch (type) {
      case IBF_OBJECT_DATA_ENCODING:
        {
            VALUE encobj = rb_enc_from_encoding(rb_enc_find(data));
            return encobj;
        }
    }

    return ibf_load_object_unsupported(load, header, offset);
}

static void
ibf_dump_object_complex_rational(struct ibf_dump *dump, VALUE obj)
{
    long data[2];
    data[0] = (long)ibf_dump_object(dump, RCOMPLEX(obj)->real);
    data[1] = (long)ibf_dump_object(dump, RCOMPLEX(obj)->imag);

    (void)IBF_W(data, long, 2);
}

static VALUE
ibf_load_object_complex_rational(const struct ibf_load *load, const struct ibf_object_header *header, ibf_offset_t offset)
{
    const struct ibf_object_complex_rational *nums = IBF_OBJBODY(struct ibf_object_complex_rational, offset);
    VALUE a = ibf_load_object(load, nums->a);
    VALUE b = ibf_load_object(load, nums->b);
    VALUE obj = header->type == T_COMPLEX ?
      rb_complex_new(a, b) : rb_rational_new(a, b);

    if (header->internal) rb_obj_hide(obj);
    if (header->frozen) rb_ractor_make_shareable(rb_obj_freeze(obj));
    return obj;
}

static void
ibf_dump_object_symbol(struct ibf_dump *dump, VALUE obj)
{
    ibf_dump_object_string(dump, rb_sym2str(obj));
}

static VALUE
ibf_load_object_symbol(const struct ibf_load *load, const struct ibf_object_header *header, ibf_offset_t offset)
{
    ibf_offset_t reading_pos = offset;

    int encindex = (int)ibf_load_small_value(load, &reading_pos);
    const long len = (long)ibf_load_small_value(load, &reading_pos);
    const char *ptr = load->current_buffer->buff + reading_pos;

    if (encindex > RUBY_ENCINDEX_BUILTIN_MAX) {
        VALUE enc_name_str = ibf_load_object(load, encindex - RUBY_ENCINDEX_BUILTIN_MAX);
        encindex = rb_enc_find_index(RSTRING_PTR(enc_name_str));
    }

    ID id = rb_intern3(ptr, len, rb_enc_from_index(encindex));
    return ID2SYM(id);
}

typedef void (*ibf_dump_object_function)(struct ibf_dump *dump, VALUE obj);
static const ibf_dump_object_function dump_object_functions[RUBY_T_MASK+1] = {
    ibf_dump_object_unsupported, /* T_NONE */
    ibf_dump_object_unsupported, /* T_OBJECT */
    ibf_dump_object_class,       /* T_CLASS */
    ibf_dump_object_unsupported, /* T_MODULE */
    ibf_dump_object_float,       /* T_FLOAT */
    ibf_dump_object_string,      /* T_STRING */
    ibf_dump_object_regexp,      /* T_REGEXP */
    ibf_dump_object_array,       /* T_ARRAY */
    ibf_dump_object_hash,        /* T_HASH */
    ibf_dump_object_struct,      /* T_STRUCT */
    ibf_dump_object_bignum,      /* T_BIGNUM */
    ibf_dump_object_unsupported, /* T_FILE */
    ibf_dump_object_data,        /* T_DATA */
    ibf_dump_object_unsupported, /* T_MATCH */
    ibf_dump_object_complex_rational, /* T_COMPLEX */
    ibf_dump_object_complex_rational, /* T_RATIONAL */
    ibf_dump_object_unsupported, /* 0x10 */
    ibf_dump_object_unsupported, /* 0x11 T_NIL */
    ibf_dump_object_unsupported, /* 0x12 T_TRUE */
    ibf_dump_object_unsupported, /* 0x13 T_FALSE */
    ibf_dump_object_symbol,      /* 0x14 T_SYMBOL */
    ibf_dump_object_unsupported, /* T_FIXNUM */
    ibf_dump_object_unsupported, /* T_UNDEF */
    ibf_dump_object_unsupported, /* 0x17 */
    ibf_dump_object_unsupported, /* 0x18 */
    ibf_dump_object_unsupported, /* 0x19 */
    ibf_dump_object_imemo,       /* T_IMEMO 0x1a */
    ibf_dump_object_unsupported, /* T_NODE 0x1b */
    ibf_dump_object_unsupported, /* T_ICLASS 0x1c */
    ibf_dump_object_unsupported, /* T_ZOMBIE 0x1d */
    ibf_dump_object_unsupported, /* 0x1e */
    ibf_dump_object_unsupported, /* 0x1f */
};

static void
ibf_dump_object_object_header(struct ibf_dump *dump, const struct ibf_object_header header)
{
    unsigned char byte =
        (header.type          << 0) |
        (header.special_const << 5) |
        (header.frozen        << 6) |
        (header.internal      << 7);

    IBF_WV(byte);
}

static struct ibf_object_header
ibf_load_object_object_header(const struct ibf_load *load, ibf_offset_t *offset)
{
    unsigned char byte = ibf_load_byte(load, offset);

    struct ibf_object_header header;
    header.type          = (byte >> 0) & 0x1f;
    header.special_const = (byte >> 5) & 0x01;
    header.frozen        = (byte >> 6) & 0x01;
    header.internal      = (byte >> 7) & 0x01;

    return header;
}

static ibf_offset_t
ibf_dump_object_object(struct ibf_dump *dump, VALUE obj)
{
    struct ibf_object_header obj_header;
    ibf_offset_t current_offset;
    IBF_ZERO(obj_header);
    obj_header.type = TYPE(obj);

    IBF_W_ALIGN(ibf_offset_t);
    current_offset = ibf_dump_pos(dump);

    if (SPECIAL_CONST_P(obj) &&
        ! (SYMBOL_P(obj) ||
           RB_FLOAT_TYPE_P(obj))) {
        obj_header.special_const = TRUE;
        obj_header.frozen = TRUE;
        obj_header.internal = TRUE;
        ibf_dump_object_object_header(dump, obj_header);
        ibf_dump_write_small_value(dump, obj);
    }
    else {
        obj_header.internal = SPECIAL_CONST_P(obj) ? FALSE : (RBASIC_CLASS(obj) == 0) ? TRUE : FALSE;
        obj_header.special_const = FALSE;
        obj_header.frozen = OBJ_FROZEN(obj) ? TRUE : FALSE;
        ibf_dump_object_object_header(dump, obj_header);
        (*dump_object_functions[obj_header.type])(dump, obj);
    }

    return current_offset;
}

typedef VALUE (*ibf_load_object_function)(const struct ibf_load *load, const struct ibf_object_header *header, ibf_offset_t offset);
static const ibf_load_object_function load_object_functions[RUBY_T_MASK+1] = {
    ibf_load_object_unsupported, /* T_NONE */
    ibf_load_object_unsupported, /* T_OBJECT */
    ibf_load_object_class,       /* T_CLASS */
    ibf_load_object_unsupported, /* T_MODULE */
    ibf_load_object_float,       /* T_FLOAT */
    ibf_load_object_string,      /* T_STRING */
    ibf_load_object_regexp,      /* T_REGEXP */
    ibf_load_object_array,       /* T_ARRAY */
    ibf_load_object_hash,        /* T_HASH */
    ibf_load_object_struct,      /* T_STRUCT */
    ibf_load_object_bignum,      /* T_BIGNUM */
    ibf_load_object_unsupported, /* T_FILE */
    ibf_load_object_data,        /* T_DATA */
    ibf_load_object_unsupported, /* T_MATCH */
    ibf_load_object_complex_rational, /* T_COMPLEX */
    ibf_load_object_complex_rational, /* T_RATIONAL */
    ibf_load_object_unsupported, /* 0x10 */
    ibf_load_object_unsupported, /* T_NIL */
    ibf_load_object_unsupported, /* T_TRUE */
    ibf_load_object_unsupported, /* T_FALSE */
    ibf_load_object_symbol,
    ibf_load_object_unsupported, /* T_FIXNUM */
    ibf_load_object_unsupported, /* T_UNDEF */
    ibf_load_object_unsupported, /* 0x17 */
    ibf_load_object_unsupported, /* 0x18 */
    ibf_load_object_unsupported, /* 0x19 */
    ibf_load_object_imemo,       /* T_IMEMO 0x1a */
    ibf_load_object_unsupported, /* T_NODE 0x1b */
    ibf_load_object_unsupported, /* T_ICLASS 0x1c */
    ibf_load_object_unsupported, /* T_ZOMBIE 0x1d */
    ibf_load_object_unsupported, /* 0x1e */
    ibf_load_object_unsupported, /* 0x1f */
};

static VALUE
ibf_load_object(const struct ibf_load *load, VALUE object_index)
{
    if (object_index == 0) {
        return Qnil;
    }
    else {
        VALUE obj = pinned_list_fetch(load->current_buffer->obj_list, (long)object_index);
        if (!obj) {
            ibf_offset_t *offsets = (ibf_offset_t *)(load->current_buffer->obj_list_offset + load->current_buffer->buff);
            ibf_offset_t offset = offsets[object_index];
            const struct ibf_object_header header = ibf_load_object_object_header(load, &offset);

#if IBF_ISEQ_DEBUG
            fprintf(stderr, "ibf_load_object: list=%#x offsets=%p offset=%#x\n",
                    load->current_buffer->obj_list_offset, (void *)offsets, offset);
            fprintf(stderr, "ibf_load_object: type=%#x special=%d frozen=%d internal=%d\n",
                    header.type, header.special_const, header.frozen, header.internal);
#endif
            if (offset >= load->current_buffer->size) {
                rb_raise(rb_eIndexError, "object offset out of range: %u", offset);
            }

            if (header.special_const) {
                ibf_offset_t reading_pos = offset;

                obj = ibf_load_small_value(load, &reading_pos);
            }
            else {
                obj = (*load_object_functions[header.type])(load, &header, offset);
            }

            pinned_list_store(load->current_buffer->obj_list, (long)object_index, obj);
        }
#if IBF_ISEQ_DEBUG
        fprintf(stderr, "ibf_load_object: index=%#"PRIxVALUE" obj=%#"PRIxVALUE"\n",
                object_index, obj);
#endif
        return obj;
    }
}

struct ibf_dump_object_list_arg
{
    struct ibf_dump *dump;
    VALUE offset_list;
};

static int
ibf_dump_object_list_i(st_data_t key, st_data_t val, st_data_t ptr)
{
    VALUE obj = (VALUE)key;
    struct ibf_dump_object_list_arg *args = (struct ibf_dump_object_list_arg *)ptr;

    ibf_offset_t offset = ibf_dump_object_object(args->dump, obj);
    rb_ary_push(args->offset_list, UINT2NUM(offset));

    return ST_CONTINUE;
}

static void
ibf_dump_object_list(struct ibf_dump *dump, ibf_offset_t *obj_list_offset, unsigned int *obj_list_size)
{
    st_table *obj_table = dump->current_buffer->obj_table;
    VALUE offset_list = rb_ary_hidden_new(obj_table->num_entries);

    struct ibf_dump_object_list_arg args;
    args.dump = dump;
    args.offset_list = offset_list;

    st_foreach(obj_table, ibf_dump_object_list_i, (st_data_t)&args);

    IBF_W_ALIGN(ibf_offset_t);
    *obj_list_offset = ibf_dump_pos(dump);

    st_index_t size = obj_table->num_entries;
    st_index_t i;

    for (i=0; i<size; i++) {
        ibf_offset_t offset = NUM2UINT(RARRAY_AREF(offset_list, i));
        IBF_WV(offset);
    }

    *obj_list_size = (unsigned int)size;
}

static void
ibf_dump_mark(void *ptr)
{
    struct ibf_dump *dump = (struct ibf_dump *)ptr;
    rb_gc_mark(dump->global_buffer.str);

    rb_mark_set(dump->global_buffer.obj_table);
    rb_mark_set(dump->iseq_table);
}

static void
ibf_dump_free(void *ptr)
{
    struct ibf_dump *dump = (struct ibf_dump *)ptr;
    if (dump->global_buffer.obj_table) {
        st_free_table(dump->global_buffer.obj_table);
        dump->global_buffer.obj_table = 0;
    }
    if (dump->iseq_table) {
        st_free_table(dump->iseq_table);
        dump->iseq_table = 0;
    }
}

static size_t
ibf_dump_memsize(const void *ptr)
{
    struct ibf_dump *dump = (struct ibf_dump *)ptr;
    size_t size = 0;
    if (dump->iseq_table) size += st_memsize(dump->iseq_table);
    if (dump->global_buffer.obj_table) size += st_memsize(dump->global_buffer.obj_table);
    return size;
}

static const rb_data_type_t ibf_dump_type = {
    "ibf_dump",
    {ibf_dump_mark, ibf_dump_free, ibf_dump_memsize,},
    0, 0, RUBY_TYPED_THREAD_SAFE_FREE | RUBY_TYPED_EMBEDDABLE
};

static void
ibf_dump_setup(struct ibf_dump *dump, VALUE dumper_obj)
{
    dump->global_buffer.obj_table = NULL; // GC may run before a value is assigned
    dump->iseq_table = NULL;

    RB_OBJ_WRITE(dumper_obj, &dump->global_buffer.str, rb_str_new(0, 0));
    dump->global_buffer.obj_table = ibf_dump_object_table_new();
    dump->iseq_table = st_init_numtable(); /* need free */

    dump->current_buffer = &dump->global_buffer;
}

static VALUE
iseq_ibf_dump_0(struct ibf_dump *dump, const rb_iseq_t *iseq, VALUE opt)
{
    struct ibf_header header = {{0}};

    ibf_dump_write(dump, &header, sizeof(header));
    ibf_dump_iseq(dump, iseq);

    header.magic[0] = 'Y'; /* YARB */
    header.magic[1] = 'A';
    header.magic[2] = 'R';
    header.magic[3] = 'B';
    header.major_version = IBF_MAJOR_VERSION;
    header.minor_version = IBF_MINOR_VERSION;
    header.endian = IBF_ENDIAN_MARK;
    header.wordsize = (uint8_t)SIZEOF_VALUE;
    ibf_dump_iseq_list(dump, &header);
    ibf_dump_object_list(dump, &header.global_object_list_offset, &header.global_object_list_size);
    header.size = ibf_dump_pos(dump);

    if (RTEST(opt)) {
        VALUE opt_str = opt;
        const char *ptr = StringValuePtr(opt_str);
        header.extra_size = RSTRING_LENINT(opt_str);
        ibf_dump_write(dump, ptr, header.extra_size);
    }
    else {
        header.extra_size = 0;
    }

    ibf_dump_overwrite(dump, &header, sizeof(header), 0);

    return dump->global_buffer.str;
}

VALUE
rb_iseq_ibf_dump(const rb_iseq_t *iseq, VALUE opt)
{
    struct ibf_dump *dump;
    VALUE dump_obj;
    VALUE str;

    if (ISEQ_BODY(iseq)->parent_iseq != NULL ||
        ISEQ_BODY(iseq)->local_iseq != iseq) {
        rb_raise(rb_eRuntimeError, "should be top of iseq");
    }
    if (RTEST(ISEQ_COVERAGE(iseq))) {
        rb_raise(rb_eRuntimeError, "should not compile with coverage");
    }

    dump_obj = TypedData_Make_Struct(0, struct ibf_dump, &ibf_dump_type, dump);
    ibf_dump_setup(dump, dump_obj);

    str = iseq_ibf_dump_0(dump, iseq, opt);
    RB_GC_GUARD(dump_obj);
    return str;
}

static const ibf_offset_t *
ibf_iseq_list(const struct ibf_load *load)
{
    return (const ibf_offset_t *)(load->global_buffer.buff + load->header->iseq_list_offset);
}

void
rb_ibf_load_iseq_complete(rb_iseq_t *iseq)
{
    struct ibf_load *load = RTYPEDDATA_DATA(iseq->aux.loader.obj);
    rb_iseq_t *prev_src_iseq = load->iseq;
    ibf_offset_t offset = ibf_iseq_list(load)[iseq->aux.loader.index];
    load->iseq = iseq;
#if IBF_ISEQ_DEBUG
    fprintf(stderr, "rb_ibf_load_iseq_complete: index=%#x offset=%#x size=%#x\n",
            iseq->aux.loader.index, offset,
            load->header->size);
#endif
    ibf_load_iseq_each(load, iseq, offset);
    ISEQ_COMPILE_DATA_CLEAR(iseq);
    FL_UNSET((VALUE)iseq, ISEQ_NOT_LOADED_YET);
    rb_iseq_init_trace(iseq);
    load->iseq = prev_src_iseq;
}

#if USE_LAZY_LOAD
const rb_iseq_t *
rb_iseq_complete(const rb_iseq_t *iseq)
{
    rb_ibf_load_iseq_complete((rb_iseq_t *)iseq);
    return iseq;
}
#endif

static rb_iseq_t *
ibf_load_iseq(const struct ibf_load *load, const rb_iseq_t *index_iseq)
{
    int iseq_index = (int)(VALUE)index_iseq;

#if IBF_ISEQ_DEBUG
    fprintf(stderr, "ibf_load_iseq: index_iseq=%p iseq_list=%p\n",
            (void *)index_iseq, (void *)load->iseq_list);
#endif
    if (iseq_index == -1) {
        return NULL;
    }
    else {
        VALUE iseqv = pinned_list_fetch(load->iseq_list, iseq_index);

#if IBF_ISEQ_DEBUG
        fprintf(stderr, "ibf_load_iseq: iseqv=%p\n", (void *)iseqv);
#endif
        if (iseqv) {
            return (rb_iseq_t *)iseqv;
        }
        else {
            rb_iseq_t *iseq = iseq_imemo_alloc();
#if IBF_ISEQ_DEBUG
            fprintf(stderr, "ibf_load_iseq: new iseq=%p\n", (void *)iseq);
#endif
            FL_SET((VALUE)iseq, ISEQ_NOT_LOADED_YET);
            RB_OBJ_WRITE((VALUE)iseq, &iseq->aux.loader.obj, load->loader_obj);
            iseq->aux.loader.index = iseq_index;
#if IBF_ISEQ_DEBUG
            fprintf(stderr, "ibf_load_iseq: iseq=%p loader_obj=%p index=%d\n",
                    (void *)iseq, (void *)load->loader_obj, iseq_index);
#endif
            pinned_list_store(load->iseq_list, iseq_index, (VALUE)iseq);

            if (!USE_LAZY_LOAD || GET_VM()->builtin_function_table) {
#if IBF_ISEQ_DEBUG
                fprintf(stderr, "ibf_load_iseq: loading iseq=%p\n", (void *)iseq);
#endif
                rb_ibf_load_iseq_complete(iseq);
            }

#if IBF_ISEQ_DEBUG
            fprintf(stderr, "ibf_load_iseq: iseq=%p loaded %p\n",
                    (void *)iseq, (void *)load->iseq);
#endif
            return iseq;
        }
    }
}

static void
ibf_load_setup_bytes(struct ibf_load *load, VALUE loader_obj, const char *bytes, size_t size)
{
    struct ibf_header *header = (struct ibf_header *)bytes;
    load->loader_obj = loader_obj;
    load->global_buffer.buff = bytes;
    load->header = header;
    load->global_buffer.size = header->size;
    load->global_buffer.obj_list_offset = header->global_object_list_offset;
    load->global_buffer.obj_list_size = header->global_object_list_size;
    RB_OBJ_WRITE(loader_obj, &load->iseq_list, pinned_list_new(header->iseq_list_size));
    RB_OBJ_WRITE(loader_obj, &load->global_buffer.obj_list, pinned_list_new(load->global_buffer.obj_list_size));
    load->iseq = NULL;

    load->current_buffer = &load->global_buffer;

    if (size < header->size) {
        rb_raise(rb_eRuntimeError, "broken binary format");
    }
    if (strncmp(header->magic, "YARB", 4) != 0) {
        rb_raise(rb_eRuntimeError, "unknown binary format");
    }
    if (header->major_version != IBF_MAJOR_VERSION ||
        header->minor_version != IBF_MINOR_VERSION) {
        rb_raise(rb_eRuntimeError, "unmatched version file (%u.%u for %u.%u)",
                 header->major_version, header->minor_version, IBF_MAJOR_VERSION, IBF_MINOR_VERSION);
    }
    if (header->endian != IBF_ENDIAN_MARK) {
        rb_raise(rb_eRuntimeError, "unmatched endian: %c", header->endian);
    }
    if (header->wordsize != SIZEOF_VALUE) {
        rb_raise(rb_eRuntimeError, "unmatched word size: %d", header->wordsize);
    }
    if (header->iseq_list_offset % RUBY_ALIGNOF(ibf_offset_t)) {
        rb_raise(rb_eArgError, "unaligned iseq list offset: %u",
                 header->iseq_list_offset);
    }
    if (load->global_buffer.obj_list_offset % RUBY_ALIGNOF(ibf_offset_t)) {
        rb_raise(rb_eArgError, "unaligned object list offset: %u",
                 load->global_buffer.obj_list_offset);
    }
}

static void
ibf_load_setup(struct ibf_load *load, VALUE loader_obj, VALUE str)
{
    StringValue(str);

    if (RSTRING_LENINT(str) < (int)sizeof(struct ibf_header)) {
        rb_raise(rb_eRuntimeError, "broken binary format");
    }

    if (USE_LAZY_LOAD) {
        str = rb_str_new(RSTRING_PTR(str), RSTRING_LEN(str));
    }

    ibf_load_setup_bytes(load, loader_obj, RSTRING_PTR(str), RSTRING_LEN(str));
    RB_OBJ_WRITE(loader_obj, &load->str, str);
}

static void
ibf_loader_mark(void *ptr)
{
    struct ibf_load *load = (struct ibf_load *)ptr;
    rb_gc_mark(load->str);
    rb_gc_mark(load->iseq_list);
    rb_gc_mark(load->global_buffer.obj_list);
}

static void
ibf_loader_free(void *ptr)
{
    struct ibf_load *load = (struct ibf_load *)ptr;
    SIZED_FREE(load);
}

static size_t
ibf_loader_memsize(const void *ptr)
{
    return sizeof(struct ibf_load);
}

static const rb_data_type_t ibf_load_type = {
    "ibf_loader",
    {ibf_loader_mark, ibf_loader_free, ibf_loader_memsize,},
    0, 0, RUBY_TYPED_WB_PROTECTED | RUBY_TYPED_THREAD_SAFE_FREE
};

const rb_iseq_t *
rb_iseq_ibf_load(VALUE str)
{
    struct ibf_load *load;
    rb_iseq_t *iseq;
    VALUE loader_obj = TypedData_Make_Struct(0, struct ibf_load, &ibf_load_type, load);

    ibf_load_setup(load, loader_obj, str);
    iseq = ibf_load_iseq(load, 0);

    RB_GC_GUARD(loader_obj);
    return iseq;
}

const rb_iseq_t *
rb_iseq_ibf_load_bytes(const char *bytes, size_t size)
{
    struct ibf_load *load;
    rb_iseq_t *iseq;
    VALUE loader_obj = TypedData_Make_Struct(0, struct ibf_load, &ibf_load_type, load);

    ibf_load_setup_bytes(load, loader_obj, bytes, size);
    iseq = ibf_load_iseq(load, 0);

    RB_GC_GUARD(loader_obj);
    return iseq;
}

/* collect the iseq table into an array indexed by iseq-list index */
static int
ibf_dump_iseq_table_collect_i(st_data_t key, st_data_t val, st_data_t arg)
{
    const rb_iseq_t **srcs = (const rb_iseq_t **)arg;
    srcs[(int)val] = (const rb_iseq_t *)key;
    return ST_CONTINUE;
}

/* pc2branchindex (branch coverage) is per-iseq, so pair each copy with its
 * source by list index */
static void
iseq_subtree_copy_pc2branchindex(const struct ibf_dump *dump, const struct ibf_load *load)
{
    unsigned int iseq_count = (unsigned int)dump->iseq_table->num_entries;
    VALUE srcs_buf;
    const rb_iseq_t **srcs = ALLOCV_N(const rb_iseq_t *, srcs_buf, iseq_count);
    st_foreach(dump->iseq_table, ibf_dump_iseq_table_collect_i, (st_data_t)srcs);

    for (unsigned int i = 0; i < iseq_count; i++) {
        rb_iseq_t *copy = ibf_load_iseq(load, (const rb_iseq_t *)(VALUE)i);
        VALUE pc2branchindex = ISEQ_PC2BRANCHINDEX(srcs[i]);
        if (pc2branchindex != Qnil) {
            ISEQ_PC2BRANCHINDEX_SET(copy, pc2branchindex);
        }
    }

    ALLOCV_END(srcs_buf);
}

/* Deep-copy an iseq subtree with independent inline caches for Proc#refined */
const rb_iseq_t *
rb_iseq_dup_with_independent_caches(const rb_iseq_t *src_root)
{
    struct ibf_dump *dump;
    VALUE dump_obj;
    VALUE str;

    /* --- dump the subtree --- */
    dump_obj = TypedData_Make_Struct(0, struct ibf_dump, &ibf_dump_type, dump);
    ibf_dump_setup(dump, dump_obj);

    str = iseq_ibf_dump_0(dump, src_root, Qnil);

    /* --- load it back --- */
    struct ibf_load *load;
    VALUE loader_obj = TypedData_Make_Struct(0, struct ibf_load, &ibf_load_type, load);
    ibf_load_setup(load, loader_obj, str);

    /* What the loader could not reconstruct is the same for every iseq in
     * the subtree: the only absent parent is the top's, every absent
     * local_iseq is the source's local root (runtime walks compare it
     * against the original frames, so a copy must not stand in), and
     * pathobj/script_lines/coverage are per-file values. */
    const struct rb_iseq_constant_body *sb = ISEQ_BODY(src_root);
    unsigned int iseq_count = (unsigned int)dump->iseq_table->num_entries;
    const rb_iseq_t *result = NULL;
    for (unsigned int i = 0; i < iseq_count; i++) {
        rb_iseq_t *copy = ibf_load_iseq(load, (const rb_iseq_t *)(VALUE)i);
        /* force completion even under USE_LAZY_LOAD */
        if (FL_TEST((VALUE)copy, ISEQ_NOT_LOADED_YET)) {
            rb_ibf_load_iseq_complete(copy);
        }

        FL_SET((VALUE)copy, ISEQ_REFINED_COPY);

        struct rb_iseq_constant_body *cb = ISEQ_BODY(copy);
        if (!cb->local_iseq) RB_OBJ_WRITE(copy, &cb->local_iseq, sb->local_iseq);
        RB_OBJ_WRITE(copy, &cb->location.pathobj, sb->location.pathobj);
        VALUE sl = ISEQ_SCRIPT_LINES(src_root);
        VALUE cov = ISEQ_COVERAGE(src_root);
        if (!NIL_P(sl) || !NIL_P(cov)) {
            struct rb_iseq_variable *v = rb_iseq_variable_ensure(copy);
            RB_OBJ_WRITE(copy, &v->script_lines, sl);
            RB_OBJ_WRITE(copy, &v->coverage, cov);
        }

        if (i == 0) {
            RB_OBJ_WRITE(copy, &cb->parent_iseq, sb->parent_iseq);
            result = copy;
        }

        /* Shared across Ractors through the Proc#refined memo; the duplicated
         * subtree is self-contained, so mark each copy shareable. */
        RB_OBJ_SET_SHAREABLE((VALUE)copy);
    }

    if (ISEQ_PC2BRANCHINDEX(src_root) != Qnil) {
        iseq_subtree_copy_pc2branchindex(dump, load);
    }

    RB_GC_GUARD(dump_obj);
    RB_GC_GUARD(loader_obj);
    return result;
}

VALUE
rb_iseq_ibf_load_extra_data(VALUE str)
{
    struct ibf_load *load;
    VALUE loader_obj = TypedData_Make_Struct(0, struct ibf_load, &ibf_load_type, load);
    VALUE extra_str;

    ibf_load_setup(load, loader_obj, str);
    extra_str = rb_str_new(load->global_buffer.buff + load->header->size, load->header->extra_size);
    RB_GC_GUARD(loader_obj);
    return extra_str;
}
