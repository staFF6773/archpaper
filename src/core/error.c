#include "archpaper/error.h"

const char *ap_error_string(ap_result result) {
    switch (result) {
    case AP_OK: return "Success";
    case AP_INVALID: return "Invalid value or path";
    case AP_IO: return "Could not read or write application data";
    case AP_NOMEM: return "Not enough memory";
    case AP_NOT_FOUND: return "File or executable not found";
    case AP_PROCESS: return "External program failed";
    case AP_TIMEOUT: return "Operation timed out";
    case AP_CANCELLED: return "Operation cancelled";
    case AP_BUSY: return "Operation already running";
    case AP_UNSUPPORTED: return "Unsupported or incomplete Wallpaper Engine project (scene/video required)";
    case AP_ENGINE_MISSING: return "Install linux-wallpaperengine-git to play Wallpaper Engine scenes";
    case AP_ASSETS_MISSING: return "Wallpaper Engine assets not found; set the assets directory in Settings or --engine-assets";
    case AP_OUTPUT_MISSING: return "Could not detect Hyprland monitors; set a monitor in Settings or --engine-output";
    }
    return "Unknown error";
}
