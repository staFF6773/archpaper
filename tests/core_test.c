#define _GNU_SOURCE
#include "archpaper/cache.h"
#include "archpaper/config.h"
#include "archpaper/daemon.h"
#include "archpaper/history.h"
#include "archpaper/library.h"
#include "archpaper/process.h"
#include "archpaper/storage.h"
#include "archpaper/utils.h"
#include "archpaper/wallpaper.h"
#include "archpaper/engine.h"

#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <json-c/json.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); } } while (0)
static char root[] = "/tmp/archpaper-core-test-XXXXXX";
static char media_dir[4096], first[4096], second[4096], log_path[4096];
static const char *helper, *cli;

static void write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "w");
    CHECK(f != NULL);
    CHECK(fputs(text, f) >= 0);
    CHECK(fclose(f) == 0);
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "r");
    CHECK(f != NULL);
    CHECK(fseek(f, 0, SEEK_END) == 0);
    long size = ftell(f);
    CHECK(size >= 0);
    rewind(f);
    char *text = calloc((size_t)size + 1, 1);
    CHECK(text != NULL);
    CHECK(fread(text, 1, (size_t)size, f) == (size_t)size);
    fclose(f);
    return text;
}

static void setup(void) {
    CHECK(mkdtemp(root) != NULL);
    CHECK(setenv("HOME", root, 1) == 0);
    const char *vars[] = {"XDG_CONFIG_HOME", "XDG_CACHE_HOME", "XDG_RUNTIME_DIR", "PATH"};
    const char *dirs[] = {"config", "cache", "runtime", "bin"};
    char path[4096];
    for (size_t i = 0; i < 4; ++i) {
        snprintf(path, sizeof(path), "%s/%s", root, dirs[i]);
        CHECK(mkdir(path, 0700) == 0);
        CHECK(setenv(vars[i], path, 1) == 0);
    }
    const char *commands[] = {"awww", "awww-daemon", "swaybg", "hyprpaper", "mpvpaper", "pkill", "wallust", "ffprobe", "ffmpeg", "ffmpegthumbnailer", "linux-wallpaperengine", "hyprctl", NULL};
    for (size_t i = 0; commands[i]; ++i) {
        snprintf(path, sizeof(path), "%s/bin/%s", root, commands[i]);
        CHECK(symlink(helper, path) == 0);
    }
    snprintf(path, sizeof(path), "%s/bin/sh", root);
    CHECK(symlink("/bin/sh", path) == 0);
    snprintf(log_path, sizeof(log_path), "%s/process.log", root);
    CHECK(setenv("AP_TEST_LOG", log_path, 1) == 0);
    snprintf(media_dir, sizeof(media_dir), "%s/wallpapers", root);
    CHECK(mkdir(media_dir, 0700) == 0);
    snprintf(first, sizeof(first), "%s/wallpapers/a 'quote' $dollar; image.PNG", root);
    snprintf(second, sizeof(second), "%s/wallpapers/b video.MP4", root);
    write_file(first, "image");
    write_file(second, "video");
}

static ap_result failed_writer(FILE *f, const void *data) {
    (void)data;
    fputs("partial", f);
    return AP_IO;
}

static void test_config(void) {
    config_t cfg, loaded;
    CHECK(config_load(&cfg) == AP_OK);
    CHECK(cfg.daemon_interval == 300 && !strcmp(cfg.mode, "fill"));
    CHECK(config_add_folder(&cfg, media_dir) == AP_OK);
    CHECK(config_add_folder(&cfg, media_dir) == AP_OK && cfg.folder_count == 1);
    CHECK(config_save(&cfg) == AP_OK);
    CHECK(config_load(&loaded) == AP_OK && loaded.folder_count == 1);
    int interval = 50;
    CHECK(config_parse_interval("300junk", &interval) == AP_INVALID && interval == 50);
    CHECK(config_parse_interval("999999999999999999999", &interval) == AP_INVALID);
    CHECK(config_parse_interval("9", &interval) == AP_INVALID);
    char path[4096];
    CHECK(ap_config_path("config", path, sizeof(path)) == AP_OK);
    char *before = read_file(path);
    cfg.daemon_interval = -1;
    CHECK(config_save(&cfg) == AP_INVALID);
    char *after = read_file(path);
    CHECK(!strcmp(before, after));
    free(after);
    CHECK(ap_write_atomic(path, failed_writer, NULL) == AP_IO);
    after = read_file(path);
    CHECK(!strcmp(before, after));
    free(after);
    write_file(path, "mode=garbage\n");
    CHECK(config_load(&loaded) == AP_INVALID && !strcmp(loaded.mode, "fill"));
    write_file(path, before);
    free(before);
    char oversized[5000];
    memset(oversized, 'x', sizeof(oversized) - 1); oversized[sizeof(oversized) - 1] = 0;
    CHECK(config_add_folder(&loaded, oversized) == AP_INVALID);
    CHECK(config_add_folder(&loaded, "bad\nfolder") == AP_INVALID);
    CHECK(ap_copy_string(loaded.mode, sizeof(loaded.mode), oversized) == AP_INVALID);
    CHECK(ap_config_path("config", path, 3) == AP_INVALID);
    CHECK(setenv("XDG_CONFIG_HOME", "", 1) == 0);
    CHECK(ap_config_path("config", path, sizeof(path)) == AP_OK && strstr(path, "/.config/archpaper/config"));
    snprintf(path, sizeof(path), "%s/config", root);
    setenv("XDG_CONFIG_HOME", path, 1);
    puts("PASS configuration validation and atomic persistence");
}

static int cancel_now(void *ctx) { return *(int *)ctx; }

