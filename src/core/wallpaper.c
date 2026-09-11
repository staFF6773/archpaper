#define _GNU_SOURCE
#include "archpaper/wallpaper.h"
#include "archpaper/history.h"
#include "archpaper/storage.h"
#include "archpaper/utils.h"
#include "archpaper/wallust.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

static ap_result application_lock(int *fd) {
    char path[4096];
    ap_result rc = ap_runtime_path("apply.lock", path, sizeof(path));
    if (rc != AP_OK) return rc;
    *fd = open(path, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (*fd < 0) return AP_IO;
    if (flock(*fd, LOCK_EX | LOCK_NB) != 0) { close(*fd); return AP_BUSY; }
    return AP_OK;
}

ap_result ap_wallpaper_apply(const char *path, const config_t *options, unsigned flags, ap_apply_result *out) {
    if (!out) return AP_INVALID;
    *out = (ap_apply_result){0};
    if (config_validate(options) != AP_OK || !path || (flags & ~AP_APPLY_SAVE_OPTIONS)) return AP_INVALID;
    char *expanded = expand_path(path);
    if (!expanded) return AP_NOMEM;
    char *absolute = realpath(expanded, NULL);
    free(expanded);
    if (!absolute) return AP_NOT_FOUND;
    struct stat st;
    if (stat(absolute, &st) != 0 || !S_ISREG(st.st_mode)) { free(absolute); return AP_NOT_FOUND; }
    config_t effective = *options;
    ap_result rc = ap_copy_string(effective.last_wallpaper, sizeof(effective.last_wallpaper), absolute);
    if (rc != AP_OK || strchr(absolute, '\n') || strchr(absolute, '\r') ||
        (!is_image(absolute) && !is_video(absolute))) { free(absolute); return AP_INVALID; }
    effective.backend = select_backend_for_path(absolute, options->backend);
    out->backend = effective.backend;
    int lock;
    rc = application_lock(&lock);
    if (rc != AP_OK) { free(absolute); return rc; }
    rc = backend_apply(absolute, &effective);
    if (rc != AP_OK) goto done;
    out->applied = 1;
    /* Preserve the latest folder list, which may have changed while preparing
     * a conversion. Persist the preferred backend, not a one-file fallback. */
    out->persistence = config_record_wallpaper(absolute, flags & AP_APPLY_SAVE_OPTIONS ? options : NULL);
    ap_result history = ap_recent_add(absolute);
    if (out->persistence == AP_OK) out->persistence = history;
    if (options->wallust_enabled) {
        out->wallust_missing = !wallust_available();
        out->theme = out->wallust_missing ? AP_NOT_FOUND : wallust_run(absolute);
        /* A failed wallust run must not trigger a hook against incomplete output.
         * With wallust absent, preserve support for a standalone extra hook. */
        if (out->theme == AP_OK || out->wallust_missing) {
            ap_result hook = wallust_hook_run(options->wallust_hook, absolute);
            if (hook != AP_OK) out->theme = hook;
        }
    }
done:
    close(lock);
    free(absolute);
    return rc;
}

ap_result ap_wallpaper_clear(void) {
    int lock;
    ap_result rc = application_lock(&lock);
    if (rc != AP_OK) return rc;
    rc = clear_wallpaper();
    if (rc == AP_OK) rc = config_record_wallpaper("", NULL);
    close(lock);
    return rc;
}
