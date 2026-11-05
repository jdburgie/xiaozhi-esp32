// round-box-clock: analog clock face. See clock_face.h.
//
// Layout (outside in): 12/3/6/9 numerals on the outer ring, then a ring of
// 60 tick marks (lv_scale, ROUND_INNER, no scale labels), then the hands.
// The scale's value range is 0..720 over 360 degrees (one step = 30 s of
// minute-hand travel), so the hour hand moves between hour marks.
#include "clock_face.h"

LV_FONT_DECLARE(font_puhui_basic_20_4);  // from the 78/xiaozhi-fonts component

namespace {

constexpr int kRange = 720;  // full dial
const lv_font_t* const kNumeralFont = &font_puhui_basic_20_4;

const lv_color_t kFaceBg = lv_color_hex(0x000000);
const lv_color_t kMajorTick = lv_color_hex(0xF2F2F2);
const lv_color_t kMinorTick = lv_color_hex(0x5A5A5A);
const lv_color_t kNumeral = lv_color_hex(0xE6E6E6);
const lv_color_t kHand = lv_color_hex(0xF2F2F2);
const lv_color_t kSecondHand = lv_color_hex(0xFF8C1A);  // amber

lv_obj_t* MakeHand(lv_obj_t* scale, int width, lv_color_t color) {
    lv_obj_t* hand = lv_line_create(scale);
    lv_obj_set_style_line_width(hand, width, 0);
    lv_obj_set_style_line_color(hand, color, 0);
    lv_obj_set_style_line_rounded(hand, true, 0);
    return hand;
}

void MakeNumeral(lv_obj_t* parent, const char* text, int dx, int dy) {
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, kNumeralFont, 0);
    lv_obj_set_style_text_color(label, kNumeral, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, dx, dy);
}

}  // namespace

ClockFace::ClockFace(lv_obj_t* parent, int size) {
    // Opaque black disc covering the whole round panel (and XiaoZhi's UI beneath it).
    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, size, size);
    lv_obj_center(root_);
    lv_obj_set_style_bg_color(root_, kFaceBg, 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_CLICKABLE);  // let touches reach the board's handler

    // Numerals: centred on a ring just inside the panel edge.
    const int half = size / 2;
    const int glyph_h = lv_font_get_line_height(kNumeralFont);
    const int numeral_r = half - glyph_h / 2 - 6;
    MakeNumeral(root_, "12", 0, -numeral_r);
    MakeNumeral(root_, "3", numeral_r, 0);
    MakeNumeral(root_, "6", 0, numeral_r);
    MakeNumeral(root_, "9", -numeral_r, 0);

    // Tick ring sits inside the numerals.
    const int tick_r = numeral_r - glyph_h / 2 - 4;
    scale_ = lv_scale_create(root_);
    lv_obj_set_size(scale_, tick_r * 2, tick_r * 2);
    lv_obj_center(scale_);
    lv_scale_set_mode(scale_, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_range(scale_, 0, kRange);
    lv_scale_set_angle_range(scale_, 360);
    lv_scale_set_rotation(scale_, 270);  // 0 at 12 o'clock
    lv_scale_set_total_tick_count(scale_, 61);
    lv_scale_set_major_tick_every(scale_, 5);
    lv_scale_set_label_show(scale_, false);
    lv_obj_remove_flag(scale_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(scale_, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scale_, LV_OPA_TRANSP, LV_PART_MAIN);

    // Hour ticks
    lv_obj_set_style_length(scale_, 10, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(scale_, 3, LV_PART_INDICATOR);
    lv_obj_set_style_line_color(scale_, kMajorTick, LV_PART_INDICATOR);
    // Minute ticks
    lv_obj_set_style_length(scale_, 4, LV_PART_ITEMS);
    lv_obj_set_style_line_width(scale_, 1, LV_PART_ITEMS);
    lv_obj_set_style_line_color(scale_, kMinorTick, LV_PART_ITEMS);

    hour_len_ = tick_r * 55 / 100;
    minute_len_ = tick_r * 84 / 100;
    second_len_ = tick_r * 92 / 100;
    hour_hand_ = MakeHand(scale_, 7, kHand);
    minute_hand_ = MakeHand(scale_, 4, kHand);
    second_hand_ = MakeHand(scale_, 2, kSecondHand);

    // Center cap over the hand pivots
    cap_ = lv_obj_create(root_);
    lv_obj_remove_style_all(cap_);
    lv_obj_set_size(cap_, 12, 12);
    lv_obj_center(cap_);
    lv_obj_set_style_radius(cap_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(cap_, kSecondHand, 0);
    lv_obj_set_style_bg_opa(cap_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(cap_, kFaceBg, 0);
    lv_obj_set_style_border_width(cap_, 2, 0);
    lv_obj_remove_flag(cap_, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
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
    if (second != last_second_) {
        lv_scale_set_line_needle_value(scale_, second_hand_, second_len_, second * kRange / 60);
        last_second_ = second;
    }
    if (minute != last_minute_ || hour != last_hour_) {
        // minute hand: 12 steps per minute-mark; hour hand: 60 steps per hour-mark
        lv_scale_set_line_needle_value(scale_, minute_hand_, minute_len_, minute * 12);
        lv_scale_set_line_needle_value(scale_, hour_hand_, hour_len_, (hour % 12) * 60 + minute);
        last_minute_ = minute;
        last_hour_ = hour;
    }
}
