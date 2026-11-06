// round-box-clock: analog clock face. See clock_face.h.
//
// Layout, outside in:
//   outer ring  - Roman numerals XII/III/VI/IX (Cinzel), short slim marks at the other eight hours
//   inner ring  - 48 fine minute ticks + hour dots
//   hands       - classic tall-case ("grandfather") style:
//                 hour   = ringed tail, wedge with thorns, Breguet ring, spade, needle tip
//                 minute = ringed tail, wedge, double loop, thorns, lozenge, long needle
//                 second = needle (same ink as the hands), diamond near the tip, hollow counterweight
// Palettes: day = parchment face, near-black hands; night = inverse (black face, parchment hands).
//
// Rendering: ticks and hands are rasterized here in software, not with LVGL's
// triangle/arc drawing. LVGL 9.3's SW triangle fill produced visible garbage
// on the hands (solid blocks, saw-tooth lozenges; photographed 2026-11-05).
// Each primitive sets bits in a 4x4-supersample coverage mask; all primitives
// of one hand are unioned in that mask and composited once in the hand colour,
// so shared edges inside a hand leave no seams. Coordinates are floats
// throughout (no pixel rounding). The result goes to a transparent ARGB8888
// canvas above the numeral labels.
#include "clock_face.h"
#include "clock_settings.h"

#include <esp_heap_caps.h>
#include <esp_log.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>

LV_FONT_DECLARE(font_cinzel_bold_22);  // Cinzel Bold (SIL OFL 1.1), glyphs I V X only; see font_cinzel_OFL.txt

#define TAG "ClockFace"

namespace {

const lv_font_t* const kNumeralFont = &font_cinzel_bold_22;

// Colours and day/night hours come from ClockSettings (web settings page). Defaults: face
// CBB07E parchment (this panel has a cool cast, so it needs a strong warm tone; ~8.2:1 vs ink),
// ink 1C1A18. Night is the inverse of day: the day face colour becomes the night ink.

lv_color_t Mix(uint32_t a, uint32_t b, float t) {  // t = 0 -> a, 1 -> b
    auto ch = [&](int shift) {
        const float x = ((a >> shift) & 0xFF) * (1 - t) + ((b >> shift) & 0xFF) * t;
        return static_cast<uint32_t>(x + 0.5f) & 0xFF;
    };
    return lv_color_hex((ch(16) << 16) | (ch(8) << 8) | ch(0));
}

void BuildPalettes(uint32_t face, uint32_t ink, ClockFace::Palette& day, ClockFace::Palette& night) {
    day.face = lv_color_hex(face);
    day.numeral = Mix(ink, face, 0.08f);
    day.hour_mark = day.numeral;
    day.minute_tick = Mix(face, ink, 0.45f);
    day.hand = lv_color_hex(ink);
    day.second_hand = lv_color_hex(ink);

    night.face = lv_color_hex(0x000000);
    night.numeral = lv_color_hex(face);
    night.hour_mark = night.numeral;
    night.minute_tick = Mix(0x000000, face, 0.30f);
    night.hand = lv_color_hex(face);
    night.second_hand = lv_color_hex(face);
}

constexpr float kPi = 3.14159265f;

struct Pt {
    float x, y;
};

// Hand-local frame: t runs from the pivot outward along the hand, w is the
// perpendicular offset (positive to the right of the hand's direction).
struct Frame {
    float cx, cy, ux, uy, nx, ny;
    Frame(float cx_, float cy_, float angle_deg) : cx(cx_), cy(cy_) {
        const float a = angle_deg * kPi / 180.0f;  // 0 = 12 o'clock, clockwise
        ux = std::sin(a);
        uy = -std::cos(a);
        nx = std::cos(a);
        ny = std::sin(a);
    }
    Pt P(float t, float w) const { return {cx + t * ux + w * nx, cy + t * uy + w * ny}; }
};

// Coverage-mask rasterizer over a size x size area. Pixel (x, y) covers
// [x, x+1) x [y, y+1); samples sit on a 4x4 grid inside it.
class Mask {
public:
    Mask(uint16_t* bits, int size) : bits_(bits), size_(size) { Reset(); }

    void Reset() { x0_ = y0_ = size_; x1_ = y1_ = -1; }

