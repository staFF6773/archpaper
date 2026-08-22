#include "archpaper/backend.h"
#include "archpaper/utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define SWAYBG_CMD "swaybg"
#define HYPRPAPER_CMD "hyprpaper"

const char *backend_to_string(backend_t b) {
    return b == BACKEND_HYPRPAPER ? "hyprpaper" : "swaybg";
}

backend_t backend_from_string(const char *s) {
    if (!s) return BACKEND_SWAYBG;
    if (strcasecmp(s, "hyprpaper") == 0) return BACKEND_HYPRPAPER;
    return BACKEND_SWAYBG;
}

static int cmd_exists(const char *cmd) {
    const char *path_env = getenv("PATH");
    if (!path_env) return 0;

    char *path = strdup(path_env);
    if (!path) return 0;

    int found = 0;
    char *saveptr = NULL;
    char *dir = strtok_r(path, ":", &saveptr);
    char full[4096];

    while (dir) {
        snprintf(full, sizeof(full), "%s/%s", dir, cmd);
        if (access(full, X_OK) == 0) {
            found = 1;
            break;
        }
        dir = strtok_r(NULL, ":", &saveptr);
    }

    free(path);
    return found;
}

int backend_available(backend_t b) {
    const char *cmd = (b == BACKEND_HYPRPAPER) ? HYPRPAPER_CMD : SWAYBG_CMD;
    return cmd_exists(cmd);
}

backend_t detect_backend(void) {
    /* Si estamos en Hyprland y hyprpaper está disponible, preferirlo. */
    if (getenv("HYPRLAND_INSTANCE_SIGNATURE") && backend_available(BACKEND_HYPRPAPER))
        return BACKEND_HYPRPAPER;
    return BACKEND_SWAYBG;
}

static int pkill_backend(const char *name) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "pkill -x %s 2>/dev/null", name);
    return system(cmd);
}

static void kill_wallpaper_backends(void) {
    pkill_backend(SWAYBG_CMD);
    pkill_backend(HYPRPAPER_CMD);
    usleep(100000); /* 100 ms para que liberen recursos */
}

static const char *map_swaybg_mode(const char *mode) {
    if (!mode) return "fill";
    if (strcasecmp(mode, "fill") == 0) return "fill";
    if (strcasecmp(mode, "fit") == 0) return "fit";
    if (strcasecmp(mode, "stretch") == 0 || strcasecmp(mode, "scale") == 0) return "stretch";
    if (strcasecmp(mode, "center") == 0) return "center";
    if (strcasecmp(mode, "tile") == 0) return "tile";
    return "fill";
}

static int run_fork(const char *argv[]) {
    pid_t pid = fork();
    if (pid < 0) return 1;
    if (pid > 0) {
        /* Esperamos al hijo intermedio para que no quede como zombie. */
        waitpid(pid, NULL, 0);
        return 0;
    }

    /* Hijo intermedio: segundo fork y salir. */
    pid_t pid2 = fork();
    if (pid2 < 0) _exit(1);
    if (pid2 > 0) _exit(0);

    /* Nieto: adoptado por init, ejecuta el backend en segundo plano. */
    setsid();
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    execvp(argv[0], (char *const *)argv);
    _exit(1);
}

static int set_swaybg(const char *path, const char *mode) {
    const char *mode_str = map_swaybg_mode(mode);
    const char *argv[] = {SWAYBG_CMD, "-i", path, "-m", mode_str, NULL};
    return run_fork(argv);
}

static int set_hyprpaper(const char *path) {
    const char *cfg_path = "/tmp/archpaper_hyprpaper.conf";
    FILE *f = fopen(cfg_path, "w");
    if (!f) return 1;

    fprintf(f, "preload = %s\n", path);
    fprintf(f, "wallpaper = ,%s\n", path);
    fprintf(f, "splash = false\n");
    fclose(f);

    const char *argv[] = {HYPRPAPER_CMD, "--config", cfg_path, NULL};
    return run_fork(argv);
}

int set_wallpaper(backend_t b, const char *path, const char *mode) {
    if (!path || !file_exists(path)) return 1;

    kill_wallpaper_backends();

    if (b == BACKEND_HYPRPAPER) return set_hyprpaper(path);
    return set_swaybg(path, mode);
}

int clear_wallpaper(void) {
    kill_wallpaper_backends();
    return 0;
}
