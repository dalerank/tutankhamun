// Play mode of tutankhamun: the game in an SDL window of its own. It does what a Windows message loop, window set-up
// and window procedure did around the game: runs the frames, presents the 16-bit frame buffer (platform/screen.c),
// feeds mouse and keyboard into the input globals, and pumps messages for the game's own modal loops. Sound is Miles
// on SDL_mixer (miles_sdl.c), video Bink on Akhenaten's decoder (video_bink.c). The display size and full screen
// state come from the command line or the [display] section of tutankhamun.ini, and are written back when changed.
//
// What the window does differently from a plain SDL program, so that the game behaves as it did on Windows:
// - the game stays active when the window loses the focus (WM_ACTIVATEAPP used to pause it);
// - only the game's two modal loops (the message box and the video loop) get the message pump; any other unfiltered
//   peek gets Windows' answer, and a filtered peek is always Windows' own;
// - in a video a key down or a left click ends it as the Win32 message would, and the key's text is not passed on;
// - the screen fade draws on the frame buffer in RGB565 when the game draws in 565 (display mode 2), else RGB555, and
//   takes as many random numbers as on the DirectDraw surface (mode 1: one per row).
// On Android the game is full screen in landscape at a size of the screen's shape about 720 high; a tap is a left
// click, a long press a right click, a drag a left drag, and the back button is Escape; a click's button goes up at
// the next frame, since the game reads the buttons once a frame. A tap in a text field (where the game drew its cursor)
// opens the on-screen keyboard, as Julius does; a tap elsewhere, or the field gone for a while, closes it. Building
// takes a confirming second tap and has a cancel button (widget/touch_construction.c).

#include "app/play.h"

#include "app/app_files.h"
#include "app/app_icon.h"
#include "app/media_lib.h"
#include "app/play_cursors.h"
#include "building/building_data.h"
#include "graphics/graphics_data.h"
#include "input/input_data.h"
#include "io/game_file.h"
#include "platform/platform_data.h"
#include "window/window_data.h"
#include "window/window_id.h"
#include "game/discord_presence.h"
#include "graphics/screen_fade.h"
#include "platform/platform_screen.h"
#include "platform/platform_dialog.h"
#include "platform/platform_input.h"
#include "platform/platform_time.h"
#include "platform/platform_message.h"
#include "platform/platform_display.h"
#include "platform/platform_shell.h"
#include "platform/platform_win32.h"
#include "platform/screen.h"
#include "video/bink.h"
#include "window/display_resolution_list.h"
#include "window/free_play.h"
#include "window/window_proc.h"
#include "widget/touch_construction.h"

#if defined(_WIN32)
#include <windows.h>
#include <mmsystem.h>
#include <intrin.h>
#endif
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "SDL.h"

#if defined(__EMSCRIPTEN__)
// In the browser a frame ends by giving the page its turn until the next animation frame (ASYNCIFY unwinds the game's
// loop and comes back into it): the canvas is shown, the events come in, and the frames keep the display's pace as
// the vsync of the other systems does. A hidden tab gets no animation frames: a timer of a quarter second goes on then.
EM_ASYNC_JS(void, browser_next_frame, (void), {
    await new Promise(function(resolve) {
        var done = false;
        var finish = function() {
            if (!done) {
                done = true;
                resolve();
            }
        };
        requestAnimationFrame(finish);
        setTimeout(finish, 250);
    });
});
#endif
#if defined(_WIN32)
#include "SDL_syswm.h"
#endif


static void no_log(const char *format, ...)
{
    (void) format;
}

void (*play_log)(const char *format, ...) = no_log;
static play_host host;


#if defined(_WIN32)
typedef MSG play_native_message;
#else
typedef struct {
    void *hwnd;
    uint32_t message;
    uintptr_t wParam;
    intptr_t lParam;
    uint32_t time;
} play_native_message;
#endif

static_assert(sizeof(play_native_message) <= sizeof(((platform_message *) 0)->native), "platform_message has room for a play message");

enum play_peek {
    PLAY_PEEK_KEEP = 0,
    PLAY_PEEK_REMOVE = 1,
};

#if defined(_WIN32)
static bool system_peek(play_native_message *msg, uint32_t first, uint32_t last, uint32_t remove)
{
    return PeekMessageA(msg, 0, first, last, remove) != 0;
}

static bool system_get(play_native_message *msg)
{
    return GetMessageA(msg, 0, 0, 0) != 0;
}

static void system_wait(void)
{
    WaitMessage();
}

static bool system_post(void *window, uint32_t message, uintptr_t wparam, intptr_t lparam)
{
    return PostMessageA((HWND) window, message, wparam, lparam) != 0;
}

static uint32_t system_scan_code(int32_t vk)
{
    return MapVirtualKeyA((UINT) vk, 0);
}

static uint32_t system_time_ms(void)
{
    return timeGetTime();
}

static void *native_window(SDL_Window *window)
{
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    return window && SDL_GetWindowWMInfo(window, &info) ? (void *) info.info.win.window : 0;
}
#else
static bool system_peek(play_native_message *msg, uint32_t first, uint32_t last, uint32_t remove)
{
    return false;
}

static bool system_get(play_native_message *msg)
{
    memset(msg, 0, sizeof(*msg));
    msg->message = PLATFORM_MSG_QUIT;
    return false;
}

static void system_wait(void)
{
    SDL_Delay(5);
}

static bool system_post(void *window, uint32_t message, uintptr_t wparam, intptr_t lparam)
{
    return false;
}