static void test_process(void) {
    char output[128];
    const char *echo[] = {helper, "echo", "spaces 'quotes' \"double\" $dollar; $(literal)", NULL};
    CHECK(ap_process_run(echo, 2000, output, sizeof(output)) == AP_OK);
    CHECK(!strcmp(output, echo[2]));
    const char *missing[] = {"does-not-exist", NULL};
    CHECK(ap_process_run(missing, 1000, NULL, 0) == AP_NOT_FOUND);
    const char *fail[] = {helper, "fail", NULL};
    ap_process process;
    CHECK(ap_process_start(&process, fail, NULL, 0) == AP_OK);
    CHECK(ap_process_wait(&process, 1000) == AP_PROCESS && process.exit_code == 7);
    const char *sleep[] = {helper, "sleep", NULL};
    CHECK(ap_process_run(sleep, 40, NULL, 0) == AP_TIMEOUT);
    CHECK(ap_process_start(&process, sleep, NULL, 0) == AP_OK);
    CHECK(ap_process_poll(&process) == AP_BUSY);
    CHECK(ap_process_cancel(&process) == AP_CANCELLED && process.pid == -1);
    const char *flood[] = {helper, "flood", NULL};
    CHECK(ap_process_run(flood, 3000, output, sizeof(output)) == AP_OK);
    CHECK(strlen(output) == sizeof(output) - 1);
    CHECK(ap_process_start_logged(&process, flood, output, sizeof(output)) == AP_OK);
    CHECK(ap_process_wait(&process, 3000) == AP_OK && strstr(output, "Final diagnostic"));
    CHECK(ap_process_start_logged(&process, flood, output, 1) == AP_OK);
    CHECK(ap_process_wait(&process, 3000) == AP_OK && !output[0]);
    int cancel = 1;
    ap_process_set_cancel_check(cancel_now, &cancel);
    CHECK(ap_process_run(echo, 1000, NULL, 0) == AP_CANCELLED);
    ap_process_set_cancel_check(NULL, NULL);
    char bad[4096];
    snprintf(bad, sizeof(bad), "%s/bad-executable", root);
    write_file(bad, "not an executable format\n");
    CHECK(chmod(bad, 0700) == 0);
    const char *broken[] = {bad, NULL};
    CHECK(ap_process_detach(broken) == AP_PROCESS);
    CHECK(ap_process_detach(missing) == AP_NOT_FOUND);
    puts("PASS process arguments, failures, output draining, timeouts and cancellation");
}

static void test_library_history(void) {
    char path[4096];
    snprintf(path, sizeof(path), "%s/wallpapers/fake.jpg", root);
    CHECK(mkdir(path, 0700) == 0);
    snprintf(path, sizeof(path), "%s/wallpapers/.hidden.png", root);
    write_file(path, "hidden");
    snprintf(path, sizeof(path), "%s/wallpapers/readme.txt", root);
    write_file(path, "text");
    ap_path_list list = {0};
    CHECK(ap_library_scan(media_dir, &list) == AP_OK && list.count == 2);
    CHECK(ap_library_matches(first, "QUOTE") && !ap_library_matches(first, "video"));
    for (int i = 0; i < 30; ++i) {
        char *selected = NULL;
        CHECK(ap_library_random(media_dir, &selected) == AP_OK);
        CHECK(ap_path_list_contains(&list, selected));
        free(selected);
    }
    ap_path_list_free(&list);
    CHECK(ap_path_list_append(&list, first) == AP_OK);
    CHECK(ap_path_list_append(&list, first) == AP_OK);
    ap_path_list_remove(&list, list.paths[0]);
    CHECK(list.count == 0);
    ap_path_list_free(&list);
    int favorite;
    CHECK(ap_favorite_toggle(first, &favorite) == AP_OK && favorite);
    CHECK(ap_history_load(AP_FAVORITES, &list) == AP_OK && list.count == 1 && !strcmp(list.paths[0], first));
    CHECK(ap_favorite_toggle(first, &favorite) == AP_OK && !favorite);
    pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0) _exit(ap_favorite_toggle(first, NULL));
    CHECK(ap_favorite_toggle(second, NULL) == AP_OK);
    int status;
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    CHECK(ap_history_load(AP_FAVORITES, &list) == AP_OK && list.count == 2);
    for (int i = 0; i < 60; ++i) {
        snprintf(path, sizeof(path), "%s/recent-%d.png", root, i);
        write_file(path, "image");
        CHECK(ap_recent_add(path) == AP_OK);
    }
    CHECK(ap_history_load(AP_RECENT, &list) == AP_OK && list.count == 50 && !strcmp(list.paths[0], path));
    CHECK(ap_recent_add(first) == AP_OK && ap_recent_add(first) == AP_OK);
    CHECK(ap_history_load(AP_RECENT, &list) == AP_OK && list.count == 50 && !strcmp(list.paths[0], first));
    ap_path_list_free(&list);
    puts("PASS library scanning, reservoir selection, concurrent favorites and bounded history");
}

static void test_apply_cache(void) {
    config_t cfg, loaded;
    CHECK(config_load(&cfg) == AP_OK);
    cfg.backend = BACKEND_SWWW;
    strcpy(cfg.cache_quality, "original");
    strcpy(cfg.last_wallpaper, "unchanged.png");
    CHECK(config_save(&cfg) == AP_OK);
    ap_apply_result result;
    setenv("AP_TEST_BACKEND_FAIL", "1", 1);
    CHECK(ap_wallpaper_apply(first, &cfg, AP_APPLY_SAVE_OPTIONS, &result) == AP_PROCESS && !result.applied);
    CHECK(config_load(&loaded) == AP_OK && !strcmp(loaded.last_wallpaper, "unchanged.png"));
    unsetenv("AP_TEST_BACKEND_FAIL");
    char done[4096], hook[4096], hook_output[4096];
    snprintf(done, sizeof(done), "%s/theme-done", root);
    snprintf(hook, sizeof(hook), "%s/hook 'quoted'.sh", root);
    snprintf(hook_output, sizeof(hook_output), "%s/hook-output", root);
    setenv("AP_TEST_THEME_DONE", done, 1);
    setenv("AP_TEST_HOOK_OUTPUT", hook_output, 1);
    write_file(hook, "test -f \"$AP_TEST_THEME_DONE\" || exit 12\n"
                     "test \"$1\" = \"$WALLPAPER\" || exit 13\n"
                     "printf '%s' \"$WALLPAPER\" > \"$AP_TEST_HOOK_OUTPUT\"\n");
    cfg.wallust_enabled = 1;
    strcpy(cfg.wallust_hook, hook);
    CHECK(ap_wallpaper_apply(first, &cfg, AP_APPLY_SAVE_OPTIONS, &result) == AP_OK && result.applied);
    CHECK(result.theme == AP_OK && result.persistence == AP_OK);
    char *text = read_file(hook_output);
    CHECK(!strcmp(text, first));
    free(text);
    CHECK(config_load(&loaded) == AP_OK && !strcmp(loaded.last_wallpaper, first));
    write_file(hook, "exit 7\n");
    CHECK(ap_wallpaper_apply(first, &cfg, AP_APPLY_SAVE_OPTIONS, &result) == AP_OK && result.theme == AP_PROCESS);
    cfg.wallust_enabled = 0;
    strcpy(cfg.mode, "fit");
    CHECK(ap_wallpaper_apply(first, &cfg, 0, &result) == AP_OK);
    CHECK(config_load(&loaded) == AP_OK && !strcmp(loaded.mode, "fill"));
    CHECK(config_save(&cfg) == AP_OK);
    char cached[4096];
    setenv("AP_TEST_LARGE", "1", 1);
    CHECK(ap_cache_prepare(second, "monitor", cached, sizeof(cached)) == AP_OK);
    CHECK(strcmp(cached, second) && strstr(cached, "/cache/archpaper/videos/") && file_exists(cached));
    CHECK(unlink(cached) == 0);
    setenv("AP_TEST_FFMPEG_FAIL", "1", 1);
    CHECK(ap_cache_prepare(second, "monitor", cached, sizeof(cached)) == AP_OK && !strcmp(cached, second));
    unsetenv("AP_TEST_FFMPEG_FAIL");
    unsetenv("AP_TEST_LARGE");
    CHECK(ap_wallpaper_clear() == AP_OK);
    CHECK(config_load(&loaded) == AP_OK && !loaded.last_wallpaper[0]);
    puts("PASS shared application, post-success persistence, ordered hooks and atomic cache fallback");
}

