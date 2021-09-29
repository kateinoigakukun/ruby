#ifndef RUBY_THREAD_NONE_H
#define RUBY_THREAD_NONE_H

#define RB_NATIVETHREAD_LOCK_INIT (void*)(0)
#define RB_NATIVETHREAD_COND_INIT (void*)(0)

typedef struct native_thread_data_struct {
    union {
        struct list_node ubf;
        struct list_node gvl;
    } node;
} native_thread_data_t;


typedef struct rb_global_vm_lock_struct {
  struct list_head waitq;
} rb_global_vm_lock_t;


typedef struct {
  void *value;
}*native_tls_key_t;

RUBY_SYMBOL_EXPORT_BEGIN
  RUBY_EXTERN native_tls_key_t ruby_current_ec_key;
RUBY_SYMBOL_EXPORT_END


static inline void *
native_tls_get(native_tls_key_t key)
{
    return key->value;
}

static inline void
native_tls_set(native_tls_key_t key, void *ptr)
{
  key->value = ptr;
}

#endif /* RUBY_THREAD_PTHREAD_H */
