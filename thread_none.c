#ifdef THREAD_SYSTEM_DEPENDENT_IMPLEMENTATION

#include "gc.h"
#include "mjit.h"

#include <signal.h>
#include <time.h>

#define USE_EVENTFD (0)

#define DEBUG_OUT() (void)(0);
#define TIME_QUANTUM_USEC 0

#define UBF_TIMER_NONE 0
#define UBF_TIMER_POSIX 1
#define UBF_TIMER_PTHREAD 2

#define UBF_TIMER UBF_TIMER_NONE

// FIXME(katei): original was `signal_self_pipe.owner_process == getpid()`
#define TIMER_THREAD_CREATED_P() (1)

#define BUSY_WAIT_SIGNALS (0)

#define THREAD_INVALID ((const rb_thread_t *)-1)
static const rb_thread_t *sigwait_th;

#define native_thread_yield() ((void)0)

static const void *const condattr_monotonic = NULL;

#define TIME_QUANTUM_MSEC (100)
#define TIME_QUANTUM_USEC (TIME_QUANTUM_MSEC * 1000)
#define TIME_QUANTUM_NSEC (TIME_QUANTUM_USEC * 1000)

static void gvl_acquire(rb_global_vm_lock_t *gvl, rb_thread_t *th) {}

static void gvl_release(rb_global_vm_lock_t *gvl) {}

static void gvl_yield(rb_global_vm_lock_t *gvl, rb_thread_t *th) {}

void rb_gvl_init(rb_global_vm_lock_t *gvl) {}

static void gvl_destroy(rb_global_vm_lock_t *gvl) {}

#define NATIVE_MUTEX_LOCK_DEBUG 0

void rb_native_mutex_lock(rb_nativethread_lock_t *lock) {}

void rb_native_mutex_unlock(rb_nativethread_lock_t *lock) {}

int rb_native_mutex_trylock(rb_nativethread_lock_t *lock) { return 0; }

void rb_native_mutex_initialize(rb_nativethread_lock_t *lock) {}

void rb_native_mutex_destroy(rb_nativethread_lock_t *lock) {}

void rb_native_cond_initialize(rb_nativethread_cond_t *cond) {}

void rb_native_cond_destroy(rb_nativethread_cond_t *cond) {}

void rb_native_cond_signal(rb_nativethread_cond_t *cond) {}

void rb_native_cond_broadcast(rb_nativethread_cond_t *cond) {}

void rb_native_cond_wait(rb_nativethread_cond_t *cond,
                         rb_nativethread_lock_t *mutex) {}

void rb_native_cond_timedwait(rb_nativethread_cond_t *cond,
                              rb_nativethread_lock_t *mutex,
                              unsigned long msec) {}

#define native_cleanup_push pthread_cleanup_push
#define native_cleanup_pop pthread_cleanup_pop

static pthread_key_t ruby_native_thread_key;

rb_thread_t *ruby_thread_from_native(void) { return NULL; }

int ruby_thread_set_native(rb_thread_t *th) { return 1; }

void Init_native_thread(rb_thread_t *th) {}

#define USE_THREAD_CACHE 0

static void native_thread_destroy(rb_thread_t *th) {}

#undef ruby_init_stack
void ruby_init_stack(volatile VALUE *addr) {}

static int native_thread_init_stack(rb_thread_t *th) { return 0; }

static int native_thread_create(rb_thread_t *th) {
  int err = 0;
  return err;
}

#define register_ubf_list(th) (void)(th)
#define unregister_ubf_list(th) (void)(th)
#define ubf_select 0
static void ubf_wakeup_all_threads(void) { return; }
static int ubf_threads_empty(void) { return 1; }
#define ubf_list_atfork()                                                      \
  do {                                                                         \
  } while (0)

#define TT_DEBUG 0
#define WRITE_CONST(fd, str) (void)(write((fd), (str), sizeof(str) - 1) < 0)

static struct {
  /* pipes are closed in forked children when owner_process does not match */
  int normal[2];  /* [0] == sigwait_fd */
  int ub_main[2]; /* unblock main thread from native_ppoll_sleep */

  /* volatile for signal handler use: */
  volatile rb_pid_t owner_process;
} signal_self_pipe = {
    {-1, -1},
    {-1, -1},
};

static void rb_thread_wakeup_timer_thread_fd(int fd) {}

static void ubf_timer_arm(rb_pid_t current) {}

void rb_thread_wakeup_timer_thread(int sig) {}

static VALUE threadptr_invoke_proc_location(rb_thread_t *th);

static void native_set_thread_name(rb_thread_t *th) {}

static void native_set_another_thread_name(rb_nativethread_id_t thread_id,
                                           VALUE name) {}

#define USE_NATIVE_THREAD_NATIVE_THREAD_ID 0

static void rb_thread_create_timer_thread(void) {}

static void ubf_timer_disarm(void) {}

static void ubf_timer_destroy(void) {}

static int native_stop_timer_thread(void) {
  int stopped;
  stopped = --system_working <= 0;
  if (stopped)
    ubf_timer_destroy();

  if (TT_DEBUG)
    fprintf(stderr, "stop timer thread\n");
  return stopped;
}

static void native_reset_timer_thread(void) {
  if (TT_DEBUG)
    fprintf(stderr, "reset timer thread\n");
}

int rb_reserved_fd_p(int fd) {
  /* no false-positive if out-of-FD at startup */
  if (fd < 0)
    return 0;

  if (fd == signal_self_pipe.normal[0] || fd == signal_self_pipe.normal[1])
    goto check_pid;
  if (fd == signal_self_pipe.ub_main[0] || fd == signal_self_pipe.ub_main[1])
    goto check_pid;
  return 0;
check_pid:
  // FIXME(katei):
  // if (signal_self_pipe.owner_process == getpid()) /* async-signal-safe */
  //   return 1;
  return 0;
}

rb_nativethread_id_t rb_nativethread_self(void) { return NULL; }

int rb_sigwait_fd_get(const rb_thread_t *th) {
  return -1; /* avoid thundering herd and work stealing/starvation */
}

void rb_sigwait_fd_put(const rb_thread_t *th, int fd) {}

void rb_sigwait_sleep(rb_thread_t *th, int sigwait_fd, const rb_hrtime_t *rel) {
}

static void native_sleep(rb_thread_t *th, rb_hrtime_t *rel) {}

static VALUE ubf_caller(void *ignore) {
  rb_thread_sleep_forever();

  return Qfalse;
}

/*
 * Called if and only if one thread is running, and
 * the unblock function is NOT async-signal-safe
 * This assumes USE_THREAD_CACHE is true for performance reasons
 */
static VALUE rb_thread_start_unblock_thread(void) {
  return rb_thread_create(ubf_caller, 0);
}
#endif /* THREAD_SYSTEM_DEPENDENT_IMPLEMENTATION */