static uint32_t system_scan_code(int32_t vk)
{
    return 0;
}

static uint32_t system_time_ms(void)
{
    return SDL_GetTicks();
}

static void *native_window(SDL_Window *window)
{
    return window;
}
#endif

static void screen_rect_set(int32_t width, int32_t height)
{
    window_screen_rect.left = 0;
    window_screen_rect.top = 0;
    window_screen_rect.right = width;
    window_screen_rect.bottom = height;
}

static struct {
    int32_t cursor_x;
    int32_t cursor_y;
    int32_t frame;
    int32_t quit;
    int32_t running;
    int32_t modal_frames;
    uint32_t video_poll_ms;
    int32_t has_message;
    play_native_message message;
    int32_t other_peeks;
    uint8_t key_down[256];
    uint8_t key_pressed[256];
} play;

static bool play_cursor_position(int32_t *x, int32_t *y)
{
    *x = play.cursor_x;
    *y = play.cursor_y;
    return true;
}

static void *shown_cursor;

static void *play_cursor_set(void *cursor)
{
    void *before = shown_cursor;
    if (cursor != shown_cursor) {
        play_cursor_show(cursor);
        shown_cursor = cursor;
    }
    return before;
}

static int16_t play_key_state(int32_t vk)
{
    if (vk < 0 || vk > 255) {
        return 0;
    }
    int16_t state = (int16_t) ((play.key_down[vk] ? 0x8000 : 0) | play.key_pressed[vk]);
    play.key_pressed[vk] = 0;
    return state;
}

static void key_state(int32_t vk, int32_t down)
{
    play.key_pressed[vk] |= (uint8_t) (down && !play.key_down[vk]);
    play.key_down[vk] = (uint8_t) down;
}

static void modal_pump(void);

static void modal_message(uint32_t message, uintptr_t wparam)
{
    memset(&play.message, 0, sizeof(play.message));
    play.message.message = message;
    play.message.wParam = wparam;
    play.message.time = platform_tick_count();
    play.has_message = 1;
}

static bool take_modal_message(play_native_message *msg, uint32_t remove)
{
    if (!play.has_message) {
        return false;
    }
    *msg = play.message;
    if (remove & PLAY_PEEK_REMOVE) {
        play.has_message = 0;
    }
    return true;
}


static int32_t in_modal_loop(void)
{
    return window_current_id == WINDOW_VIDEO || window_current_id == WINDOW_MESSAGE_BOX;
}

static bool peek_message(play_native_message *msg, uint32_t first, uint32_t last, uint32_t remove, void *caller)
{
    if (first || last || !play.running) {
        return system_peek(msg, first, last, remove);
    }
    if (!in_modal_loop()) {
        if (!play.other_peeks++) {
            play_log("play: frame %d unfiltered PeekMessageA from 0x%08X in window %d: no pump outside the modal loops", play.frame,
                (uint32_t) (uintptr_t) caller, window_current_id);
        }
        return system_peek(msg, first, last, remove);
    }
    if (take_modal_message(msg, remove)) {
        return true;
    }
    if (system_peek(msg, 0, 0, remove)) {
        return true;
    }
    modal_pump();
    return take_modal_message(msg, remove);
}

static bool play_message_peek(platform_message *msg, uint32_t first, uint32_t last, bool remove)
{
#if defined(_MSC_VER)
    void *caller = _ReturnAddress();
#elif defined(__EMSCRIPTEN__)
    void *caller = 0; // WebAssembly has no return addresses to read (the log line alone tells of the peek)
#else
    void *caller = __builtin_return_address(0);
#endif
    play_native_message *native = (play_native_message *) msg->native;
    if (!peek_message(native, first, last, remove ? PLAY_PEEK_REMOVE : PLAY_PEEK_KEEP, caller)) {
        return false;
    }
    msg->message = native->message;
    return true;
}

static bool play_message_get(platform_message *msg)
{
    play_native_message *native = (play_native_message *) msg->native;
    bool more;
    if (play.running && take_modal_message(native, PLAY_PEEK_REMOVE)) {
        more = native->message != PLATFORM_MSG_QUIT;
    } else {
        more = system_get(native);
    }
    msg->message = native->message;
    return more;
}

static void play_message_wait(void)
{
    if (!play.running) {
        system_wait();
        return;
    }
    SDL_Delay(5);
}

static int32_t default_box_answer(uint32_t type)
{
    switch (type & PLATFORM_BOX_TYPE_MASK) {
        case PLATFORM_BOX_ABORT_RETRY_IGNORE:
            return PLATFORM_ANSWER_IGNORE;
        case PLATFORM_BOX_YES_NO:
        case PLATFORM_BOX_YES_NO_CANCEL:
            return PLATFORM_ANSWER_NO;
        case PLATFORM_BOX_RETRY_CANCEL:
        case PLATFORM_BOX_OK_CANCEL:
            return PLATFORM_ANSWER_CANCEL;
        default:
            return PLATFORM_ANSWER_OK;
    }
}

