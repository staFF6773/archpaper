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
    const char *commands[] = {"awww", "awww-daemon", "swaybg", "hyprpaper", "mpvpaper", "pkill", "wallust", "ffprobe", "ffmpeg", "ffmpegthumbnailer", NULL};
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

int main(int argc, char **argv) {
    CHECK(argc == 3);
    helper = argv[1]; cli = argv[2];
    setup();
    test_config();
    test_process();
    test_library_history();
    test_apply_cache();
    test_cli_daemon();
    CHECK(nftw(root, remove_entry, 32, FTW_DEPTH | FTW_PHYS) == 0);
    puts("All core tests passed without Qt or real wallpaper backends.");
    return 0;
}
