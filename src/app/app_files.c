// The program's own files. On Windows: GetModuleFileName for the program, the config folder and the ini beside it,
// the ini read and written by the Windows profile functions. Elsewhere: /proc/self/exe (macOS: _NSGetExecutablePath)
// for the program, the configs in the user's folder when there, else beside the program, in
// ../share/tutankhamun/config or, in the macOS app bundle, in Contents/Resources/config, the ini in the user's folder
// (~/.config/tutankhamun; macOS: ~/Library/Application Support/tutankhamun; on Android both in the app's internal
// storage), read and written here ([section] lines and key=value lines; a write keeps the other lines as they are).
// In the browser the program is /app/tutankhamun with the configs built in beside it, the user's folder is in
// Emscripten's $HOME (/home/web_user), which the page keeps in IndexedDB.

#include "app/app_files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)

#include <windows.h>

bool app_program_path(char *path, size_t size)
{
    DWORD length = GetModuleFileNameA(0, path, (DWORD) size);
    return length > 0 && length < size;
}

bool app_program_dir(char *dir, size_t size)
{
    if (!app_program_path(dir, size)) {
        return false;
    }
    char *slash = strrchr(dir, '\\');
    if (!slash) {
        return false;
    }
    *slash = 0;
    return true;
}

static bool is_dir(const char *path)
{
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool app_config_dir(char *dir, size_t size)
{
    if (!app_program_dir(dir, size) || strlen(dir) + sizeof("\\config") > size) {
        return false;
    }
    strcat(dir, "\\config");
    return is_dir(dir);
}

bool app_ini_path(char *path, size_t size)
{
    if (!app_program_path(path, size)) {
        path[0] = 0;
        return false;
    }
    char *slash = strrchr(path, '\\');
    char *dot = slash ? strrchr(slash, '.') : 0;
    if (!dot || (size_t) (dot - path) + sizeof(".ini") > size) {
        path[0] = 0;
        return false;
    }
    memcpy(dot, ".ini", sizeof(".ini"));
    return true;
}

int32_t app_ini_read_int(const char *ini, const char *section, const char *key, int32_t fallback)
{
    return (int32_t) GetPrivateProfileIntA(section, key, fallback, ini);
}

void app_ini_read_text(const char *ini, const char *section, const char *key, char *value, size_t size)
{
    GetPrivateProfileStringA(section, key, "", value, (DWORD) size, ini);
}

bool app_ini_write(const char *ini, const char *section, const char *key, const char *value)
{
    return WritePrivateProfileStringA(section, key, value, ini) != 0;
}

#else

#if defined(__ANDROID__)
#include "platform/platform_android.h"
#endif

#if defined(__APPLE__)
#include <limits.h>
#include <mach-o/dyld.h>
#include <stdint.h>
#endif
#include <ctype.h>
#include <errno.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

enum app_ini_limits {
    INI_LINE_SIZE = 1200,
    INI_FILE_MAX = 65536,
};

#if defined(__APPLE__)
bool app_program_path(char *path, size_t size)
{
    char link[1024];
    uint32_t link_size = sizeof(link);
    char resolved[PATH_MAX];
    if (_NSGetExecutablePath(link, &link_size) != 0 || !realpath(link, resolved) || strlen(resolved) >= size) {
        path[0] = 0;
        return false;
    }
    strcpy(path, resolved);
    return true;
}
#elif defined(__EMSCRIPTEN__)
// the browser has no program file: the program is /app/tutankhamun, beside the built-in /app/config (CMakeLists.txt)
bool app_program_path(char *path, size_t size)
{
    snprintf(path, size, "/app/tutankhamun");
    return true;
}
#else
bool app_program_path(char *path, size_t size)
{
    ssize_t length = readlink("/proc/self/exe", path, size - 1);
    if (length <= 0) {
        path[0] = 0;
        return false;
    }
    path[length] = 0;
    return true;
}
#endif

bool app_program_dir(char *dir, size_t size)
{
    if (!app_program_path(dir, size)) {
        return false;
    }
    char *slash = strrchr(dir, '/');
    if (!slash) {
        return false;
    }
    *slash = slash == dir ? *slash : 0;
    return true;
}

static bool is_dir(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

// the user's own folder of the program: ~/Library/Application Support/tutankhamun on macOS, else
// $XDG_CONFIG_HOME/tutankhamun or ~/.config/tutankhamun
static bool user_dir(char *dir, size_t size)
{
    const char *home = getenv("HOME");
#if defined(__APPLE__)
    if (!home || !home[0]) {
        return false;
    }
    snprintf(dir, size, "%s/Library/Application Support/tutankhamun", home);
    return true;
#else
    const char *config_home = getenv("XDG_CONFIG_HOME");
    if (config_home && config_home[0] == '/') {
        snprintf(dir, size, "%s/tutankhamun", config_home);
    } else if (home && home[0]) {
        snprintf(dir, size, "%s/.config/tutankhamun", home);
    } else {
        return false;
    }
    return true;
#endif
}

static bool user_config_dir(char *dir, size_t size)
{
    char user[1024];
    if (!user_dir(user, sizeof(user))) {
        return false;
    }
    snprintf(dir, size, "%s/config", user);
    return is_dir(dir);
}

bool app_config_dir(char *dir, size_t size)
{
#if defined(__ANDROID__)
    snprintf(dir, size, "%s/config", android_storage_path());
    return is_dir(dir);
#endif
    if (user_config_dir(dir, size)) {
        return true;
    }
    char program[1024];
    if (!app_program_dir(program, sizeof(program))) {
        return false;
    }
    snprintf(dir, size, "%s/config", program);
    if (is_dir(dir)) {
        return true;
    }
    snprintf(dir, size, "%s/../share/tutankhamun/config", program);
    if (is_dir(dir)) {
        return true;
    }
#if defined(__APPLE__)
    // the app bundle: Contents/MacOS/tutankhamun and Contents/Resources/config
    snprintf(dir, size, "%s/../Resources/config", program);
    if (is_dir(dir)) {
        return true;
    }
#endif
    snprintf(dir, size, "%s/config", program);
    return false;
}

static bool make_dirs(char *path)
{
    for (char *p = path + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(path, 0755) != 0 && errno != EEXIST) {
                *p = '/';
                return false;
            }
            *p = '/';
        }
    }
    return mkdir(path, 0755) == 0 || errno == EEXIST;
}