static int32_t play_message_box(void *window, const char *text, const char *caption, uint32_t type)
{
    (void) window;
    static const struct {
        int32_t count;
        int32_t ids[3];
        const char *texts[3];
    } layouts[] = {
        { 1, { PLATFORM_ANSWER_OK }, { "OK" } },
        { 2, { PLATFORM_ANSWER_OK, PLATFORM_ANSWER_CANCEL }, { "OK", "Cancel" } },
        { 3, { PLATFORM_ANSWER_ABORT, PLATFORM_ANSWER_RETRY, PLATFORM_ANSWER_IGNORE }, { "Abort", "Retry", "Ignore" } },
        { 3, { PLATFORM_ANSWER_YES, PLATFORM_ANSWER_NO, PLATFORM_ANSWER_CANCEL }, { "Yes", "No", "Cancel" } },
        { 2, { PLATFORM_ANSWER_YES, PLATFORM_ANSWER_NO }, { "Yes", "No" } },
        { 2, { PLATFORM_ANSWER_RETRY, PLATFORM_ANSWER_CANCEL }, { "Retry", "Cancel" } },
    };
    uint32_t layout = type & PLATFORM_BOX_TYPE_MASK;
    if (layout >= sizeof(layouts) / sizeof(layouts[0])) {
        layout = 0;
    }
    SDL_MessageBoxButtonData buttons[3];
    int32_t count = layouts[layout].count;
    for (int32_t i = 0; i < count; i++) {
        buttons[i].flags = (i == 0 ? SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT : 0) | (i == count - 1 ? SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT : 0);
        buttons[i].buttonid = layouts[layout].ids[i];
        buttons[i].text = layouts[layout].texts[i];
    }
    uint32_t icon = type & PLATFORM_BOX_ICON_MASK;
    SDL_MessageBoxData box;
    memset(&box, 0, sizeof(box));
    box.flags = icon == PLATFORM_BOX_ICON_ERROR                        ? SDL_MESSAGEBOX_ERROR
        : icon == PLATFORM_BOX_ICON_WARNING || icon == PLATFORM_BOX_ICON_QUESTION ? SDL_MESSAGEBOX_WARNING
                                                            : SDL_MESSAGEBOX_INFORMATION;
    box.window = (SDL_Window *) platform_screen_window();
    box.title = caption ? caption : "";
    box.message = text ? text : "";
    box.numbuttons = count;
    box.buttons = buttons;
    int32_t answer = -1;
    if (SDL_ShowMessageBox(&box, &answer) != 0) {
        play_log("play: message box \"%s\" not shown: %s", box.title, SDL_GetError());
        answer = default_box_answer(type);
    } else if (answer < 0) {
        answer = layouts[layout].ids[count - 1];
    }
    play_log("play: frame %d message box \"%s\": %s: answer %d", play.frame, box.title, box.message, answer);
    return answer;
}

enum play_posted_keys {
    POSTED_KEYS_MAX = 16,
};

static struct {
    uint32_t message;
    uintptr_t wparam;
    intptr_t lparam;
} posted_keys[POSTED_KEYS_MAX];
static int32_t posted_key_count;

static int32_t is_key_message(uint32_t message)
{
    return message == PLATFORM_MSG_KEY_DOWN || message == PLATFORM_MSG_KEY_UP || message == PLATFORM_MSG_CHAR || message == PLATFORM_MSG_SYS_KEY_DOWN || message == PLATFORM_MSG_SYS_KEY_UP;
}

static bool play_message_post(void *window, uint32_t message, uintptr_t wparam, intptr_t lparam)
{
    if (!play.running || window != platform_main_window || !is_key_message(message)) {
        return system_post(window, message, wparam, lparam);
    }
    if (posted_key_count >= POSTED_KEYS_MAX) {
        play_log("play: frame %d posted key 0x%X dropped, the queue is full", play.frame, (uint32_t) wparam);
        return false;
    }
    posted_keys[posted_key_count].message = message;
    posted_keys[posted_key_count].wparam = wparam;
    posted_keys[posted_key_count].lparam = lparam;
    posted_key_count++;
    return true;
}

static uint32_t play_tick_count(void)
{
    return system_time_ms();
}

static const platform_bindings play_bindings = {
    .tick_count = play_tick_count,
    .key_state = play_key_state,
    .cursor_position = play_cursor_position,
    .cursor_set = play_cursor_set,
    .message_peek = play_message_peek,
    .message_get = play_message_get,
    .message_wait = play_message_wait,
    .message_post = play_message_post,
    .message_box = play_message_box,
};

void play_bind(const play_host *h)
{
    host = *h;
    play_log = host.log;
    platform_bind(&play_bindings);
    media_sound = miles_sdl_library;
    media_video = play_video_library;
}

static void mouse_message(uint32_t message, int32_t x, int32_t y)
{
    play.cursor_x = x;
    play.cursor_y = y;
    switch (message) {
        case PLATFORM_MSG_LEFT_DOWN:
            mouse_left_clicked = 1;
            mouse_buttons |= 1;
            break;
        case PLATFORM_MSG_LEFT_UP:
            mouse_buttons &= (uint8_t) ~1;
            break;
        case PLATFORM_MSG_RIGHT_DOWN:
            mouse_buttons |= 2;
            break;
        case PLATFORM_MSG_RIGHT_UP:
            mouse_buttons &= (uint8_t) ~2;
            break;
        default:
            break;
    }
    mouse_window_x = (int16_t) x;
    mouse_window_y = (int16_t) y;
}

static void window_message(uint32_t message, uintptr_t wparam, intptr_t lparam)
{
    window_proc(platform_main_window, message, wparam, lparam);
}

