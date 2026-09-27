// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "lvgl.h"
namespace ui { namespace widgets {
void styleSurface(lv_obj_t* obj, uint32_t bg, lv_coord_t radius = 10);
void styleCard(lv_obj_t* obj);
void styleButton(lv_obj_t* obj);
void styleEpaperControlOutline(lv_obj_t* obj, lv_style_selector_t selector);
uint32_t lightSurfaceTextRgb(uint32_t color);
lv_color_t lightSurfaceTextColor(uint32_t color);
void normalizeLightSurfaceRecolor(char* text);
void taSetPlaceholder(lv_obj_t* ta, const char* text);
void tanCloseRed(lv_obj_t* label);
lv_obj_t* addCloseXBadge(lv_obj_t* card, lv_event_cb_t cb, void* user_data = nullptr);
void setSelectionGlow(lv_obj_t* obj, bool selected, lv_style_selector_t selector);
void setNavSelectionGlow(lv_obj_t* obj, bool selected);
void refreshSelectionColors();
} }
