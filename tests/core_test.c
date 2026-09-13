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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
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
    CHECK(ap_wallpaper_apply(scene, &cfg, 0, &applied) == AP_OK && applied.backend == BACKEND_WALLPAPER_ENGINE);
    CHECK(config_load(&loaded) == AP_OK && !strcmp(loaded.last_wallpaper, manifest));
    CHECK(ap_history_load(AP_RECENT, &list) == AP_OK && !strcmp(list.paths[0], manifest));
    ap_path_list_free(&list);
    text = read_file(log_path);
    CHECK(strstr(text, "linux-wallpaperengine [--assets-dir]") && strstr(text, "[--fps] [45] [--silent]"));
    CHECK(strstr(text, "[--screen-root] [DP-1] [--bg]") && strstr(text, "[--screen-root] [HDMI-A-1] [--bg]") && strstr(text, scene));
    free(text);
    char ready[4096]; CHECK(ap_runtime_path("engine.ready", ready, sizeof(ready)) == AP_OK);
    CHECK(file_exists(ready));
    CHECK(ap_wallpaper_apply(first, &cfg, 0, &applied) == AP_OK && !file_exists(ready));
    CHECK(ap_engine_stop() == AP_OK);
    CHECK(ap_wallpaper_apply(video, &cfg, 0, &applied) == AP_OK && applied.backend == BACKEND_MPVPPAPER);
    CHECK(config_load(&loaded) == AP_OK && strstr(loaded.last_wallpaper, "/video/project.json"));
    usleep(50000);
    text = read_file(log_path); CHECK(strstr(text, "movie 'quoted'.mp4]")); free(text);

    setenv("AP_TEST_ENGINE_FAIL", "1", 1);
    CHECK(ap_wallpaper_apply(scene, &cfg, 0, &applied) == AP_PROCESS && !applied.applied);
    CHECK(config_load(&loaded) == AP_OK && strstr(loaded.last_wallpaper, "/video/project.json"));
    CHECK(!file_exists(ready));
    CHECK(ap_runtime_path("engine.log", path, sizeof(path)) == AP_OK);
    text = read_file(path); CHECK(strstr(text, "Scene initialization failed")); free(text);
    unsetenv("AP_TEST_ENGINE_FAIL");

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
