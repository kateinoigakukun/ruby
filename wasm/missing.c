#include <errno.h>
#include <sys/types.h>

#define WASM_MISSING_LIBC_FUNC __attribute__((weak))

WASM_MISSING_LIBC_FUNC
int chmod(const char *pathname, int mode) { return 0; }

WASM_MISSING_LIBC_FUNC
int chown(const char *pathname, int owner, int group) { return 0; }

WASM_MISSING_LIBC_FUNC
int dup(int oldfd) { return ENOTSUP; }

WASM_MISSING_LIBC_FUNC
int dup2(int oldfd, int newfd) { return ENOTSUP; }

WASM_MISSING_LIBC_FUNC
int execl(const char *path, const char *arg, ...) { return ENOTSUP; }

WASM_MISSING_LIBC_FUNC
int execle(const char *path, const char *arg, ...) { return ENOTSUP; }

WASM_MISSING_LIBC_FUNC
int execv(const char *path, char *const argv[]) { return ENOTSUP; }

WASM_MISSING_LIBC_FUNC
int execve(const char *filename, char *const argv[], char *const envp[]) {
  return ENOTSUP;
}

WASM_MISSING_LIBC_FUNC
uid_t geteuid(void) { return 0; }

WASM_MISSING_LIBC_FUNC
uid_t getuid(void) { return 0; }

WASM_MISSING_LIBC_FUNC
int getppid() { return 0; }

WASM_MISSING_LIBC_FUNC
int getegid(void) { return 0; }

WASM_MISSING_LIBC_FUNC
int getgid(void) { return 0; }

WASM_MISSING_LIBC_FUNC
char *getlogin(void) { return 0; }

WASM_MISSING_LIBC_FUNC
int umask(int mask) { return 0; }

WASM_MISSING_LIBC_FUNC
int mprotect(const void *addr, size_t len, int prot) { return 0; }

WASM_MISSING_LIBC_FUNC
int pipe(int pipefd[2]) { return ENOTSUP; }

WASM_MISSING_LIBC_FUNC
int posix_madvise(void *addr, size_t len, int advice) { return 0; }

WASM_MISSING_LIBC_FUNC
int kill(pid_t pid, int sig) { return 0; }

WASM_MISSING_LIBC_FUNC
void tzset(void) { return; }

WASM_MISSING_LIBC_FUNC
int shutdown(int s, int how) { return ENOTSUP; }

WASM_MISSING_LIBC_FUNC
int system(const char *command) { return ENOTSUP; }
