/* Standalone C entry point; the Qt launcher uses archpaper_cli() directly. */
#include "archpaper/cli.h"

int main(int argc, char **argv) {
    return archpaper_cli(argc, argv);
}
