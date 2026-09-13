#ifndef ARCHPAPER_PROCESS_H
#define ARCHPAPER_PROCESS_H

#include <stddef.h>
#include <sys/types.h>
#include "error.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Each handle belongs to one caller. Keep the output buffer alive until done.
 * Output is NUL-terminated; excess output is drained and discarded.
 * poll returns AP_BUSY while running, then the final result. Always wait or
 * cancel a started process to reap it. No shell parsing is performed. */
typedef struct {
    pid_t pid;
    int output_fd;
    int exit_code;
    char *output;
    size_t output_size;
    size_t output_used;
    size_t output_total;
    int output_tail;
    /* Optional borrowed observer for all output bytes, including bytes which
     * will not fit in the capture buffer. Called synchronously during poll. */
    void (*output_observer)(const char *data, size_t size, void *context);
    void *output_context;
    ap_result result;
} ap_process;

int ap_process_available(const char *name);
/* Optional per-thread cancellation check, invoked between process polls.
 * Callback/context are borrowed; clear them before their lifetime ends. */
void ap_process_set_cancel_check(int (*check)(void *), void *context);
int ap_process_cancel_requested(void);
ap_result ap_process_start(ap_process *process, const char *const argv[],
                           char *output, size_t output_size);
/* Same lifecycle, but captures stderr together with stdout and retains the
 * most recent output when full, so fatal errors survive verbose startup logs. */
ap_result ap_process_start_logged(ap_process *process, const char *const argv[],
                                  char *output, size_t output_size);
ap_result ap_process_poll(ap_process *process);
ap_result ap_process_wait(ap_process *process, int timeout_ms);
ap_result ap_process_cancel(ap_process *process);
ap_result ap_process_run(const char *const argv[], int timeout_ms,
                         char *output, size_t output_size);
/* Detached services: confirms exec succeeded, not that the service is ready. */
ap_result ap_process_detach(const char *const argv[]);

#ifdef __cplusplus
}
#endif
#endif
