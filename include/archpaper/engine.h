#ifndef ARCHPAPER_ENGINE_H
#define ARCHPAPER_ENGINE_H

#include "library.h"
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { AP_ENGINE_UNSUPPORTED, AP_ENGINE_VIDEO, AP_ENGINE_SCENE } ap_engine_type;
typedef struct {
    char manifest[4096];
    char directory[4096];
    char title[1024];
    char type_name[64];
    char file[4096];
    char preview[4096];
    ap_engine_type type;
} ap_engine_project;

/* A directory containing project.json, or project.json itself. No parsing. */
int ap_engine_is_project(const char *path);
/* Fixed-size, caller-owned metadata; paths are canonical. Unsupported types
 * still return metadata for display. Missing optional previews are allowed. */
ap_result ap_engine_read(const char *path, ap_engine_project *out);
/* Playback-only compatibility overlay for known text/script/shader variants.
 * Unchanged projects return directory.
 * Release the overlay after the engine exits; Workshop files are never edited. */
ap_result ap_engine_prepare(const ap_engine_project *project, char *out, size_t size);
void ap_engine_cleanup(const ap_engine_project *project, const char *prepared);
/* Existing Workshop folders, deduplicated across native/Flatpak Steam aliases. */
ap_result ap_engine_discover(ap_path_list *out);
ap_result ap_engine_assets(const config_t *cfg, char *out, size_t size);
ap_result ap_engine_outputs(const config_t *cfg, ap_path_list *out);
int ap_engine_parse_fps(const char *value, int *out);

/* Managed service: lock owner is an exec'd supervisor, never a saved PID. */
ap_result ap_engine_start(const char *const args[]);
ap_result ap_engine_stop(void);
ap_result ap_engine_status(int *pid);
int ap_engine_run(const char *const args[]);
ap_result ap_engine_diagnostic(char *out, size_t size);

#ifdef __cplusplus
}
#endif
#endif