static int32_t virtual_key(SDL_Keycode key)
{
    if (key >= SDLK_a && key <= SDLK_z) {
        return 'A' + (key - SDLK_a);
    }
    if (key >= SDLK_0 && key <= SDLK_9) {
        return key;
    }
    if (key >= SDLK_F1 && key <= SDLK_F12) {
        return PLATFORM_KEY_F1 + (key - SDLK_F1);
    }
    if (key >= SDLK_KP_1 && key <= SDLK_KP_9) {
        return PLATFORM_KEY_NUMPAD_1 + (key - SDLK_KP_1);
    }
    switch (key) {
        case SDLK_KP_0:
            return PLATFORM_KEY_NUMPAD_0;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            return PLATFORM_KEY_RETURN;
        case SDLK_ESCAPE:
            return PLATFORM_KEY_ESCAPE;
        case SDLK_BACKSPACE:
            return PLATFORM_KEY_BACKSPACE;
        case SDLK_TAB:
            return PLATFORM_KEY_TAB;
        case SDLK_SPACE:
            return PLATFORM_KEY_SPACE;
        case SDLK_LEFT:
            return PLATFORM_KEY_LEFT;
        case SDLK_RIGHT:
            return PLATFORM_KEY_RIGHT;
        case SDLK_UP:
            return PLATFORM_KEY_UP;
        case SDLK_DOWN:
            return PLATFORM_KEY_DOWN;
        case SDLK_PAGEUP:
            return PLATFORM_KEY_PAGE_UP;
        case SDLK_PAGEDOWN:
            return PLATFORM_KEY_PAGE_DOWN;
        case SDLK_HOME:
            return PLATFORM_KEY_HOME;
        case SDLK_END:
            return PLATFORM_KEY_END;
        case SDLK_INSERT:
            return PLATFORM_KEY_INSERT;
        case SDLK_DELETE:
            return PLATFORM_KEY_DELETE;
        case SDLK_LSHIFT:
        case SDLK_RSHIFT:
            return PLATFORM_KEY_SHIFT;
        case SDLK_LCTRL:
        case SDLK_RCTRL:
            return PLATFORM_KEY_CONTROL;
        case SDLK_LALT:
        case SDLK_RALT:
            return PLATFORM_KEY_ALT;
        case SDLK_MINUS:
        case SDLK_KP_MINUS:
            return PLATFORM_KEY_OEM_MINUS;
        case SDLK_EQUALS:
        case SDLK_KP_PLUS:
            return PLATFORM_KEY_OEM_PLUS;
        case SDLK_COMMA:
            return PLATFORM_KEY_OEM_COMMA;
        case SDLK_PERIOD:
            return PLATFORM_KEY_OEM_PERIOD;
        case SDLK_LEFTBRACKET:
            return PLATFORM_KEY_OEM_OPEN_BRACKET;
        case SDLK_RIGHTBRACKET:
            return PLATFORM_KEY_OEM_CLOSE_BRACKET;
        case SDLK_PAUSE:
            return PLATFORM_KEY_PAUSE;
        case SDLK_AC_BACK:
            return PLATFORM_KEY_ESCAPE;
        default:
            return 0;
    }
}

static void key_event(const SDL_KeyboardEvent *e)
{
    int32_t vk = virtual_key(e->keysym.sym);
    if (!vk) {
        return;
    }
    int32_t down = e->type == SDL_KEYDOWN;
    key_state(vk, down);
    if (vk == PLATFORM_KEY_ALT || vk == PLATFORM_KEY_SHIFT || vk == PLATFORM_KEY_CONTROL) {
        key_state(vk == PLATFORM_KEY_ALT ? PLATFORM_KEY_LEFT_ALT : vk == PLATFORM_KEY_SHIFT ? PLATFORM_KEY_LEFT_SHIFT : PLATFORM_KEY_LEFT_CONTROL, down);
    }
    int32_t alt = (e->keysym.mod & KMOD_ALT) != 0 && vk != PLATFORM_KEY_ALT;
    intptr_t lparam = 1 | (intptr_t) (system_scan_code(vk) << 16) | (alt ? 1 << 29 : 0);
    if (e->type == SDL_KEYDOWN) {
        window_message(alt ? PLATFORM_MSG_SYS_KEY_DOWN : PLATFORM_MSG_KEY_DOWN, (uintptr_t) vk, lparam | (e->repeat ? 1 << 30 : 0));
        if (!alt && (vk == PLATFORM_KEY_BACKSPACE || vk == PLATFORM_KEY_TAB || vk == PLATFORM_KEY_RETURN || vk == PLATFORM_KEY_ESCAPE)) {
            window_message(PLATFORM_MSG_CHAR, (uintptr_t) vk, lparam);
        }
    } else {
        window_message(alt ? PLATFORM_MSG_SYS_KEY_UP : PLATFORM_MSG_KEY_UP, (uintptr_t) vk, lparam | (intptr_t) 0xC0000000u);
    }
}

static void text_event(const char *text)
{
    const uint8_t *p = (const uint8_t *) text;
    while (*p) {
        uint32_t c = *p++;
        if (c >= 0xC0 && c < 0xE0 && (*p & 0xC0) == 0x80) {
            c = ((c & 0x1F) << 6) | (*p++ & 0x3F);
        } else if (c >= 0x80) {
            continue;
        }
        if (c < 0x100) {
            window_message(PLATFORM_MSG_CHAR, (uintptr_t) c, 1);
        }
    }
}

#if defined(__ANDROID__)
enum touch_state {
    TOUCH_NONE = 0,
    TOUCH_PENDING = 1,
    TOUCH_DRAG = 2,
    TOUCH_DONE = 3,
};

enum touch_limits {
    TOUCH_LONG_PRESS_MS = 500,
    TOUCH_DRAG_PIXELS = 12,
};

