// Building on a touch screen, after Julius (widget/city.c): with a building chosen, a first tap only puts its ghost on
// the tapped tile and a second tap on the ghost (within its footprint) builds it there; dragging moves the ghost. The
// tools that are dragged with a mouse (roads, walls, canals, clearing, housing, plazas, gardens, festival places) are
// still dragged and built when the finger lifts. A cancel button at the top right of the city view drops the chosen
// building; it sits under the city view's rotate button. Off unless the platform sets touch_construction_enabled.

#include "widget/touch_construction.h"
#include "building/building_data.h"
#include "building/building_type.h"
#include "building/construction_place.h"
#include "graphics/image_collection.h"
#include "graphics/image_draw.h"
#include "input/input_data.h"
#include "graphics/graphics_data.h"
#include "widget/city_view.h"
#include "widget/city_viewport.h"
#include "window/window_id.h"
#include "widget/widget_data.h"
#include "window/window_data.h"
#include "window/window_input.h"

enum touch_cancel_button {
    TOUCH_CANCEL_GROUP = 365,
    TOUCH_CANCEL_IMAGE = 4,
    TOUCH_CANCEL_SIZE = 34,
    TOUCH_CANCEL_MARGIN = 8,
    TOUCH_CANCEL_TOP = 44,
    TOUCH_CANCEL_REACH = 8,
};

int32_t touch_construction_enabled;

static struct {
    int32_t type;
    int32_t offset;
    int32_t x;
    int32_t y;
    int32_t swallow;
} pending;

static void cancel_button_origin(int32_t *x, int32_t *y)
{
    int32_t view_width = g_sidebar_collapsed ? city_view_width_collapsed : city_view_width_expanded;
    *x = view_width - TOUCH_CANCEL_SIZE - TOUCH_CANCEL_MARGIN;
    *y = VIEWPORT_TOP + TOUCH_CANCEL_TOP;
}

static int32_t on_cancel_button(void)
{
    int32_t x;
    int32_t y;
    cancel_button_origin(&x, &y);
    return mouse_x >= x - TOUCH_CANCEL_REACH && mouse_x < x + TOUCH_CANCEL_SIZE + TOUCH_CANCEL_REACH && mouse_y >= y - TOUCH_CANCEL_REACH &&
        mouse_y < y + TOUCH_CANCEL_SIZE + TOUCH_CANCEL_REACH;
}

static void remember_hover(void)
{
    pending.offset = map_info.hover_offset;
    pending.x = map_info.hover_x;
    pending.y = map_info.hover_y;
}

static int32_t on_ghost(int32_t type)
{
    if (pending.offset == 0) {
        return 0;
    }
    int32_t size = building_property_get(type)->size;
    if (size < 1) {
        size = 1;
    }
    int32_t dx = map_info.hover_x - pending.x;
    int32_t dy = map_info.hover_y - pending.y;
    return dx > -size && dx < size && dy > -size && dy < size;
}

int32_t touch_map_pan_allowed(int32_t x, int32_t y)
{
    if (!touch_construction_enabled || window_current_id != WINDOW_CITY || construction_building_type != BUILDING_NONE) {
        return 0;
    }
    int32_t view_width = g_sidebar_collapsed ? city_view_width_collapsed : city_view_width_expanded;
    return x >= 0 && x < view_width && y >= VIEWPORT_TOP && y < g_screen_height;
}

void touch_map_pan(int32_t columns, int32_t row_pairs)
{
    city_view_pan(columns, row_pairs);
}

int32_t touch_construction_cancel_input(void)
{
    if (!touch_construction_enabled || construction_building_type == BUILDING_NONE || !on_cancel_button()) {
        return 0;
    }
    if (!mouse_left_went_down && !mouse_left_down && !mouse_left_went_up) {
        return 0;
    }
    if (mouse_left_went_up) {
        construction_cancel();
        pending.type = BUILDING_NONE;
        pending.offset = 0;
        pending.swallow = 0;
    }
    return 1;
}

int32_t touch_construction_input(void)
{
    if (!touch_construction_enabled) {
        return 0;
    }
    int32_t type = construction_building_type;
    if (type != pending.type || type == BUILDING_NONE) {
        pending.type = type;
        pending.offset = 0;
        pending.swallow = 0;
    }
    if (type == BUILDING_NONE) {
        return 0;
    }
    if (building_construction_is_drag_tool(type) || map_info.selected_grid == 0) {
        return 0;
    }
    if (pending.swallow) {
        if (mouse_left_down) {
            remember_hover();
        }
        if (mouse_left_went_up || !mouse_left_down) {
            pending.swallow = 0;
        }
        return 1;
    }
    if (!mouse_left_went_down) {
        return 0;
    }
    if (on_ghost(type)) {
        map_info.hover_offset = pending.offset;
        map_info.hover_x = pending.x;
        map_info.hover_y = pending.y;
        pending.offset = 0;
        return 0;
    }
    remember_hover();
    pending.swallow = 1;
    return 1;
}

void touch_construction_draw(void)
{
    if (!touch_construction_enabled || construction_building_type == BUILDING_NONE) {
        return;
    }
    int32_t x;
    int32_t y;
    cancel_button_origin(&x, &y);
    image_draw(image_group(TOUCH_CANCEL_GROUP) + TOUCH_CANCEL_IMAGE, x, y);
}
