// round-box-clock: analog clock face. See clock_face.h.
//
// Layout, outside in:
//   outer ring  - Roman numerals XII/III/VI/IX (Puhui 30), short slim marks at the other eight hours
//   inner ring  - 48 fine minute ticks + hour dots
//   hands       - classic tall-case ("grandfather") style, drawn as filled polygons:
//                 hour   = ringed tail, wedge with thorns, Breguet ring, spade, needle tip
//                 minute = ringed tail, wedge, double loop, thorns, lozenge, long needle
//                 second = grey needle, diamond near the tip, hollow counterweight
// Palettes: day = white face, black hands; night = black face, light grey hands (less glare).
// Ticks and hands are custom-drawn on one transparent object (DrawCb) that sits
// above the numeral labels, so long hands pass over the numerals.
#include "clock_face.h"

#include <cmath>
#include <cstdlib>
#include <initializer_list>

LV_FONT_DECLARE(font_puhui_basic_30_4);  // from the 78/xiaozhi-fonts component

namespace {

const lv_font_t* const kNumeralFont = &font_puhui_basic_30_4;

// Day runs [kDayStartHour, kNightStartHour). Fixed for now; sunrise/sunset is roadmap step 4.
constexpr int kDayStartHour = 7;
constexpr int kNightStartHour = 19;

const ClockFace::Palette kDay = {
    .face = lv_color_hex(0xFAFAFA),
    .numeral = lv_color_hex(0x111111),
    .hour_mark = lv_color_hex(0x111111),
    .minute_tick = lv_color_hex(0x9A9A9A),
    .hand = lv_color_hex(0x111111),
    .second_hand = lv_color_hex(0x8A8A8A),
};
const ClockFace::Palette kNight = {
    .face = lv_color_hex(0x000000),
    .numeral = lv_color_hex(0xB4B4B4),
    .hour_mark = lv_color_hex(0xB4B4B4),
    .minute_tick = lv_color_hex(0x3C3C3C),
    .hand = lv_color_hex(0xB4B4B4),
    .second_hand = lv_color_hex(0x707070),
};

constexpr float kPi = 3.14159265f;

// Hand-local frame: t runs from the pivot outward along the hand, w is the
// perpendicular offset (positive to the right of the hand's direction).
struct Frame {
    float cx, cy;  // pivot in screen coords
    float ux, uy;  // unit vector along the hand
    float nx, ny;  // unit perpendicular

    Frame(float cx_, float cy_, float angle_deg) : cx(cx_), cy(cy_) {
        const float a = angle_deg * kPi / 180.0f;  // 0 = 12 o'clock, clockwise
        ux = std::sin(a);
        uy = -std::cos(a);
        nx = std::cos(a);
        ny = std::sin(a);
    }
    lv_point_precise_t P(float t, float w) const {
        lv_point_precise_t p;
        p.x = static_cast<lv_value_precise_t>(std::lround(cx + t * ux + w * nx));
        p.y = static_cast<lv_value_precise_t>(std::lround(cy + t * uy + w * ny));
        return p;
    }
};

void Line(lv_layer_t* layer, lv_color_t c, int width, lv_point_precise_t a, lv_point_precise_t b) {
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = c;
    dsc.opa = LV_OPA_COVER;
    dsc.width = width;
    dsc.p1 = a;
    dsc.p2 = b;
    dsc.round_start = 0;
    dsc.round_end = 0;
    lv_draw_line(layer, &dsc);
}

// Filled triangle. Once rotated corners are rounded to whole pixels, thin slivers
// (needle tips) can become collinear or near-collinear; LVGL's software triangle
// rasterizer can then fill a large wrong triangle (the occasional "odd triangle"
// seen on the minute/second hands). Draw those as a 1 px line along the longest edge.
void Tri(lv_layer_t* layer, lv_color_t c, lv_point_precise_t a, lv_point_precise_t b, lv_point_precise_t d) {
    const int32_t area2 = std::abs(static_cast<int32_t>((b.x - a.x) * (d.y - a.y) - (b.y - a.y) * (d.x - a.x)));
    if (area2 < 4) {
        auto len2 = [](lv_point_precise_t p, lv_point_precise_t q) {
            const int32_t dx = static_cast<int32_t>(p.x - q.x), dy = static_cast<int32_t>(p.y - q.y);
            return dx * dx + dy * dy;
        };
        const int32_t ab = len2(a, b), ad = len2(a, d), bd = len2(b, d);
        if (ab >= ad && ab >= bd) {
            Line(layer, c, 1, a, b);
        } else if (ad >= bd) {
            Line(layer, c, 1, a, d);
        } else {
            Line(layer, c, 1, b, d);
        }
        return;
    }
    lv_draw_triangle_dsc_t dsc;
    lv_draw_triangle_dsc_init(&dsc);
    dsc.color = c;
    dsc.opa = LV_OPA_COVER;
    dsc.p[0] = a;
    dsc.p[1] = b;
    dsc.p[2] = d;
    lv_draw_triangle(layer, &dsc);
}

// Quad from (t0, +-w0) to (t1, +-w1): a straight taper along the hand.
void Taper(lv_layer_t* layer, const Frame& f, lv_color_t c, float t0, float w0, float t1, float w1) {
    Tri(layer, c, f.P(t0, -w0), f.P(t0, w0), f.P(t1, w1));
    Tri(layer, c, f.P(t0, -w0), f.P(t1, w1), f.P(t1, -w1));
}

// Diamond: point at t0, widest (+-w) at tm, point at t1.
void Lozenge(lv_layer_t* layer, const Frame& f, lv_color_t c, float t0, float tm, float w, float t1) {
    Tri(layer, c, f.P(t0, 0), f.P(tm, w), f.P(t1, 0));
    Tri(layer, c, f.P(t0, 0), f.P(t1, 0), f.P(tm, -w));
}

// A pair of thorns swept toward the tip, rooted on the shaft edge at t = a.
void Thorns(lv_layer_t* layer, const Frame& f, lv_color_t c, float a, float root_w, float len, float span) {
    for (float s : {-1.0f, 1.0f}) {
        Tri(layer, c, f.P(a, s * root_w), f.P(a + len * 0.55f, s * root_w), f.P(a + len, s * span));
    }
}

void Ring(lv_layer_t* layer, lv_color_t c, lv_point_precise_t centre, int r, int width) {
    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.color = c;
    dsc.opa = LV_OPA_COVER;
    dsc.width = width;
    dsc.start_angle = 0;
    dsc.end_angle = 360;
    dsc.center.x = static_cast<int32_t>(centre.x);
    dsc.center.y = static_cast<int32_t>(centre.y);
    dsc.radius = static_cast<uint16_t>(r);
    lv_draw_arc(layer, &dsc);
}

void Disc(lv_layer_t* layer, lv_color_t c, lv_point_precise_t centre, int r) {
    Ring(layer, c, centre, r, r);  // an arc as wide as its radius is a filled disc
}

lv_obj_t* MakeNumeral(lv_obj_t* parent, const char* text, int dx, int dy) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, kNumeralFont, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, dx, dy);
    return label;
}

}  // namespace

