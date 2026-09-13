#define _GNU_SOURCE
#include "archpaper/process.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

static _Thread_local int (*cancel_check)(void *);
static _Thread_local void *cancel_context;

void ap_process_set_cancel_check(int (*check)(void *), void *context) {
    cancel_check = check;
    cancel_context = context;
}

static int cancelled(void) { return cancel_check && cancel_check(cancel_context); }

static char *executable_path(const char *name) {
    if (!name || !*name) return NULL;
    if (strchr(name, '/')) {
        struct stat st;
        return access(name, X_OK) == 0 && stat(name, &st) == 0 && S_ISREG(st.st_mode)
                   ? strdup(name) : NULL;
    }
    const char *path = getenv("PATH");
    if (!path) path = "/usr/local/bin:/usr/bin:/bin";
    do {
        const char *end = strchr(path, ':');
        size_t len = end ? (size_t)(end - path) : strlen(path);
        char *full = malloc(len + strlen(name) + 3);
        if (!full) return NULL;
        if (len) snprintf(full, len + strlen(name) + 3, "%.*s/%s", (int)len, path, name);
        else sprintf(full, "./%s", name);
        struct stat st;
        if (access(full, X_OK) == 0 && stat(full, &st) == 0 && S_ISREG(st.st_mode)) return full;
        free(full);
        if (!end) break;
        path = end + 1;
    } while (1);
    return NULL;
}

int ap_process_available(const char *name) {
    char *path = executable_path(name);
    int found = path != NULL;
    free(path);
    return found;
}

static ap_result process_start(ap_process *p, const char *const argv[], char *out, size_t size, int merge_stderr) {
    if (!p || !argv || !argv[0] || (size && !out)) return AP_INVALID;
    *p = (ap_process){.pid = -1, .output_fd = -1, .exit_code = -1,
                       .output = out, .output_size = size, .output_tail = merge_stderr, .result = AP_BUSY};
    if (size) out[0] = '\0';
    if (cancelled()) return p->result = AP_CANCELLED;
    int pipefd[2] = {-1, -1};
    if (size && pipe2(pipefd, O_CLOEXEC) != 0) return p->result = AP_IO;
    posix_spawn_file_actions_t actions;
    posix_spawnattr_t attr;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        if (size) { close(pipefd[0]); close(pipefd[1]); }
        return p->result = AP_NOMEM;
    }
    if (posix_spawnattr_init(&attr) != 0) {
        posix_spawn_file_actions_destroy(&actions);
        if (size) { close(pipefd[0]); close(pipefd[1]); }
        return p->result = AP_NOMEM;
    }
    int err = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    if (!err && size && merge_stderr) err = posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDERR_FILENO);
    else if (!err) err = posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    if (!err && size) err = posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
    if (!err && size) err = posix_spawn_file_actions_addclose(&actions, pipefd[0]);
    if (!err && size && pipefd[1] != STDOUT_FILENO)
        err = posix_spawn_file_actions_addclose(&actions, pipefd[1]);
    if (!err && !size) err = posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    sigset_t empty, defaults;
    sigemptyset(&empty);
    sigemptyset(&defaults);
    sigaddset(&defaults, SIGTERM);
    sigaddset(&defaults, SIGINT);
    sigaddset(&defaults, SIGPIPE);
    if (!err) err = posix_spawnattr_setsigmask(&attr, &empty);
    if (!err) err = posix_spawnattr_setsigdefault(&attr, &defaults);
    if (!err) err = posix_spawnattr_setpgroup(&attr, 0);
    if (!err) err = posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
    if (!err) err = posix_spawnp(&p->pid, argv[0], &actions, &attr, (char *const *)argv, environ);
    posix_spawnattr_destroy(&attr);
    posix_spawn_file_actions_destroy(&actions);
    if (size) close(pipefd[1]);
    if (err) {
        if (size) close(pipefd[0]);
        p->pid = -1;
        return p->result = err == ENOENT ? AP_NOT_FOUND : AP_PROCESS;
    }
    if (size) {
        p->output_fd = pipefd[0];
        if (fcntl(p->output_fd, F_SETFL, O_NONBLOCK) < 0) {
            ap_process_cancel(p);
            return p->result = AP_IO;
        }
    }
    return AP_OK;
}

ap_result ap_process_start(ap_process *p, const char *const argv[], char *out, size_t size) {
    return process_start(p, argv, out, size, 0);
}

ap_result ap_process_start_logged(ap_process *p, const char *const argv[], char *out, size_t size) {
    return process_start(p, argv, out, size, 1);
}

