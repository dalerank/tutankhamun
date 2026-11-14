// The city view: going to a tile, the view lookup (VIEW_ROWS x VIEW_COLS grid offsets, or "no tile"), the
// viewport size in tiles and a footprint's pixel offset from its tile.

#ifndef WIDGET_CITY_VIEW_H
#define WIDGET_CITY_VIEW_H

#include <stdint.h>

enum view_lookup_size {
    VIEW_COLS = 231,
    VIEW_ROWS = 457,
    VIEW_LOOKUP_EMPTY = 0x0FFF0000,
};

void city_view_go_to_grid_offset(uint32_t offset);
int32_t city_view_reset_lookup(void);
int32_t city_view_mouse_to_grid_offset(void);
int32_t city_view_pixels_to_grid_offset(int32_t x, int32_t y);
void city_view_calculate_map_bounds(void);
void city_view_mouse_select(void);
void city_view_scroll_direction(int32_t direction);
void city_view_pan(int32_t columns, int32_t row_pairs);
int32_t city_scroll_rate(void);
int32_t scroll_get_direction(void);
void city_view_calculate_lookup(int32_t orientation);
int32_t city_view_get_viewport_width_tiles(void);
int32_t city_view_get_viewport_height_tiles(void);
int32_t city_view_check_camera_boundaries(void);
void city_view_grid_offset_to_xy_view(uint32_t offset);
int32_t city_view_footprint_offset(int32_t size, int32_t *dx, int32_t *dy);

#endif
