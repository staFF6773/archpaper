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
    }
    return "Unknown error";
}