    void Triangle(Pt a, Pt b, Pt c) {
        float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        if (std::fabs(area) < 1e-4f) return;
        if (area < 0) std::swap(b, c);  // make it counter-clockwise in screen space
        const int x0 = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
        const int y0 = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
        const int x1 = std::min(size_ - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
        const int y1 = std::min(size_ - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
        auto edge = [](Pt p, Pt q, float sx, float sy) { return (q.x - p.x) * (sy - p.y) - (q.y - p.y) * (sx - p.x); };
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                uint16_t m = 0;
                for (int j = 0; j < 4; ++j) {
                    const float sy = y + (j + 0.5f) * 0.25f;
                    for (int i = 0; i < 4; ++i) {
                        const float sx = x + (i + 0.5f) * 0.25f;
                        if (edge(a, b, sx, sy) >= 0 && edge(b, c, sx, sy) >= 0 && edge(c, a, sx, sy) >= 0) {
                            m |= 1u << (j * 4 + i);
                        }
                    }
                }
                if (m) bits_[y * size_ + x] |= m;
            }
        }
        Grow(x0, y0, x1, y1);
    }

    // Filled annulus: r_inner <= distance <= r_outer (r_inner 0 = disc).
    void Annulus(Pt c, float r_outer, float r_inner) {
        const int x0 = std::max(0, static_cast<int>(std::floor(c.x - r_outer)));
        const int y0 = std::max(0, static_cast<int>(std::floor(c.y - r_outer)));
        const int x1 = std::min(size_ - 1, static_cast<int>(std::ceil(c.x + r_outer)));
        const int y1 = std::min(size_ - 1, static_cast<int>(std::ceil(c.y + r_outer)));
        const float ro2 = r_outer * r_outer, ri2 = r_inner * r_inner;
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                uint16_t m = 0;
                for (int j = 0; j < 4; ++j) {
                    const float dy = y + (j + 0.5f) * 0.25f - c.y;
                    for (int i = 0; i < 4; ++i) {
                        const float dx = x + (i + 0.5f) * 0.25f - c.x;
                        const float d2 = dx * dx + dy * dy;
                        if (d2 <= ro2 && d2 >= ri2) m |= 1u << (j * 4 + i);
                    }
                }
                if (m) bits_[y * size_ + x] |= m;
            }
        }
        Grow(x0, y0, x1, y1);
    }

    // Composite the accumulated coverage in `c` over the ARGB8888 pixels, then clear.
    void Flush(uint32_t* px, lv_color_t c) {
        if (x1_ < 0) return;
        for (int y = y0_; y <= y1_; ++y) {
            for (int x = x0_; x <= x1_; ++x) {
                uint16_t& m = bits_[y * size_ + x];
                if (!m) continue;
                const uint32_t sa = __builtin_popcount(m) * 255u / 16u;
                m = 0;
                uint32_t& d = px[y * size_ + x];
                const uint32_t da = d >> 24;
                const uint32_t oa = sa + da * (255u - sa) / 255u;
                if (oa == 0) continue;
                auto mix = [&](uint32_t sc, uint32_t dc) { return (sc * sa + dc * da * (255u - sa) / 255u) / oa; };
                const uint32_t r = mix(c.red, (d >> 16) & 0xFF);
                const uint32_t g = mix(c.green, (d >> 8) & 0xFF);
                const uint32_t b = mix(c.blue, d & 0xFF);
                d = (oa << 24) | (r << 16) | (g << 8) | b;  // LVGL ARGB8888 in memory: B, G, R, A
            }
        }
        Reset();
    }

private:
    void Grow(int x0, int y0, int x1, int y1) {
        x0_ = std::min(x0_, x0); y0_ = std::min(y0_, y0);
        x1_ = std::max(x1_, x1); y1_ = std::max(y1_, y1);
    }
    uint16_t* bits_;
    int size_;
    int x0_, y0_, x1_, y1_;
};

// --- shape helpers in a hand frame ---
void Taper(Mask& m, const Frame& f, float t0, float w0, float t1, float w1) {
    m.Triangle(f.P(t0, -w0), f.P(t0, w0), f.P(t1, w1));
    m.Triangle(f.P(t0, -w0), f.P(t1, w1), f.P(t1, -w1));
}
void Lozenge(Mask& m, const Frame& f, float t0, float tm, float w, float t1) {
    m.Triangle(f.P(t0, 0), f.P(tm, w), f.P(t1, 0));
    m.Triangle(f.P(t0, 0), f.P(t1, 0), f.P(tm, -w));
}
void Thorns(Mask& m, const Frame& f, float a, float root_w, float len, float span) {
    for (float s : {-1.0f, 1.0f}) {
        m.Triangle(f.P(a, s * root_w), f.P(a + len * 0.55f, s * root_w), f.P(a + len, s * span));
    }
}
void Ring(Mask& m, Pt c, float r, float width) { m.Annulus(c, r, std::max(0.0f, r - width)); }
void Disc(Mask& m, Pt c, float r) { m.Annulus(c, r, 0); }

