// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "lvgl.h"
#define TOUCH_SYM_STAR     "*"             /* render in Montserrat at any size */
#define TOUCH_SYM_STAR_BIG "\xE2\x98\x85"  /* render ONLY with star_font_28    */
#define TOUCH_SYM_PERSON  "\xEF\x80\x87"   /* U+F007 user */
#define TOUCH_SYM_ANTENNA "\xEF\x94\x99"   /* U+F519 tower-broadcast */
#define TOUCH_SYM_GROUP   "\xEF\x83\x80"   /* U+F0C0 users (group) */
#define TOUCH_SYM_ZOOM    "\xEF\x80\x82"   /* U+F002 magnifying-glass */
#define TOUCH_SYM_SUN  "\xEF\x86\x85"   /* U+F185 sun */
#define TOUCH_SYM_MOON "\xEF\x86\x86"   /* U+F186 moon */
#define TOUCH_SYM_LOCK       "\xEF\x80\xA3"   /* U+F023 lock */
#define TOUCH_SYM_BELL       "\xEF\x83\xB3"   /* U+F0F3 bell (notifications on) */
#define TOUCH_SYM_BELL_SLASH "\xEF\x87\xB6"   /* U+F1F6 bell-slash (silenced) */
extern "C" const lv_font_t star_font_28;
extern "C" const lv_font_t star_font_14;
extern "C" const lv_font_t person_font;
extern "C" const lv_font_t person_font14;
extern "C" const lv_font_t zoom_font;
extern "C" const lv_font_t sleepicons_font;
extern "C" const lv_font_t cc_icons_16;
extern "C" const lv_font_t extras_12;
extern "C" const lv_font_t extras_14;
extern "C" const lv_font_t extras_16;
extern "C" const lv_font_t extras_lat_28;
extern "C" const lv_font_t extras_lat_20;
extern "C" const lv_font_t extras_20;
extern "C" const lv_font_t extras_24;
extern "C" const lv_font_t extras_lat_24;
namespace ui { namespace theme {
const lv_font_t& font12();
const lv_font_t& font14();
const lv_font_t& font16();
const lv_font_t* emojiFont();
const lv_font_t* uiChromeFont();
const lv_font_t* chatMessageFont();
void initTouchFontFallbacks();
void useChainedFont(lv_obj_t* label);
void uiFitLabelWidth(lv_obj_t* label, lv_coord_t max_width);
lv_coord_t SC(int px);
lv_coord_t PSC(int px);
lv_coord_t PCW(int px);
} }
