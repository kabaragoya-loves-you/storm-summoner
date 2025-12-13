/*******************************************************************************
 * Size: 8 px
 * Bpp: 4
 * Opts: --bpp 4 --size 8 --stride 1 --align 1 --font chalet-newyork-1960.woff --symbols ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz1234567890,./<>?[]\{}|`~!@#$%^&*()_+
-= ;':" --format lvgl -o chalet_ny_8.c
 ******************************************************************************/

#ifdef __has_include
    #if __has_include("lvgl.h")
        #ifndef LV_LVGL_H_INCLUDE_SIMPLE
            #define LV_LVGL_H_INCLUDE_SIMPLE
        #endif
    #endif
#endif

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
    #include "lvgl.h"
#else
    #include "lvgl/lvgl.h"
#endif



#ifndef CHALET_NY_8
#define CHALET_NY_8 1
#endif

#if CHALET_NY_8

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */

    /* U+0021 "!" */
    0x6b, 0x0, 0xc5, 0x85, 0x80,

    /* U+0022 "\"" */
    0xb8, 0xa2, 0xd3, 0x85, 0x80,

    /* U+0023 "#" */
    0xb, 0x39, 0x6, 0x4e, 0x42, 0x63, 0xa8, 0x2a,
    0x29, 0xf0, 0xe8, 0x7a, 0x0,

    /* U+0024 "$" */
    0x1a, 0x90, 0xb1, 0x5, 0x97, 0x51, 0xb6, 0x66,
    0xc8, 0x1c, 0x0,

    /* U+0025 "%" */
    0x6c, 0x81, 0xb0, 0x5, 0x6a, 0xc4, 0x0, 0x11,
    0x4e, 0xb1, 0x20, 0x96, 0xcc, 0xf3, 0x20, 0x7a,
    0x98, 0x12,

    /* U+0026 "&" */
    0xb, 0xe5, 0x0, 0x95, 0xc, 0x0, 0x84, 0x84,
    0xe1, 0xd3, 0x76, 0xe0, 0x98, 0x81, 0x10, 0x0,

    /* U+0027 "'" */
    0xb4, 0x26, 0x82,

    /* U+0028 "(" */
    0x9, 0x33, 0x11, 0xde, 0x1, 0x10, 0x8, 0xc3,
    0x34, 0x10, 0x4c,

    /* U+0029 ")" */
    0x93, 0xb, 0xf0, 0x21, 0x20, 0x66, 0x3, 0x30,
    0x40, 0xae, 0x40,

    /* U+002A "*" */
    0x27, 0x35, 0x8a, 0x58, 0xa0,

    /* U+002B "+" */
    0x7, 0xa0, 0xa8, 0x5d, 0xa8, 0x5d, 0x5, 0xe0,

    /* U+002C "," */
    0x0, 0x54, 0x4f, 0x13, 0x0,

    /* U+002D "-" */
    0xbe, 0x50,

    /* U+002E "." */
    0x0, 0x54, 0x0,

    /* U+002F "/" */
    0x7, 0xa0, 0xcf, 0x4, 0x48, 0xc7, 0x50, 0x12,
    0xc0,

    /* U+0030 "0" */
    0x1c, 0xe9, 0x9, 0x4e, 0x43, 0x36, 0x83, 0xb1,
    0x68, 0x3b, 0x42, 0x72, 0x18,

    /* U+0031 "1" */
    0x6e, 0x46, 0x20, 0xf, 0xf0,

    /* U+0032 "2" */
    0x2c, 0xe9, 0x5, 0xbe, 0x61, 0x74, 0xc6, 0x15,
    0xef, 0x90, 0xe2, 0xcf, 0x10,

    /* U+0033 "3" */
    0x2d, 0xe8, 0x7, 0x8c, 0x70, 0x54, 0xd4, 0x7,
    0x5f, 0x42, 0x3a, 0xe6, 0x20,

    /* U+0034 "4" */
    0x1, 0xe9, 0x0, 0x52, 0x80, 0x18, 0x74, 0x1,
    0x49, 0xc, 0x59, 0xd0, 0xc4,

    /* U+0035 "5" */
    0x5f, 0xf5, 0x86, 0x91, 0x34, 0x2a, 0x72, 0x41,
    0x44, 0x0, 0x29, 0x1f, 0x22,

    /* U+0036 "6" */
    0x1b, 0xeb, 0x9, 0x53, 0x30, 0x12, 0x6b, 0x19,
    0x80, 0x34, 0x37, 0x39, 0x80,

    /* U+0037 "7" */
    0xcf, 0xfb, 0x3c, 0xe8, 0x1d, 0x94, 0x2f, 0x80,
    0x6, 0xa0,

    /* U+0038 "8" */
    0x2d, 0xe7, 0x5, 0x4c, 0x90, 0x46, 0xd4, 0x9,
    0x97, 0xa0, 0xf5, 0xf2, 0x8,

    /* U+0039 "9" */
    0x4d, 0xe7, 0xf, 0xbf, 0x81, 0xfb, 0xd0, 0x36,
    0xfd, 0x2, 0x1f, 0xf4, 0x80,

    /* U+003A ":" */
    0x98, 0x98, 0x0, 0x4c, 0x0,

    /* U+003B ";" */
    0x98, 0x98, 0x0, 0x4c, 0x57, 0x93, 0x80,

    /* U+003C "<" */
    0x2, 0x94, 0x8f, 0xf2, 0x70, 0x78, 0xb6, 0xf9,
    0x0,

    /* U+003D "=" */
    0xaf, 0xf5, 0xd7, 0xfa, 0xeb, 0xb9, 0x60,

    /* U+003E ">" */
    0x96, 0x0, 0x5f, 0xf1, 0xb2, 0x90, 0x9f, 0xe1,
    0x0,

    /* U+003F "?" */
    0x3d, 0xe8, 0x5, 0x9c, 0x70, 0x65, 0xfa, 0x0,
    0x1c, 0xa8, 0x1, 0x38, 0x0,

    /* U+0040 "@" */
    0x8, 0xcb, 0xa1, 0x70, 0x76, 0x9, 0xce, 0x31,
    0xe1, 0xcf, 0x36, 0x8d, 0x71, 0x1, 0x76,

    /* U+0041 "A" */
    0x3, 0xf8, 0x0, 0xa9, 0x58, 0x2, 0x7b, 0x24,
    0x5, 0x1f, 0x3e, 0x9, 0xae, 0xe2, 0x88,

    /* U+0042 "B" */
    0x7f, 0xee, 0x28, 0x37, 0x47, 0x3, 0x74, 0xe0,
    0x37, 0x5c, 0x83, 0x75, 0xc8,

    /* U+0043 "C" */
    0x8, 0xee, 0x40, 0x3a, 0x7f, 0x84, 0xf2, 0x84,
    0x52, 0x79, 0xa0, 0x32, 0x6e, 0x5d, 0xe2, 0x60,

    /* U+0044 "D" */
    0x7f, 0xec, 0x30, 0x3, 0x74, 0xf0, 0x6, 0x52,
    0x0, 0xca, 0x40, 0x6, 0xe9, 0xe0,

    /* U+0045 "E" */
    0x7f, 0xfa, 0x1, 0xbf, 0xd0, 0xd, 0xdc, 0x10,
    0x6e, 0xe0, 0x83, 0x7f, 0xa4,

    /* U+0046 "F" */
    0x7f, 0xf9, 0xc1, 0xbf, 0xce, 0xd, 0xfe, 0x10,
    0x6f, 0xf0, 0x80, 0x70,

    /* U+0047 "G" */
    0x8, 0xee, 0x48, 0x3a, 0x77, 0x45, 0x94, 0x19,
    0x37, 0x9a, 0x18, 0xa0, 0xe7, 0xba, 0x70,

    /* U+0048 "H" */
    0x79, 0x0, 0x78, 0x7, 0x9b, 0xfc, 0x0, 0x6f,
    0xf0, 0x7, 0x80,

    /* U+0049 "I" */
    0x79, 0x0, 0xfc,

    /* U+004A "J" */
    0x0, 0x26, 0x0, 0x61, 0x0, 0xb1, 0x9, 0xe2,
    0xa3, 0xc0,

    /* U+004B "K" */
    0x79, 0x1c, 0x90, 0x1, 0xea, 0x48, 0x1, 0x4e,
    0xc0, 0x26, 0xad, 0x10, 0x9, 0x5b, 0x40,

    /* U+004C "L" */
    0x79, 0x0, 0xff, 0xe3, 0xb7, 0xf9, 0x40,

    /* U+004D "M" */
    0x7f, 0x30, 0xcf, 0x1, 0xb1, 0x20, 0x2, 0x2b,
    0xa0, 0x0, 0x86, 0xfc, 0x2, 0xb2, 0x40,

    /* U+004E "N" */
    0x7f, 0x20, 0xf1, 0x1, 0x90, 0xc, 0xee, 0x50,
    0xd, 0xd6, 0x1, 0x8a, 0x40, 0x0,

    /* U+004F "O" */
    0x9, 0xee, 0x40, 0x3a, 0xff, 0x91, 0xf2, 0x84,
    0x55, 0xb9, 0x42, 0x2a, 0xd7, 0x5f, 0xf2, 0x38,

    /* U+0050 "P" */
    0x7f, 0xf7, 0x20, 0x37, 0xb7, 0x3, 0x72, 0xc8,
    0x37, 0x69, 0x80, 0x70,

    /* U+0051 "Q" */
    0x9, 0xee, 0x40, 0x3a, 0xff, 0x91, 0xf2, 0x84,
    0x55, 0xb9, 0x45, 0x76, 0xc7, 0x5c, 0x53, 0x20,

    /* U+0052 "R" */
    0x7f, 0xf7, 0x38, 0x37, 0xec, 0x83, 0x76, 0x48,
    0x37, 0x56, 0x0, 0x4c, 0xe0,

    /* U+0053 "S" */
    0x2c, 0xfc, 0x2a, 0x6f, 0x94, 0x93, 0xce, 0x26,
    0xfc, 0xe8, 0x5a, 0xe8, 0x90,

    /* U+0054 "T" */
    0xdf, 0xf8, 0xb6, 0x57, 0xc8, 0x3, 0xff, 0x86,

    /* U+0055 "U" */
    0x88, 0x2, 0xe0, 0x7, 0xf3, 0xf9, 0x86, 0x8b,
    0x7, 0xac, 0x0,

    /* U+0056 "V" */
    0xc4, 0x7, 0x9a, 0xe0, 0xbd, 0x74, 0x22, 0x20,
    0xaf, 0x30, 0x3, 0x8e, 0x80,

    /* U+0057 "W" */
    0xd4, 0x2f, 0x52, 0xf0, 0x5c, 0x42, 0xd4, 0x50,
    0xb4, 0xcb, 0x4c, 0xf0, 0x32, 0x46, 0x24, 0x40,
    0x1, 0xcc, 0x15, 0x42, 0x0,

    /* U+0058 "X" */
    0x8a, 0xc, 0x78, 0x7b, 0x77, 0x4, 0x27, 0x80,
    0x21, 0x60, 0x29, 0x29, 0xe0,

    /* U+0059 "Y" */
    0xb, 0x70, 0xb7, 0x9, 0x92, 0xb3, 0x81, 0x35,
    0x48, 0x5, 0x42, 0x80, 0x1f, 0x0,

    /* U+005A "Z" */
    0xaf, 0xf9, 0xeb, 0xe8, 0x98, 0x16, 0x30, 0x52,
    0x30, 0x42, 0xb, 0xbd, 0xc0,

    /* U+005B "[" */
    0x7d, 0x50, 0xa5, 0x0, 0xff, 0xe0, 0xd2, 0x80,

    /* U+005C "\\" */
    0xf, 0x20, 0x2, 0xa0, 0x3, 0x34, 0x0, 0xe8,
    0x0, 0x12, 0x40, 0x2, 0xe0,

    /* U+005D "]" */
    0xae, 0x2a, 0x21, 0x0, 0xff, 0xe0, 0x51, 0x8,

    /* U+005E "^" */
    0x3, 0x60, 0xa, 0x20, 0x40, 0x4d, 0x34, 0x14,
    0xac, 0xc0,

    /* U+005F "_" */
    0xf, 0xff, 0x38,

    /* U+0060 "`" */
    0x1b, 0x21, 0xb2,

    /* U+0061 "a" */
    0x2d, 0xd5, 0x80, 0xd5, 0x10, 0xa7, 0x48, 0x43,
    0x1e, 0x0, 0x0,

    /* U+0062 "b" */
    0x88, 0x0, 0x7f, 0x2f, 0x60, 0x83, 0x72, 0x40,
    0x6, 0x30, 0x6e, 0x4b,

    /* U+0063 "c" */
    0x1c, 0xea, 0xb, 0x4e, 0xf1, 0x0, 0xe8, 0x4e,
    0xf1,

    /* U+0064 "d" */
    0x0, 0xa5, 0xc0, 0x38, 0xb3, 0x90, 0x21, 0x38,
    0x40, 0x1e, 0xe, 0x11, 0x7a, 0xc0,

    /* U+0065 "e" */
    0x1c, 0xdb, 0xb, 0x20, 0x55, 0x3, 0xeb, 0xba,
    0xd7, 0x80, 0x80,

    /* U+0066 "f" */
    0xc, 0x65, 0x76, 0xb4, 0x59, 0x55, 0x0, 0x78,

    /* U+0067 "g" */
    0x2c, 0xed, 0x78, 0x4e, 0x10, 0xf, 0x42, 0xf0,
    0x80, 0xcf, 0x9, 0xc, 0x73, 0x28,

    /* U+0068 "h" */
    0x88, 0x0, 0x7f, 0x37, 0xc8, 0x1, 0x3d, 0x88,
    0x8, 0x4, 0x3, 0xc0,

    /* U+0069 "i" */
    0x78, 0xf1, 0x0, 0xf0,

    /* U+006A "j" */
    0x8, 0x70, 0x1f, 0x0, 0xff, 0xbc, 0x9d, 0x0,

    /* U+006B "k" */
    0x88, 0x0, 0x7f, 0xa7, 0x0, 0xc, 0xcd, 0x0,
    0x19, 0x38, 0x1, 0x7a, 0x48,

    /* U+006C "l" */
    0x89, 0x0, 0xff, 0x0,

    /* U+006D "m" */
    0x8e, 0xe5, 0xee, 0x90, 0x13, 0x96, 0x62, 0x0,
    0x40, 0x6, 0x10, 0xf, 0xc0,

    /* U+006E "n" */
    0x8e, 0xe5, 0x0, 0x1f, 0x90, 0x40, 0x40, 0x6,
    0x1, 0xc0,

    /* U+006F "o" */
    0x1c, 0xe8, 0xb, 0xb7, 0x2a, 0x0, 0x74, 0x5f,
    0x22, 0x0,

    /* U+0070 "p" */
    0x8d, 0xd6, 0x8, 0x36, 0xdc, 0x0, 0x5e, 0x0,
    0x6e, 0x4b, 0x5, 0xec, 0x20, 0xe,

    /* U+0071 "q" */
    0x2c, 0xed, 0x78, 0x4e, 0x10, 0xf, 0x42, 0x70,
    0x81, 0x67, 0x20, 0x7, 0x80,

    /* U+0072 "r" */
    0x0, 0xa3, 0x6c, 0x1e, 0xc0, 0x80, 0x30,

    /* U+0073 "s" */
    0x5e, 0xe3, 0x61, 0x49, 0x6d, 0x7d, 0x4, 0x33,
    0x0,

    /* U+0074 "t" */
    0x7a, 0x9, 0x55, 0x4a, 0xa8, 0x44, 0xe, 0xcc,

    /* U+0075 "u" */
    0x88, 0x7, 0x10, 0x7, 0xe1, 0xa, 0x5d, 0x0,
    0x0,

    /* U+0076 "v" */
    0xc4, 0xf, 0x1a, 0xe5, 0x50, 0xb2, 0x77, 0x0,
    0x8, 0xa8, 0x0,

    /* U+0077 "w" */
    0xd4, 0x8c, 0xf, 0x15, 0xc4, 0x25, 0x31, 0xb5,
    0x6, 0x8b, 0x3, 0x4, 0xc2, 0x50,

    /* U+0078 "x" */
    0x99, 0x3d, 0x4, 0xa4, 0xd8, 0x8, 0x6, 0xb5,
    0x8d, 0x0,

    /* U+0079 "y" */
    0xc4, 0x1f, 0x1a, 0xe4, 0x51, 0x74, 0xee, 0x0,
    0xa2, 0xa0, 0x0, 0x98, 0x0, 0xcb, 0x20, 0x0,

    /* U+007A "z" */
    0xae, 0xfb, 0xaa, 0xc, 0x1d, 0x69, 0xf0, 0xed,
    0x80,

    /* U+007B "{" */
    0x0, 0xd6, 0xc0, 0xac, 0x46, 0x1c, 0xa1, 0xca,
    0x4, 0x60, 0x5, 0x60, 0xb6,

    /* U+007C "|" */
    0x79, 0x0, 0xff, 0x0,

    /* U+007D "}" */
    0x0, 0xa9, 0x82, 0x74, 0x5, 0x80, 0x81, 0xc8,
    0x1c, 0x58, 0x27, 0x42, 0x98, 0x0,

    /* U+007E "~" */
    0x38, 0x70
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 39, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 0, .adv_w = 37, .box_w = 2, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 5, .adv_w = 48, .box_w = 3, .box_h = 3, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 10, .adv_w = 69, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 23, .adv_w = 63, .box_w = 4, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 34, .adv_w = 102, .box_w = 7, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 52, .adv_w = 84, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 68, .adv_w = 25, .box_w = 2, .box_h = 3, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 71, .adv_w = 42, .box_w = 3, .box_h = 7, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 82, .adv_w = 42, .box_w = 3, .box_h = 7, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 93, .adv_w = 50, .box_w = 3, .box_h = 3, .ofs_x = 0, .ofs_y = 3},
    {.bitmap_index = 98, .adv_w = 67, .box_w = 4, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 106, .adv_w = 30, .box_w = 2, .box_h = 4, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 111, .adv_w = 42, .box_w = 3, .box_h = 1, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 113, .adv_w = 30, .box_w = 2, .box_h = 2, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 116, .adv_w = 43, .box_w = 3, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 125, .adv_w = 75, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 138, .adv_w = 54, .box_w = 3, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 143, .adv_w = 72, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 156, .adv_w = 71, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 169, .adv_w = 71, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 182, .adv_w = 70, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 195, .adv_w = 72, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 208, .adv_w = 66, .box_w = 4, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 218, .adv_w = 71, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 231, .adv_w = 72, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 244, .adv_w = 31, .box_w = 2, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 249, .adv_w = 31, .box_w = 2, .box_h = 6, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 256, .adv_w = 57, .box_w = 4, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 265, .adv_w = 65, .box_w = 4, .box_h = 3, .ofs_x = 0, .ofs_y = 1},
    {.bitmap_index = 272, .adv_w = 58, .box_w = 4, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 281, .adv_w = 71, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 294, .adv_w = 95, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 309, .adv_w = 85, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 324, .adv_w = 83, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 337, .adv_w = 91, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 353, .adv_w = 85, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 367, .adv_w = 78, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 380, .adv_w = 75, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 392, .adv_w = 96, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 407, .adv_w = 88, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 418, .adv_w = 34, .box_w = 2, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 421, .adv_w = 68, .box_w = 4, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 431, .adv_w = 83, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 446, .adv_w = 72, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 453, .adv_w = 104, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 468, .adv_w = 90, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 482, .adv_w = 95, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 498, .adv_w = 78, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 510, .adv_w = 95, .box_w = 6, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 526, .adv_w = 85, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 539, .adv_w = 83, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 552, .adv_w = 69, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 560, .adv_w = 83, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 571, .adv_w = 77, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 584, .adv_w = 114, .box_w = 8, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 605, .adv_w = 78, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 618, .adv_w = 76, .box_w = 6, .box_h = 5, .ofs_x = -1, .ofs_y = 0},
    {.bitmap_index = 632, .adv_w = 76, .box_w = 5, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 645, .adv_w = 41, .box_w = 3, .box_h = 7, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 653, .adv_w = 43, .box_w = 4, .box_h = 6, .ofs_x = -1, .ofs_y = 0},
    {.bitmap_index = 666, .adv_w = 43, .box_w = 3, .box_h = 7, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 674, .adv_w = 71, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 2},
    {.bitmap_index = 684, .adv_w = 103, .box_w = 8, .box_h = 1, .ofs_x = -1, .ofs_y = -1},
    {.bitmap_index = 687, .adv_w = 53, .box_w = 3, .box_h = 2, .ofs_x = 0, .ofs_y = 4},
    {.bitmap_index = 690, .adv_w = 74, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 701, .adv_w = 79, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 713, .adv_w = 73, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 722, .adv_w = 80, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 736, .adv_w = 75, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 747, .adv_w = 40, .box_w = 3, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 755, .adv_w = 78, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 769, .adv_w = 74, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 781, .adv_w = 33, .box_w = 2, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 785, .adv_w = 31, .box_w = 3, .box_h = 7, .ofs_x = -1, .ofs_y = -2},
    {.bitmap_index = 793, .adv_w = 71, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 806, .adv_w = 33, .box_w = 2, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 810, .adv_w = 116, .box_w = 7, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 823, .adv_w = 74, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 833, .adv_w = 74, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 843, .adv_w = 79, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 857, .adv_w = 79, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 870, .adv_w = 47, .box_w = 3, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 877, .adv_w = 67, .box_w = 4, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 886, .adv_w = 40, .box_w = 3, .box_h = 5, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 894, .adv_w = 74, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 903, .adv_w = 68, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 914, .adv_w = 100, .box_w = 7, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 928, .adv_w = 66, .box_w = 5, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 938, .adv_w = 68, .box_w = 5, .box_h = 6, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 954, .adv_w = 63, .box_w = 4, .box_h = 4, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 963, .adv_w = 43, .box_w = 3, .box_h = 9, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 976, .adv_w = 34, .box_w = 2, .box_h = 6, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 980, .adv_w = 43, .box_w = 3, .box_h = 9, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 994, .adv_w = 55, .box_w = 4, .box_h = 1, .ofs_x = 0, .ofs_y = 4}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/



/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 32, .range_length = 95, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 1,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif

};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t chalet_ny_8 = {
#else
lv_font_t chalet_ny_8 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 9,          /*The maximum line height required by the font*/
    .base_line = 2,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = 0,
    .underline_thickness = 0,
#endif
    .static_bitmap = 0,
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if CHALET_NY_8*/
