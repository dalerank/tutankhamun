// The city view: the lookup of the grid offset each view tile shows for the orientation, the camera's map
// bounds, pixel to tile conversion, going to a tile, the tile selected with the mouse, and scrolling at the
// screen's edges and by the keys.
//
// Making the bounds and camera rows even also cuts them to their low 16 bits.

#include "widget/city_view.h"
#include "building/building_data.h"
#include "figure/figure_data.h"
#include "graphics/image_collection.h"
#include "input/input_data.h"
#include "map/grid.h"
#include "platform/platform_data.h"
#include "platform/platform_time.h"
#include "sound/city_view.h"
#include "widget/city_view_rotate.h"
#include "widget/construction_ghost_monuments.h"
#include "widget/widget_data.h"
#include "widget/widget_image_group.h"
#include "window/window_id.h"
#include <stdint.h>

enum city_view_layout {
    VIEW_LAST_COL = VIEW_COLS - 1,
    VIEW_LAST_ROW = VIEW_ROWS - 1,
    VIEW_MIDDLE_ROW = GRID_SIZE,
    VIEW_MIDDLE_COL = 115,
    VIEW_LAST_EVEN_ROW = VIEW_ROWS - 3,
    VIEW_EVEN_ROW_MASK = 0xFFFE,
    MAP_IMAGE_FIRST_SHOWN = 6,
    TILE_GAP_X = 2,
    EDITOR_BOUNDS_LEFT = 2,
    EDITOR_BOUNDS_TOP = 5,
    EDITOR_BOUNDS_RIGHT = 3,
    EDITOR_BOUNDS_BOTTOM = 5,
};

enum view_edge {
    VIEW_EDGE_NONE = 0,
    VIEW_EDGE_PAST_LAST = 1,
    VIEW_EDGE_FIRST = 2,
};

enum scroll_rule {
    SCROLL_NONE = DIRECTION_AT_DESTINATION,
    SCROLL_TILES = 1,
    SCROLL_FAST_TILES = 4,
    SCROLL_SPEED_MAX = 100,
    SCROLL_SPEED_PER_STEP = 10,
    SCROLL_STEPS = 10,
    SCROLL_MS_PER_STEP = 12,
    SCROLL_MS_MIN = 2,
    SCROLL_ELAPSED_WRAPPED = 10000,
};

typedef struct {
    int32_t row;
    int32_t col2;
    int32_t row_step_y;
    int32_t col2_step_y;
    int32_t row_step_x;
    int32_t col2_step_x;
} lookup_walk;

const int32_t city_view_scroll_fast = 1;

static int32_t lookup_empty(void)
{
    return image_group(IMAGE_GROUP_VIEW_LOOKUP_EMPTY) + VIEW_LOOKUP_EMPTY;
}

static int32_t is_in_view(int32_t row, int32_t col)
{
    return row >= 0 && col >= 0 && row < VIEW_ROWS && col < VIEW_COLS;
}

int32_t city_view_reset_lookup(void)
{
    int32_t empty = lookup_empty();
    city_view.x_max = VIEW_LAST_COL;
    for (int32_t i = 0; i < VIEW_COLS * VIEW_ROWS; i++) {
        city_view.lookup[i] = empty;
    }
    city_view.y_max = VIEW_LAST_ROW;
    city_view.x_min = 0;
    city_view.y_min = 0;
    return 0;
}

static lookup_walk lookup_walk_for(int32_t orientation)
{
    lookup_walk walk;
    if (orientation == VIEW_ORIENTATION_NORTH) {
        walk.row = 1;
        walk.row_step_y = 1;
        walk.row_step_x = 1;
        walk.col2 = VIEW_LAST_COL;
        walk.col2_step_y = -1;
        walk.col2_step_x = 1;
    } else if (orientation == VIEW_ORIENTATION_EAST) {
        walk.row = VIEW_LAST_COL;
        walk.row_step_y = 1;
        walk.row_step_x = -1;
        walk.col2 = 1;
        walk.col2_step_y = 1;
        walk.col2_step_x = 1;
    } else {
        if (orientation == VIEW_ORIENTATION_SOUTH) {
            walk.row = VIEW_LAST_ROW - 1;
            walk.row_step_x = -1;
            walk.col2 = VIEW_LAST_COL;
            walk.col2_step_y = 1;
        } else {
            walk.row = VIEW_LAST_COL;
            walk.row_step_x = 1;
            walk.col2 = VIEW_LAST_ROW - 1;
            walk.col2_step_y = -1;
        }
        walk.row_step_y = -1;
        walk.col2_step_x = -1;
    }
    return walk;
}

