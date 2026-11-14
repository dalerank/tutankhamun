// The Android side of the platform (platform_android.c): the game's folder that the user picked through the Storage
// Access Framework, its files opened, listed, created and deleted through the Java FileManager, and the app's own
// storage. The paths are the game's (relative to the picked folder, with \ or /, any case).

#ifndef PLATFORM_PLATFORM_ANDROID_H
#define PLATFORM_PLATFORM_ANDROID_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum android_path_kind {
    ANDROID_PATH_NONE = 0,
    ANDROID_PATH_FILE = 1,
    ANDROID_PATH_DIR = 2,
};

int32_t android_file_open(const char *path, const char *mode);
int32_t android_path_kind(const char *path);
bool android_dir_create(const char *path);
bool android_file_delete(const char *path);
char **android_dir_list(const char *path, int32_t *count);
void android_dir_list_free(char **names, int32_t count);

bool android_game_folder_set(const char *uri);
bool android_game_folder_pick(bool again, char *uri, size_t size);
const char *android_storage_path(void);

// a POST of a JSON body through the Java side (HttpURLConnection), from any thread; the answer's status, 0 when none
int32_t android_http_post_json(const char *url, const char *body, size_t size);

#endif
