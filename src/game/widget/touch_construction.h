// The functions of touch_construction.c that other files call, and its switch, which the platform sets on a touch
// screen.

#ifndef WIDGET_TOUCH_CONSTRUCTION_H
#define WIDGET_TOUCH_CONSTRUCTION_H

#include <stdint.h>

extern int32_t touch_construction_enabled;

int32_t touch_construction_cancel_input(void);
int32_t touch_construction_input(void);
void touch_construction_draw(void);

enum touch_map_pan_step {
    TOUCH_MAP_PAN_COLUMN_PIXELS = 60,
    TOUCH_MAP_PAN_ROW_PAIR_PIXELS = 30,
};

// a finger put down at x, y moves the map, as in Julius: in the city's own window, on the city view, with no building
// chosen (a chosen building's drag places it)
int32_t touch_map_pan_allowed(int32_t x, int32_t y);
void touch_map_pan(int32_t columns, int32_t row_pairs);

#endif
