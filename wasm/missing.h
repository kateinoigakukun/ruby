#ifndef RUBY_WASM_MISSING_H
#define RUBY_WASM_MISSING_H

#if defined(__wasi__)

#include <stdio.h>

#define WNOHANG 0

int posix_madvise(void *addr, size_t len, int advice);

uid_t geteuid(void);

uid_t getuid(void);

int getegid(void);

int getgid(void);

char *getlogin(void);

int umask(int mask);

int chmod(const char *pathname, int mode);

int chown(const char *pathname, int owner, int group);

int pclose(FILE *stream);

FILE *popen(const char *command, const char *type);

int pipe(int pipefd[2]);

int dup(int oldfd);

int dup2(int oldfd, int newfd);

int kill(pid_t pid, int sig);

void tzset(void);

int execl(const char *path, const char *arg, ...);

int execle(const char *path, const char *arg, ...);

int execv(const char *path, char *const argv[]);

int execve(const char *filename, char *const argv[], char *const envp[]);

int getppid(void);

int shutdown(int s, int how);

int system(const char *command);

#else
# error "wasm/missing.h only supports WASI platform"
#endif // defined(__wasi__)

#endif // RUBY_WASM_MISSING_H
