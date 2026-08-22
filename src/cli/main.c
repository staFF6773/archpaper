#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "archpaper/backend.h"
#include "archpaper/config.h"
#include "archpaper/daemon.h"
#include "archpaper/utils.h"
#include "archpaper/wallust.h"
#include "archpaper/cli.h"

static void print_usage(const char *name) {
    printf("Uso: %s <comando> [opciones]\n\n", name);
    printf("Comandos:\n");
    printf("  set <imagen> [--mode MODO]      Establece un wallpaper estático.\n");
    printf("  clear                           Elimina el wallpaper actual.\n");
    printf("  random <directorio> [--mode MODO]  Selecciona una imagen aleatoria.\n");
    printf("  daemon <directorio> --interval <s> [--mode MODO]\n");
    printf("                                  Cambia el wallpaper periódicamente.\n");
    printf("  status                          Muestra el estado actual.\n");
    printf("  backend                         Muestra backend detectado y activo.\n");
    printf("\nModos (swaybg): fill, fit, stretch, center, tile\n");
    printf("Opciones globales:\n");
    printf("  --backend <swaybg|hyprpaper>    Fuerza un backend concreto.\n");
    printf("  --mode <MODO>                   Modo de ajuste de imagen.\n");
    printf("  --wallust                       Ejecuta 'wallust run' tras aplicar.\n");
    printf("  --wallust-hook <script>         Script a ejecutar tras wallust.\n");
}

static int check_wayland(void) {
    if (getenv("WAYLAND_DISPLAY")) return 1;
    const char *stype = getenv("XDG_SESSION_TYPE");
    return stype && strcmp(stype, "wayland") == 0;
}

static void parse_global_args(int argc, char *argv[], backend_t *backend, const char **mode,
                              int *wallust_enabled, const char **wallust_hook) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--backend") == 0 && i + 1 < argc) {
            *backend = backend_from_string(argv[++i]);
        } else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            *mode = argv[++i];
        } else if (strcmp(argv[i], "--wallust") == 0) {
            *wallust_enabled = 1;
        } else if (strcmp(argv[i], "--wallust-hook") == 0 && i + 1 < argc) {
            *wallust_hook = argv[++i];
        }
    }
}

static void run_wallust_theme(int enabled, const char *image_path, const char *wallust_hook) {
    if (!enabled) return;
    if (wallust_available()) {
        wallust_run(image_path);
    } else {
        fprintf(stderr, "Advertencia: wallust no encontrado en PATH; se omite la generación de colores.\n");
    }
    wallust_hook_run(wallust_hook, image_path);
}