static void drain_output(ap_process *p) {
    char buf[4096];
    /* Bound work per poll so a chatty process cannot defeat cancellation. */
    for (int i = 0; p->output_fd >= 0 && i < 64; ++i) {
        ssize_t n = read(p->output_fd, buf, sizeof(buf));
        if (n > 0) {
            if (p->output_observer) p->output_observer(buf, (size_t)n, p->output_context);
            size_t copy = (size_t)n;
            const char *source = buf;
            p->output_total += copy;
            if (p->output_tail) {
                size_t capacity = p->output_size - 1;
                if (copy > capacity) { source += copy - capacity; copy = capacity; }
                if (p->output_used + copy > capacity) {
                    size_t drop = p->output_used + copy - capacity;
                    memmove(p->output, p->output + drop, p->output_used - drop);
                    p->output_used -= drop;
                }
            }
            size_t left = p->output_size - p->output_used - 1;
            if (copy > left) copy = left;
            memcpy(p->output + p->output_used, source, copy);
            p->output_used += copy;
            p->output[p->output_used] = '\0';
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
                close(p->output_fd);
                p->output_fd = -1;
            }
            break;
        }
    }
}

ap_result ap_process_poll(ap_process *p) {
    if (!p) return AP_INVALID;
    if (p->pid <= 0) return p->result;
    drain_output(p);
    int status;
    pid_t rc = waitpid(p->pid, &status, WNOHANG);
    if (!rc || (rc < 0 && errno == EINTR)) return AP_BUSY;
    drain_output(p);
    if (p->output_fd >= 0) close(p->output_fd);
    p->output_fd = -1;
    p->pid = -1;
    p->exit_code = rc > 0 && WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return p->result = p->exit_code == 0 ? AP_OK : AP_PROCESS;
}

ap_result ap_process_cancel(ap_process *p) {
    if (!p) return AP_INVALID;
    if (p->pid <= 0) return p->result;
    kill(-p->pid, SIGKILL);
    while (waitpid(p->pid, NULL, 0) < 0 && errno == EINTR) {}
    p->pid = -1;
    if (p->output_fd >= 0) close(p->output_fd);
    p->output_fd = -1;
    return p->result = AP_CANCELLED;
}

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

ap_result ap_process_wait(ap_process *p, int timeout_ms) {
    if (!p || timeout_ms < 0) return AP_INVALID;
    long long deadline = now_ms() + timeout_ms;
    ap_result rc;
    while ((rc = ap_process_poll(p)) == AP_BUSY) {
        if (cancelled()) return ap_process_cancel(p);
        if (timeout_ms && now_ms() >= deadline) {
            ap_process_cancel(p);
            return p->result = AP_TIMEOUT;
        }
        struct pollfd fd = {.fd = p->output_fd, .events = POLLIN};
        poll(&fd, 1, 10);
    }
    return rc;
}

ap_result ap_process_run(const char *const argv[], int timeout_ms, char *out, size_t size) {
    if (timeout_ms < 0) return AP_INVALID;
    ap_process p;
    ap_result rc = ap_process_start(&p, argv, out, size);
    return rc == AP_OK ? ap_process_wait(&p, timeout_ms) : rc;
}

int ap_process_cancel_requested(void) { return cancelled(); }

ap_result ap_process_detach(const char *const argv[]) {
    if (!argv || !argv[0]) return AP_INVALID;
    if (cancelled()) return AP_CANCELLED;
    char *path = executable_path(argv[0]);
    if (!path) return AP_NOT_FOUND;
    int errors[2];
    if (pipe2(errors, O_CLOEXEC) != 0) { free(path); return AP_IO; }
    int nullfd = open("/dev/null", O_RDWR | O_CLOEXEC);
    if (nullfd < 0) { close(errors[0]); close(errors[1]); free(path); return AP_IO; }
    pid_t child = fork();
    if (child == 0) {
        close(errors[0]);
        if (setsid() < 0) goto fail;
        pid_t service = fork();
        if (service < 0) goto fail;
        if (service > 0) _exit(0);
        for (int fd = 0; fd < 3; ++fd) if (dup2(nullfd, fd) < 0) goto fail;
        if (nullfd > 2) close(nullfd);
        sigset_t empty;
        sigemptyset(&empty);
        sigprocmask(SIG_SETMASK, &empty, NULL);
        struct sigaction action = {0};
        action.sa_handler = SIG_DFL;
        sigemptyset(&action.sa_mask);
        sigaction(SIGTERM, &action, NULL);
        sigaction(SIGINT, &action, NULL);
        sigaction(SIGPIPE, &action, NULL);
        sigaction(SIGCHLD, &action, NULL);
        execve(path, (char *const *)argv, environ);
fail:;
        int error = errno;
        (void)write(errors[1], &error, sizeof(error));
        _exit(127);
    }
    close(nullfd);
    close(errors[1]);
    free(path);
    if (child < 0) { close(errors[0]); return AP_PROCESS; }
    int error = 0;
    ssize_t n;
    do { n = read(errors[0], &error, sizeof(error)); } while (n < 0 && errno == EINTR);
    close(errors[0]);
    int status = 0;
    pid_t waited;
    do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
    if (n != 0 || waited < 0 || !WIFEXITED(status) || WEXITSTATUS(status))
        return error == ENOENT ? AP_NOT_FOUND : AP_PROCESS;
    return AP_OK;
}
