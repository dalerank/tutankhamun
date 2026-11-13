// tutankhamun's run time on Linux and the other POSIX systems: the game's static initializers run, and a crash (a
// fatal signal) is logged with its address and the return addresses of the stack, relative to the program's image,
// before the program ends with the fault's exit code (3). Android has no backtrace before API 33: there the fault
// alone is logged. In the browser (Emscripten) there is no crash handler: the console shows the trap and its stack.

#define _GNU_SOURCE

#include "app/image.h"
#include "core/static_init.h"
#include "platform/log.h"

#include <dlfcn.h>
#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
#include <execinfo.h>
#endif
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if !defined(__EMSCRIPTEN__)
enum port_crash {
    PORT_CRASH_FRAMES = 48,
    PORT_CRASH_EXIT_CODE = 3,
};

static const char *signal_name(int number)
{
    switch (number) {
        case SIGSEGV:
            return "access violation";
        case SIGBUS:
            return "bus error";
        case SIGILL:
            return "illegal instruction";
        case SIGFPE:
            return "arithmetic fault";
        case SIGABRT:
            return "abort";
        default:
            return "signal";
    }
}

static uintptr_t image_base(void)
{
    Dl_info info;
    if (!dladdr((void *) image_base, &info)) {
        return 0;
    }
    return (uintptr_t) info.dli_fbase;
}

static void handle_crash(int number, siginfo_t *info, void *context)
{
    (void) context;
    void *frames[PORT_CRASH_FRAMES];
#if defined(__ANDROID__)
    int count = 0;
#else
    int count = backtrace(frames, PORT_CRASH_FRAMES);
#endif
    uintptr_t base = image_base();
    platform_log("port: %s (signal %d) at address 0x%llX", signal_name(number), number, (unsigned long long) (uintptr_t) info->si_addr);
    fprintf(stderr, "%s (signal %d) at address 0x%llX\n", signal_name(number), number, (unsigned long long) (uintptr_t) info->si_addr);
    platform_log("port:   our image at 0x%llX", (unsigned long long) base);
    for (int i = 0; i < count; i++) {
        uintptr_t pc = (uintptr_t) frames[i];
        platform_log("port:   frame %d 0x%llX (offset 0x%llX)", i, (unsigned long long) pc, (unsigned long long) (pc - base));
    }
#if !defined(__ANDROID__)
    char **names = backtrace_symbols(frames, count);
    if (names) {
        for (int i = 0; i < count; i++) {
            platform_log("port:   %s", names[i]);
        }
    }
#endif
    _exit(PORT_CRASH_EXIT_CODE);
}

static void crash_handler_install(void)
{
    static const int fatal[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT };
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_sigaction = handle_crash;
    action.sa_flags = SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&action.sa_mask);
    for (size_t i = 0; i < sizeof(fatal) / sizeof(fatal[0]); i++) {
        sigaction(fatal[i], &action, NULL);
    }
}
#else
// in the browser a fault is a WebAssembly trap, not a signal: the page's console shows it with the stack
static void crash_handler_install(void)
{
}
#endif

void image_run_static_initializers(void)
{
    game_static_init();
}

int32_t image_init(void)
{
    crash_handler_install();
    return 1;
}
