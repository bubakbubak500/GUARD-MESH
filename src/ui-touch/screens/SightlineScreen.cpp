// SPDX-License-Identifier: GPL-3.0-or-later
#include "SightlineScreen.h"
#include "../i18n.h"
#include "../theme/Fonts.h"
#include "../theme/Theme.h"
#include "../widgets/ObjectRef.h"
#include "../widgets/Styles.h"
#include <cstdio>
#include <cstring>
#include <new>
namespace ui {
namespace screens {
namespace sightlineScreen {
using namespace theme;
using namespace widgets;
static Host host{};
static Selection selection{};
static uint32_t generation = 0, requestId = 0;
static int shownAttempt = -1;
static constexpr int k_los_samples = sightline::Samples;
static constexpr double k_los_re_eff = sightline::EarthRadius;
static float s_los_ant_self = 2, s_los_ant_peer = 2;
static int s_los_got = 0;
static float s_los_elev[k_los_samples];
struct PlotPoints {
  lv_point_t terrain[k_los_samples], sight[2];
};
static bool chatLandscape() { return lv_disp_get_hor_res(nullptr) > lv_disp_get_ver_res(nullptr); }
static lv_coord_t contentTop() { return host.contentTop ? host.contentTop() : 0; }
static ObjectRef s_los_root, s_los_card, s_los_msg, s_los_plot, s_los_verdict, s_los_self_h_lbl,
    s_los_peer_h_lbl;
static bool owns(lv_event_t *event) {
  for (auto *object = lv_event_get_target(event); s_los_root.get() && object;
       object = lv_obj_get_parent(object))
    if (object == s_los_root.get())
      return true;
  return false;
}
void close() {
  ++generation;
  requestId = 0;
  auto *old = s_los_root.get();
  s_los_root.set(nullptr);
  s_los_card.set(nullptr);
  s_los_msg.set(nullptr);
  s_los_plot.set(nullptr);
  s_los_verdict.set(nullptr);
  s_los_self_h_lbl.set(nullptr);
  s_los_peer_h_lbl.set(nullptr);
  if (old) {
    if (host.closeRoot)
      host.closeRoot(&old);
    else
      lv_obj_del(old);
  }
}
void configure(const Host &value) {
  close();
  host = value;
}
bool isOpen() { return s_los_root.get() != nullptr; }
static void losModalCloseCb(lv_event_t *event) {
  if (!owns(event))
    return;
  lv_indev_t *act = lv_indev_get_act();
  if (act)
    lv_indev_wait_release(act);
  close();
}
static void losDrawPlot() {
  if (!s_los_card.get() || s_los_got < k_los_samples)
    return;
  lv_obj_t *card = s_los_card.get();
  // PSC() scales card geometry up ~1.7× on Tanmatsu (no-op on T-Deck/V4), so
  // the chart fills the 800×480 panel instead of looking lost. Width, graph
  // height and every stacked Y offset below all go through PSC() so the plot
  // stays aligned with its axis labels / sliders / verdict at any board+scale.
  const int card_w = PCW(230);

  sightline::Analysis analysis{};
  if (!sightline::analyze(selection.path, s_los_elev, s_los_ant_self, s_los_ant_peer, selection.frequency,
                          analysis))
    return;
  const float *elev = s_los_elev;
  const double dist_km = analysis.distanceKm, D = dist_km * 1000;
  const double brg = analysis.bearing, freq_mhz = analysis.frequency;
  const double h0 = analysis.h0, hN = analysis.hN;
  const double min_clear = analysis.clearance, worst_d1 = analysis.worstDistance;
  const double worst_f1 = analysis.worstFresnel;
  const int worst_i = analysis.worstIndex;
  const auto verdict = analysis.verdict;
  // ---- Cross-section drawing area (recreated each draw) ----
  // Shorter graph in landscape so the antenna controls + verdict still fit the
  // capped card height. cy in losRenderResult must stay = gy + gh + 6.
  const int gx = 0, gy = PSC(28), gw = card_w - 16, gh = PSC(chatLandscape() ? 58 : 80);
  if (s_los_plot.get()) {
    lv_obj_del(s_los_plot.get());
    s_los_plot.set(nullptr);
  }
  lv_obj_t *graph = lv_obj_create(card);
  s_los_plot.set(graph);
  auto *points = new (std::nothrow) PlotPoints{};
  if (!points) {
    lv_obj_del(graph);
    s_los_plot.set(nullptr);
    return;
  }
  lv_obj_add_event_cb(
      graph,
      [](lv_event_t *event) {
        if (lv_event_get_target(event) == lv_event_get_current_target(event))
          delete static_cast<PlotPoints *>(lv_event_get_user_data(event));
      },
      LV_EVENT_DELETE, points);
  lv_obj_remove_style_all(graph);
  lv_obj_set_size(graph, gw, gh);
  lv_obj_set_pos(graph, gx, gy);
  lv_obj_set_style_bg_color(graph, lv_color_hex(colors().COLOR_CHART_BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(graph, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(graph, 4, LV_PART_MAIN);
  lv_obj_clear_flag(graph, LV_OBJ_FLAG_SCROLLABLE);

  double vmin = 1e9, vmax = -1e9;
  auto bump = [&](double v) {
    if (v < vmin)
      vmin = v;
    if (v > vmax)
      vmax = v;
  };
  for (int i = 0; i < k_los_samples; ++i) {
    const double f = (double)i / (k_los_samples - 1);
    const double d1 = f * D, d2 = (1.0 - f) * D;
    bump((double)elev[i] + d1 * d2 / (2.0 * k_los_re_eff));
  }
  bump(h0);
  bump(hN);
  if (vmax - vmin < 1.0)
    vmax = vmin + 1.0;
  const int pad = 6;
  auto X = [&](int i) { return (lv_coord_t)(i * (gw - 1) / (k_los_samples - 1)); };
  auto Y = [&](double v) {
    const double t = (v - vmin) / (vmax - vmin);
    return (lv_coord_t)(gh - pad - t * (gh - 2 * pad));
  };
  for (int i = 0; i < k_los_samples; ++i) {
    const double f = (double)i / (k_los_samples - 1);
    const double d1 = f * D, d2 = (1.0 - f) * D;
    const double terr = (double)elev[i] + d1 * d2 / (2.0 * k_los_re_eff);
    points->terrain[i].x = X(i);
    points->terrain[i].y = Y(terr);
  }
  lv_obj_t *tline = lv_line_create(graph);
  lv_line_set_points(tline, points->terrain, k_los_samples);
  lv_obj_set_style_line_color(tline, lv_color_hex(0x6FBF73), LV_PART_MAIN);
  lv_obj_set_style_line_width(tline, 2, LV_PART_MAIN);
  lv_obj_set_style_line_rounded(tline, true, LV_PART_MAIN);

  points->sight[0] = {X(0), Y(h0)};
  points->sight[1] = {X(k_los_samples - 1), Y(hN)};
  lv_obj_t *sline = lv_line_create(graph);
  lv_line_set_points(sline, points->sight, 2);
  lv_obj_set_style_line_color(sline,
                              lv_color_hex(verdict == sightline::Blocked    ? 0xD7574E
                                           : verdict == sightline::Marginal ? 0xC8A030
                                                                            : 0x4DA8FF),
                              LV_PART_MAIN);
  lv_obj_set_style_line_width(sline, 2, LV_PART_MAIN);

  if (verdict != sightline::Clear && worst_i >= 0) {
    lv_obj_t *dot = lv_obj_create(graph);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 6, 6);
    lv_obj_set_style_radius(dot, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(dot, lv_color_hex(0xD7574E), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
    const double f = (double)worst_i / (k_los_samples - 1);
    const double d1 = f * D, d2 = (1.0 - f) * D;
    lv_obj_set_pos(dot, X(worst_i) - 3, Y((double)elev[worst_i] + d1 * d2 / (2.0 * k_los_re_eff)) - 3);
  }

  // ---- Height value labels ----
  if (s_los_self_h_lbl.get())
    lv_label_set_text_fmt(s_los_self_h_lbl.get(), "%dm", (int)s_los_ant_self);
  if (s_los_peer_h_lbl.get())
    lv_label_set_text_fmt(s_los_peer_h_lbl.get(), "%dm", (int)s_los_ant_peer);

  // ---- Verdict text ----
  if (s_los_verdict.get()) {
    const bool miles = selection.miles;
    char dist_s[16];
    if (miles)
      snprintf(dist_s, sizeof(dist_s), "%.1f mi", dist_km * 0.621371);
    else
      snprintf(dist_s, sizeof(dist_s), "%.1f km", dist_km);

    const char *vstr = (verdict == sightline::Blocked)    ? "NO LINE OF SIGHT"
                       : (verdict == sightline::Marginal) ? "MARGINAL (Fresnel)"
                                                          : "LINE OF SIGHT";
    const uint32_t vcol = (verdict == sightline::Blocked)    ? 0xD7574E
                          : (verdict == sightline::Marginal) ? 0xC8A030
                                                             : 0x6FBF73;

    char detail[96];
    if (verdict == sightline::Blocked)
      snprintf(detail, sizeof detail, "Blocked @ %.1f km, %.0fm over sight", worst_d1 / 1000, -min_clear);
    else if (verdict == sightline::Marginal)
      snprintf(detail, sizeof detail, "Grazes terrain \xc2\xb7 F1=%.0fm", worst_f1);
    else
      snprintf(detail, sizeof detail, "Clear by %.0fm at tightest", min_clear);
    char body[384];
    snprintf(body, sizeof body,
             "#a0a6ad %s  \xc2\xb7  brg %03d\xc2\xb0 %s#\n#a0a6ad %s#\n"
             "#%06x %s#\n#5b6168 you %dm \xc2\xb7 peer %dm \xc2\xb7 %.0f MHz#",
             dist_s, (int)(brg + 0.5), sightline::compass(brg), detail, (unsigned)vcol, vstr,
             (int)s_los_ant_self, (int)s_los_ant_peer, freq_mhz);
    normalizeLightSurfaceRecolor(body);
    lv_label_set_text(s_los_verdict.get(), body);
  }
}

// Antenna +/- handlers — adjust by 1 m (clamped 0..150) and redraw. No
// network: reuses the cached elevation profile.
static void losSelfMinusCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !owns(e))
    return;
  if (s_los_ant_self >= 1.0f)
    s_los_ant_self -= 1.0f;
  losDrawPlot();
}
static void losSelfPlusCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !owns(e))
    return;
  if (s_los_ant_self < 150.0f)
    s_los_ant_self += 1.0f;
  losDrawPlot();
}
static void losPeerMinusCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !owns(e))
    return;
  if (s_los_ant_peer >= 1.0f)
    s_los_ant_peer -= 1.0f;
  losDrawPlot();
}
static void losPeerPlusCb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_CLICKED || !owns(e))
    return;
  if (s_los_ant_peer < 150.0f)
    s_los_ant_peer += 1.0f;
  losDrawPlot();
}

