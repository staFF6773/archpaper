#ifndef ARCHPAPER_WALLPAPER_H
#define ARCHPAPER_WALLPAPER_H

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int applied;
    backend_t backend;
    ap_result persistence;
    ap_result theme;
    int wallust_missing;
    /* Scene startup failure: original error output and best-effort undo. */
    int restored;
    ap_result recovery;
    char diagnostic[8192];
} ap_apply_result;

enum { AP_APPLY_SAVE_OPTIONS = 1u };

/* Shared CLI/GUI/daemon transaction. Validates and resolves an absolute path,
 * chooses a compatible backend, applies, then records config/history and runs
 * wallust followed by the hook. AP_OK means the wallpaper was applied; inspect
 * persistence/theme for non-fatal post-apply failures. Synchronous and bounded;
 * a GUI should call this on its worker thread with a configuration snapshot.
 * flags=0 records only the last wallpaper/history, preserving current settings.
 * AP_APPLY_SAVE_OPTIONS also persists explicit CLI option overrides. */
ap_result ap_wallpaper_apply(const char *path, const config_t *options, unsigned flags, ap_apply_result *out);
ap_result ap_wallpaper_clear(void);
/* Supervisor-only recovery after an unexpected scene exit. Checks the saved
 * identity under the apply lock so a newer wallpaper is never overwritten. */
ap_result ap_wallpaper_recover(const char *failed, const config_t *previous, const char *diagnostic);

#ifdef __cplusplus
}
#endif
#endif
