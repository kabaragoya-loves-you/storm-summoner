/*******************************************************************************
 * Size: 14 px
 * Bpp: 1
 * Opts: --bpp 1 --size 14 --no-compress --font chalet-newyork-1960.woff --symbols ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmopqrstuvwxyz1234567890,!-*+.<>?/\~@#$%^&()=: ;"' --format lvgl -o chalet_ny_1960.c
 ******************************************************************************/

#include "lvgl.h"

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */
    0x0,

    /* U+0021 "!" */
    0xff, 0xfc, 0x3c,

    /* U+0022 "\"" */
    0x5a, 0xd6, 0xa0,

    /* U+0023 "#" */
    0x34, 0x69, 0xfb, 0xf2, 0x45, 0xbf, 0xff, 0x68,
    0xd0,

    /* U+0024 "$" */
    0x21, 0xef, 0xfb, 0xe1, 0xe3, 0xfb, 0xfd, 0xe2,
    0x0,

    /* U+0025 "%" */
    0x71, 0x1f, 0x63, 0x68, 0x6d, 0xf, 0xc0, 0xef,
    0x83, 0xf8, 0x73, 0x16, 0x62, 0xfc, 0x8f, 0x0,

    /* U+0026 "&" */
    0x38, 0x3e, 0x1b, 0xd, 0x83, 0x83, 0x83, 0xed,
    0x9e, 0xc6, 0x7f, 0x8f, 0xc0,

    /* U+0027 "'" */
    0x55,

    /* U+0028 "(" */
    0x36, 0x6c, 0xcc, 0xcc, 0xcc, 0x66, 0x30,

    /* U+0029 ")" */
    0xc6, 0x62, 0x33, 0x33, 0x32, 0x66, 0xc0,

    /* U+002A "*" */
    0x53, 0xbe, 0xe5, 0x0,

    /* U+002B "+" */
    0x30, 0xc3, 0x3f, 0xfc, 0xc3, 0x0,

    /* U+002C "," */
    0xf7,

    /* U+002D "-" */
    0xff,

    /* U+002E "." */
    0xf0,

    /* U+002F "/" */
    0x18, 0xcc, 0x63, 0x31, 0x8c, 0x66, 0x30,

    /* U+0030 "0" */
    0x38, 0xfb, 0xbe, 0x3c, 0x78, 0xf1, 0xe3, 0xee,
    0xf8, 0xe0,

    /* U+0031 "1" */
    0x33, 0xff, 0x33, 0x33, 0x33, 0x30,

    /* U+0032 "2" */
    0x3c, 0xff, 0x1e, 0x30, 0xe3, 0x8e, 0x30, 0xc1,
    0xff, 0xf8,

    /* U+0033 "3" */
    0x3c, 0xff, 0x18, 0x31, 0xc3, 0x81, 0xe3, 0xc6,
    0xf8, 0xe0,

    /* U+0034 "4" */
    0x1c, 0x38, 0xf1, 0xe6, 0xcd, 0xb3, 0x7f, 0xfe,
    0x18, 0x30,

    /* U+0035 "5" */
    0x7c, 0xf9, 0x2, 0xf, 0x9f, 0x91, 0x83, 0xc7,
    0xf9, 0xe0,

    /* U+0036 "6" */
    0x38, 0xf9, 0x9e, 0xf, 0x9f, 0xb1, 0xe3, 0xc6,
    0xf8, 0xe0,

    /* U+0037 "7" */
    0xff, 0xfc, 0x30, 0xc3, 0x86, 0xc, 0x30, 0x60,
    0xc1, 0x80,

    /* U+0038 "8" */
    0x7d, 0xff, 0x1e, 0x37, 0xcf, 0xb1, 0xe3, 0xc6,
    0xf8, 0xe0,

    /* U+0039 "9" */
    0x38, 0xfb, 0x1e, 0x3c, 0x7f, 0xdf, 0x83, 0xce,
    0xf8, 0xe0,

    /* U+003A ":" */
    0xf0, 0xf,

    /* U+003B ";" */
    0xf0, 0xf, 0x70,

    /* U+003C "<" */
    0x4, 0x77, 0xb8, 0xe1, 0xf1, 0xc1,

    /* U+003D "=" */
    0xff, 0xf0, 0x3f, 0xfc,

    /* U+003E ">" */
    0x83, 0x87, 0x87, 0x3f, 0xcc, 0x0,

    /* U+003F "?" */
    0x7b, 0xfc, 0xf3, 0x1c, 0xe3, 0xc, 0x0, 0xc3,
    0x0,

    /* U+0040 "@" */
    0x1e, 0xf, 0xe7, 0x1b, 0xbb, 0xde, 0xf6, 0xbd,
    0x4f, 0xfe, 0x7b, 0xcf, 0xe1, 0xf0,

    /* U+0041 "A" */
    0x1c, 0xe, 0x7, 0x86, 0xc3, 0x61, 0x99, 0x8c,
    0xfe, 0x7f, 0x30, 0xf0, 0x60,

    /* U+0042 "B" */
    0xfe, 0xff, 0xc3, 0xc3, 0xc3, 0xfe, 0xfe, 0xc3,
    0xc3, 0xff, 0xfe,

    /* U+0043 "C" */
    0x1e, 0x3f, 0x98, 0xfc, 0x3c, 0x6, 0x3, 0x1,
    0xc3, 0x63, 0xbf, 0x87, 0x80,

    /* U+0044 "D" */
    0xfc, 0xfe, 0xc6, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3,
    0xc6, 0xfe, 0xfc,

    /* U+0045 "E" */
    0xff, 0xff, 0x6, 0xc, 0x1f, 0xff, 0xe0, 0xc1,
    0xff, 0xf8,

    /* U+0046 "F" */
    0xff, 0xff, 0x6, 0xc, 0x1f, 0xbf, 0x60, 0xc1,
    0x83, 0x0,

    /* U+0047 "G" */
    0x1e, 0x3f, 0x98, 0xd8, 0x3c, 0x6, 0x3f, 0x1f,
    0x83, 0x63, 0xbf, 0xcf, 0x60,

    /* U+0048 "H" */
    0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xff, 0xff, 0xc3,
    0xc3, 0xc3, 0xc3,

    /* U+0049 "I" */
    0xff, 0xff, 0xfc,

    /* U+004A "J" */
    0xc, 0x30, 0xc3, 0xc, 0x30, 0xf3, 0xcf, 0xf7,
    0x80,

    /* U+004B "K" */
    0xc7, 0xce, 0xcc, 0xd8, 0xf0, 0xf8, 0xfc, 0xcc,
    0xce, 0xc6, 0xc3,

    /* U+004C "L" */
    0xc1, 0x83, 0x6, 0xc, 0x18, 0x30, 0x60, 0xc1,
    0xff, 0xf8,

    /* U+004D "M" */
    0xe1, 0xf8, 0x7f, 0x3f, 0xcf, 0xf3, 0xf4, 0xbd,
    0xef, 0x7b, 0xde, 0xf3, 0x3c, 0xcc,

    /* U+004E "N" */
    0xe3, 0xe3, 0xf3, 0xf3, 0xd3, 0xdb, 0xcb, 0xcf,
    0xcf, 0xc7, 0xc7,

    /* U+004F "O" */
    0x1e, 0xf, 0xc6, 0x3b, 0x87, 0xc0, 0xf0, 0x3c,
    0xf, 0x87, 0x61, 0x9f, 0xc1, 0xe0,

    /* U+0050 "P" */
    0xfd, 0xff, 0x1e, 0x3c, 0x7f, 0xff, 0x60, 0xc1,
    0x83, 0x0,

    /* U+0051 "Q" */
    0x1e, 0xf, 0xe6, 0x1b, 0x87, 0xc0, 0xf0, 0x3c,
    0xf, 0x9f, 0x63, 0x9f, 0xe1, 0xfc,

    /* U+0052 "R" */
    0xfe, 0xff, 0xc3, 0xc3, 0xc3, 0xfe, 0xfe, 0xc3,
    0xc3, 0xc3, 0xc3,

    /* U+0053 "S" */
    0x3c, 0xfe, 0xc7, 0xc3, 0xf0, 0x7e, 0x7, 0xc3,
    0xe3, 0x7e, 0x3c,

    /* U+0054 "T" */
    0xff, 0xfc, 0x60, 0xc1, 0x83, 0x6, 0xc, 0x18,
    0x30, 0x60,

    /* U+0055 "U" */
    0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3,
    0xe7, 0x7e, 0x3c,

    /* U+0056 "V" */
    0xc3, 0xc3, 0x63, 0x62, 0x66, 0x26, 0x36, 0x3c,
    0x1c, 0x1c, 0x18,

    /* U+0057 "W" */
    0xc6, 0x3c, 0x73, 0x67, 0x36, 0xf3, 0x6d, 0x26,
    0xd6, 0x2d, 0xe3, 0x9e, 0x39, 0xc3, 0x9c, 0x18,
    0xc0,

    /* U+0058 "X" */
    0xe3, 0x33, 0x8d, 0x87, 0x81, 0xc0, 0xc0, 0x70,
    0x7c, 0x76, 0x31, 0xb8, 0xc0,

    /* U+0059 "Y" */
    0xc3, 0xe7, 0x66, 0x66, 0x3c, 0x3c, 0x18, 0x18,
    0x18, 0x18, 0x18,

    /* U+005A "Z" */
    0xff, 0xfc, 0x18, 0x61, 0x87, 0xc, 0x30, 0xc1,
    0xff, 0xf8,

    /* U+005C "\\" */
    0xc6, 0x30, 0xc6, 0x30, 0x86, 0x31, 0x86,

    /* U+005E "^" */
    0x18, 0x30, 0xf1, 0xe6, 0x4c, 0xd9, 0x80,

    /* U+0061 "a" */
    0x3c, 0xff, 0x18, 0xff, 0x78, 0xff, 0xbf,

    /* U+0062 "b" */
    0xc1, 0x83, 0x7, 0xcf, 0xdd, 0xf1, 0xe3, 0xef,
    0xfb, 0xe0,

    /* U+0063 "c" */
    0x3c, 0xff, 0x9e, 0xc, 0x1c, 0xdf, 0x9e,

    /* U+0064 "d" */
    0x6, 0xc, 0x19, 0xf7, 0xfd, 0xf1, 0xe3, 0xee,
    0xfc, 0xf8,

    /* U+0065 "e" */
    0x38, 0xfb, 0x1f, 0xff, 0xf8, 0xdf, 0x1c,

    /* U+0066 "f" */
    0x37, 0x6f, 0xf6, 0x66, 0x66, 0x60,

    /* U+0067 "g" */
    0x3e, 0xff, 0xbe, 0x3c, 0x7d, 0xdf, 0x9f, 0x6,
    0xf8, 0xe0,

    /* U+0068 "h" */
    0xc1, 0x83, 0x7, 0xef, 0xf8, 0xf1, 0xe3, 0xc7,
    0x8f, 0x18,

    /* U+0069 "i" */
    0xf3, 0xff, 0xfc,

    /* U+006A "j" */
    0x6c, 0x36, 0xdb, 0x6d, 0xbf, 0x80,

    /* U+006B "k" */
    0xc1, 0x83, 0x6, 0x6d, 0xdf, 0x3c, 0x7c, 0xdd,
    0x9b, 0x38,

    /* U+006C "l" */
    0xff, 0xff, 0xfc,

    /* U+006D "m" */
    0xfd, 0xef, 0xff, 0xc6, 0x3c, 0x63, 0xc6, 0x3c,
    0x63, 0xc6, 0x3c, 0x63,

    /* U+006F "o" */
    0x38, 0xfb, 0xbe, 0x3c, 0x7d, 0xdf, 0x1c,

    /* U+0070 "p" */
    0xf9, 0xfb, 0xbe, 0x3c, 0x7d, 0xff, 0x7c, 0xc1,
    0x83, 0x0,

    /* U+0071 "q" */
    0x3e, 0xff, 0xbe, 0x3c, 0x7d, 0xdf, 0x9b, 0x6,
    0xc, 0x18,

    /* U+0072 "r" */
    0xff, 0xcc, 0xcc, 0xcc,

    /* U+0073 "s" */
    0x38, 0xf9, 0x1b, 0xc1, 0xf8, 0xdf, 0x9e,

    /* U+0074 "t" */
    0x66, 0xff, 0x66, 0x66, 0x73,

    /* U+0075 "u" */
    0xc7, 0x8f, 0x1e, 0x3c, 0x78, 0xff, 0xbf,

    /* U+0076 "v" */
    0xc6, 0x8d, 0x9b, 0x62, 0xc7, 0x8e, 0xc,

    /* U+0077 "w" */
    0xce, 0x79, 0xcd, 0xbb, 0x35, 0x67, 0xac, 0x77,
    0xe, 0xe1, 0x8c,

    /* U+0078 "x" */
    0xe6, 0xd8, 0xf1, 0xc3, 0x85, 0x9b, 0x63,

    /* U+0079 "y" */
    0xc6, 0x66, 0x66, 0x6c, 0x3c, 0x3c, 0x18, 0x18,
    0x30, 0x70, 0x60,

    /* U+007A "z" */
    0xff, 0xf1, 0xce, 0x71, 0x8f, 0xff,

    /* U+007E "~" */
    0xdb
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 69, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1, .adv_w = 64, .box_w = 2, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 4, .adv_w = 83, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 7},
    {.bitmap_index = 7, .adv_w = 120, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 16, .adv_w = 110, .box_w = 6, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 25, .adv_w = 179, .box_w = 11, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 41, .adv_w = 148, .box_w = 9, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 54, .adv_w = 44, .box_w = 2, .box_h = 4, .ofs_x = 0, .ofs_y = 7},
    {.bitmap_index = 55, .adv_w = 73, .box_w = 4, .box_h = 13, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 62, .adv_w = 73, .box_w = 4, .box_h = 13, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 69, .adv_w = 88, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 6},
    {.bitmap_index = 73, .adv_w = 117, .box_w = 6, .box_h = 7, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 79, .adv_w = 52, .box_w = 2, .box_h = 4, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 80, .adv_w = 74, .box_w = 4, .box_h = 2, .ofs_x = 1, .ofs_y = 3},
    {.bitmap_index = 81, .adv_w = 52, .box_w = 2, .box_h = 2, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 82, .adv_w = 75, .box_w = 5, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 89, .adv_w = 131, .box_w = 7, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 99, .adv_w = 94, .box_w = 4, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 105, .adv_w = 126, .box_w = 7, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 115, .adv_w = 125, .box_w = 7, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 125, .adv_w = 124, .box_w = 7, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 135, .adv_w = 123, .box_w = 7, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 145, .adv_w = 126, .box_w = 7, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 155, .adv_w = 116, .box_w = 7, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 165, .adv_w = 125, .box_w = 7, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 175, .adv_w = 126, .box_w = 7, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 185, .adv_w = 55, .box_w = 2, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 187, .adv_w = 55, .box_w = 2, .box_h = 10, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 190, .adv_w = 101, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 196, .adv_w = 113, .box_w = 6, .box_h = 5, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 200, .adv_w = 101, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 206, .adv_w = 124, .box_w = 6, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 215, .adv_w = 167, .box_w = 10, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 229, .adv_w = 149, .box_w = 9, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 242, .adv_w = 146, .box_w = 8, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 253, .adv_w = 158, .box_w = 9, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 266, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 277, .adv_w = 136, .box_w = 7, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 287, .adv_w = 131, .box_w = 7, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 297, .adv_w = 167, .box_w = 9, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 310, .adv_w = 155, .box_w = 8, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 321, .adv_w = 59, .box_w = 2, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 324, .adv_w = 118, .box_w = 6, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 333, .adv_w = 145, .box_w = 8, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 344, .adv_w = 125, .box_w = 7, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 354, .adv_w = 182, .box_w = 10, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 368, .adv_w = 157, .box_w = 8, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 379, .adv_w = 167, .box_w = 10, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 393, .adv_w = 137, .box_w = 7, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 403, .adv_w = 167, .box_w = 10, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 417, .adv_w = 149, .box_w = 8, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 428, .adv_w = 145, .box_w = 8, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 439, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 449, .adv_w = 146, .box_w = 8, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 460, .adv_w = 135, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 471, .adv_w = 199, .box_w = 12, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 488, .adv_w = 136, .box_w = 9, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 501, .adv_w = 133, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 512, .adv_w = 133, .box_w = 7, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 522, .adv_w = 76, .box_w = 5, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 529, .adv_w = 124, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 4},
    {.bitmap_index = 536, .adv_w = 130, .box_w = 7, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 543, .adv_w = 139, .box_w = 7, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 553, .adv_w = 128, .box_w = 7, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 560, .adv_w = 139, .box_w = 7, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 570, .adv_w = 131, .box_w = 7, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 577, .adv_w = 70, .box_w = 4, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 583, .adv_w = 137, .box_w = 7, .box_h = 11, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 593, .adv_w = 129, .box_w = 7, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 603, .adv_w = 58, .box_w = 2, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 606, .adv_w = 53, .box_w = 3, .box_h = 14, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 612, .adv_w = 125, .box_w = 7, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 622, .adv_w = 58, .box_w = 2, .box_h = 11, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 625, .adv_w = 203, .box_w = 12, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 637, .adv_w = 130, .box_w = 7, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 644, .adv_w = 139, .box_w = 7, .box_h = 11, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 654, .adv_w = 139, .box_w = 7, .box_h = 11, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 664, .adv_w = 81, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 668, .adv_w = 118, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 675, .adv_w = 71, .box_w = 4, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 680, .adv_w = 129, .box_w = 7, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 687, .adv_w = 119, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 694, .adv_w = 176, .box_w = 11, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 705, .adv_w = 115, .box_w = 7, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 712, .adv_w = 119, .box_w = 8, .box_h = 11, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 723, .adv_w = 110, .box_w = 6, .box_h = 8, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 729, .adv_w = 96, .box_w = 4, .box_h = 2, .ofs_x = 1, .ofs_y = 9}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/

