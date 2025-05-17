#ifndef UI_H
#define UI_H

// Define the number of slices
#define SLICE_COUNT 8

void ui_init(void);
void draw_lizard(void);
void boundary_circle(void);
void pizza(void);
void pizza2(const bool slice_states[SLICE_COUNT]);

#endif // UI_H 