void city_view_calculate_lookup(int32_t orientation)
{
    city_view_reset_lookup();
    city_view.map_size_b = GRID_SIZE;
    city_view.map_size_a = GRID_SIZE;
    city_view.map_start_b = 0;
    city_view.map_start_a = 0;
    city_view.lookup_orientation = orientation;
    lookup_walk walk = lookup_walk_for(orientation);
    city_view.x_min = VIEW_LAST_COL;
    city_view.y_min = VIEW_LAST_ROW;
    city_view.x_max = 0;
    city_view.y_max = 0;
    for (int32_t y = 0; y < GRID_SIZE; y++) {
        int32_t row = walk.row;
        int32_t col2 = walk.col2;
        for (int32_t x = 0; x < GRID_SIZE; x++) {
            int32_t grid_offset = (y * GRID_SIZE) + x;
            int32_t col = col2 / 2;
            if (map_grids.images[grid_offset] < MAP_IMAGE_FIRST_SHOWN) {
                if (is_in_view(row, col)) {
                    city_view.lookup[(row * VIEW_COLS) + col] = lookup_empty();
                }
            } else {
                if (col <= city_view.x_min) {
                    city_view.x_min = col;
                }
                if (row <= city_view.y_min) {
                    city_view.y_min = row;
                }
                if (city_view.x_max <= col) {
                    city_view.x_max = col;
                }
                if (city_view.y_max <= row) {
                    city_view.y_max = row;
                }
                if (is_in_view(row, col)) {
                    city_view.lookup[(row * VIEW_COLS) + col] = grid_offset;
                }
            }
            row += walk.row_step_x;
            col2 += walk.col2_step_x;
        }
        walk.row += walk.row_step_y;
        walk.col2 += walk.col2_step_y;
    }
}

int32_t city_view_pixels_to_grid_offset(int32_t x, int32_t y)
{
    if (x < city_view.origin_x) {
        return 0;
    }
    if (x >= ((city_view.tile_width + TILE_GAP_X) * city_view.width_tiles) + city_view.origin_x) {
        return 0;
    }
    if (y < city_view.origin_y) {
        return 0;
    }
    if (y >= (city_view.tile_half_height * city_view.height_tiles) + city_view.origin_y) {
        return 0;
    }
    int32_t dy = y - city_view.origin_y;
    int32_t dx = x - city_view.origin_x;
    int32_t row = dy / city_view.tile_half_height;
    city_view.last_row = row;
    int32_t half_tile_width = city_view.tile_half_width + 1;
    int32_t half_col = dx / half_tile_width;
    int32_t half_col_odd = half_col & 1;
    int32_t row_odd = row & 1;
    int32_t parity = (row + half_col) & 1;
    int32_t inside_x = (dx % half_tile_width) / 2;
    int32_t inside_y = dy % city_view.tile_half_height;
    int32_t col = half_col / 2;
    city_view.last_col = col;
    if (parity == 0) {
        if (inside_y > inside_x) {
            row++;
            city_view.last_row = row;
        } else if (half_col_odd != 0 && row_odd != 0) {
            col++;
            city_view.last_col = col;
        }
    } else if (inside_y + inside_x >= city_view.tile_half_height - 1) {
        row++;
        city_view.last_row = row;
        if (half_col_odd != 0 && row_odd == 0) {
            col++;
            city_view.last_col = col;
        }
    }
    int32_t grid_offset = city_view.lookup[((city_view.camera_row + row) * VIEW_COLS) + city_view.camera_col + col];
    city_view.last_grid_offset = grid_offset;
    if (grid_offset >= VIEW_LOOKUP_EMPTY) {
        return 0;
    }
    city_view.last_edge_y = VIEW_EDGE_NONE;
    city_view.last_edge_x = VIEW_EDGE_NONE;
    if (row == 0) {
        city_view.last_edge_y = VIEW_EDGE_FIRST;
    } else if (row >= city_view.height_tiles) {
        city_view.last_edge_y = VIEW_EDGE_PAST_LAST;
    }
    if (row & 1) {
        if (col == 0) {
            city_view.last_edge_x = VIEW_EDGE_FIRST;
        } else if (col >= city_view.width_tiles) {
            city_view.last_edge_x = VIEW_EDGE_PAST_LAST;
        }
    }
    return grid_offset;
}

int32_t city_view_get_viewport_width_tiles(void)
{
    return city_view.width_tiles;
}

int32_t city_view_get_viewport_height_tiles(void)
{
    return city_view.height_tiles;
}