static const uint8_t glyph_id_ofs_list_1[] = {
    0, 0, 1, 0, 0, 2, 3, 4,
    5, 6, 7, 8, 9, 10, 11, 12,
    13, 14, 0, 15, 16, 17, 18, 19,
    20, 21, 22, 23, 24, 25, 26, 0,
    0, 0, 27
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 32, .range_length = 59, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    {
        .range_start = 92, .range_length = 35, .glyph_id_start = 60,
        .unicode_list = NULL, .glyph_id_ofs_list = glyph_id_ofs_list_1, .list_length = 35, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/


static const lv_font_fmt_txt_dsc_t font_dsc = {
  .glyph_bitmap = glyph_bitmap,
  .glyph_dsc = glyph_dsc,
  .cmaps = cmaps,
  .kern_dsc = NULL,
  .kern_scale = 0,
  .cmap_num = 2,
  .bpp = 1,
  .kern_classes = 0,
  .bitmap_format = 0,
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
const lv_font_t chalet_ny_1960 = {
  .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
  .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
  .line_height = 14,          /*The maximum line height required by the font*/
  .base_line = 3,             /*Baseline measured from the bottom of the line*/
  .subpx = LV_FONT_SUBPX_NONE,
  .underline_position = -1,
  .underline_thickness = 0,
  .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
  .fallback = NULL,
  .user_data = NULL,
};
