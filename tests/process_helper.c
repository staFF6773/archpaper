#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void pause_ms(long ms) {
    struct timespec time = {ms / 1000, (ms % 1000) * 1000000};
    nanosleep(&time, NULL);
}

int main(int argc, char **argv) {
    const char *name = strrchr(argv[0], '/');
    name = name ? name + 1 : argv[0];
    if (!strcmp(name, "process-helper")) {
        if (argc < 2) return 2;
        if (!strcmp(argv[1], "echo")) { if (argc == 3) fputs(argv[2], stdout); return 0; }
        if (!strcmp(argv[1], "fail")) return 7;
        if (!strcmp(argv[1], "sleep")) { pause_ms(5000); return 0; }
        if (!strcmp(argv[1], "flood")) {
            for (int i = 0; i < 200000; ++i) fputs("output", stdout);
            return 0;
        }
        return 2;
    }
    const char *log = getenv("AP_TEST_LOG");
    if (log) {
        FILE *f = fopen(log, "a");
        if (!f) return 20;
        fprintf(f, "%s", name);
        for (int i = 1; i < argc; ++i) fprintf(f, " [%s]", argv[i]);
        fputc('\n', f);
        fclose(f);
    }
    if (!strcmp(name, "pkill")) return 1;
    if (!strcmp(name, "linux-wallpaperengine")) {
        if (getenv("AP_TEST_ENGINE_FAIL")) { fputs("Scene initialization failed\n", stderr); return 9; }
        for (;;) pause_ms(1000);
    }
    if (!strcmp(name, "hyprctl")) {
        if (getenv("AP_TEST_MONITORS_FAIL")) return 1;
        puts("[{\"name\":\"DP-1\"},{\"name\":\"HDMI-A-1\"}]");
    }
    if (!strcmp(name, "awww")) {
        if (argc > 1 && !strcmp(argv[1], "query")) { puts("test-output: 2560x1440"); return 0; }
        if (argc > 1 && !strcmp(argv[1], "img")) {
            if (getenv("AP_TEST_BLOCK")) pause_ms(5000);
            if (getenv("AP_TEST_BACKEND_FAIL")) return 9;
        }
    }
    if (!strcmp(name, "ffprobe")) puts(getenv("AP_TEST_LARGE") ? "4096x3072" : "1280x720");
    if (!strcmp(name, "ffmpeg") || !strcmp(name, "ffmpegthumbnailer")) {
        if (getenv("AP_TEST_THUMB_BLOCK")) pause_ms(5000);
        if (argc < 2) return 2;
        const char *output = argv[argc - 1];
        if (!strcmp(name, "ffmpegthumbnailer")) {
            for (int i = 1; i + 1 < argc; ++i) if (!strcmp(argv[i], "-o")) output = argv[i + 1];
        }
        FILE *f = fopen(output, "w");
        if (!f) return 4;
        fputs("converted media", f);
        fclose(f);
        if (getenv("AP_TEST_FFMPEG_FAIL")) return 7;
    }
    if (!strcmp(name, "wallust")) {
        pause_ms(80);
        const char *done = getenv("AP_TEST_THEME_DONE");
        FILE *f = done ? fopen(done, "w") : NULL;
        if (!f) return 5;
        fputs("done", f);
        fclose(f);
    }
    return 0;
}
