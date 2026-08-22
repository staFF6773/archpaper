#ifndef ARCHPAPER_UTILS_H
#define ARCHPAPER_UTILS_H

/* Expand a path that starts with ~ to the value of $HOME. */
char *expand_path(const char *path);

/* Check whether a file or directory exists. */
int file_exists(const char *path);

/* Check whether the path is a directory. */
int is_dir(const char *path);

/* Indicate whether the path has a supported image extension. */
int is_image(const char *path);

/* Return a random image from a directory (must be freed with free). */
char *random_image(const char *dir);

/* Return the user's home directory. */
const char *get_home(void);

#endif