static void test_cli_daemon(void) {
    char output[8192];
    const char *list[] = {cli, "list", media_dir, NULL};
    CHECK(ap_process_run(list, 2000, output, sizeof(output)) == AP_OK && strstr(output, first) && strstr(output, second));
    const char *invalid[] = {cli, "set", first, "--mode", "invalid", NULL};
    CHECK(ap_process_run(invalid, 2000, NULL, 0) == AP_PROCESS);
    setenv("AP_TEST_BLOCK", "1", 1);
    const char *start[] = {cli, "daemon", media_dir, "--interval", "10", NULL};
    CHECK(ap_process_run(start, 3000, output, sizeof(output)) == AP_OK);
    int pid;
    CHECK(daemon_status(&pid) == AP_OK && pid > 0);
    ap_process process;
    CHECK(ap_process_start(&process, start, NULL, 0) == AP_OK);
    CHECK(ap_process_wait(&process, 3000) == AP_PROCESS && process.exit_code == AP_BUSY);
    usleep(200000);
    CHECK(daemon_stop() == AP_OK);
    CHECK(daemon_status(&pid) == AP_OK && pid == 0);
    CHECK(daemon_stop() == AP_OK);
    unsetenv("AP_TEST_BLOCK");
    puts("PASS C CLI and daemon start/status/single-instance/stop");
}

static int remove_entry(const char *path, const struct stat *st, int flag, struct FTW *state) {
    (void)st; (void)flag; (void)state;
    return remove(path);
}

static void put_u32(FILE *f, uint32_t n) {
    unsigned char bytes[] = {n & 255, (n >> 8) & 255, (n >> 16) & 255, (n >> 24) & 255};
    CHECK(fwrite(bytes, 1, 4, f) == 4);
}

static void test_engine_compat(const char *scene) {
    const char *json = "{\"objects\":[{\"text\":\"Clock\",\"padding\":\"32.00000 32.00000\"},"
        "{\"text\":\"Asymmetric\",\"padding\":\"12 24\"},"
        "{\"image\":\"model.json\",\"padding\":\"32 32\"},"
        "{\"text\":\"Invalid\",\"padding\":\"nan nan\"}]}";
    char path[8192], prepared[4096];
    snprintf(path, sizeof(path), "%s/scene.pkg", scene);
    FILE *f = fopen(path, "wb"); CHECK(f);
    put_u32(f, 8); CHECK(fwrite("PKGV0024", 1, 8, f) == 8);
    put_u32(f, 1); put_u32(f, 10); CHECK(fwrite("scene.json", 1, 10, f) == 10);
    put_u32(f, 0); put_u32(f, (uint32_t)strlen(json));
    CHECK(fputs(json, f) >= 0 && fclose(f) == 0);
    ap_engine_project project;
    CHECK(ap_engine_read(scene, &project) == AP_OK);
    CHECK(ap_engine_prepare(&project, prepared, sizeof(prepared)) == AP_OK && strcmp(prepared, scene));
    snprintf(path, sizeof(path), "%s/project.json", prepared);
    json_object *manifest = json_object_from_file(path), *file;
    CHECK(manifest && json_object_object_get_ex(manifest, "file", &file));
    snprintf(path, sizeof(path), "%s/%s", prepared, json_object_get_string(file));
    json_object *fixed = json_object_from_file(path), *objects, *padding;
    CHECK(fixed && json_object_object_get_ex(fixed, "objects", &objects));
    CHECK(json_object_object_get_ex(json_object_array_get_idx(objects, 0), "padding", &padding));
    CHECK(json_object_is_type(padding, json_type_int) && json_object_get_int(padding) == 32);
    for (int i = 1; i < 4; ++i) {
        CHECK(json_object_object_get_ex(json_object_array_get_idx(objects, i), "padding", &padding));
        CHECK(json_object_is_type(padding, json_type_string));
    }
    json_object_put(fixed); json_object_put(manifest);
    snprintf(path, sizeof(path), "%s/scene.pkg", prepared);
    struct stat st; CHECK(lstat(path, &st) == 0 && S_ISLNK(st.st_mode));
    ap_engine_cleanup(&project, prepared);
    CHECK(!file_exists(prepared) && file_exists(project.manifest));
    /* Loose scenes take precedence; unsupported layouts remain untouched. */
    snprintf(path, sizeof(path), "%s/scene.json", scene);
    write_file(path, "{\"objects\":[{\"text\":\"Clock\",\"padding\":32}]}");
    CHECK(ap_engine_prepare(&project, prepared, sizeof(prepared)) == AP_OK && !strcmp(prepared, scene));
    ap_engine_cleanup(&project, prepared);
    CHECK(file_exists(path));
    write_file(path, json);
    CHECK(ap_engine_prepare(&project, prepared, sizeof(prepared)) == AP_OK && strcmp(prepared, scene));
    ap_engine_cleanup(&project, prepared);
    char *original = read_file(path); CHECK(!strcmp(original, json)); free(original);
    CHECK(unlink(path) == 0);
    /* Truncated package metadata cannot cause an oversized read or allocation. */
    snprintf(path, sizeof(path), "%s/scene.pkg", scene);
    CHECK(truncate(path, 25) == 0);
    CHECK(ap_engine_prepare(&project, prepared, sizeof(prepared)) == AP_OK && !strcmp(prepared, scene));
    write_file(path, "package");
}