enum android_screen {
    ANDROID_SCREEN_HEIGHT = 720,
};

enum text_field_area {
    TEXT_FIELD_LEFT = 16,
    TEXT_FIELD_TOP = 12,
    TEXT_FIELD_WIDTH = 336,
    TEXT_FIELD_HEIGHT = 40,
    TEXT_FIELD_GONE_FRAMES = 30,
};

static struct {
    uint32_t draws;
    int32_t idle_frames;
} text_field = { 0, TEXT_FIELD_GONE_FRAMES };

static bool text_field_shown(void)
{
    return text_field.idle_frames < TEXT_FIELD_GONE_FRAMES;
}

static void text_field_frame(void)
{
    if (text_field_draws != text_field.draws) {
        text_field.draws = text_field_draws;
        text_field.idle_frames = 0;
    } else if (text_field.idle_frames < TEXT_FIELD_GONE_FRAMES) {
        text_field.idle_frames++;
    }
    if (!text_field_shown() && SDL_IsTextInputActive()) {
        SDL_StopTextInput();
    }
}

static void text_field_input_rect(int32_t left, int32_t top)
{
    SDL_Window *window = platform_screen_window();
    int32_t window_width = 0;
    int32_t window_height = 0;
    if (window) {
        SDL_GetWindowSize(window, &window_width, &window_height);
    }
    if (window_width <= 0 || window_height <= 0 || g_screen_width <= 0 || g_screen_height <= 0) {
        return;
    }
    float scale_x = (float) window_width / (float) g_screen_width;
    float scale_y = (float) window_height / (float) g_screen_height;
    float scale = scale_x < scale_y ? scale_x : scale_y;
    float origin_x = ((float) window_width - ((float) g_screen_width * scale)) / 2.0f;
    float origin_y = ((float) window_height - ((float) g_screen_height * scale)) / 2.0f;
    SDL_Rect rect = {
        (int) (origin_x + ((float) left * scale)),
        (int) (origin_y + ((float) top * scale)),
        (int) ((float) TEXT_FIELD_WIDTH * scale),
        (int) ((float) TEXT_FIELD_HEIGHT * scale),
    };
    SDL_SetTextInputRect(&rect);
}

static void text_field_tap(int32_t x, int32_t y)
{
    int32_t left = text_field_x - TEXT_FIELD_LEFT;
    int32_t top = text_field_y - TEXT_FIELD_TOP;
    bool inside = text_field_shown() && x >= left && x < left + TEXT_FIELD_WIDTH && y >= top && y < top + TEXT_FIELD_HEIGHT;
    if (!inside) {
        if (SDL_IsTextInputActive()) {
            SDL_StopTextInput();
        }
        return;
    }
    text_field_input_rect(left, top);
    if (!SDL_IsTextInputActive()) {
        SDL_StartTextInput();
    }
}

static struct {
    int32_t state;
    int32_t x;
    int32_t y;
    uint32_t down_ms;
    int32_t release_message;
    int32_t release_key;
} touch;

static void touch_left_down(int32_t x, int32_t y)
{
    key_state(PLATFORM_KEY_LEFT_BUTTON, 1);
    mouse_message(PLATFORM_MSG_LEFT_DOWN, x, y);
}

static void touch_left_up(int32_t x, int32_t y)
{
    key_state(PLATFORM_KEY_LEFT_BUTTON, 0);
    mouse_message(PLATFORM_MSG_LEFT_UP, x, y);
}

static void touch_click(int32_t key, int32_t down_message, int32_t up_message, int32_t x, int32_t y)
{
    key_state(key, 1);
    mouse_message(down_message, x, y);
    touch.release_message = up_message;
    touch.release_key = key;
    touch.x = x;
    touch.y = y;
}

static void touch_release(void)
{
    if (!touch.release_message) {
        return;
    }
    key_state(touch.release_key, 0);
    mouse_message(touch.release_message, touch.x, touch.y);
    touch.release_message = 0;
}

static bool touch_moved_far(int32_t x, int32_t y)
{
    int32_t dx = x - touch.x;
    int32_t dy = y - touch.y;
    return (dx * dx) + (dy * dy) > TOUCH_DRAG_PIXELS * TOUCH_DRAG_PIXELS;
}

static bool touch_event(const SDL_Event *e)
{
    switch (e->type) {
        case SDL_MOUSEBUTTONDOWN:
            if (e->button.which != SDL_TOUCH_MOUSEID) {
                return false;
            }
            touch_release();
            touch.state = TOUCH_PENDING;
            touch.x = e->button.x;
            touch.y = e->button.y;
            touch.down_ms = SDL_GetTicks();
            mouse_message(PLATFORM_MSG_MOUSE_MOVE, touch.x, touch.y);
            return true;
        case SDL_MOUSEMOTION:
            if (e->motion.which != SDL_TOUCH_MOUSEID) {
                return false;
            }
            if (touch.state == TOUCH_PENDING && touch_moved_far(e->motion.x, e->motion.y)) {
                touch.state = TOUCH_DRAG;
                touch_left_down(touch.x, touch.y);
            }
            mouse_message(PLATFORM_MSG_MOUSE_MOVE, e->motion.x, e->motion.y);
            return true;
        case SDL_MOUSEBUTTONUP:
            if (e->button.which != SDL_TOUCH_MOUSEID) {
                return false;
            }
            if (touch.state == TOUCH_PENDING) {
                touch_click(PLATFORM_KEY_LEFT_BUTTON, PLATFORM_MSG_LEFT_DOWN, PLATFORM_MSG_LEFT_UP, e->button.x, e->button.y);
                text_field_tap(e->button.x, e->button.y);
            } else if (touch.state == TOUCH_DRAG) {
                touch_left_up(e->button.x, e->button.y);
            }
            touch.state = TOUCH_NONE;
            return true;
        default:
            return false;
    }
}