lv_obj_t* MakeNumeral(lv_obj_t* parent, const char* text, int dx, int dy) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, kNumeralFont, 0);
    lv_obj_set_style_text_letter_space(label, 1, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, dx, dy);
    return label;
}

}  // namespace

ClockFace::ClockFace(lv_obj_t* parent, int size) : size_(size) {
    radius_ = size / 2;
    glyph_h_ = lv_font_get_line_height(kNumeralFont);  // caps-only font: ~cap height
    numeral_r_ = radius_ - glyph_h_ / 2 - 5;

    pixels_ = static_cast<uint32_t*>(heap_caps_malloc(size * size * 4, MALLOC_CAP_SPIRAM));
    mask_ = static_cast<uint16_t*>(heap_caps_calloc(size * size, 2, MALLOC_CAP_SPIRAM));
    if (!pixels_ || !mask_) {
        ESP_LOGE(TAG, "PSRAM allocation failed; clock face disabled");
    }

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
    numerals_[1] = MakeNumeral(root_, "III", numeral_r_ - 5, 0);
    numerals_[2] = MakeNumeral(root_, "VI", 0, numeral_r_);
    numerals_[3] = MakeNumeral(root_, "IX", -(numeral_r_ - 5), 0);

    if (pixels_) {
        std::memset(pixels_, 0, size * size * 4);
        canvas_ = lv_canvas_create(root_);
        lv_canvas_set_buffer(canvas_, pixels_, size, size, LV_COLOR_FORMAT_ARGB8888);
        lv_obj_center(canvas_);
        lv_obj_remove_flag(canvas_, LV_OBJ_FLAG_CLICKABLE);
    }

    SyncSettings();
    palette_ = &night_palette_;
    ApplyPalette();
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

ClockFace::~ClockFace() {
    if (root_) lv_obj_delete(root_);
    heap_caps_free(pixels_);
    heap_caps_free(mask_);
}

void ClockFace::ApplyPalette() {
    lv_obj_set_style_bg_color(root_, palette_->face, 0);
    for (lv_obj_t* label : numerals_) {
        lv_obj_set_style_text_color(label, palette_->numeral, 0);
    }
    lv_obj_invalidate(root_);
}

void ClockFace::Render() {
    if (!pixels_ || !mask_) return;
    const Palette& p = *palette_;
    const float c0 = size_ / 2.0f;
    const float R = static_cast<float>(radius_);
    std::memset(pixels_, 0, size_ * size_ * 4);
    Mask m(mask_, size_);

    // Hour marks at the outer edge (skip 12/3/6/9): short and slim so the numerals dominate.
    for (int h = 0; h < 12; ++h) {
        if (h % 3 == 0) continue;
        Taper(m, Frame(c0, c0, h * 30.0f), R - 12, 1.2f, R - 3, 1.0f);
    }
    m.Flush(pixels_, p.hour_mark);

    // Minute ticks on the inner ring, just inside the numerals, with hour dots.
    const float tick_outer = numeral_r_ - glyph_h_ / 2.0f - 12;  // comfortable gap below the numerals
    for (int i = 0; i < 60; ++i) {
        Frame f(c0, c0, i * 6.0f);
        if (i % 5 == 0) {
            Disc(m, f.P(tick_outer - 2.5f, 0), 2.0f);
        } else {
            Taper(m, f, tick_outer - 5, 0.5f, tick_outer, 0.5f);
        }
    }
    m.Flush(pixels_, p.minute_tick);

    if (second_ < 0) {
        lv_obj_invalidate(canvas_);
        return;  // no time yet
    }

    // Hour hand: ringed tail, wedge with thorns, Breguet ring, spade, needle tip.
    {
        const float a = ((hour_ % 12) * 60 + minute_ + second_ / 60.0f) * 0.5f;  // 720 min = 360 deg
        const float L = R * 0.62f;
        const float ring_r = 6;
        const float ring_t = L * 0.50f;
        const float spade_t0 = ring_t + ring_r + 2;
        Frame f(c0, c0, a);
        Ring(m, f.P(-11, 0), 4, 2);                                   // tail ring
        Taper(m, f, -8, 2.0f, 0, 4.2f);                               // tail neck
        Taper(m, f, 0, 4.2f, ring_t - ring_r + 1, 2.0f);              // wedge
        Thorns(m, f, ring_t * 0.42f, 2.6f, 9, 7.5f);
        Ring(m, f.P(ring_t, 0), ring_r, 3);                           // Breguet ring
        Taper(m, f, ring_t + ring_r - 1, 1.6f, spade_t0 + 1, 1.4f);   // neck
        Lozenge(m, f, spade_t0, spade_t0 + 5, 6.0f, spade_t0 + 12);   // spade
        Taper(m, f, spade_t0 + 11, 1.5f, L, 0.3f);                    // needle
        m.Flush(pixels_, p.hand);
    }

    // Minute hand: ringed tail, wedge, double loop, thorns, lozenge, long needle.
    {
        const float a = (minute_ * 60 + second_) * 0.1f;  // 3600 s = 360 deg
        const float L = R * 0.90f;
        const float loop_r = 5;
        const float loop1 = L * 0.34f;
        const float loop2 = loop1 + 2 * loop_r - 1;
        Frame f(c0, c0, a);
        Ring(m, f.P(-15, 0), 4, 2);                                   // tail ring
        Taper(m, f, -12, 2.0f, 0, 3.6f);                              // tail neck
        Taper(m, f, 0, 3.6f, loop1 - loop_r + 1, 2.0f);               // wedge
        Ring(m, f.P(loop1, 0), loop_r, 2);                            // double loop
        Ring(m, f.P(loop2, 0), loop_r, 2);
        Taper(m, f, loop2 + loop_r - 1, 1.8f, L * 0.62f, 1.4f);       // shaft
        Thorns(m, f, L * 0.53f, 1.6f, 8, 5.5f);
        Lozenge(m, f, L * 0.61f, L * 0.665f, 4.5f, L * 0.72f);
        Taper(m, f, L * 0.71f, 1.3f, L, 0.3f);                        // needle
        m.Flush(pixels_, p.hand);
    }

    // Second hand: hollow counterweight, slim needle, small diamond near the tip.
    if (show_seconds_) {
        const float a = second_ * 6.0f;
        const float L = R * 0.93f;
        Frame f(c0, c0, a);
        Taper(m, f, -14, 1.0f, L * 0.72f, 1.0f);
        Taper(m, f, L * 0.72f, 1.0f, L, 0.3f);
        Lozenge(m, f, L * 0.66f, L * 0.71f, 3.0f, L * 0.76f);
        Ring(m, f.P(-19, 0), 5, 2);
        m.Flush(pixels_, p.second_hand);
    }

    // Centre cap: solid boss with a face-coloured pivot hole.
    Disc(m, {c0, c0}, 7);
    m.Flush(pixels_, p.hand);
    Disc(m, {c0, c0}, 2.5f);
    m.Flush(pixels_, p.face);

    lv_obj_invalidate(canvas_);
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

bool ClockFace::SyncSettings() {
    auto& cs = ClockSettings::Get();
    if (cs.version() == settings_version_) {
        return false;
    }
    settings_version_ = cs.version();
    const ClockSettings::Values v = cs.Load();
    BuildPalettes(v.face, v.ink, day_palette_, night_palette_);
    day_start_ = v.day_start;
    night_start_ = v.night_start;
    show_seconds_ = v.show_seconds;
    return true;
}

bool ClockFace::IsNight(int hour) const {
    if (day_start_ < night_start_) {
        return hour < day_start_ || hour >= night_start_;
    }
    return hour >= night_start_ && hour < day_start_;  // day window wraps past midnight
}

void ClockFace::SetTime(int hour, int minute, int second) {
    const bool settings_changed = SyncSettings();
    if (!settings_changed && hour == hour_ && minute == minute_ && second == second_) {
        return;
    }
    const bool night = IsNight(hour);
    if (settings_changed || night != night_ || hour_ < 0) {
        night_ = night;
        palette_ = night ? &night_palette_ : &day_palette_;
        ApplyPalette();
    }
    hour_ = hour;
    minute_ = minute;
    second_ = second;
    Render();
}