void city_view_calculate_map_bounds(void)
{
    int32_t index = VIEW_MIDDLE_ROW * VIEW_COLS;
    int32_t col = 0;
    while (map_grid_offset_is_outside(city_view.lookup[index])) {
        index++;
        col++;
        if (index >= (VIEW_MIDDLE_ROW * VIEW_COLS) + VIEW_LAST_COL) {
            break;
        }
    }
    g_map_visible_x0 = col + 1;

    index = (VIEW_MIDDLE_ROW * VIEW_COLS) + VIEW_LAST_COL;
    col = VIEW_LAST_COL;
    while (map_grid_offset_is_outside(city_view.lookup[index])) {
        index--;
        col--;
        if (index <= VIEW_MIDDLE_ROW * VIEW_COLS) {
            break;
        }
    }
    g_map_visible_x1 = col - 1;

    index = VIEW_MIDDLE_COL;
    int32_t row = 0;
    while (map_grid_offset_is_outside(city_view.lookup[index])) {
        index += 2 * VIEW_COLS;
        row += 2;
        if (index >= (VIEW_LAST_ROW * VIEW_COLS) + VIEW_MIDDLE_COL) {
            break;
        }
    }
    g_map_visible_y0 = row + 1;

    index = (VIEW_LAST_EVEN_ROW * VIEW_COLS) + VIEW_MIDDLE_COL;
    row = VIEW_LAST_EVEN_ROW;
    while (map_grid_offset_is_outside(city_view.lookup[index])) {
        index -= 2 * VIEW_COLS;
        row -= 2;
        if (index <= VIEW_COLS + VIEW_MIDDLE_COL) {
            break;
        }
    }
    g_map_visible_y0 &= VIEW_EVEN_ROW_MASK;
    g_map_visible_y1 = (row - 1) & VIEW_EVEN_ROW_MASK;
}

int32_t city_view_check_camera_boundaries(void)
{
    int32_t min_x = g_map_visible_x0;
    int32_t min_y = g_map_visible_y0;
    int32_t max_x = g_map_visible_x1;
    int32_t max_y = g_map_visible_y1;
    int32_t moved = 0;
    if (editor_wide_bounds != 0 && editor_active == 1) {
        min_x -= EDITOR_BOUNDS_LEFT;
        min_y -= EDITOR_BOUNDS_TOP;
        max_x += EDITOR_BOUNDS_RIGHT;
        max_y += EDITOR_BOUNDS_BOTTOM;
    }
    if (map_info.camera_x < min_x) {
        map_info.camera_x = min_x;
        moved = 1;
    }
    if (map_info.camera_y < min_y) {
        map_info.camera_y = min_y;
        moved = 1;
    }
    if (city_view_get_viewport_width_tiles() + map_info.camera_x > max_x) {
        map_info.camera_x = max_x - city_view_get_viewport_width_tiles();
        moved = 1;
    }
    int32_t camera_y;
    if (city_view_get_viewport_height_tiles() + map_info.camera_y > max_y) {
        camera_y = max_y - city_view_get_viewport_height_tiles();
        moved = 1;
    } else {
        camera_y = map_info.camera_y;
    }
    if (moved && editor_active == 1) {
        window_redraw_request = WINDOW_REDRAW_ALL;
    }
    camera_y &= VIEW_EVEN_ROW_MASK;
    map_info.camera_y = camera_y;
    return camera_y;
}

int32_t scroll_get_direction(void)
{
    if (city_scroll_rate() == 0) {
        return SCROLL_NONE;
    }
    int32_t left = cursor_raw_x < 1;
    int32_t right = cursor_raw_x >= desktop_width - 1;
    int32_t top = cursor_raw_y < 1;
    int32_t bottom = cursor_raw_y >= desktop_height - 1;
    int32_t key_left = key_scroll_left != 0;
    int32_t key_right = key_scroll_right != 0;
    int32_t key_up = key_scroll_up != 0;
    int32_t key_down = key_scroll_down != 0;
    scroll_in_progress = left || right || top || bottom || key_left || key_right || key_up || key_down;
    key_scroll_left = key_scroll_right = key_scroll_up = key_scroll_down = 0;
    int32_t to_left = key_left || left;
    int32_t to_right = key_right || right;
    int32_t to_top = key_up || top;
    int32_t to_bottom = key_down || bottom;
    if (to_left && to_top) {
        return DIRECTION_TOP_LEFT;
    }
    if (to_left && to_bottom) {
        return DIRECTION_BOTTOM_LEFT;
    }
    if (to_right && to_top) {
        return DIRECTION_TOP_RIGHT;
    }
    if (to_right && to_bottom) {
        return DIRECTION_BOTTOM_RIGHT;
    }
    if (to_left) {
        return DIRECTION_LEFT;
    }
    if (to_top) {
        return DIRECTION_TOP;
    }
    if (to_bottom) {
        return DIRECTION_BOTTOM;
    }
    if (to_right) {
        return DIRECTION_RIGHT;
    }
    return SCROLL_NONE;
}

