#ifndef INPUT_H
#define INPUT_H

#include <xkbcommon/xkbcommon.h>

/* ─── XKB ───────────────────────────────────────────────────────────────── */
void init_xkb(void);
void cleanup_xkb(void);
void update_layout_name(void);

/* ─── Keyboard devices ──────────────────────────────────────────────────── */
void init_keyboards(void);
void cleanup_keyboards(void);

/* ─── Mouse devices ─────────────────────────────────────────────────────── */
void init_mice(void);
void cleanup_mice(void);

/* ─── Event handlers ────────────────────────────────────────────────────── */
void handle_keysym(xkb_keysym_t sym, const char *utf8);
void handle_mouse_click(double mx, double my, int width, int height);
void handle_mouse_motion(double mx, double my, int width, int height);

/* ─── Hit testing ───────────────────────────────────────────────────────── */
int hit_test_element(double mx, double my, int width, int height,
                     int *sess_idx);

#endif /* INPUT_H */