static void test_engine_shader_compat(const char *scene) {
    const char *names[] = {"scene.json",
        "shaders/workshop/2973943998/effects/iris_movement__.vert",
        "shaders/workshop/3082978660/effects/Simple_Audio_Bars.vert",
        "shaders/workshop/3082978660/effects/Simple_Audio_Bars.frag"};
    const char *sources[] = {"{\"objects\":[]}",
        "uniform vec2 g_CursorScaleLimit;\r\n#if FOLLOWCURSOR\r\nvoid main() {\r\n"
        "vec4 transformedCursorPosition;\r\nvec2 da = transformedCursorPosition * g_CursorScale;\r\n"
        "#endif\r\n#endif\r\n}\r\n",
        "#if DEFORMITY\nfloat i_DCorrectingFactor;\n#endif\n#endif\n",
        "varying vec2 v_TexCoord;\nvoid main() {\nv_TexCoord = v_TexCoord.yx;\n}\n"};
    char path[8192], prepared[4096];
    snprintf(path, sizeof(path), "%s/scene.pkg", scene);
    FILE *f = fopen(path, "wb"); CHECK(f);
    put_u32(f, 8); CHECK(fwrite("PKGV0023", 1, 8, f) == 8); put_u32(f, 4);
    uint32_t offset = 0;
    for (int i = 0; i < 4; ++i) {
        put_u32(f, (uint32_t)strlen(names[i])); CHECK(fputs(names[i], f) >= 0);
        put_u32(f, offset); put_u32(f, (uint32_t)strlen(sources[i])); offset += (uint32_t)strlen(sources[i]);
    }
    for (int i = 0; i < 4; ++i) CHECK(fputs(sources[i], f) >= 0);
    CHECK(fclose(f) == 0);
    /* Copy-on-write must preserve a linked, already existing zcompat tree. */
    snprintf(path, sizeof(path), "%s/zcompat/scene/shaders", scene); CHECK(ap_mkdirs(path) == AP_OK);
    snprintf(path, sizeof(path), "%s/zcompat/scene/shaders/keep.txt", scene); write_file(path, "original");
    ap_engine_project project;
    CHECK(ap_engine_read(scene, &project) == AP_OK);
    CHECK(ap_engine_prepare(&project, prepared, sizeof(prepared)) == AP_OK && strcmp(prepared, scene));
    snprintf(path, sizeof(path), "%s/zcompat/scene/shaders/2973943998/iris_movement__.vert", prepared);
    char *text = read_file(path);
    CHECK(strstr(text, "transformedCursorPosition.xy * g_CursorScale"));
    char *endif = strstr(text, "#endif"); CHECK(endif && !strstr(endif + 6, "#endif")); free(text);
    snprintf(path, sizeof(path), "%s/zcompat/scene/shaders/3082978660/Simple_Audio_Bars.vert", prepared);
    text = read_file(path); endif = strstr(text, "#endif"); CHECK(endif && !strstr(endif + 6, "#endif")); free(text);
    snprintf(path, sizeof(path), "%s/zcompat/scene/shaders/3082978660/Simple_Audio_Bars.frag", prepared);
    text = read_file(path);
    CHECK(strstr(text, "varying vec2 v_TexCoord;") && strstr(text, "vec2 ap_TexCoord = v_TexCoord;") &&
        strstr(text, "ap_TexCoord = ap_TexCoord.yx;")); free(text);
    snprintf(path, sizeof(path), "%s/zcompat/scene/shaders/keep.txt", prepared);
    text = read_file(path); CHECK(!strcmp(text, "original")); free(text);
    ap_engine_cleanup(&project, prepared); CHECK(!file_exists(prepared));
    snprintf(path, sizeof(path), "%s/zcompat/scene/shaders/keep.txt", scene);
    text = read_file(path); CHECK(!strcmp(text, "original")); free(text);
    snprintf(path, sizeof(path), "%s/zcompat/scene/shaders/2973943998", scene); CHECK(!file_exists(path));
    /* The clock workaround preserves normal text and only guards its optional
     * placeholder property. It must also work without a padding adjustment. */
    snprintf(path, sizeof(path), "%s/scene.pkg", scene); write_file(path, "package");
    snprintf(path, sizeof(path), "%s/scene.json", scene);
    const char *clock = "{\"objects\":[{\"text\":{\"value\":\"12:34\",\"script\":"
        "\"export let __workshopId = '3006161764';\\nnewString.replace('$', engine.userProperties.name)\"}}]}";
    write_file(path, clock);
    CHECK(ap_engine_prepare(&project, prepared, sizeof(prepared)) == AP_OK && strcmp(prepared, scene));
    char *original = read_file(path); CHECK(!strcmp(original, clock)); free(original);
    snprintf(path, sizeof(path), "%s/project.json", prepared);
    json_object *manifest = json_object_from_file(path), *file;
    CHECK(json_object_object_get_ex(manifest, "file", &file));
    snprintf(path, sizeof(path), "%s/%s", prepared, json_object_get_string(file));
    text = read_file(path); CHECK(strstr(text, "(engine.userProperties || {}).name || ''")); free(text);
    json_object_put(manifest);
    ap_engine_cleanup(&project, prepared); CHECK(!file_exists(prepared));
    snprintf(path, sizeof(path), "%s/scene.json", scene); CHECK(unlink(path) == 0);
}