static void touch_long_press(void)
{
    if (touch.state != TOUCH_PENDING || SDL_GetTicks() - touch.down_ms < TOUCH_LONG_PRESS_MS) {
        return;
    }
    touch.state = TOUCH_DONE;
    touch_click(PLATFORM_KEY_RIGHT_BUTTON, PLATFORM_MSG_RIGHT_DOWN, PLATFORM_MSG_RIGHT_UP, touch.x, touch.y);
}

static void android_display_size(play_display_request *request)
{
    SDL_DisplayMode desktop;
    if (SDL_GetDesktopDisplayMode(0, &desktop) != 0 || desktop.w <= 0 || desktop.h <= 0) {
        return;
    }
    int32_t long_side = desktop.w > desktop.h ? desktop.w : desktop.h;
    int32_t short_side = desktop.w > desktop.h ? desktop.h : desktop.w;
    int32_t height = short_side < ANDROID_SCREEN_HEIGHT ? short_side : ANDROID_SCREEN_HEIGHT;
    request->height = height;
    request->width = ((height * long_side) / short_side) & ~1;
}
#endif

static void handle_event(const SDL_Event *e)
{
#if defined(__ANDROID__)
    if (touch_event(e)) {
        return;
    }
#endif
    switch (e->type) {
        case SDL_QUIT:
            play.quit = 1;
            break;
        case SDL_WINDOWEVENT:
            if (e->window.event == SDL_WINDOWEVENT_EXPOSED || e->window.event == SDL_WINDOWEVENT_RESTORED) {
                window_redraw_request = 1;
                g_force_refresh = 1;
            }
            if (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST || e->window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                int32_t lost = e->window.event == SDL_WINDOWEVENT_FOCUS_LOST;
                play_log("play: frame %d focus %s (window %d)", play.frame, lost ? "lost" : "gained", window_current_id);
            }
            break;
        case SDL_MOUSEMOTION:
            mouse_message(PLATFORM_MSG_MOUSE_MOVE, e->motion.x, e->motion.y);
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
            int32_t down = e->type == SDL_MOUSEBUTTONDOWN;
            if (e->button.button == SDL_BUTTON_LEFT || e->button.button == SDL_BUTTON_RIGHT) {
                key_state(e->button.button == SDL_BUTTON_LEFT ? PLATFORM_KEY_LEFT_BUTTON : PLATFORM_KEY_RIGHT_BUTTON, down);
            }
            if (e->button.button == SDL_BUTTON_LEFT) {
                mouse_message(down ? PLATFORM_MSG_LEFT_DOWN : PLATFORM_MSG_LEFT_UP, e->button.x, e->button.y);
            } else if (e->button.button == SDL_BUTTON_RIGHT) {
                mouse_message(down ? PLATFORM_MSG_RIGHT_DOWN : PLATFORM_MSG_RIGHT_UP, e->button.x, e->button.y);
            }
            break;
        }
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            key_event(&e->key);
            break;
        case SDL_TEXTINPUT:
            text_event(e->text.text);
            break;
        default:
            break;
    }
}

void play_load_save(const char *path)
{
    if (game_file_load_saved_game(path) != 0) {
        play_log("play: cannot load %s", path);
        return;
    }
    window_current_id = WINDOW_CITY;
    window_redraw_request = 1;
    play_log("play: loaded %s", path);
}

static size_t frame_buffer_bytes;
static int32_t fullscreen_setting;
static int32_t fullscreen;

static void display_init(void)
{
    size_t bytes = (size_t) g_screen_width * (size_t) g_screen_height * 2;
    void **pixels = (void **) &screen_rgb565_ptr;
    if (bytes != frame_buffer_bytes || !*pixels) {
        free(*pixels);
        *pixels = calloc(1, bytes);
        frame_buffer_bytes = bytes;
    }
    SDL_Window *window = platform_screen_window();
    if (window) {
        if (settings_data.fullscreen != fullscreen_setting) {
            fullscreen_setting = settings_data.fullscreen;
            fullscreen = !fullscreen;
            SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        }
        if (!fullscreen) {
            SDL_SetWindowSize(window, g_screen_width, g_screen_height);
        }
    }
    screen_rect_set(g_screen_width, g_screen_height);
    desktop_width = g_screen_width;
    desktop_height = g_screen_height;
    screen_fade_set_frame(&fade_object, screen_rgb565_ptr, g_screen_width, g_screen_height);
    play_log("play: display %dx%d%s", g_screen_width, g_screen_height, fullscreen ? ", full screen" : "");
}

static int32_t display_shutdown(int32_t release_display)
{
    (void) release_display;
    int32_t result = dd_enabled;
    if (result) {
        result = 1;
        dd_enabled = 0;
    }
    return result;
}

static int32_t display_begin(void)
{
    g_skip_refresh = 1;
    display_shutdown(0);
    display_init();
    dd_good_display_mode = settings_data.display_mode;
    dd_good_fullscreen = settings_data.fullscreen;
    dd_mode_retry_flag = 0;
    dd_enabled = 1;
    memset(screen_rgb565_ptr, 0, frame_buffer_bytes);
    g_skip_refresh = 0;
    app_active = 1;
    window_redraw_request = 1;
    return 1;
}

