/* archpaper - Copyright (C) 2024 archpaper contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Synchronous core operations: callers can run the application flow on a worker. */
#include "archpaper/wallust.h"
#include "archpaper/process.h"
#include "archpaper/utils.h"

int wallust_available(void) {
    return ap_process_available("wallust");
}

int wallust_run(const char *image_path) {
    if (!image_path || !*image_path) return AP_INVALID;
    const char *args[] = {"wallust", "run", image_path, NULL};
    return ap_process_run(args, 60000, NULL, 0);
}

int wallust_hook_run(const char *hook_path, const char *image_path) {
    if (!hook_path || !*hook_path) return AP_OK;
    if (!image_path || !*image_path) return AP_INVALID;
    if (!file_exists(hook_path) || is_dir(hook_path)) return AP_NOT_FOUND;
    /* Only this fixed wrapper is parsed by sh. User paths stay positional
     * arguments; no interpolation into shell source or global setenv(). */
    const char *args[] = {"sh", "-c", "export WALLPAPER=\"$2\"; exec sh \"$1\" \"$2\"",
                          "archpaper-hook", hook_path, image_path, NULL};
    return ap_process_run(args, 60000, NULL, 0);
}