int archpaper_cli(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    if (!check_wayland()) {
        fprintf(stderr, "Advertencia: no se detectó una sesión Wayland (WAYLAND_DISPLAY no está definido).\n");
    }

    config_t cfg;
    config_load(&cfg);

    backend_t backend = cfg.backend;
    const char *mode = cfg.mode;
    const char *wallust_hook_arg = NULL;
    int wallust_enabled = cfg.wallust_enabled;
    parse_global_args(argc, argv, &backend, &mode, &wallust_enabled, &wallust_hook_arg);

    if (!backend_available(backend)) {
        fprintf(stderr, "Error: backend '%s' no encontrado en PATH.\n", backend_to_string(backend));
        return 1;
    }

    const char *command = argv[1];

    if (strcmp(command, "set") == 0) {
        if (argc < 3) { print_usage(argv[0]); return 1; }
        char *path = expand_path(argv[2]);
        if (!path || !file_exists(path)) {
            fprintf(stderr, "Error: no se encontró '%s'\n", argv[2]);
            free(path);
            return 1;
        }

        cfg.backend = backend;
        strncpy(cfg.mode, mode, sizeof(cfg.mode) - 1);
        cfg.mode[sizeof(cfg.mode) - 1] = '\0';
        cfg.wallust_enabled = wallust_enabled;
        strncpy(cfg.last_wallpaper, path, sizeof(cfg.last_wallpaper) - 1);
        cfg.last_wallpaper[sizeof(cfg.last_wallpaper) - 1] = '\0';
        config_save(&cfg);

        int r = set_wallpaper(backend, path, mode);
        if (r == 0) {
            run_wallust_theme(wallust_enabled, path,
                              wallust_hook_arg ? wallust_hook_arg : cfg.wallust_hook);
        }
        free(path);
        return r;

    } else if (strcmp(command, "clear") == 0) {
        return clear_wallpaper();

    } else if (strcmp(command, "random") == 0) {
        if (argc < 3) { print_usage(argv[0]); return 1; }
        char *dir = expand_path(argv[2]);
        if (!dir || !is_dir(dir)) {
            fprintf(stderr, "Error: '%s' no es un directorio.\n", argv[2]);
            free(dir);
            return 1;
        }

        char *img = random_image(dir);
        if (!img) {
            fprintf(stderr, "Error: no se encontraron imágenes en '%s'.\n", dir);
            free(dir);
            return 1;
        }

        printf("Seleccionado: %s\n", img);

        cfg.backend = backend;
        strncpy(cfg.mode, mode, sizeof(cfg.mode) - 1);
        cfg.mode[sizeof(cfg.mode) - 1] = '\0';
        cfg.wallust_enabled = wallust_enabled;
        strncpy(cfg.last_wallpaper, img, sizeof(cfg.last_wallpaper) - 1);
        cfg.last_wallpaper[sizeof(cfg.last_wallpaper) - 1] = '\0';
        config_save(&cfg);

        int r = set_wallpaper(backend, img, mode);
        if (r == 0) {
            run_wallust_theme(wallust_enabled, img,
                              wallust_hook_arg ? wallust_hook_arg : cfg.wallust_hook);
        }
        free(img);
        free(dir);
        return r;

    } else if (strcmp(command, "daemon") == 0) {
        if (argc < 3) { print_usage(argv[0]); return 1; }

        const char *dir_raw = NULL;
        int interval = 300;

        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--interval") == 0 && i + 1 < argc) {
                interval = atoi(argv[++i]);
            } else if (argv[i][0] != '-' && !dir_raw) {
                dir_raw = argv[i];
            }
        }

        if (!dir_raw) { print_usage(argv[0]); return 1; }
        char *dir = expand_path(dir_raw);
        if (!dir || !is_dir(dir)) {
            fprintf(stderr, "Error: '%s' no es un directorio.\n", dir_raw);
            free(dir);
            return 1;
        }

        printf("Iniciando daemon: backend=%s, intervalo=%ds, wallust=%s, directorio=%s\n",
               backend_to_string(backend), interval,
               wallust_enabled ? "si" : "no", dir);
        const char *wallust_hook = wallust_hook_arg ? wallust_hook_arg : cfg.wallust_hook;
        return daemonize_random(dir, interval, backend, mode, wallust_enabled, wallust_hook);

    } else if (strcmp(command, "status") == 0) {
        printf("Backend activo:   %s\n", backend_to_string(backend));
        printf("Modo:             %s\n", mode);
        printf("Backend detectado:%s\n", backend_to_string(detect_backend()));
        printf("Último wallpaper: %s\n",
               cfg.last_wallpaper[0] ? cfg.last_wallpaper : "(ninguno)");
        return 0;

    } else if (strcmp(command, "backend") == 0) {
        printf("Detectado: %s\n", backend_to_string(detect_backend()));
        printf("Activo:    %s\n", backend_to_string(backend));
        return 0;

    } else if (strcmp(command, "--help") == 0 || strcmp(command, "-h") == 0) {
        print_usage(argv[0]);
        return 0;

    } else {
        fprintf(stderr, "Comando desconocido: %s\n\n", command);
        print_usage(argv[0]);
        return 1;
    }
}
