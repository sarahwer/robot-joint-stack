/*
 * test_text_screen.c - change tracking, clipping and glyph rendering.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include "rjs/text_screen.h"
#include "test_framework.h"

typedef struct {
    uint16_t calls;
    char     last_ch;
    uint8_t  last_row;
    uint8_t  last_col;
    char     collected[RJS_TS_COLS + 1];
} capture_t;

static void on_cell(uint8_t row, uint8_t col, char ch, rjs_color_t fg, rjs_color_t bg, void *user)
{
    (void)fg;
    (void)bg;
    capture_t *c = (capture_t *)user;
    c->calls++;
    c->last_ch  = ch;
    c->last_row = row;
    c->last_col = col;
    if (row == 0 && col < RJS_TS_COLS) {
        c->collected[col] = ch;
    }
}

static void test_init_marks_everything_dirty(void)
{
    rjs_text_screen_t s;
    rjs_ts_init(&s, RJS_WHITE, RJS_BLACK);
    CHECK_EQ(rjs_ts_dirty_count(&s), RJS_TS_ROWS * RJS_TS_COLS);
    capture_t cap = {0};
    CHECK_EQ(rjs_ts_flush(&s, on_cell, &cap), RJS_TS_ROWS * RJS_TS_COLS);
    CHECK_EQ(cap.calls, RJS_TS_ROWS * RJS_TS_COLS);
    CHECK_EQ(rjs_ts_dirty_count(&s), 0);   /* flush clears the marks */
}

static void test_only_changed_cells_are_redrawn(void)
{
    rjs_text_screen_t s;
    capture_t cap = {0};
    rjs_ts_init(&s, RJS_WHITE, RJS_BLACK);
    rjs_ts_flush(&s, NULL, NULL);

    rjs_ts_print(&s, 2, 0, "node 1");
    /* 5 cells: the space in "node 1" already matches the cleared screen. */
    CHECK_EQ(rjs_ts_dirty_count(&s), 5);
    rjs_ts_flush(&s, on_cell, &cap);
    CHECK_EQ(cap.calls, 5);

    /* Printing the same text again costs nothing. */
    rjs_ts_print(&s, 2, 0, "node 1");
    CHECK_EQ(rjs_ts_dirty_count(&s), 0);

    /* Only the digit changes. */
    rjs_ts_print(&s, 2, 0, "node 2");
    CHECK_EQ(rjs_ts_dirty_count(&s), 1);
    cap.calls = 0;
    rjs_ts_flush(&s, on_cell, &cap);
    CHECK_EQ(cap.calls, 1);
    CHECK_EQ(cap.last_ch, '2');
    CHECK_EQ(cap.last_row, 2);
    CHECK_EQ(cap.last_col, 5);
}

static void test_colour_change_marks_dirty(void)
{
    rjs_text_screen_t s;
    rjs_ts_init(&s, RJS_WHITE, RJS_BLACK);
    rjs_ts_flush(&s, NULL, NULL);
    rjs_ts_print(&s, 1, 0, "OK");
    rjs_ts_flush(&s, NULL, NULL);
    rjs_ts_set_colors(&s, RJS_RED, RJS_BLACK);
    rjs_ts_print(&s, 1, 0, "OK");          /* same text, new colour */
    CHECK_EQ(rjs_ts_dirty_count(&s), 2);
}

static void test_clipping_and_padding(void)
{
    rjs_text_screen_t s;
    capture_t cap = {0};
    memset(cap.collected, '?', sizeof(cap.collected) - 1);
    rjs_ts_init(&s, RJS_WHITE, RJS_BLACK);
    rjs_ts_flush(&s, NULL, NULL);

    /* Longer than the screen: no overflow, no crash (ASan would catch it). */
    rjs_ts_print(&s, 0, 0, "%s", "0123456789012345678901234567890123456789");
    CHECK_EQ(rjs_ts_dirty_count(&s), RJS_TS_COLS);
    rjs_ts_flush(&s, on_cell, &cap);
    CHECK_EQ(cap.collected[RJS_TS_COLS - 1], '9');

    /* print_line pads the rest of the row, so old text disappears:
     * 5 cells get the new text, the other 25 become spaces. */
    rjs_ts_print_line(&s, 0, 0, "short");
    CHECK_EQ(rjs_ts_dirty_count(&s), RJS_TS_COLS);
    rjs_ts_flush(&s, on_cell, &cap);
    CHECK_EQ(cap.collected[0], 's');
    CHECK_EQ(cap.collected[5], ' ');

    /* Writes outside the screen are ignored. */
    rjs_ts_print(&s, RJS_TS_ROWS + 5, 0, "x");
    rjs_ts_print(&s, 0, RJS_TS_COLS + 5, "x");
}

static void test_bar(void)
{
    rjs_text_screen_t s;
    rjs_ts_init(&s, RJS_WHITE, RJS_BLACK);
    rjs_ts_flush(&s, NULL, NULL);
    rjs_ts_bar(&s, 5, 0, 10, 500);
    CHECK_EQ(s.ch[5][4], '#');
    CHECK_EQ(s.ch[5][5], '.');
    rjs_ts_bar(&s, 5, 0, 10, 5000);     /* clamped to 100 % */
    CHECK_EQ(s.ch[5][9], '#');
}

static void test_render_cell(void)
{
    rjs_color_t px[RJS_FONT_WIDTH * RJS_FONT_HEIGHT];
    rjs_ts_render_cell(' ', RJS_WHITE, RJS_BLACK, px);
    for (uint16_t i = 0; i < RJS_FONT_WIDTH * RJS_FONT_HEIGHT; ++i) {
        CHECK_EQ(px[i], RJS_BLACK);       /* space = background only */
    }

    rjs_ts_render_cell('A', RJS_WHITE, RJS_BLACK, px);
    uint16_t fg_pixels = 0;
    for (uint16_t i = 0; i < RJS_FONT_WIDTH * RJS_FONT_HEIGHT; ++i) {
        CHECK(px[i] == RJS_WHITE || px[i] == RJS_BLACK);
        if (px[i] == RJS_WHITE) {
            fg_pixels++;
        }
    }
    CHECK(fg_pixels > 10 && fg_pixels < 100);   /* 'A' is drawn, and is not a filled box */

    rjs_ts_render_cell((char)200, RJS_WHITE, RJS_BLACK, px);
    CHECK_EQ(px[0], RJS_WHITE);                 /* unknown character: filled box */
}

static void test_rgb565_packing(void)
{
    CHECK_EQ(RJS_RGB(0, 0, 0), 0x0000);
    CHECK_EQ(RJS_RGB(255, 255, 255), 0xFFFF);
    CHECK_EQ(RJS_RGB(255, 0, 0), 0xF800);
    CHECK_EQ(RJS_RGB(0, 255, 0), 0x07E0);
    CHECK_EQ(RJS_RGB(0, 0, 255), 0x001F);
}

int main(void)
{
    RUN(test_init_marks_everything_dirty);
    RUN(test_only_changed_cells_are_redrawn);
    RUN(test_colour_change_marks_dirty);
    RUN(test_clipping_and_padding);
    RUN(test_bar);
    RUN(test_render_cell);
    RUN(test_rgb565_packing);
    TEST_MAIN_END();
}