bool app_ini_path(char *path, size_t size)
{
#if defined(__ANDROID__)
    snprintf(path, size, "%s/tutankhamun.ini", android_storage_path());
    return true;
#endif
    char dir[1024];
    if (!user_dir(dir, sizeof(dir))) {
        path[0] = 0;
        return false;
    }
    make_dirs(dir);
    snprintf(path, size, "%s/tutankhamun.ini", dir);
    return true;
}

static char *trim(char *text)
{
    while (isspace((unsigned char) *text)) {
        text++;
    }
    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char) end[-1])) {
        *--end = 0;
    }
    return text;
}

static bool line_is_section(char *line, const char *section)
{
    char *text = trim(line);
    size_t length = strlen(text);
    if (length < 2 || text[0] != '[' || text[length - 1] != ']') {
        return false;
    }
    text[length - 1] = 0;
    return strcasecmp(trim(text + 1), section) == 0;
}

static bool line_starts_section(const char *line)
{
    while (isspace((unsigned char) *line)) {
        line++;
    }
    return *line == '[';
}

static bool line_value(char *line, const char *key, char **value)
{
    char *equals = strchr(line, '=');
    if (!equals) {
        return false;
    }
    *equals = 0;
    if (strcasecmp(trim(line), key) != 0) {
        return false;
    }
    *value = trim(equals + 1);
    return true;
}

void app_ini_read_text(const char *ini, const char *section, const char *key, char *value, size_t size)
{
    value[0] = 0;
    FILE *file = ini && ini[0] ? fopen(ini, "r") : NULL;
    if (!file) {
        return;
    }
    char line[INI_LINE_SIZE];
    bool in_section = false;
    while (fgets(line, sizeof(line), file)) {
        if (line_starts_section(line)) {
            in_section = line_is_section(line, section);
            continue;
        }
        char *found;
        if (in_section && line_value(line, key, &found)) {
            snprintf(value, size, "%s", found);
            break;
        }
    }
    fclose(file);
}

int32_t app_ini_read_int(const char *ini, const char *section, const char *key, int32_t fallback)
{
    char text[64];
    app_ini_read_text(ini, section, key, text, sizeof(text));
    if (!text[0]) {
        return fallback;
    }
    char *end;
    long value = strtol(text, &end, 10);
    return end == text ? fallback : (int32_t) value;
}

static size_t read_file(const char *path, char *buffer, size_t size)
{
    FILE *file = fopen(path, "r");
    if (!file) {
        buffer[0] = 0;
        return 0;
    }
    size_t length = fread(buffer, 1, size - 1, file);
    buffer[length] = 0;
    fclose(file);
    return length;
}

bool app_ini_write(const char *ini, const char *section, const char *key, const char *value)
{
    if (!ini || !ini[0]) {
        return false;
    }
    char *old = malloc(INI_FILE_MAX);
    char *out = malloc(INI_FILE_MAX + INI_LINE_SIZE * 2);
    if (!old || !out) {
        free(old);
        free(out);
        return false;
    }
    read_file(ini, old, INI_FILE_MAX);
    out[0] = 0;
    bool in_section = false;
    bool section_seen = false;
    bool written = false;
    char *line = old;
    while (*line) {
        char *next = strchr(line, '\n');
        size_t length = next ? (size_t) (next - line) + 1 : strlen(line);
        char copy[INI_LINE_SIZE];
        snprintf(copy, sizeof(copy), "%.*s", (int) (length < sizeof(copy) ? length : sizeof(copy) - 1), line);
        char probe[INI_LINE_SIZE];
        strcpy(probe, copy);
        if (line_starts_section(probe)) {
            if (in_section && !written) {
                sprintf(out + strlen(out), "%s=%s\n", key, value);
                written = true;
            }
            in_section = line_is_section(probe, section);
            section_seen = section_seen || in_section;
        } else {
            char *found;
            strcpy(probe, copy);
            if (in_section && !written && line_value(probe, key, &found)) {
                sprintf(out + strlen(out), "%s=%s\n", key, value);
                written = true;
                line += length;
                continue;
            }
        }
        strncat(out, line, length);
        line += length;
    }
    if (!written) {
        size_t used = strlen(out);
        if (used && out[used - 1] != '\n') {
            strcat(out, "\n");
        }
        if (!section_seen) {
            sprintf(out + strlen(out), "[%s]\n", section);
        }
        sprintf(out + strlen(out), "%s=%s\n", key, value);
    }
    FILE *file = fopen(ini, "w");
    bool ok = file && fputs(out, file) >= 0;
    if (file) {
        ok = fclose(file) == 0 && ok;
    }
    free(old);
    free(out);
    return ok;
}

#endif