static void deliver_posted_keys(void)
{
    int32_t count = posted_key_count;
    posted_key_count = 0;
    for (int32_t i = 0; i < count; i++) {
        window_message(posted_keys[i].message, posted_keys[i].wparam, posted_keys[i].lparam);
    }
}

static void frame_begin(void)
{
    SDL_Event e;
#if defined(__ANDROID__)
    touch_release();
#endif
    while (SDL_PollEvent(&e)) {
        handle_event(&e);
    }
#if defined(__ANDROID__)
    touch_long_press();
    text_field_frame();
#endif
    deliver_posted_keys();
}

static void frame_end(void)
{
    platform_screen_update();
    platform_screen_render();
#if defined(__EMSCRIPTEN__)
    browser_next_frame();
#endif
    if (game_quit_request == 1) {
        play.quit = 1;
    }
    play.frame++;
}

static void video_pump(void)
{
    if (play.quit) {
        if (!play.has_message) {
            play_log("play: frame %d quit asked in a video: the video ends", play.frame);
            modal_message(PLATFORM_MSG_KEY_DOWN, PLATFORM_KEY_ESCAPE);
        }
        return;
    }
    uint32_t now = SDL_GetTicks();
    if (now - play.video_poll_ms < 10) {
        return;
    }
    play.video_poll_ms = now;
    SDL_Event e;
    while (!play.has_message && SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) {
            play.quit = 1;
            play_log("play: frame %d window closed in a video: the video ends", play.frame);
            modal_message(PLATFORM_MSG_KEY_DOWN, PLATFORM_KEY_ESCAPE);
        } else if (e.type == SDL_KEYDOWN) {
            int32_t vk = virtual_key(e.key.keysym.sym);
            play_log("play: frame %d key %d in a video: the video ends", play.frame, vk);
            modal_message(PLATFORM_MSG_KEY_DOWN, (uintptr_t) vk);
        } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            play_log("play: frame %d click in a video: the video ends", play.frame);
            mouse_message(PLATFORM_MSG_MOUSE_MOVE, e.button.x, e.button.y);
            modal_message(PLATFORM_MSG_LEFT_DOWN, 0);
        } else if (e.type == SDL_KEYUP) {
            int32_t vk = virtual_key(e.key.keysym.sym);
            if (vk) {
                key_state(vk, 0);
            }
        } else if (e.type != SDL_TEXTINPUT) {
            handle_event(&e);
        }
    }
}

static void modal_pump(void)
{
    if (window_current_id == WINDOW_VIDEO) {
        video_pump();
        return;
    }
    frame_end();
    play.modal_frames++;
    frame_begin();
    if (play.quit && !play.has_message) {
        play_log("play: frame %d quit in the modal loop of window %d: PLATFORM_MSG_QUIT", play.frame, window_current_id);
        modal_message(PLATFORM_MSG_QUIT, 0);
    }
}

static int32_t fullscreen_setting_before_video;

int32_t play_fullscreen_for_video(void)
{
    fullscreen_setting_before_video = fullscreen_setting;
    fullscreen_setting = fullscreen;
    return fullscreen;
}

void play_fullscreen_after_video(void)
{
    fullscreen_setting = fullscreen_setting_before_video;
}

static void install_icon_and_cursors(void)
{
    SDL_Surface *icon = SDL_CreateRGBSurfaceWithFormatFrom((void *) app_icon_argb, APP_ICON_SIZE, APP_ICON_SIZE, 32,
        APP_ICON_SIZE * (int) sizeof(app_icon_argb[0]), SDL_PIXELFORMAT_ARGB8888);
    if (icon) {
        SDL_SetWindowIcon(platform_screen_window(), icon);
        SDL_FreeSurface(icon);
    }
    int32_t cursors = play_cursors_install();
    play_log("play: icon %s, %d own cursors made", icon ? "set" : "missing", cursors);
}

enum play_display_file {
    DISPLAY_FILE_VALUE_SIZE = 16,
    DISPLAY_MODES_MAX = 128,
};

static const char display_ini_section[] = "display";

static struct {
    int32_t chosen;
    int32_t width;
    int32_t height;
    int32_t fullscreen;
} display_start;

static play_display_request display_file_read(void)
{
    play_display_request remembered = { 0, 0, -1 };
    char path[1024];
    app_ini_path(path, sizeof(path));
    if (!path[0]) {
        return remembered;
    }
    remembered.width = app_ini_read_int(path, display_ini_section, "width", 0);
    remembered.height = app_ini_read_int(path, display_ini_section, "height", 0);
    remembered.fullscreen = app_ini_read_int(path, display_ini_section, "fullscreen", -1);
    if (remembered.fullscreen > 0) {
        remembered.fullscreen = 1;
    }
    return remembered;
}

static int32_t display_ini_write(const char *path, const char *key, int32_t value)
{
    char text[DISPLAY_FILE_VALUE_SIZE];
    snprintf(text, sizeof(text), "%d", value);
    return app_ini_write(path, display_ini_section, key, text);
}

static void display_file_write(void)
{
    char path[1024];
    app_ini_path(path, sizeof(path));
    int32_t written = path[0] && display_ini_write(path, "width", g_screen_width) && display_ini_write(path, "height", g_screen_height) &&
        display_ini_write(path, "fullscreen", fullscreen);
    play_log(written ? "play: display %dx%d%s remembered in %s" : "play: cannot remember the display %dx%d%s in %s", g_screen_width,
        g_screen_height, fullscreen ? ", full screen" : "", path[0] ? path : "?");
}