static void check_elaina_variant(const char *scene, const char *expected) {
    ap_engine_project project;
    char prepared[4096], path[8192];
    CHECK(ap_engine_read(scene, &project) == AP_OK);
    char *original = read_file(project.manifest);
    CHECK(ap_engine_prepare(&project, prepared, sizeof(prepared)) == AP_OK && strcmp(prepared, scene));
    snprintf(path, sizeof(path), "%s/project.json", prepared);
    json_object *manifest = json_object_from_file(path), *value;
    CHECK(manifest && json_object_object_get_ex(manifest, "archpaper_video_variant", &value));
    CHECK(!strcmp(json_object_get_string(value), expected));
    CHECK(json_object_object_get_ex(manifest, "file", &value));
    snprintf(path, sizeof(path), "%s/%s", prepared, json_object_get_string(value));
    json_object *fixed = json_object_from_file(path), *objects;
    CHECK(fixed && json_object_object_get_ex(fixed, "objects", &objects));
    int videos = 0, placeholders = 0, controller = 0, decoration = 0;
    for (size_t i = 0; i < json_object_array_length(objects); ++i) {
        json_object *object = json_object_array_get_idx(objects, i), *name;
        CHECK(json_object_object_get_ex(object, "name", &name));
        const char *n = json_object_get_string(name);
        if (!strcmp(n, "morning") || !strcmp(n, "day") || !strcmp(n, "dusk") || !strcmp(n, "night") || !strcmp(n, "mddn")) {
            if (json_object_object_get_ex(object, "image", &value)) {
                ++videos; CHECK(!strcmp(n, expected));
                CHECK(json_object_object_get_ex(object, "visible", &value) && json_object_get_boolean(value));
                CHECK(json_object_object_get_ex(object, "effects", &value));
            } else {
                ++placeholders;
                CHECK(!json_object_object_get_ex(object, "effects", &value));
                CHECK(json_object_object_get_ex(object, "visible", &value) && !json_object_get_boolean(value));
            }
        } else if (!strcmp(n, "controller")) {
            ++controller;
            CHECK(json_object_object_get_ex(object, "image", &value));
            CHECK(json_object_object_get_ex(object, "visible", &value) && json_object_is_type(value, json_type_boolean));
        } else if (!strcmp(n, "decoration")) {
            ++decoration;
            CHECK(json_object_object_get_ex(object, "parent", &value) && json_object_get_int(value) == 142);
            CHECK(json_object_object_get_ex(object, "image", &value));
        } else if (!strcmp(n, "myLayer") && !strcmp(expected, "mddn")) {
            CHECK(json_object_object_get_ex(object, "visible", &value) && json_object_get_boolean(value));
        }
    }
    CHECK(videos == 1 && placeholders == 4 && controller == 1 && decoration == 1);
    json_object_put(fixed); json_object_put(manifest);
    ap_engine_cleanup(&project, prepared); CHECK(!file_exists(prepared));
    char *after = read_file(project.manifest); CHECK(!strcmp(original, after)); free(after); free(original);
}

static void test_engine_elaina(const char *scene) {
    char manifest_path[8192], scene_path[8192], prepared[4096];
    snprintf(manifest_path, sizeof(manifest_path), "%s/project.json", scene);
    snprintf(scene_path, sizeof(scene_path), "%s/scene.json", scene);
    char *original_manifest = read_file(manifest_path);
    const char *source = "{\"objects\":["
        "{\"id\":130,\"name\":\"myLayer\",\"visible\":false},"
        "{\"id\":193,\"name\":\"group\"},"
        "{\"id\":147,\"name\":\"morning\",\"image\":\"morning.json\",\"parent\":193,\"effects\":[]},"
        "{\"id\":144,\"name\":\"day\",\"image\":\"day.json\",\"parent\":193,\"effects\":[]},"
        "{\"id\":142,\"name\":\"dusk\",\"image\":\"dusk.json\",\"parent\":193,\"effects\":[]},"
        "{\"id\":138,\"name\":\"night\",\"image\":\"night.json\",\"parent\":193,\"effects\":[]},"
        "{\"id\":221,\"name\":\"mddn\",\"image\":\"gradient.json\",\"parent\":130,\"effects\":[]},"
        "{\"id\":6852,\"name\":\"controller\",\"image\":\"post.json\",\"visible\":{\"value\":true,"
        "\"script\":\"var displayVideo = []; displayVideo.forEach(v => v.getVideoTexture().pause());\"}},"
        "{\"id\":999,\"name\":\"decoration\",\"image\":\"decoration.json\",\"parent\":142}]}";
    write_file(scene_path, source);
    json_object *manifest = json_tokener_parse("{\"workshopid\":\"3470764447\",\"type\":\"scene\",\"file\":\"scene.json\","
        "\"general\":{\"properties\":{\"timevarying\":{\"value\":true},\"display\":{\"value\":\"1\"},"
        "\"morningtime\":{\"value\":\"6\"},\"daytime\":{\"value\":9},"
        "\"dusktime\":{\"value\":\"17\"},\"nighttime\":{\"value\":\"20\"}}}}");
    CHECK(manifest);
    write_file(manifest_path, json_object_to_json_string(manifest));
    char *tz = getenv("TZ") ? strdup(getenv("TZ")) : NULL;
    const int hours[] = {0, 5, 6, 8, 9, 16, 17, 19, 20, 23};
    const char *expected[] = {"night", "night", "morning", "morning", "day", "day", "dusk", "dusk", "night", "night"};
    for (size_t i = 0; i < sizeof(hours) / sizeof(hours[0]); ++i) {
        time_t now = time(NULL); struct tm utc;
        CHECK(gmtime_r(&now, &utc));
        /* Select a timezone with the requested hour and minute 30, so a test
         * crossing a real UTC hour boundary does not become flaky. */
        int minutes = utc.tm_hour * 60 + utc.tm_min - (hours[i] * 60 + 30);
        char zone[32]; snprintf(zone, sizeof(zone), "UTC%c%d:%02d", minutes < 0 ? '-' : '+', abs(minutes) / 60, abs(minutes) % 60);
        CHECK(setenv("TZ", zone, 1) == 0); tzset();
        check_elaina_variant(scene, expected[i]);
    }
    json_object *general, *properties, *property;
    CHECK(json_object_object_get_ex(manifest, "general", &general));
    CHECK(json_object_object_get_ex(general, "properties", &properties));
    time_t now = time(NULL); struct tm utc;
    CHECK(gmtime_r(&now, &utc));
    int minutes = utc.tm_hour * 60 + utc.tm_min - (18 * 60 + 30);
    char zone[32]; snprintf(zone, sizeof(zone), "UTC%c%d:%02d", minutes < 0 ? '-' : '+', abs(minutes) / 60, abs(minutes) % 60);
    CHECK(setenv("TZ", zone, 1) == 0); tzset();
    CHECK(json_object_object_get_ex(properties, "dusktime", &property));
    json_object_object_add(property, "value", json_object_new_string("19"));
    CHECK(json_object_object_get_ex(properties, "nighttime", &property));
    json_object_object_add(property, "value", json_object_new_string("22"));
    write_file(manifest_path, json_object_to_json_string(manifest));
    check_elaina_variant(scene, "day");
    /* Invalid/overlapping schedules fall back to a coherent default schedule. */
    CHECK(json_object_object_get_ex(properties, "nighttime", &property));
    json_object_object_add(property, "value", json_object_new_string("2"));
    write_file(manifest_path, json_object_to_json_string(manifest));
    check_elaina_variant(scene, "dusk");
    CHECK(json_object_object_get_ex(properties, "timevarying", &property));
    json_object_object_add(property, "value", json_object_new_boolean(0));
    CHECK(json_object_object_get_ex(properties, "display", &property));
    const char *manual[] = {"morning", "day", "dusk", "night", "mddn"};
    for (int i = 0; i < 5; ++i) {
        char choice[2] = {(char)('0' + i), 0};
        json_object_object_add(property, "value", json_object_new_string(choice));
        write_file(manifest_path, json_object_to_json_string(manifest));
        check_elaina_variant(scene, manual[i]);
    }
    if (tz) { setenv("TZ", tz, 1); free(tz); } else unsetenv("TZ");
    tzset();
    /* Other wallpapers with similar names and future layouts are not rewritten. */
    json_object_object_add(manifest, "workshopid", json_object_new_string("other"));
    write_file(manifest_path, json_object_to_json_string(manifest));
    ap_engine_project project;
    CHECK(ap_engine_read(scene, &project) == AP_OK);
    CHECK(ap_engine_prepare(&project, prepared, sizeof(prepared)) == AP_OK && !strcmp(prepared, scene));
    char *after = read_file(scene_path); CHECK(!strcmp(after, source)); free(after);
    json_object_object_add(manifest, "workshopid", json_object_new_string("3470764447"));
    write_file(manifest_path, json_object_to_json_string(manifest));
    write_file(scene_path, "{\"objects\":[]}");
    CHECK(ap_engine_prepare(&project, prepared, sizeof(prepared)) == AP_OK && !strcmp(prepared, scene));
    json_object_put(manifest);
    CHECK(unlink(scene_path) == 0);
    write_file(manifest_path, original_manifest); free(original_manifest);
}

