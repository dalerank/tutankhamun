// HTTPS requests through WinHTTP (http.h); stubs elsewhere.

#include "platform/http.h"
#include "platform/log.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32) && !defined(__ANDROID__)

#include <windows.h>
#include <winhttp.h>

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif

enum http_limits {
    HTTP_URL = 2048,
    HTTP_HOST = 256,
    HTTP_CHUNK = 65536,
};

int32_t http_supported(void)
{
    return 1;
}

static HINTERNET open_session(void)
{
    const wchar_t *agent = L"tutankhamun";
    HINTERNET session = WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        // before Windows 8.1
        session = WinHttpOpen(agent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (session) {
        WinHttpSetTimeouts(session, 15000, 15000, 60000, 60000);
    }
    return session;
}

typedef struct http_request {
    HINTERNET session;
    HINTERNET connection;
    HINTERNET request;
} http_request;

static void close_request(http_request *r)
{
    if (r->request) {
        WinHttpCloseHandle(r->request);
    }
    if (r->connection) {
        WinHttpCloseHandle(r->connection);
    }
    if (r->session) {
        WinHttpCloseHandle(r->session);
    }
}

static int32_t open_request(http_request *r, const char *url, const wchar_t *verb)
{
    wchar_t wide[HTTP_URL], host[HTTP_HOST], path[HTTP_URL];
    memset(r, 0, sizeof(*r));
    if (MultiByteToWideChar(CP_UTF8, 0, url, -1, wide, HTTP_URL) == 0) {
        return 0;
    }
    URL_COMPONENTS parts;
    memset(&parts, 0, sizeof(parts));
    parts.dwStructSize = sizeof(parts);
    parts.lpszHostName = host;
    parts.dwHostNameLength = HTTP_HOST;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = HTTP_URL;
    if (!WinHttpCrackUrl(wide, 0, 0, &parts)) {
        platform_log("http: a bad address %s", url);
        return 0;
    }
    r->session = open_session();
    r->connection = r->session ? WinHttpConnect(r->session, host, parts.nPort, 0) : 0;
    DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    r->request = r->connection ? WinHttpOpenRequest(r->connection, verb, path, 0, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags) : 0;
    return r->request != 0;
}

static DWORD answer_status(http_request *r)
{
    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(r->request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
        WINHTTP_NO_HEADER_INDEX);
    return status;
}

int32_t http_get(const char *url, http_sink sink, void *context)
{
    http_request r;
    int32_t ok = 0;
    if (open_request(&r, url, L"GET") && WinHttpSendRequest(r.request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
        && WinHttpReceiveResponse(r.request, 0)) {
        DWORD status = answer_status(&r);
        if (status == 200) {
            uint8_t *buffer = malloc(HTTP_CHUNK);
            ok = buffer != 0;
            while (ok) {
                DWORD got = 0;
                if (!WinHttpReadData(r.request, buffer, HTTP_CHUNK, &got)) {
                    ok = 0;
                    break;
                }
                if (got == 0) {
                    break;
                }
                if (!sink(context, buffer, got)) {
                    ok = 0;
                }
            }
            free(buffer);
        } else {
            platform_log("http: %s answered %lu", url, (unsigned long) status);
        }
    } else {
        platform_log("http: no answer from %s (error %lu)", url, (unsigned long) GetLastError());
    }
    close_request(&r);
    return ok;
}

int32_t http_post_json(const char *url, const char *body, size_t size, int32_t *status)
{
    http_request r;
    int32_t ok = 0;
    *status = 0;
    const wchar_t *headers = L"Content-Type: application/json\r\n";
    if (open_request(&r, url, L"POST")
        && WinHttpSendRequest(r.request, headers, (DWORD) -1L, (LPVOID) body, (DWORD) size, (DWORD) size, 0)
        && WinHttpReceiveResponse(r.request, 0)) {
        *status = (int32_t) answer_status(&r);
        ok = *status >= 200 && *status < 300;
        if (!ok) {
            platform_log("http: %s answered %d", url, *status);
        }
    } else {
        platform_log("http: no answer from %s (error %lu)", url, (unsigned long) GetLastError());
    }
    close_request(&r);
    return ok;
}

#elif defined(__ANDROID__)

#include "platform/platform_android.h"

int32_t http_supported(void)
{
    return 1;
}

int32_t http_get(const char *url, http_sink sink, void *context)
{
    (void) url;
    (void) sink;
    (void) context;
    return 0;
}

int32_t http_post_json(const char *url, const char *body, size_t size, int32_t *status)
{
    *status = android_http_post_json(url, body, size);
    return *status >= 200 && *status < 300;
}

#else

int32_t http_supported(void)
{
    return 0;
}

int32_t http_get(const char *url, http_sink sink, void *context)
{
    (void) url;
    (void) sink;
    (void) context;
    return 0;
}

int32_t http_post_json(const char *url, const char *body, size_t size, int32_t *status)
{
    (void) url;
    (void) body;
    (void) size;
    *status = 0;
    return 0;
}

#endif
