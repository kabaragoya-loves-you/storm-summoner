/*******************************************************************************
 * Size: 14 px
 * Bpp: 1
 * Opts: --bpp 1 --size 14 --no-compress --font flyer-venice.woff --symbols ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmopqrstuvwxyz1234567890 --format lvgl -o flyer_venice_14.c
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
    0x7f, 0x40,

    /* U+0022 "\"" */
    0xdb, 0x0,

    /* U+0023 "#" */
    0x0, 0xf6, 0x4b, 0x7c, 0xa2, 0x80,

    /* U+0024 "$" */
    0x1, 0x26, 0x63, 0x62,

    /* U+0025 "%" */
    0xc3, 0x21, 0xc, 0x61, 0xed, 0x80,

    /* U+0026 "&" */
    0x39, 0x25, 0x8c, 0x79, 0xe7, 0x82,

    /* U+0027 "'" */
    0x58,

    /* U+0028 "(" */
    0x3, 0x6c, 0x62, 0x10,

    /* U+0029 ")" */
    0x84, 0x31, 0x34, 0x0,

    /* U+002A "*" */
    0x27, 0x72,

    /* U+002B "+" */
    0x4, 0xe4, 0x44,

    /* U+002C "," */
    0x60,

    /* U+002D "-" */
    0x3e, 0x80,

    /* U+002E "." */
    0x10,

    /* U+002F "/" */
    0x0, 0x4, 0x10, 0x61, 0x87, 0x1c, 0x70, 0x20,

    /* U+0030 "0" */
    0x7c, 0x89, 0x16, 0x2c, 0x58, 0xa1, 0xc3, 0xff,
    0xf8,

    /* U+0031 "1" */
    0x2c, 0xb6, 0xdb, 0x78,

    /* U+0032 "2" */
    0x1f, 0x7e, 0xe, 0xe, 0xc, 0x1c, 0x38, 0x38,
    0x78, 0x3e,

    /* U+0033 "3" */
    0x6, 0x7d, 0x30, 0xe7, 0xe1, 0x83, 0xc, 0x10,
    0xc0,

    /* U+0034 "4" */
    0x2, 0x24, 0xb2, 0xcb, 0xf1, 0x87, 0x1c, 0x40,

    /* U+0035 "5" */
    0x7c, 0xc1, 0x83, 0xf0, 0x61, 0x83, 0xc, 0xf0,

    /* U+0036 "6" */
    0x0, 0x20, 0xc1, 0x6, 0xf, 0xdf, 0xb1, 0x63,
    0xcf, 0xf0,

    /* U+0037 "7" */
    0x1e, 0xfe, 0xe, 0xc, 0xc, 0x8, 0x18, 0x10,
    0x20, 0x0,

    /* U+0038 "8" */
    0x0, 0xfd, 0x99, 0x23, 0xc3, 0xe, 0x1e, 0x66,
    0xcc, 0xf0,

    /* U+0039 "9" */
    0xff, 0x38, 0xe6, 0xf8, 0x63, 0xc, 0x42, 0x0,

    /* U+003A ":" */
    0xd1,

    /* U+003B ";" */
    0x11, 0x80,

    /* U+003C "<" */
    0x4, 0x31, 0xc3, 0x81, 0x80, 0x80,

    /* U+003D "=" */
    0x38, 0x1c, 0x80,

    /* U+003E ">" */
    0x40, 0x60, 0x30, 0xe3, 0x8, 0x0,

    /* U+003F "?" */
    0xfc, 0x70, 0x8e, 0x61, 0x86, 0x18,

    /* U+0040 "@" */
    0xff, 0xfc,

    /* U+0041 "A" */
    0x10, 0x20, 0xe1, 0xc2, 0xcf, 0x99, 0x22, 0x83,
    0x4, 0x0,

    /* U+0042 "B" */
    0x61, 0xe5, 0xd3, 0x59, 0xe4, 0xd7, 0x79, 0x80,

    /* U+0043 "C" */
    0x61, 0xfc, 0x30, 0xc3, 0xc, 0x38, 0xff, 0x0,

    /* U+0044 "D" */
    0x40, 0xc1, 0xc3, 0xc6, 0xcc, 0xdb, 0x3c, 0x70,
    0xc0,

    /* U+0045 "E" */
    0x1c, 0xe1, 0x3, 0xc6, 0x18, 0x30, 0x70, 0xf9,
    0x0,

    /* U+0046 "F" */
    0x1c, 0xe1, 0x82, 0x4f, 0x18, 0x30, 0x60, 0xc1,
    0x80,

    /* U+0047 "G" */
    0x70, 0xfb, 0x6, 0xc, 0x79, 0xb3, 0x66, 0xfd,
    0xc0,

    /* U+0048 "H" */
    0x0, 0x89, 0x12, 0x24, 0x4f, 0xf9, 0xf3, 0xe7,
    0x98,

    /* U+0049 "I" */
    0x15, 0x7f, 0xf0,

    /* U+004A "J" */
    0x4, 0x10, 0x43, 0xc, 0x30, 0xd3, 0x3c, 0x70,

    /* U+004B "K" */
    0x1, 0x1a, 0x67, 0xf, 0x1b, 0x37, 0x66, 0xcd,
    0xd0,

    /* U+004C "L" */
    0x1, 0x4, 0x10, 0xc3, 0xc, 0x38, 0xfa, 0x0,

    /* U+004D "M" */
    0x82, 0x85, 0x8b, 0xb7, 0xef, 0xd6, 0xad, 0x52,
    0x4,

    /* U+004E "N" */
    0x80, 0x89, 0x8b, 0xb7, 0x6f, 0xdf, 0xa7, 0x46,
    0x8, 0x0,

    /* U+004F "O" */
    0x7c, 0x89, 0x12, 0x2c, 0x58, 0xf1, 0xe3, 0xff,
    0xf8,

    /* U+0050 "P" */
    0x41, 0xc5, 0x92, 0xdb, 0xcc, 0x30, 0xc3, 0x80,

    /* U+0051 "Q" */
    0x3c, 0xc9, 0xa, 0x14, 0x68, 0xf1, 0xe7, 0xff,
    0xfc, 0x30, 0x0,

    /* U+0052 "R" */
    0x61, 0x65, 0xb7, 0xfb, 0xcd, 0xb2, 0xcf, 0x20,

    /* U+0053 "S" */
    0x6, 0x1c, 0x38, 0x70, 0x78, 0x3c, 0x1e, 0xc,
    0x18, 0x60,

    /* U+0054 "T" */
    0xe, 0xf8, 0xe0, 0xa0, 0x20, 0x60, 0x60, 0x60,
    0x60, 0x60,

    /* U+0055 "U" */
    0x44, 0x89, 0x12, 0x24, 0x58, 0xb1, 0x62, 0xff,
    0xfc,

    /* U+0056 "V" */
    0x2, 0x18, 0x61, 0xcf, 0x3f, 0xbe, 0x79, 0xe0,

    /* U+0057 "W" */
    0x1, 0x26, 0x4c, 0x9d, 0x7b, 0xff, 0xff, 0x7e,
    0xdc,

    /* U+0058 "X" */
    0x42, 0x24, 0x14, 0x18, 0x18, 0x3c, 0x3e, 0x77,
    0x67, 0x64,

    /* U+0059 "Y" */
    0x1, 0x3, 0x13, 0x67, 0x86, 0xc, 0x18, 0x30,
    0x40,

    /* U+005A "Z" */
    0xfe, 0x3c, 0x30, 0x61, 0xc3, 0xe, 0x38, 0x78,
    0xfc,

    /* U+005C "\\" */
    0x82, 0x4, 0x10, 0x20, 0xc1, 0x87, 0x18,

    /* U+005E "^" */
    0x4e, 0x0,

    /* U+0061 "a" */
    0x10, 0x20, 0xe1, 0xc2, 0xcf, 0x99, 0x22, 0x83,
    0x4, 0x0,

    /* U+0062 "b" */
    0x61, 0xe5, 0xd3, 0x59, 0xe4, 0xd7, 0x79, 0x80,

    /* U+0063 "c" */
    0x61, 0xfc, 0x30, 0xc3, 0xc, 0x38, 0xff, 0x0,

    /* U+0064 "d" */
    0x40, 0xc1, 0xc3, 0xc6, 0xcc, 0xdb, 0x3c, 0x70,
    0xc0,

    /* U+0065 "e" */
    0x1c, 0xe1, 0x3, 0xc6, 0x18, 0x30, 0x70, 0xf9,
    0x0,

    /* U+0066 "f" */
    0x1c, 0xe1, 0x82, 0x4f, 0x18, 0x30, 0x60, 0xc1,
    0x80,

    /* U+0067 "g" */
    0x70, 0xfb, 0x6, 0xc, 0x79, 0xb3, 0x66, 0xfd,
    0xc0,

    /* U+0068 "h" */
    0x0, 0x89, 0x12, 0x24, 0x4f, 0xf9, 0xf3, 0xe7,
    0x98,

    /* U+0069 "i" */
    0x15, 0x7f, 0xf0,

    /* U+006A "j" */
    0x4, 0x10, 0x43, 0xc, 0x30, 0xd3, 0x3c, 0x70,

    /* U+006B "k" */
    0x1, 0x1a, 0x67, 0xf, 0x1b, 0x37, 0x66, 0xcd,
    0xd0,

    /* U+006C "l" */
    0x1, 0x4, 0x10, 0xc3, 0xc, 0x38, 0xfa, 0x0,

    /* U+006D "m" */
    0x82, 0x85, 0x8b, 0xb7, 0xef, 0xd6, 0xad, 0x52,
    0x4,

    /* U+006F "o" */
    0x7c, 0x89, 0x12, 0x2c, 0x58, 0xf1, 0xe3, 0xff,
    0xf8,

    /* U+0070 "p" */
    0x41, 0xc5, 0x92, 0xdb, 0xcc, 0x30, 0xc3, 0x80,

    /* U+0071 "q" */
    0x3c, 0xc9, 0xa, 0x14, 0x68, 0xf1, 0xe7, 0xff,
    0xfc, 0x30, 0x0,

    /* U+0072 "r" */
    0x61, 0x65, 0xb7, 0xfb, 0xcd, 0xb2, 0xcf, 0x20,

    /* U+0073 "s" */
    0x6, 0x1c, 0x38, 0x70, 0x78, 0x3c, 0x1e, 0xc,
    0x18, 0x60,

    /* U+0074 "t" */
    0xe, 0xf8, 0xe0, 0xa0, 0x20, 0x60, 0x60, 0x60,
    0x60, 0x60,

    /* U+0075 "u" */
    0x44, 0x89, 0x12, 0x24, 0x58, 0xb1, 0x62, 0xff,
    0xfc,

    /* U+0076 "v" */
    0x2, 0x18, 0x61, 0xcf, 0x3f, 0xbe, 0x79, 0xe0,

    /* U+0077 "w" */
    0x1, 0x26, 0x4c, 0x9d, 0x7b, 0xff, 0xff, 0x7e,
    0xdc,

    /* U+0078 "x" */
    0x42, 0x24, 0x14, 0x18, 0x18, 0x3c, 0x3e, 0x77,
    0x67, 0x64,

    /* U+0079 "y" */
    0x1, 0x3, 0x13, 0x67, 0x86, 0xc, 0x18, 0x30,
    0x40,

    /* U+007A "z" */
    0xfe, 0x3c, 0x30, 0x61, 0xc3, 0xe, 0x38, 0x78,
    0xfc,

    /* U+007E "~" */
    0x43
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 67, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1, .adv_w = 41, .box_w = 1, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 3, .adv_w = 62, .box_w = 3, .box_h = 4, .ofs_x = 1, .ofs_y = 6},
    {.bitmap_index = 5, .adv_w = 110, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 11, .adv_w = 89, .box_w = 4, .box_h = 8, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 15, .adv_w = 103, .box_w = 6, .box_h = 7, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 21, .adv_w = 102, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 27, .adv_w = 35, .box_w = 2, .box_h = 4, .ofs_x = 0, .ofs_y = 6},
    {.bitmap_index = 28, .adv_w = 76, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 32, .adv_w = 77, .box_w = 4, .box_h = 7, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 36, .adv_w = 74, .box_w = 4, .box_h = 4, .ofs_x = 0, .ofs_y = 7},
    {.bitmap_index = 38, .adv_w = 80, .box_w = 4, .box_h = 6, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 41, .adv_w = 37, .box_w = 2, .box_h = 2, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 42, .adv_w = 86, .box_w = 5, .box_h = 2, .ofs_x = 0, .ofs_y = 4},
    {.bitmap_index = 44, .adv_w = 39, .box_w = 2, .box_h = 2, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 45, .adv_w = 124, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 53, .adv_w = 133, .box_w = 7, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 62, .adv_w = 61, .box_w = 3, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 66, .adv_w = 132, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 76, .adv_w = 121, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 85, .adv_w = 121, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 93, .adv_w = 127, .box_w = 7, .box_h = 9, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 101, .adv_w = 118, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 111, .adv_w = 126, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 121, .adv_w = 120, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 131, .adv_w = 118, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 139, .adv_w = 40, .box_w = 2, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 140, .adv_w = 40, .box_w = 2, .box_h = 5, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 142, .adv_w = 116, .box_w = 7, .box_h = 6, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 148, .adv_w = 87, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 151, .adv_w = 115, .box_w = 7, .box_h = 6, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 157, .adv_w = 107, .box_w = 6, .box_h = 8, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 163, .adv_w = 70, .box_w = 4, .box_h = 4, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 165, .adv_w = 118, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 175, .adv_w = 106, .box_w = 6, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 183, .adv_w = 120, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 191, .adv_w = 118, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 200, .adv_w = 123, .box_w = 7, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 209, .adv_w = 99, .box_w = 7, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 218, .adv_w = 122, .box_w = 7, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 227, .adv_w = 120, .box_w = 7, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 236, .adv_w = 61, .box_w = 2, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 239, .adv_w = 110, .box_w = 6, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 247, .adv_w = 123, .box_w = 7, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 256, .adv_w = 113, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 264, .adv_w = 127, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 273, .adv_w = 117, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 283, .adv_w = 131, .box_w = 7, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 292, .adv_w = 105, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 300, .adv_w = 130, .box_w = 7, .box_h = 12, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 311, .adv_w = 119, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 319, .adv_w = 126, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 329, .adv_w = 94, .box_w = 8, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 339, .adv_w = 123, .box_w = 7, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 348, .adv_w = 113, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 356, .adv_w = 128, .box_w = 7, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 365, .adv_w = 133, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 375, .adv_w = 112, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 384, .adv_w = 119, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 393, .adv_w = 104, .box_w = 6, .box_h = 9, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 400, .adv_w = 58, .box_w = 3, .box_h = 3, .ofs_x = 0, .ofs_y = 8},
    {.bitmap_index = 402, .adv_w = 118, .box_w = 7, .box_h = 11, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 412, .adv_w = 106, .box_w = 6, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 420, .adv_w = 120, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 428, .adv_w = 118, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 437, .adv_w = 123, .box_w = 7, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 446, .adv_w = 99, .box_w = 7, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 455, .adv_w = 122, .box_w = 7, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 464, .adv_w = 120, .box_w = 7, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 473, .adv_w = 61, .box_w = 2, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 476, .adv_w = 110, .box_w = 6, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 484, .adv_w = 123, .box_w = 7, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 493, .adv_w = 113, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 501, .adv_w = 127, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 510, .adv_w = 131, .box_w = 7, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 519, .adv_w = 105, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 527, .adv_w = 130, .box_w = 7, .box_h = 12, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 538, .adv_w = 119, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 546, .adv_w = 126, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 556, .adv_w = 94, .box_w = 8, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 566, .adv_w = 123, .box_w = 7, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 575, .adv_w = 113, .box_w = 6, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 583, .adv_w = 128, .box_w = 7, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 592, .adv_w = 133, .box_w = 8, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 602, .adv_w = 112, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 611, .adv_w = 119, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 620, .adv_w = 74, .box_w = 4, .box_h = 2, .ofs_x = 0, .ofs_y = 9}
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
    .cmap_num = 4,
    .bpp = 1,
    .kern_classes = 0,
    .bitmap_format = 0,
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
const lv_font_t flyer_venice_14 = {
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 13,          /*The maximum line height required by the font*/
    .base_line = 2,             /*Baseline measured from the bottom of the line*/
    .subpx = LV_FONT_SUBPX_NONE,
    .underline_position = -2,
    .underline_thickness = 0,
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
    .fallback = NULL,
    .user_data = NULL,
};
