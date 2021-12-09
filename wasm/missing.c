#include <errno.h>
#include <sys/types.h>
#include "wasm/missing.h"

#define WASM_MISSING_LIBC_FUNC __attribute__((weak))

WASM_MISSING_LIBC_FUNC
int chmod(const char *pathname, int mode) {
  errno = ENOTSUP;
  return -1;
}

WASM_MISSING_LIBC_FUNC
int chown(const char *pathname, int owner, int group) {
  errno = ENOTSUP;
  return -1;
}

WASM_MISSING_LIBC_FUNC
int dup(int oldfd) {
  errno = ENOTSUP;
  return -1;
}

WASM_MISSING_LIBC_FUNC
int dup2(int oldfd, int newfd) {
  errno = ENOTSUP;
  return -1;
}

WASM_MISSING_LIBC_FUNC
int execl(const char *path, const char *arg, ...) {
  errno = ENOTSUP;
  return -1;
}

WASM_MISSING_LIBC_FUNC
int execle(const char *path, const char *arg, ...) {
  errno = ENOTSUP;
  return -1;
}

WASM_MISSING_LIBC_FUNC
int execv(const char *path, char *const argv[]) {
  errno = ENOTSUP;
  return -1;
}

WASM_MISSING_LIBC_FUNC
int execve(const char *filename, char *const argv[], char *const envp[]) {
  errno = ENOTSUP;
  return -1;
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
char *getlogin(void) {
  errno = ENOTSUP;
  return NULL;
}

WASM_MISSING_LIBC_FUNC
int umask(int mask) { return 0; }

WASM_MISSING_LIBC_FUNC
int mprotect(const void *addr, size_t len, int prot) { return 0; }

int pclose(FILE *stream) {
  errno = ENOTSUP;
  return -1;
}

FILE *popen(const char *command, const char *type) {
  errno = ENOTSUP;
  return NULL;
}

WASM_MISSING_LIBC_FUNC
int pipe(int pipefd[2]) {
  errno = ENOTSUP;
  return -1;
}

WASM_MISSING_LIBC_FUNC
int posix_madvise(void *addr, size_t len, int advice) {
  errno = ENOTSUP;
  return -1;
}

WASM_MISSING_LIBC_FUNC
int kill(pid_t pid, int sig) {
  errno = ENOTSUP;
  return -1;
}


WASM_MISSING_LIBC_FUNC
void tzset(void) { return; }

WASM_MISSING_LIBC_FUNC
int shutdown(int s, int how) {
  errno = ENOTSUP;
  return -1;
}

WASM_MISSING_LIBC_FUNC
int system(const char *command) {
  errno = ENOTSUP;
  return -1;
}
