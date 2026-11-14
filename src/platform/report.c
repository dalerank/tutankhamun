// The bug report's note and its sending thread (report.h).

#include "platform/report.h"
#include "platform/http.h"
#include "platform/log.h"
#include "platform/version.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) && !defined(__ANDROID__)
#include <windows.h>
#define REPORT_SUPPORTED 1
#elif defined(__ANDROID__)
#include "SDL.h"
#define REPORT_SUPPORTED 1
#endif

enum report_limits {
    REPORT_LOG_TAIL = 500 * 1024,
};

static const char report_url[] = "https://playshhhit.com/tutankhamun";

#if defined(__ANDROID__)
#define REPORT_SYSTEM "android"
#elif defined(_WIN64)
#define REPORT_SYSTEM "windows-x64"
#elif defined(_WIN32)
#define REPORT_SYSTEM "windows"
#elif defined(__APPLE__)
#define REPORT_SYSTEM "macos"
#else
#define REPORT_SYSTEM "linux"
#endif

static volatile int32_t state = REPORT_IDLE;
static volatile int32_t failed_status;

typedef struct text_buffer {
    char *data;
    size_t used;
    size_t size;
    int32_t failed;
} text_buffer;

static void put(text_buffer *b, const char *text, size_t length)
{
    if (b->failed) {
        return;
    }
    if (b->used + length + 1 > b->size) {
        size_t size = b->size ? b->size : 4096;
        while (b->used + length + 1 > size) {
            size *= 2;
        }
        char *data = realloc(b->data, size);
        if (!data) {
            b->failed = 1;
            return;
        }
        b->data = data;
        b->size = size;
    }
    memcpy(b->data + b->used, text, length);
    b->used += length;
    b->data[b->used] = 0;
}

static void put_text(text_buffer *b, const char *text)
{
    put(b, text, strlen(text));
}

// a JSON string of single-byte characters (the game's and the log's, read as Latin-1), quotes included
static void put_json_string(text_buffer *b, const char *text, size_t length)
{
    put(b, "\"", 1);
    for (size_t i = 0; i < length; i++) {
        uint8_t c = (uint8_t) text[i];
        char escaped[8];
        if (c == '"' || c == '\\') {
            escaped[0] = '\\';
            escaped[1] = (char) c;
            put(b, escaped, 2);
        } else if (c == '\n') {
            put(b, "\\n", 2);
        } else if (c == '\r') {
            put(b, "\\r", 2);
        } else if (c == '\t') {
            put(b, "\\t", 2);
        } else if (c < 0x20) {
            snprintf(escaped, sizeof(escaped), "\\u%04x", c);
            put_text(b, escaped);
        } else if (c >= 0x80) {
            escaped[0] = (char) (0xC0 | (c >> 6));
            escaped[1] = (char) (0x80 | (c & 0x3F));
            put(b, escaped, 2);
        } else {
            put(b, (const char *) &c, 1);
        }
    }
    put(b, "\"", 1);
}

static void put_field(text_buffer *b, const char *name, const char *value, size_t length)
{
    put(b, ",\"", 2);
    put_text(b, name);
    put(b, "\":", 2);
    put_json_string(b, value, length);
}

static void put_base64(text_buffer *b, const uint8_t *data, size_t size)
{
    static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char quad[4];
    size_t i = 0;
    for (; i + 3 <= size; i += 3) {
        uint32_t n = ((uint32_t) data[i] << 16) | ((uint32_t) data[i + 1] << 8) | data[i + 2];
        quad[0] = table[(n >> 18) & 63];
        quad[1] = table[(n >> 12) & 63];
        quad[2] = table[(n >> 6) & 63];
        quad[3] = table[n & 63];
        put(b, quad, 4);
    }
    if (i < size) {
        uint32_t n = (uint32_t) data[i] << 16;
        if (i + 1 < size) {
            n |= (uint32_t) data[i + 1] << 8;
        }
        quad[0] = table[(n >> 18) & 63];
        quad[1] = table[(n >> 12) & 63];
        quad[2] = i + 1 < size ? table[(n >> 6) & 63] : '=';
        quad[3] = '=';
        put(b, quad, 4);
    }
}