void city_view_scroll_direction(int32_t direction)
{
    int32_t rows = (city_view_scroll_fast != 0) ? SCROLL_FAST_TILES : SCROLL_TILES;
    switch (direction) {
        case DIRECTION_TOP:
            map_info.camera_y -= rows;
            break;
        case DIRECTION_TOP_RIGHT:
            map_info.camera_y -= rows;
            map_info.camera_x++;
            break;
        case DIRECTION_RIGHT:
            map_info.camera_x++;
            break;
        case DIRECTION_BOTTOM_RIGHT:
            map_info.camera_y += rows;
            map_info.camera_x++;
            break;
        case DIRECTION_BOTTOM:
            map_info.camera_y += rows;
            break;
        case DIRECTION_BOTTOM_LEFT:
            map_info.camera_y += rows;
            map_info.camera_x--;
            break;
        case DIRECTION_LEFT:
            map_info.camera_x--;
            break;
        case DIRECTION_TOP_LEFT:
            map_info.camera_y -= rows;
            map_info.camera_x--;
            break;
        case SCROLL_NONE:
            return;
        default:
            break;
    }
    sound_city_decay_views();
    city_view_check_camera_boundaries();
}

// a finger's drag on a touch screen: the camera moves by whole tile columns and by pairs of rows (the camera's row is
// always even), the opposite way to the finger
void city_view_pan(int32_t columns, int32_t row_pairs)
{
    if (columns == 0 && row_pairs == 0) {
        return;
    }
    map_info.camera_x -= columns;
    map_info.camera_y -= row_pairs * 2;
    sound_city_decay_views();
    city_view_check_camera_boundaries();
}

int32_t city_scroll_rate(void)
{
    uint32_t now = platform_time_ms();
    int32_t elapsed = (int32_t) (now - city_scroll_last_time);
    if (elapsed < 0) {
        elapsed = SCROLL_ELAPSED_WRAPPED;
    }
    int32_t step = (SCROLL_SPEED_MAX - settings_data.scroll_speed) / SCROLL_SPEED_PER_STEP;
    if (step < SCROLL_STEPS && (step * SCROLL_MS_PER_STEP) + SCROLL_MS_MIN <= elapsed) {
        city_scroll_last_time = now;
        return 1;
    }
    return 0;
}

void city_view_grid_offset_to_xy_view(uint32_t offset)
{
    map_result_y = 0;
    map_result_x = 0;
    for (int32_t y = 0; y < VIEW_ROWS; y++) {
        for (int32_t x = 0; x < VIEW_COLS; x++) {
            uint32_t value = (uint32_t) city_view.lookup[(y * VIEW_COLS) + x];
            if (value < VIEW_LOOKUP_EMPTY && value == offset) {
                map_result_y = y;
                map_result_x = x;
                return;
            }
        }
    }
}

void city_view_go_to_grid_offset(uint32_t offset)
{
    city_view_grid_offset_to_xy_view(offset);
    map_info.camera_x = map_result_x - (city_view.width_tiles / 2);
    map_info.camera_y = (map_result_y - (city_view.height_tiles / 2)) & VIEW_EVEN_ROW_MASK;
    scroll_in_progress = 1;
    city_view_check_camera_boundaries();
}

int32_t city_view_mouse_to_grid_offset(void)
{
    return city_view_pixels_to_grid_offset(mouse_x, mouse_y);
}

void city_view_mouse_select(void)
{
    map_info.cursor_tile_y = 0;
    map_info.cursor_tile_x = 0;
    map_info.selected_grid = city_view_mouse_to_grid_offset();
    if (map_info.selected_grid != 0) {
        map_info.cursor_tile_x = (map_info.selected_grid - map_info.grid_start) % GRID_SIZE;
        map_info.cursor_tile_y = (map_info.selected_grid - map_info.grid_start) / GRID_SIZE;
        if (!construction_hover_locked) {
            map_info.hover_offset = map_info.selected_grid;
            map_info.hover_x = map_info.cursor_tile_x;
            map_info.hover_y = map_info.cursor_tile_y;
            pyramid_preview_update();
        }
    }
}

int32_t city_view_footprint_offset(int32_t size, int32_t *dx, int32_t *dy)
{
    switch (size) {
        case 0:
            *dx = 0;
            *dy = 0;
            return 1;
        case 1:
            *dx = city_view.tile_half_width + 1;
            *dy = city_view.tile_half_height;
            return 1;
        case 2:
            *dx = city_view.tile_width + 2;
            *dy = city_view.tile_height;
            return 1;
        case 3:
            *dx = city_view.size3_x + 3;
            *dy = city_view.size3_y;
            return 1;
        case 4:
            *dx = city_view.size4_x + 4;
            *dy = city_view.size4_y;
            return 1;
        case 5:
            *dx = city_view.size4_x + city_view.tile_half_width + 5;
            *dy = city_view.size4_y + city_view.tile_half_height;
            return 1;
        default:
            return 0;
    }
}