ClockFace::ClockFace(lv_obj_t* parent, int size) {
    radius_ = size / 2;
    glyph_h_ = lv_font_get_line_height(kNumeralFont);
    // Glyphs occupy roughly the middle 70% of the font's line height, so centre
    // them closer to the edge than half a line height would suggest.
    numeral_r_ = radius_ - glyph_h_ * 4 / 10 - 3;

    // Opaque disc covering the whole round panel (and XiaoZhi's UI beneath it).
    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, size, size);
    lv_obj_center(root_);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_CLICKABLE);  // let touches reach the board's handler

    // Roman numerals; III and IX sit a little further in so their width clears the rim.
    numerals_[0] = MakeNumeral(root_, "XII", 0, -numeral_r_);
    numerals_[1] = MakeNumeral(root_, "III", numeral_r_ - 4, 0);
    numerals_[2] = MakeNumeral(root_, "VI", 0, numeral_r_);
    numerals_[3] = MakeNumeral(root_, "IX", -(numeral_r_ - 4), 0);

    // Transparent full-size object that draws ticks and hands on top of the numerals.
    canvas_ = lv_obj_create(root_);
    lv_obj_remove_style_all(canvas_);
    lv_obj_set_size(canvas_, size, size);
    lv_obj_center(canvas_);
    lv_obj_remove_flag(canvas_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(canvas_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(canvas_, DrawCb, LV_EVENT_DRAW_MAIN, this);

    palette_ = &kNight;
    ApplyPalette();
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void ClockFace::ApplyPalette() {
    lv_obj_set_style_bg_color(root_, palette_->face, 0);
    for (lv_obj_t* label : numerals_) {
        lv_obj_set_style_text_color(label, palette_->numeral, 0);
    }
    lv_obj_invalidate(root_);
}

void ClockFace::DrawCb(lv_event_t* e) {
    auto* self = static_cast<ClockFace*>(lv_event_get_user_data(e));
    lv_layer_t* layer = lv_event_get_layer(e);
    lv_area_t area;
    lv_obj_get_coords(self->canvas_, &area);
    self->Draw(layer, area);
}

void ClockFace::Draw(lv_layer_t* layer, const lv_area_t& area) const {
    const Palette& p = *palette_;
    const float cx = (area.x1 + area.x2 + 1) / 2.0f;
    const float cy = (area.y1 + area.y2 + 1) / 2.0f;
    const float R = static_cast<float>(radius_);

    // Hour marks at the outer edge (skip 12/3/6/9): short and slim so the numerals dominate.
    const float mark_outer = R - 3;
    const float mark_inner = R - 12;
    for (int h = 0; h < 12; ++h) {
        if (h % 3 == 0) continue;
        Frame f(cx, cy, h * 30.0f);
        Taper(layer, f, p.hour_mark, mark_inner, 1.2f, mark_outer, 1.0f);
    }

    // Minute ticks on the inner ring, just inside the numerals.
    const float tick_outer = numeral_r_ - glyph_h_ * 0.4f - 3;
    const float tick_inner = tick_outer - 5;
    for (int m = 0; m < 60; ++m) {
        if (m % 5 == 0) continue;  // hours are marked on the outer ring
        Frame f(cx, cy, m * 6.0f);
        Line(layer, p.minute_tick, 1, f.P(tick_inner, 0), f.P(tick_outer, 0));
    }
    // Small dots at the hour positions on the inner ring keep its rhythm.
    for (int h = 0; h < 12; ++h) {
        Frame f(cx, cy, h * 30.0f);
        Disc(layer, p.minute_tick, f.P(tick_outer - 2.5f, 0), 2);
    }

    if (second_ < 0) {
        return;  // no time yet
    }

    // Hour hand: ringed tail, wedge with thorns, Breguet ring, spade, needle tip.
    {
        const float a = ((hour_ % 12) * 60 + minute_ + second_ / 60.0f) * 0.5f;  // 720 min = 360 deg
        const float L = R * 0.62f;
        const int ring_r = 6;
        const float ring_t = L * 0.50f;
        const float spade_t0 = ring_t + ring_r + 2;
        Frame f(cx, cy, a);
        Ring(layer, p.hand, f.P(-11, 0), 4, 2);                                  // tail ring
        Taper(layer, f, p.hand, -8, 2.0f, 0, 4.2f);                              // tail neck
        Taper(layer, f, p.hand, 0, 4.2f, ring_t - ring_r + 1, 2.0f);             // wedge
        Thorns(layer, f, p.hand, ring_t * 0.42f, 2.6f, 9, 7.5f);
        Ring(layer, p.hand, f.P(ring_t, 0), ring_r, 3);                          // Breguet ring
        Taper(layer, f, p.hand, ring_t + ring_r - 1, 1.6f, spade_t0 + 1, 1.4f);  // neck
        Lozenge(layer, f, p.hand, spade_t0, spade_t0 + 5, 6.0f, spade_t0 + 12);  // spade
        Taper(layer, f, p.hand, spade_t0 + 11, 1.5f, L, 0.3f);                   // needle
    }

    // Minute hand: ringed tail, wedge, double loop, thorns, lozenge, long needle.
    {
        const float a = (minute_ * 60 + second_) * 0.1f;  // 3600 s = 360 deg
        const float L = R * 0.90f;
        const int loop_r = 5;
        const float loop1 = L * 0.34f;
        const float loop2 = loop1 + 2 * loop_r - 1;
        Frame f(cx, cy, a);
        Ring(layer, p.hand, f.P(-15, 0), 4, 2);                                  // tail ring
        Taper(layer, f, p.hand, -12, 2.0f, 0, 3.6f);                             // tail neck
        Taper(layer, f, p.hand, 0, 3.6f, loop1 - loop_r + 1, 2.0f);              // wedge
        Ring(layer, p.hand, f.P(loop1, 0), loop_r, 2);                           // double loop
        Ring(layer, p.hand, f.P(loop2, 0), loop_r, 2);
        Taper(layer, f, p.hand, loop2 + loop_r - 1, 1.8f, L * 0.62f, 1.4f);      // shaft
        Thorns(layer, f, p.hand, L * 0.53f, 1.6f, 8, 5.5f);
        Lozenge(layer, f, p.hand, L * 0.61f, L * 0.665f, 4.5f, L * 0.72f);
        Taper(layer, f, p.hand, L * 0.71f, 1.3f, L, 0.3f);                       // needle
    }

    // Second hand: hollow counterweight, slim needle, small diamond near the tip.
    {
        const float a = second_ * 6.0f;
        const float L = R * 0.93f;
        Frame f(cx, cy, a);
        Line(layer, p.second_hand, 2, f.P(-14, 0), f.P(L * 0.72f, 0));
        Taper(layer, f, p.second_hand, L * 0.72f, 1.0f, L, 0.3f);
        Lozenge(layer, f, p.second_hand, L * 0.66f, L * 0.71f, 3.0f, L * 0.76f);
        Ring(layer, p.second_hand, f.P(-19, 0), 5, 2);
    }

    // Centre cap.
    const lv_point_precise_t c = Frame(cx, cy, 0).P(0, 0);
    Disc(layer, p.hand, c, 7);
    Disc(layer, p.second_hand, c, 4);
}

void ClockFace::SetVisible(bool visible) {
    if (visible == IsVisible()) {
        return;
    }
    if (visible) {
        lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(root_);
    } else {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
}

bool ClockFace::IsVisible() const {
    return !lv_obj_has_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void ClockFace::SetTime(int hour, int minute, int second) {
    if (hour == hour_ && minute == minute_ && second == second_) {
        return;
    }
    const bool night = hour < kDayStartHour || hour >= kNightStartHour;
    if (night != night_ || hour_ < 0) {
        night_ = night;
        palette_ = night ? &kNight : &kDay;
        ApplyPalette();
    }
    hour_ = hour;
    minute_ = minute;
    second_ = second;
    lv_obj_invalidate(canvas_);
}
