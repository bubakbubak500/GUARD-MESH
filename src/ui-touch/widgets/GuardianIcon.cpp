// SPDX-License-Identifier: GPL-3.0-or-later
#include "GuardianIcon.h"
#include <cmath>
#include <initializer_list>
namespace ui { namespace widgets {
namespace {
void draw(lv_event_t* e) {
  auto* obj=lv_event_get_target(e); auto* ctx=lv_event_get_draw_ctx(e);
  lv_area_t area; lv_obj_get_coords(obj,&area);
  const int size=lv_obj_get_width(obj);
  const auto blue=lv_obj_get_style_text_color(obj,0);
  const auto white=lv_color_hex(0xeaf1ff);
  auto point=[&](int x,int y) { return lv_point_t{lv_coord_t(area.x1+x*(size-1)/32),lv_coord_t(area.y1+y*(size-1)/32)}; };
  auto path=[&](std::initializer_list<lv_point_t> points,lv_color_t color,int width=2) {
    lv_draw_line_dsc_t d; lv_draw_line_dsc_init(&d); d.color=color; d.width=width; d.round_start=d.round_end=1;
    auto prev=points.begin(); for(auto next=prev+1;next!=points.end();++next) { auto a=point(prev->x,prev->y),b=point(next->x,next->y); lv_draw_line(ctx,&d,&a,&b); prev=next; }
  };
  auto rect=[&](int x,int y,int w,int h,lv_color_t fill,int radius,int border=0) {
    auto a=point(x,y),b=point(x+w,y+h); lv_area_t r{a.x,a.y,b.x,b.y};
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d); d.bg_color=fill; d.radius=radius;
    d.border_color=white; d.border_width=border; lv_draw_rect(ctx,&d,&r);
  };
  auto triangle=[&](lv_point_t a,lv_point_t b,lv_point_t c,lv_color_t color) {
    lv_point_t pts[]={point(a.x,a.y),point(b.x,b.y),point(c.x,c.y)};
    lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d); d.bg_color=color; lv_draw_polygon(ctx,&d,pts,3);
  };
  const auto kind=static_cast<GuardianIcon>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  switch(kind) {
    case GuardianIcon::Mail:
      rect(2,5,28,23,blue,2,2); path({{3,7},{16,18},{29,7}},white); break;
    case GuardianIcon::Network:
      path({{16,7},{6,26},{26,26},{16,7}},blue,3);
      for(auto p:{lv_point_t{16,6},lv_point_t{5,26},lv_point_t{27,26}}) rect(p.x-4,p.y-4,8,8,blue,LV_RADIUS_CIRCLE,1);
      break;
    case GuardianIcon::Pencil:
      path({{3,29},{6,19},{24,1},{31,8},{13,26},{3,29}},white);
      path({{20,5},{27,12}},white); path({{6,19},{13,26}},white); break;
    case GuardianIcon::Send:
      triangle({2,14},{30,2},{13,21},blue); triangle({13,21},{30,2},{21,30},blue);
      path({{2,14},{30,2},{21,30},{13,21},{2,14}},white); path({{13,21},{30,2}},white); break;
    case GuardianIcon::Bookmark:
      rect(6,2,20,17,blue,2); triangle({6,17},{26,17},{6,30},blue); triangle({26,17},{26,30},{16,24},blue); break;
    case GuardianIcon::Down: case GuardianIcon::Up: {
      const bool up=kind==GuardianIcon::Up;
      path({{16,up?28:4},{16,up?4:28}},blue,3);
      path({{6,up?14:18},{16,up?4:28},{26,up?14:18}},blue,3); break;
    }
    case GuardianIcon::Person:
      rect(10,2,12,12,white,LV_RADIUS_CIRCLE); rect(3,19,26,12,white,6); break;
    case GuardianIcon::Gear:
      for(int i=0;i<8;++i) { const float a=i*3.14159265f/4;
        path({{lv_coord_t(16+10*cos(a)),lv_coord_t(16+10*sin(a))},{lv_coord_t(16+14*cos(a)),lv_coord_t(16+14*sin(a))}},white,3);
      }
      rect(5,5,22,22,white,LV_RADIUS_CIRCLE); rect(10,10,12,12,blue,LV_RADIUS_CIRCLE); break;
    case GuardianIcon::Home:
      path({{2,15},{16,3},{30,15}},white); path({{7,13},{7,29},{13,29},{13,20},{20,20},{20,29},{26,29},{26,13}},white); break;
    case GuardianIcon::Chevron: path({{11,5},{22,16},{11,27}},white); break;
  }
}
}
lv_obj_t* guardianIcon(lv_obj_t* parent,GuardianIcon kind,int size,uint32_t accent) {
  auto* obj=lv_obj_create(parent); lv_obj_remove_style_all(obj); lv_obj_set_size(obj,size,size);
  lv_obj_clear_flag(obj,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_text_color(obj,lv_color_hex(accent),0);
  lv_obj_add_event_cb(obj,draw,LV_EVENT_DRAW_MAIN,reinterpret_cast<void*>(static_cast<uintptr_t>(kind)));
  return obj;
}
} }
