// The build's version. CMake defines the values on this file alone, so a new commit rebuilds only it.

#include "platform/version.h"
#include <stdio.h>

#ifndef TUTANKHAMUN_BUILD_NUMBER
#define TUTANKHAMUN_BUILD_NUMBER 0
#endif
#ifndef TUTANKHAMUN_COMMIT
#define TUTANKHAMUN_COMMIT "unknown"
#endif
#ifndef TUTANKHAMUN_BUILD_DATE
#define TUTANKHAMUN_BUILD_DATE "?"
#endif

#if defined(__ANDROID__)
#define VERSION_SYSTEM "Android"
#elif defined(_WIN64)
#define VERSION_SYSTEM "Windows x64"
#elif defined(_WIN32)
#define VERSION_SYSTEM "Windows"
#elif defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
#define VERSION_SYSTEM "macOS arm64"
#elif defined(__APPLE__)
#define VERSION_SYSTEM "macOS x64"
#elif defined(__EMSCRIPTEN__)
#define VERSION_SYSTEM "Web"
#else
#define VERSION_SYSTEM "Linux"
#endif

int32_t version_build(void)
{
    return TUTANKHAMUN_BUILD_NUMBER;
}

const char *version_commit(void)
{
    return TUTANKHAMUN_COMMIT;
}

const char *version_text(void)
{
    static char text[96];
    if (!text[0]) {
        snprintf(text, sizeof(text), "Tutankhamun 0.1.%d (%s) %s %s", TUTANKHAMUN_BUILD_NUMBER, TUTANKHAMUN_COMMIT,
            TUTANKHAMUN_BUILD_DATE, VERSION_SYSTEM);
    }
    return text;
}
