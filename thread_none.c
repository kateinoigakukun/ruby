#ifdef THREAD_SYSTEM_DEPENDENT_IMPLEMENTATION

#include <signal.h>
#include <time.h>

#define DEBUG_OUT() (void)(0);

#define TIMER_THREAD_CREATED_P() (1)

#define native_thread_yield() ((void)0)

#define TIME_QUANTUM_MSEC (100)
#define TIME_QUANTUM_USEC (TIME_QUANTUM_MSEC * 1000)
#define TIME_QUANTUM_NSEC (TIME_QUANTUM_USEC * 1000)

static void gvl_acquire(rb_global_vm_lock_t *gvl, rb_thread_t *th) {}
static void gvl_release(rb_global_vm_lock_t *gvl) {}
static void gvl_yield(rb_global_vm_lock_t *gvl, rb_thread_t *th) {}
void rb_gvl_init(rb_global_vm_lock_t *gvl) {}
static void gvl_destroy(rb_global_vm_lock_t *gvl) {}

void rb_native_mutex_lock(rb_nativethread_lock_t *lock) {}
void rb_native_mutex_unlock(rb_nativethread_lock_t *lock) {}
int rb_native_mutex_trylock(rb_nativethread_lock_t *lock) { return 0; }
void rb_native_mutex_initialize(rb_nativethread_lock_t *lock) {}
void rb_native_mutex_destroy(rb_nativethread_lock_t *lock) {}
void rb_native_cond_initialize(rb_nativethread_cond_t *cond) {}
void rb_native_cond_destroy(rb_nativethread_cond_t *cond) {}
void rb_native_cond_signal(rb_nativethread_cond_t *cond) {}
void rb_native_cond_broadcast(rb_nativethread_cond_t *cond) {}
void rb_native_cond_wait(rb_nativethread_cond_t *cond, rb_nativethread_lock_t *mutex) {}
void rb_native_cond_timedwait(rb_nativethread_cond_t *cond, rb_nativethread_lock_t *mutex, unsigned long msec) {}

static rb_thread_t *ruby_native_thread;

rb_thread_t *ruby_thread_from_native(void) { return ruby_native_thread; }

int ruby_thread_set_native(rb_thread_t *th) {
    if (th && th->ec) {
        rb_ractor_set_current_ec(th->ractor, th->ec);
    }
    ruby_native_thread = th;
    return 1;
}

void Init_native_thread(rb_thread_t *th) {
    ruby_thread_set_native(th);
    fill_thread_id_str(th);
}

static void native_thread_destroy(rb_thread_t *th) {}

#undef ruby_init_stack
void ruby_init_stack(volatile VALUE *addr) {}
static int native_thread_init_stack(rb_thread_t *th) { return 0; }

static int native_thread_create(rb_thread_t *th) { return ENOTSUP; }

#define register_ubf_list(th) (void)(th)
#define unregister_ubf_list(th) (void)(th)
#define ubf_select 0
static void ubf_wakeup_all_threads(void) { return; }
static int ubf_threads_empty(void) { return 1; }
#define ubf_list_atfork() do {} while (0)

#define TT_DEBUG 0
#define WRITE_CONST(fd, str) (void)(write((fd), (str), sizeof(str) - 1) < 0)

void rb_thread_wakeup_timer_thread(int sig) {}

static VALUE threadptr_invoke_proc_location(rb_thread_t *th);

static void native_set_thread_name(rb_thread_t *th) {}
static void native_set_another_thread_name(rb_nativethread_id_t thread_id, VALUE name) {}

#define USE_NATIVE_THREAD_NATIVE_THREAD_ID 0

static void rb_thread_create_timer_thread(void) {}
static void ubf_timer_disarm(void) {}
static int native_stop_timer_thread(void) { return 1; }

static void native_reset_timer_thread(void) {
    if (TT_DEBUG)
        fprintf(stderr, "reset timer thread\n");
}

int rb_reserved_fd_p(int fd) { return 0; }
rb_nativethread_id_t rb_nativethread_self(void) { return NULL; }

int rb_sigwait_fd_get(const rb_thread_t *th) { return -1; }
void rb_sigwait_fd_put(const rb_thread_t *th, int fd) {
    rb_bug("not implemented, should not be called rb_sigwait_fd_put");
}
void rb_sigwait_sleep(rb_thread_t *th, int sigwait_fd, const rb_hrtime_t *rel) {
    rb_bug("not implemented, should not be called rb_sigwait_sleep");
}

static void native_sleep(rb_thread_t *th, rb_hrtime_t *rel) {}

static VALUE rb_thread_start_unblock_thread(void) { return Qfalse; }
#endif /* THREAD_SYSTEM_DEPENDENT_IMPLEMENTATION */