static void display_limit_from_sdl(void)
{
    SDL_DisplayMode desktop;
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) == 0 && SDL_GetDesktopDisplayMode(0, &desktop) == 0) {
        display_resolution_set_limit(desktop.w, desktop.h);
        play_log("play: the display is %dx%d: sizes from 640x480 up to it", desktop.w, desktop.h);
    }
}

int32_t play_display_prepare(const play_display_request *command_line)
{
    int32_t use_ini = 1;
    display_limit_from_sdl();
    play_display_request none = { 0, 0, -1 };
    play_display_request request = use_ini ? display_file_read() : none;
    if (command_line->width > 0 && command_line->height > 0) {
        request.width = command_line->width;
        request.height = command_line->height;
    }
    if (command_line->fullscreen >= 0) {
        request.fullscreen = command_line->fullscreen;
    }
#if defined(__ANDROID__)
    if (request.width <= 0 || request.height <= 0) {
        android_display_size(&request);
    }
    request.fullscreen = 1;
#endif
    display_start.chosen = use_ini;
    display_start.fullscreen = request.fullscreen;
    if (request.width <= 0 || request.height <= 0) {
        return 0;
    }
    screen_size size = display_resolution_nearest(request.width, request.height);
    if (size.width != request.width || size.height != request.height) {
        fprintf(stderr, "the size %dx%d is not supported (640x480 up to the display's size): %dx%d is used\n", request.width, request.height,
            size.width, size.height);
        play_log("play: size %dx%d not supported, the nearest %dx%d is used", request.width, request.height, size.width, size.height);
    }
    display_resolution_start_custom(size);
    return display_resolution_layout_mode(size.width, size.height);
}

static void display_list_from_sdl(SDL_Window *window)
{
    screen_size modes[DISPLAY_MODES_MAX];
    int32_t count = 0;
    int32_t display = window ? SDL_GetWindowDisplayIndex(window) : -1;
    int32_t available = display >= 0 ? SDL_GetNumDisplayModes(display) : 0;
    for (int32_t i = 0; i < available && count < DISPLAY_MODES_MAX; i++) {
        SDL_DisplayMode mode;
        if (SDL_GetDisplayMode(display, i, &mode) == 0) {
            modes[count].width = mode.w;
            modes[count].height = mode.h;
            count++;
        }
    }
    display_resolution_list_set_display_modes(modes, count);
    display_resolution_list_allow(1);
    play_log("play: %d display modes, %d resolutions in the display options", available, display_resolution_list_count());
}

static void display_start_fullscreen(SDL_Window *window)
{
    if (display_start.fullscreen < 0) {
        return;
    }
    settings_data.fullscreen = display_start.fullscreen;
    fullscreen_setting = settings_data.fullscreen;
    fullscreen = settings_data.fullscreen;
    if (window && fullscreen) {
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    }
}

static void display_start_keep(void)
{
    display_start.width = g_screen_width;
    display_start.height = g_screen_height;
    display_start.fullscreen = fullscreen;
}

static void display_remember_if_changed(void)
{
    if (g_screen_width != display_start.width || g_screen_height != display_start.height || fullscreen != display_start.fullscreen) {
        display_file_write();
    }
}

int32_t play_run(const char *save)
{
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
#if defined(__ANDROID__)
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    touch_construction_enabled = 1;
#endif
    if (!platform_screen_create("Tutankhamun", g_screen_width, g_screen_height)) {
        fprintf(stderr, "cannot open the window: %s\n", SDL_GetError());
        return 2;
    }
    platform_main_window = native_window(platform_screen_window());
    install_icon_and_cursors();
    app_active = 1;
    screen_rect_set(g_screen_width, g_screen_height);
    desktop_width = g_screen_width;
    desktop_height = g_screen_height;
    frame_buffer_bytes = (size_t) g_screen_width * (size_t) g_screen_height * 2;
    fullscreen_setting = settings_data.fullscreen;
    screen_fade_set_frame(&fade_object, screen_rgb565_ptr, g_screen_width, g_screen_height);
    dd_enabled = 1;
    platform_display_set_host(display_begin, display_shutdown);
    platform_web_links_allow(1);
    free_play_allow(1);
    discord_presence_start();
    if (display_start.chosen) {
        display_list_from_sdl(platform_screen_window());
        display_start_fullscreen(platform_screen_window());
        display_start_keep();
    }
    if (!video_player_ptr) {
        video_player_ptr = video_bink_init(calloc(1, sizeof(video_player)), platform_main_window);
    }
    play_log("play: window %dx%d, hwnd %p", g_screen_width, g_screen_height, (void *) platform_main_window);
    play.running = 1;
    video_play_intro();
    if (save) {
        play_load_save(save);
    }
#if !defined(__ANDROID__)
    SDL_StartTextInput();
#endif
    while (!play.quit) {
        frame_begin();
        host.run_tick();
        host.run_frame();
        frame_end();
    }
    if (display_start.chosen) {
        display_remember_if_changed();
    }
    play_log("play: quit after %d frames (%d in the game's message boxes, %d unfiltered peeks outside them)", play.frame, play.modal_frames,
        play.other_peeks);
    discord_presence_stop();
    platform_screen_destroy();
    SDL_Quit();
    return 0;
}
