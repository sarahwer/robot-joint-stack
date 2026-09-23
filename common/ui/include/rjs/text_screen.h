/*
 * text_screen.h - character screen with change tracking, for small TFT displays.
 *
 * The 2.0" ST7789 panel (240x320) holds exactly 30 x 20 characters of the 8x16
 * font. Instead of a 150 KB framebuffer, the application writes text into this
 * screen model; flushing redraws only the cells that actually changed, which
 * keeps the SPI traffic (and the CPU time) small.
 *
 * Chip-independent and unit tested on the host.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RJS_TEXT_SCREEN_H
#define RJS_TEXT_SCREEN_H

#include <stdbool.h>
#include <stdint.h>

#include "rjs/font8x16.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RJS_TS_COLS 30u
#define RJS_TS_ROWS 20u

/** RGB565 colour, as the ST7789 expects it. */
typedef uint16_t rjs_color_t;

#define RJS_RGB(r, g, b) ((rjs_color_t)((((r) & 0xF8u) << 8) | (((g) & 0xFCu) << 3) | ((b) >> 3)))

#define RJS_BLACK  RJS_RGB(0, 0, 0)
#define RJS_WHITE  RJS_RGB(255, 255, 255)
#define RJS_GREY   RJS_RGB(128, 128, 128)
#define RJS_RED    RJS_RGB(255, 40, 40)
#define RJS_GREEN  RJS_RGB(40, 220, 80)
#define RJS_YELLOW RJS_RGB(255, 200, 0)
#define RJS_BLUE   RJS_RGB(60, 140, 255)

typedef struct {
    char        ch[RJS_TS_ROWS][RJS_TS_COLS];
    rjs_color_t fg[RJS_TS_ROWS][RJS_TS_COLS];
    rjs_color_t bg[RJS_TS_ROWS][RJS_TS_COLS];
    uint32_t    dirty[RJS_TS_ROWS];      /**< one bit per column */
    rjs_color_t cur_fg;
    rjs_color_t cur_bg;
} rjs_text_screen_t;

/** Called for every changed cell during a flush. */
typedef void (*rjs_ts_draw_cb_t)(uint8_t row, uint8_t col, char ch, rjs_color_t fg, rjs_color_t bg,
                                 void *user);

/** Clear to spaces with the given colours and mark everything dirty. */
void rjs_ts_init(rjs_text_screen_t *s, rjs_color_t fg, rjs_color_t bg);

/** Colours used by the following writes. */
void rjs_ts_set_colors(rjs_text_screen_t *s, rjs_color_t fg, rjs_color_t bg);

/** Write text at (row, col). Text beyond the right edge is cut off. */
void rjs_ts_print(rjs_text_screen_t *s, uint8_t row, uint8_t col, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

/** Write text and pad the rest of the row with spaces (removes old, longer text). */
void rjs_ts_print_line(rjs_text_screen_t *s, uint8_t row, uint8_t col, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

/** Horizontal bar in row `row` from col..col+width-1, filled to `permille`. */
void rjs_ts_bar(rjs_text_screen_t *s, uint8_t row, uint8_t col, uint8_t width, uint16_t permille);

/** Fill a whole row with spaces in the current colours. */
void rjs_ts_clear_row(rjs_text_screen_t *s, uint8_t row);

/** Redraw everything on the next flush (e.g. after a display reset). */
void rjs_ts_mark_all_dirty(rjs_text_screen_t *s);

/** Call cb for every changed cell and clear the change marks. Returns the number of cells drawn. */
uint16_t rjs_ts_flush(rjs_text_screen_t *s, rjs_ts_draw_cb_t cb, void *user);

/** Number of cells that would be drawn by a flush. */
uint16_t rjs_ts_dirty_count(const rjs_text_screen_t *s);

/**
 * Render one character into an 8x16 RGB565 block (128 pixels), for the display driver.
 * Characters outside the font range are drawn as a filled box.
 */
void rjs_ts_render_cell(char ch, rjs_color_t fg, rjs_color_t bg, rjs_color_t *out);

#ifdef __cplusplus
}
#endif

#endif /* RJS_TEXT_SCREEN_H */
