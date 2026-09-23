/*
 * text_screen.c - see text_screen.h.
 *
 * SPDX-License-Identifier: MIT
 */
#include "rjs/text_screen.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void put_char(rjs_text_screen_t *s, uint8_t row, uint8_t col, char ch)
{
    if (row >= RJS_TS_ROWS || col >= RJS_TS_COLS) {
        return;
    }
    /* Only a real change costs SPI traffic later. */
    if (s->ch[row][col] == ch && s->fg[row][col] == s->cur_fg && s->bg[row][col] == s->cur_bg) {
        return;
    }
    s->ch[row][col] = ch;
    s->fg[row][col] = s->cur_fg;
    s->bg[row][col] = s->cur_bg;
    s->dirty[row] |= (uint32_t)1u << col;
}

void rjs_ts_init(rjs_text_screen_t *s, rjs_color_t fg, rjs_color_t bg)
{
    memset(s, 0, sizeof(*s));
    s->cur_fg = fg;
    s->cur_bg = bg;
    for (uint8_t r = 0; r < RJS_TS_ROWS; ++r) {
        for (uint8_t c = 0; c < RJS_TS_COLS; ++c) {
            s->ch[r][c] = ' ';
            s->fg[r][c] = fg;
            s->bg[r][c] = bg;
        }
    }
    rjs_ts_mark_all_dirty(s);
}

void rjs_ts_set_colors(rjs_text_screen_t *s, rjs_color_t fg, rjs_color_t bg)
{
    s->cur_fg = fg;
    s->cur_bg = bg;
}

static void print_v(rjs_text_screen_t *s, uint8_t row, uint8_t col, bool pad, const char *fmt, va_list ap)
{
    char buf[RJS_TS_COLS + 1u];
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    if (n < 0) {
        return;
    }
    uint8_t c = col;
    for (const char *p = buf; *p != '\0' && c < RJS_TS_COLS; ++p, ++c) {
        put_char(s, row, c, *p);
    }
    if (pad) {
        for (; c < RJS_TS_COLS; ++c) {
            put_char(s, row, c, ' ');
        }
    }
}

void rjs_ts_print(rjs_text_screen_t *s, uint8_t row, uint8_t col, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    print_v(s, row, col, false, fmt, ap);
    va_end(ap);
}

void rjs_ts_print_line(rjs_text_screen_t *s, uint8_t row, uint8_t col, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    print_v(s, row, col, true, fmt, ap);
    va_end(ap);
}

void rjs_ts_bar(rjs_text_screen_t *s, uint8_t row, uint8_t col, uint8_t width, uint16_t permille)
{
    if (permille > 1000u) {
        permille = 1000u;
    }
    const uint16_t filled = (uint16_t)((width * permille + 500u) / 1000u);
    for (uint8_t i = 0; i < width; ++i) {
        put_char(s, row, (uint8_t)(col + i), (i < filled) ? '#' : '.');
    }
}

void rjs_ts_clear_row(rjs_text_screen_t *s, uint8_t row)
{
    for (uint8_t c = 0; c < RJS_TS_COLS; ++c) {
        put_char(s, row, c, ' ');
    }
}

void rjs_ts_mark_all_dirty(rjs_text_screen_t *s)
{
    const uint32_t all = ((uint32_t)1u << RJS_TS_COLS) - 1u;
    for (uint8_t r = 0; r < RJS_TS_ROWS; ++r) {
        s->dirty[r] = all;
    }
}

uint16_t rjs_ts_flush(rjs_text_screen_t *s, rjs_ts_draw_cb_t cb, void *user)
{
    uint16_t drawn = 0;
    for (uint8_t r = 0; r < RJS_TS_ROWS; ++r) {
        uint32_t bits = s->dirty[r];
        s->dirty[r] = 0;
        while (bits != 0u) {
            const uint8_t c = (uint8_t)__builtin_ctz(bits);
            bits &= bits - 1u;
            if (cb != NULL) {
                cb(r, c, s->ch[r][c], s->fg[r][c], s->bg[r][c], user);
            }
            drawn++;
        }
    }
    return drawn;
}

uint16_t rjs_ts_dirty_count(const rjs_text_screen_t *s)
{
    uint16_t n = 0;
    for (uint8_t r = 0; r < RJS_TS_ROWS; ++r) {
        n = (uint16_t)(n + __builtin_popcount(s->dirty[r]));
    }
    return n;
}

void rjs_ts_render_cell(char ch, rjs_color_t fg, rjs_color_t bg, rjs_color_t *out)
{
    const unsigned char code = (unsigned char)ch;
    if (code < RJS_FONT_FIRST || code > RJS_FONT_LAST) {
        for (uint16_t i = 0; i < RJS_FONT_WIDTH * RJS_FONT_HEIGHT; ++i) {
            out[i] = fg;    /* unknown character: filled box */
        }
        return;
    }
    const uint8_t *glyph = rjs_font8x16[code - RJS_FONT_FIRST];
    for (uint8_t y = 0; y < RJS_FONT_HEIGHT; ++y) {
        const uint8_t bits = glyph[y];
        for (uint8_t x = 0; x < RJS_FONT_WIDTH; ++x) {
            out[y * RJS_FONT_WIDTH + x] = (bits & (0x80u >> x)) ? fg : bg;
        }
    }
}