static void test_engine(void) {
    char projects[1024], scene[1024], video[1024], path[4096], manifest[4096], assets[4096];
    snprintf(projects, sizeof(projects), "%s/projects", root);
    snprintf(scene, sizeof(scene), "%s/projects/scene 'quoted' $dollar;", root);
    snprintf(video, sizeof(video), "%s/projects/video", root);
    CHECK(ap_mkdirs(scene) == AP_OK && ap_mkdirs(video) == AP_OK);
    snprintf(manifest, sizeof(manifest), "%s/project.json", scene);
    write_file(manifest, "{\"title\":\"Night \\u2605\",\"type\":\"scene\",\"file\":\"scene.json\","
                         "\"preview\":\"preview.png\",\"general\":{\"title\":\"Wrong nested title\"}}");
    snprintf(path, sizeof(path), "%s/scene.pkg", scene); write_file(path, "package");
    snprintf(path, sizeof(path), "%s/preview.png", scene); write_file(path, "preview");
    ap_engine_project project;
    CHECK(ap_engine_read(scene, &project) == AP_OK && project.type == AP_ENGINE_SCENE);
    CHECK(!strcmp(project.title, "Night ★") && !strcmp(project.manifest, manifest));
    CHECK(!strcmp(project.preview, path) && strstr(project.file, "scene.pkg"));
    CHECK(ap_engine_read(manifest, &project) == AP_OK);
    CHECK(ap_library_matches(manifest, "NIGHT"));
    CHECK(select_backend_for_path(manifest, BACKEND_SWWW) == BACKEND_WALLPAPER_ENGINE);
    test_engine_compat(scene);
    test_engine_shader_compat(scene);
    test_engine_elaina(scene);
    snprintf(path, sizeof(path), "%s/movie 'quoted'.mp4", video); write_file(path, "video");
    snprintf(path, sizeof(path), "%s/project.json", video);
    write_file(path, "{\"type\":\"video\",\"file\":\"movie 'quoted'.mp4\",\"preview\":\"../../outside.png\"}");
    snprintf(path, sizeof(path), "%s/outside.png", root); write_file(path, "outside");
    CHECK(ap_engine_read(video, &project) == AP_OK && project.type == AP_ENGINE_VIDEO && !project.preview[0]);
    CHECK(select_backend_for_path(video, BACKEND_HYPRPAPER) == BACKEND_MPVPPAPER);
    char invalid[1024];
    snprintf(invalid, sizeof(invalid), "%s/projects/broken", root); CHECK(ap_mkdirs(invalid) == AP_OK);
    snprintf(path, sizeof(path), "%s/project.json", invalid); write_file(path, "{broken");
    CHECK(ap_engine_read(invalid, &project) == AP_INVALID);
    /* Required embedded NULs are rejected. */
    write_file(path, "{\"type\":\"video\\u0000scene\",\"file\":\"x.mp4\"}");
    CHECK(ap_engine_read(invalid, &project) == AP_INVALID);
    write_file(path, "{\"type\":\"scene\",\"file\":\"scene.json\"} trailing");
    CHECK(ap_engine_read(invalid, &project) == AP_INVALID);
    write_file(path, "{\"type\":\"web\",\"file\":\"index.html\",\"title\":\"Web\"}");
    CHECK(ap_engine_read(invalid, &project) == AP_OK && !project.type);
    ap_path_list list = {0};
    CHECK(ap_library_scan(projects, &list) == AP_OK && list.count == 3);
    ap_path_list_free(&list);
    CHECK(ap_library_scan(scene, &list) == AP_OK && list.count == 1 && !strcmp(list.paths[0], manifest));
    ap_path_list_free(&list);
    for (int i = 0; i < 20; ++i) {
        char *random = NULL;
        CHECK(ap_library_random(projects, &random) == AP_OK);
        CHECK(!strstr(random, "/broken/") && strstr(random, "project.json"));
        free(random);
    }
    CHECK(ap_favorite_toggle(scene, NULL) == AP_OK);
    CHECK(ap_history_load(AP_FAVORITES, &list) == AP_OK && ap_path_list_contains(&list, manifest));
    ap_path_list_free(&list);
    CHECK(ap_favorite_toggle(manifest, NULL) == AP_OK);

    /* Native, external and Flatpak libraries; aliases must not duplicate results. */
    char steam[1024], external[1024], workshop[4096], vdf[16384];
    snprintf(steam, sizeof(steam), "%s/.local/share/Steam/steamapps", root);
    CHECK(ap_mkdirs(steam) == AP_OK);
    snprintf(external, sizeof(external), "%s/Steam Library", root);
    snprintf(workshop, sizeof(workshop), "%s/steamapps/workshop/content/431960", external);
    CHECK(ap_mkdirs(workshop) == AP_OK);
    snprintf(assets, sizeof(assets), "%s/steamapps/common/wallpaper_engine/assets", external);
    CHECK(ap_mkdirs(assets) == AP_OK);
    snprintf(vdf, sizeof(vdf), "\"libraryfolders\" { // comment\n\"1\" {\"path\" \"%s\" \"apps\" {\"431960\" \"123\"}} \"2\" \"%s\"}", external, external);
    snprintf(path, sizeof(path), "%s/libraryfolders.vdf", steam); write_file(path, vdf);
    snprintf(path, sizeof(path), "%s/.steam", root); CHECK(ap_mkdirs(path) == AP_OK);
    snprintf(path, sizeof(path), "%s/.steam/steam", root);
    CHECK(symlink("../.local/share/Steam", path) == 0);
    snprintf(path, sizeof(path), "%s/.var/app/com.valvesoftware.Steam/.local/share/Steam/steamapps/workshop/content/431960", root);
    CHECK(ap_mkdirs(path) == AP_OK);
    CHECK(ap_engine_discover(&list) == AP_OK && list.count == 2 && ap_path_list_contains(&list, workshop));
    ap_path_list_free(&list);
    const char *import[] = {cli, "steam", "--import", NULL};
    CHECK(ap_process_run(import, 2000, NULL, 0) == AP_OK);
    config_t cfg, loaded;
    CHECK(config_load(&cfg) == AP_OK && cfg.folder_count == 3);
    CHECK(ap_engine_assets(&cfg, path, sizeof(path)) == AP_OK && !strcmp(path, assets));
    cfg.engine_fps = 45;
    cfg.engine_audio = 1;
    strcpy(cfg.engine_output, "DP-1");
    strcpy(cfg.engine_assets, assets);
    CHECK(config_save(&cfg) == AP_OK && config_load(&loaded) == AP_OK);
    CHECK(loaded.engine_fps == 45 && loaded.engine_audio && !strcmp(loaded.engine_assets, assets) && !strcmp(loaded.engine_output, "DP-1"));
    cfg.engine_fps = 0;
    CHECK(config_validate(&cfg) == AP_INVALID);
    CHECK(ap_engine_parse_fps("60junk", &cfg.engine_fps) == AP_INVALID);
    CHECK(ap_engine_parse_fps("241", &cfg.engine_fps) == AP_INVALID);
    cfg.engine_fps = 45; cfg.engine_output[0] = 0; cfg.engine_audio = 0; cfg.wallust_enabled = 0;
    strcpy(cfg.cache_quality, "original");
    CHECK(ap_engine_outputs(&cfg, &list) == AP_OK && list.count == 2);
    ap_path_list_free(&list);

    ap_apply_result applied;
    CHECK(ap_wallpaper_apply(invalid, &cfg, 0, &applied) == AP_UNSUPPORTED && !applied.applied);
    strcpy(cfg.engine_assets, "/nonexistent-assets");
    write_file(log_path, "");
    CHECK(ap_wallpaper_apply(scene, &cfg, 0, &applied) == AP_ASSETS_MISSING);
    char *text = read_file(log_path); CHECK(!*text); free(text);
    strcpy(cfg.engine_assets, assets);
    setenv("AP_TEST_MONITORS_FAIL", "1", 1);
    CHECK(ap_wallpaper_apply(scene, &cfg, 0, &applied) == AP_OUTPUT_MISSING);
    unsetenv("AP_TEST_MONITORS_FAIL");
    snprintf(path, sizeof(path), "%s/bin/linux-wallpaperengine", root); CHECK(unlink(path) == 0);
    CHECK(ap_wallpaper_apply(scene, &cfg, 0, &applied) == AP_ENGINE_MISSING);
    CHECK(symlink(helper, path) == 0);

    write_file(log_path, "");
    setenv("AP_TEST_ENGINE_NOTICES", "1", 1);
    CHECK(ap_wallpaper_apply(scene, &cfg, 0, &applied) == AP_OK && applied.backend == BACKEND_WALLPAPER_ENGINE);
    unsetenv("AP_TEST_ENGINE_NOTICES");
    CHECK(config_load(&loaded) == AP_OK && !strcmp(loaded.last_wallpaper, manifest));
    CHECK(ap_history_load(AP_RECENT, &list) == AP_OK && !strcmp(list.paths[0], manifest));
    ap_path_list_free(&list);
    text = read_file(log_path);
    CHECK(strstr(text, "linux-wallpaperengine [--assets-dir]") && strstr(text, "[--fps] [45] [--silent]"));
    CHECK(strstr(text, "[--screen-root] [DP-1] [--bg]") && strstr(text, "[--screen-root] [HDMI-A-1] [--bg]") && strstr(text, scene));
    free(text);
    char ready[4096]; CHECK(ap_runtime_path("engine.ready", ready, sizeof(ready)) == AP_OK);
    CHECK(file_exists(ready));
    CHECK(ap_wallpaper_recover(manifest, &cfg, "stale supervisor") == AP_OK && file_exists(ready));
    CHECK(ap_wallpaper_apply(first, &cfg, 0, &applied) == AP_OK && !file_exists(ready));
    CHECK(ap_engine_stop() == AP_OK);
    CHECK(ap_wallpaper_apply(video, &cfg, 0, &applied) == AP_OK && applied.backend == BACKEND_MPVPPAPER);
    CHECK(config_load(&loaded) == AP_OK && strstr(loaded.last_wallpaper, "/video/project.json"));
    usleep(50000);
    text = read_file(log_path); CHECK(strstr(text, "movie 'quoted'.mp4]")); free(text);

    setenv("AP_TEST_ENGINE_FAIL", "1", 1);
    CHECK(ap_wallpaper_apply(scene, &cfg, 0, &applied) == AP_PROCESS && !applied.applied);
    CHECK(applied.restored && strstr(applied.diagnostic, "Scene initialization failed"));
    CHECK(config_load(&loaded) == AP_OK && strstr(loaded.last_wallpaper, "/video/project.json"));
    CHECK(!file_exists(ready));
    CHECK(ap_runtime_path("engine.log", path, sizeof(path)) == AP_OK);
    text = read_file(path); CHECK(strstr(text, "Scene initialization failed")); free(text);
    unsetenv("AP_TEST_ENGINE_FAIL");

    const char *render_failures[] = {"shader", "object", "gpu"};
    const char *render_messages[] = {"GLSL vertex", "Failed to setup object 16", "CUDA_ERROR_OUT_OF_MEMORY"};
    for (int i = 0; i < 3; ++i) {
        setenv("AP_TEST_ENGINE_RENDER_FAIL", render_failures[i], 1);
        CHECK(ap_wallpaper_apply(scene, &cfg, 0, &applied) == AP_PROCESS && !applied.applied && applied.restored);
        CHECK(strstr(applied.diagnostic, render_messages[i]) && !file_exists(ready));
        CHECK(config_load(&loaded) == AP_OK && strstr(loaded.last_wallpaper, "/video/project.json"));
    }
    unsetenv("AP_TEST_ENGINE_RENDER_FAIL");

    /* A failure after the former 250ms readiness window is not success. */
    setenv("AP_TEST_ENGINE_DELAY_FAIL", "600", 1);
    CHECK(ap_wallpaper_apply(scene, &cfg, 0, &applied) == AP_PROCESS && !applied.applied && applied.restored);
    CHECK(strstr(applied.diagnostic, "Delayed scene failure"));
    CHECK(config_load(&loaded) == AP_OK && strstr(loaded.last_wallpaper, "/video/project.json"));
    /* A later exit restores the prior wallpaper even without a running GUI. */
    setenv("AP_TEST_ENGINE_DELAY_FAIL", "2300", 1);
    CHECK(ap_wallpaper_apply(scene, &cfg, 0, &applied) == AP_OK);
    unsetenv("AP_TEST_ENGINE_DELAY_FAIL");
    char failed[4096]; CHECK(ap_runtime_path("engine.failed", failed, sizeof(failed)) == AP_OK);
    /* Recovery waits while the client still owns the apply/theme transaction. */
    CHECK(ap_runtime_path("apply.lock", path, sizeof(path)) == AP_OK);
    int held = open(path, O_RDWR | O_CLOEXEC); CHECK(held >= 0 && flock(held, LOCK_EX) == 0);
    usleep(1100000);
    CHECK(!file_exists(failed));
    close(held);
    for (int i = 0; i < 200 && !file_exists(failed); ++i) usleep(20000);
    CHECK(file_exists(failed) && !file_exists(ready));
    CHECK(config_load(&loaded) == AP_OK && strstr(loaded.last_wallpaper, "/video/project.json"));
    text = read_file(failed);
    CHECK(strstr(text, "Previous wallpaper restored") && strstr(text, "Delayed scene failure")); free(text);
    /* A renderer that stays alive after dropping a layer must also recover. */
    setenv("AP_TEST_ENGINE_RENDER_FAIL", "object", 1);
    setenv("AP_TEST_ENGINE_RENDER_LATE", "1", 1);
    CHECK(ap_wallpaper_apply(scene, &cfg, 0, &applied) == AP_OK);
    unsetenv("AP_TEST_ENGINE_RENDER_FAIL"); unsetenv("AP_TEST_ENGINE_RENDER_LATE");
    for (int i = 0; i < 200 && !file_exists(failed); ++i) usleep(20000);
    CHECK(file_exists(failed) && !file_exists(ready));
    text = read_file(failed); CHECK(strstr(text, "Failed to setup object 16")); free(text);
    CHECK(config_load(&loaded) == AP_OK && strstr(loaded.last_wallpaper, "/video/project.json"));
    /* A stale supervisor must never undo a newer selection. */
    CHECK(ap_wallpaper_apply(first, &cfg, 0, &applied) == AP_OK && !file_exists(failed));
    CHECK(ap_wallpaper_recover(manifest, &loaded, "stale failure") == AP_OK);
    CHECK(config_load(&loaded) == AP_OK && !strcmp(loaded.last_wallpaper, first));

    const char *set[] = {cli, "set", scene, "--engine-output", "DP-1", "--engine-assets", assets,
        "--engine-fps", "24", "--engine-audio", NULL};
    CHECK(ap_process_run(set, 5000, NULL, 0) == AP_OK);
    CHECK(config_load(&loaded) == AP_OK && loaded.engine_fps == 24 && loaded.engine_audio);
    CHECK(ap_wallpaper_clear() == AP_OK && !file_exists(ready));
    const char *daemon[] = {cli, "daemon", scene, "--interval", "10", "--engine-fps", "18", NULL};
    CHECK(ap_process_run(daemon, 5000, NULL, 0) == AP_OK);
    for (int i = 0; i < 100 && !file_exists(ready); ++i) usleep(20000);
    CHECK(file_exists(ready));
    CHECK(daemon_stop() == AP_OK);
    CHECK(ap_wallpaper_clear() == AP_OK);
    text = read_file(log_path); CHECK(strstr(text, "[--fps] [18]")); free(text);
    puts("PASS Wallpaper Engine metadata, Steam discovery, preflight, argv, history and supervised lifecycle");
}

int main(int argc, char **argv) {
    if (argc > 2 && !strcmp(argv[1], "__engine-run")) return ap_engine_run((const char *const *)(argv + 2));
    CHECK(argc == 3);
    helper = argv[1]; cli = argv[2];
    setup();
    test_config();
    test_process();
    test_library_history();
    test_apply_cache();
    test_cli_daemon();
    test_engine();
    CHECK(nftw(root, remove_entry, 32, FTW_DEPTH | FTW_PHYS) == 0);
    puts("All core tests passed without Qt or real wallpaper backends.");
    return 0;
}
