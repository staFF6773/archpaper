#ifndef ARCHPAPER_ERROR_H
#define ARCHPAPER_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AP_OK = 0,
    AP_INVALID,
    AP_IO,
    AP_NOMEM,
    AP_NOT_FOUND,
    AP_PROCESS,
    AP_TIMEOUT,
    AP_CANCELLED,
    AP_BUSY
} ap_result;

const char *ap_error_string(ap_result result);

#ifdef __cplusplus
}
#endif
#endif