// the last part of the run log, from the start of a line
static char *log_tail(size_t *length)
{
    *length = 0;
    const char *path = platform_log_path();
    FILE *file = path ? fopen(path, "rb") : 0;
    if (!file) {
        return 0;
    }
    fseek(file, 0, SEEK_END);
    long end = ftell(file);
    long start = end > REPORT_LOG_TAIL ? end - REPORT_LOG_TAIL : 0;
    char *text = end > 0 ? malloc((size_t) (end - start) + 1) : 0;
    size_t got = 0;
    if (text) {
        fseek(file, start, SEEK_SET);
        got = fread(text, 1, (size_t) (end - start), file);
        text[got] = 0;
    }
    fclose(file);
    if (!text) {
        return 0;
    }
    char *from = text;
    if (start > 0) {
        char *newline = strchr(text, '\n');
        from = newline ? newline + 1 : text;
    }
    *length = got - (size_t) (from - text);
    memmove(text, from, *length + 1);
    return text;
}

typedef struct report_note {
    char *body;
    size_t size;
} report_note;

static report_note *make_note(const char *kind, const char *title, const char *text, const uint8_t *png, size_t png_size)
{
    text_buffer b;
    memset(&b, 0, sizeof(b));
    put_text(&b, "{\"kind\":");
    put_json_string(&b, kind, strlen(kind));
    put_field(&b, "title", title, strlen(title));
    put_field(&b, "text", text, strlen(text));
    put_field(&b, "build", version_text(), strlen(version_text()));
    put_field(&b, "platform", REPORT_SYSTEM, strlen(REPORT_SYSTEM));
    put_field(&b, "lang", "en", 2);
    size_t log_length;
    char *log = log_tail(&log_length);
    if (log) {
        put_field(&b, "log", log, log_length);
        free(log);
    }
    if (png && png_size > 0) {
        put_text(&b, ",\"shot\":\"");
        put_base64(&b, png, png_size);
        put(&b, "\"", 1);
    }
    put(&b, "}", 1);
    if (b.failed) {
        free(b.data);
        return 0;
    }
    report_note *note = malloc(sizeof(report_note));
    if (!note) {
        free(b.data);
        return 0;
    }
    note->body = b.data;
    note->size = b.used;
    return note;
}

static void post_note(report_note *note)
{
    const char *test_url = getenv("TUTANKHAMUN_REPORT_URL");
    const char *url = test_url && test_url[0] ? test_url : report_url;
    int32_t status = 0;
    int32_t ok = http_post_json(url, note->body, note->size, &status);
    platform_log("report: %zu bytes to %s: %s (%d)", note->size, url, ok ? "sent" : "failed", status);
    free(note->body);
    free(note);
    failed_status = ok ? 0 : status;
    state = ok ? REPORT_SENT : REPORT_FAILED;
}

int32_t report_supported(void)
{
#if defined(REPORT_SUPPORTED)
    return http_supported();
#else
    return 0;
#endif
}

#if defined(__ANDROID__)
static int report_thread(void *context)
{
    post_note((report_note *) context);
    return 0;
}
#elif defined(REPORT_SUPPORTED)
static DWORD WINAPI report_thread(LPVOID context)
{
    post_note((report_note *) context);
    return 0;
}
#endif

int32_t report_send(const char *kind, const char *title, const char *text, uint8_t *png, size_t png_size)
{
    if (state == REPORT_SENDING || !report_supported()) {
        free(png);
        return 0;
    }
    report_note *note = make_note(kind, title, text, png, png_size);
    free(png);
    if (!note) {
        return 0;
    }
    state = REPORT_SENDING;
#if defined(__ANDROID__)
    SDL_Thread *thread = SDL_CreateThread(report_thread, "report", note);
    if (thread) {
        SDL_DetachThread(thread);
        return 1;
    }
#elif defined(REPORT_SUPPORTED)
    HANDLE thread = CreateThread(0, 0, report_thread, note, 0, 0);
    if (thread) {
        CloseHandle(thread);
        return 1;
    }
#endif
    free(note->body);
    free(note);
    state = REPORT_FAILED;
    return 0;
}

report_state report_current_state(void)
{
    return (report_state) state;
}

int32_t report_failed_status(void)
{
    return failed_status;
}

void report_forget(void)
{
    if (state != REPORT_SENDING) {
        state = REPORT_IDLE;
    }
}
