#ifndef TOAST_OVERLAY_H
#define TOAST_OVERLAY_H

#ifdef __cplusplus
extern "C" {
#endif

// Briefly superimpose a message over the current UI on the LVGL top layer.
// Safe to call from any task (posts to LVGL via lv_async_call).
void toast_overlay_show(const char *message);

#ifdef __cplusplus
}
#endif

#endif // TOAST_OVERLAY_H