// Runs on the UI thread once the worker has the elevation profile. Builds
// the persistent controls (height +/- and verdict label), then draws.
static void losRenderResult() {
  if (!s_los_card.get())
    return;
  lv_obj_t *card = s_los_card.get();
  const int card_w = PCW(230); // matches losDrawPlot()/builder — see PSC() note there
  const int gw = card_w - 16;

  if (s_los_got < k_los_samples) {
    if (s_los_msg.get())
      lv_label_set_text(s_los_msg.get(), TR("Couldn't fetch terrain data.\nCheck the connection and the\n"
                                            "elevation server, then\ntry again."));
    return;
  }
  if (s_los_msg.get())
    lv_obj_add_flag(s_los_msg.get(), LV_OBJ_FLAG_HIDDEN);

  // Antenna height controls, directly under each end of the graph:
  //   left = your antenna, right = the contact's. [-] value [+]
  // Just below the graph: gy + gh + 6, derived from the SAME PSC()-scaled pieces
  // losDrawPlot() uses for the graph (gy=PSC(28), gh=PSC(58|80)) so the controls
  // stay glued under the graph at any board/UI-scale instead of drifting. Button
  // + value sizes/X-offsets also go through PSC() so the clusters don't bunch up.
  const int cy = PSC(28) + PSC(chatLandscape() ? 58 : 80) + PSC(6);
  auto mk_h_btn = [&](const char *sym, int x, lv_event_cb_t cb) {
    lv_obj_t *b = lv_btn_create(card);
    lv_obj_set_size(b, PSC(26), PSC(24));
    lv_obj_set_pos(b, x, cy);
    styleButton(b);
    lv_obj_set_style_pad_all(b, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, sym);
    lv_obj_set_style_text_font(l, &font16(), LV_PART_MAIN);
    lv_obj_center(l);
  };
  auto mk_h_val = [&](int x) {
    lv_obj_t *l = lv_label_create(card);
    lv_obj_set_size(l, PSC(42), PSC(24));
    lv_obj_set_pos(l, x, cy + PSC(4));
    lv_obj_set_style_text_font(l, &font12(), LV_PART_MAIN);
    lv_obj_set_style_text_color(l, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(l, "2m");
    return l;
  };
  // Left cluster (you)
  mk_h_btn("-", 0, losSelfMinusCb);
  s_los_self_h_lbl.set(mk_h_val(PSC(28)));
  mk_h_btn("+", PSC(72), losSelfPlusCb);
  // Right cluster (peer)
  mk_h_btn("-", gw - PSC(98), losPeerMinusCb);
  s_los_peer_h_lbl.set(mk_h_val(gw - PSC(70)));
  mk_h_btn("+", gw - PSC(26), losPeerPlusCb);

  // Verdict label (text updated by losDrawPlot).
  s_los_verdict.set(lv_label_create(card));
  lv_label_set_long_mode(s_los_verdict.get(), LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_los_verdict.get(), gw);
  lv_obj_set_style_text_font(s_los_verdict.get(), &font12(), LV_PART_MAIN);
  lv_label_set_recolor(s_los_verdict.get(), true);
  lv_obj_set_pos(s_los_verdict.get(), 0, cy + PSC(30));

  losDrawPlot();
}

void poll() {
  if (!host.job)
    return;
  SightlineJob::Result result{};
  if (host.job->take(result)) {
    if (host.diagnostic) {
      char text[96];
      snprintf(text, sizeof text, "LOS %.4f,%.4f code=%d got=%d ok=%d", result.request.path.peerLat,
               result.request.path.peerLon, result.code, result.parsed, result.valid);
      host.diagnostic(text);
    }
    if (s_los_root.get() && requestId && result.request.id == requestId) {
      requestId = 0;
      s_los_got = result.ok ? k_los_samples : 0;
      std::memcpy(s_los_elev, result.elevations, sizeof s_los_elev);
      losRenderResult();
    }
  } else if (requestId && s_los_msg.get()) {
    const int attempt = host.job->attempt();
    if (shownAttempt != attempt) {
      shownAttempt = attempt;
      if (attempt >= 2)
        lv_label_set_text_fmt(s_los_msg.get(), TR("Analyzing terrain\xe2\x80\xa6\n(retry %d of 3)"), attempt);
      else
        lv_label_set_text(s_los_msg.get(), TR("Analyzing terrain\xe2\x80\xa6"));
    }
  }
}
void open(const Selection &value) {
  close();
  if (isOpen())
    return; // host close callback opened another tree
  poll();   // consume any completed request belonging to the retired tree
  selection = value;
  selection.name[sizeof selection.name - 1] = 0;
  selection.server[sizeof selection.server - 1] = 0;
  s_los_got = 0;
  shownAttempt = -1;
  // ---- Modal shell ----
  const lv_coord_t sw = lv_disp_get_hor_res(nullptr);
  const lv_coord_t sh = lv_disp_get_ver_res(nullptr);
  s_los_root.set(lv_obj_create(lv_layer_top()));
  lv_obj_remove_style_all(s_los_root.get());
  lv_obj_set_size(s_los_root.get(), sw, sh - contentTop());
  lv_obj_set_pos(s_los_root.get(), 0, contentTop());
  lv_obj_set_style_bg_color(s_los_root.get(), lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_los_root.get(), LV_OPA_70, LV_PART_MAIN);
  lv_obj_clear_flag(s_los_root.get(), LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_move_foreground(s_los_root.get());
  lv_obj_add_event_cb(s_los_root.get(), losModalCloseCb, LV_EVENT_CLICKED, nullptr);

  // Cap the card to the usable height so it fits the shorter landscape screen
  // (the graph + controls shrink to match — see chatLandscape() in losDrawPlot
  // / losRenderResult).
  const int card_w = PCW(230); // matches losDrawPlot()/losRenderResult() — see PSC() note in losDrawPlot()
  int card_h = PSC(248);
  if (card_h > (lv_disp_get_ver_res(nullptr) - contentTop() - 12))
    card_h = (lv_disp_get_ver_res(nullptr) - contentTop() - 12);
  lv_obj_t *card = lv_obj_create(s_los_root.get());
  s_los_card.set(card);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, card_w, card_h);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(colors().COLOR_PANEL), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
  lv_obj_set_style_border_color(card, lv_color_hex(colors().COLOR_BORDER), LV_PART_MAIN);
  lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
  lv_obj_set_style_pad_all(card, 8, LV_PART_MAIN);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  addCloseXBadge(card, losModalCloseCb);

  const char *nm = selection.name;
  lv_obj_t *title = lv_label_create(card);
  lv_label_set_text_fmt(title, TR(LV_SYMBOL_GPS "  LOS \xe2\x86\x92 %s"), nm[0] ? nm : "?");
  lv_obj_set_style_text_color(title, lv_color_hex(colors().COLOR_TEXT), LV_PART_MAIN);
  lv_obj_set_style_text_font(title, &font14(), LV_PART_MAIN);
  lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(title, card_w - 16 - 28);
  lv_obj_set_pos(title, 0, 0);

  lv_obj_t *msg = lv_label_create(card);
  s_los_msg.set(msg);
  lv_label_set_long_mode(msg, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(msg, card_w - 16);
  lv_obj_set_style_text_font(msg, &font12(), LV_PART_MAIN);
  lv_obj_set_style_text_color(msg, lv_color_hex(colors().COLOR_SUB), LV_PART_MAIN);
  lv_obj_set_pos(msg, 0, 24);

  // Early outs that need no network.
  if (selection.path.selfLat == 0.0 && selection.path.selfLon == 0.0) {
    lv_label_set_text(
        msg, TR("Your location is unknown.\nSet GPS / position in\nSettings \xe2\x86\x92 Profile first."));
    return;
  }
  if (selection.path.peerLat == 0.0 && selection.path.peerLon == 0.0) {
    lv_label_set_text(msg, TR("This contact hasn't shared\na GPS position."));
    return;
  }
  if (!selection.networkReady) {
    lv_label_set_text(msg, TR("Wi-Fi needed to fetch the\nterrain profile for this path.\nConnect in "
                              "Settings \xe2\x86\x92 Wi-Fi."));
    return;
  }

  if (host.job && host.job->active()) {
    lv_label_set_text(msg, TR("Still analyzing the previous\npath\xe2\x80\xa6 try again in a moment."));
    return;
  }
  if (!host.job || !host.ensureExecutor || !host.ensureExecutor()) {
    lv_label_set_text(msg, TR("Couldn't start the analyzer.\nTry again."));
    return;
  }
  SightlineJob::Request request{};
  if (++generation == 0)
    ++generation;
  request.id = generation;
  request.path = selection.path;
  std::memcpy(request.server, selection.server, sizeof request.server);
  if (!host.job->request(request)) {
    lv_label_set_text(msg, TR("Couldn't start the analyzer.\nTry again."));
    return;
  }
  requestId = request.id;
  lv_label_set_text(msg, TR("Analyzing terrain\xe2\x80\xa6"));
}
} // namespace sightlineScreen
} // namespace screens
} // namespace ui
