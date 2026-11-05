// round-box-clock: analog clock face for the 240x240 round GC9A01 panel.
// Full-screen LVGL overlay; the owning display shows it while the device is idle.
// All methods must be called with the display (LVGL) lock held.
#ifndef CLOCK_FACE_H
#define CLOCK_FACE_H

#include <lvgl.h>

class ClockFace {
public:
    // Creates the face as a hidden child of `parent`, sized `size` x `size`, centered.
    ClockFace(lv_obj_t* parent, int size);

    void SetVisible(bool visible);
    bool IsVisible() const;

    // 24 h local time. Redraws only when the second changes; switches the
    // day/night palette when the hour crosses the boundaries.
    void SetTime(int hour, int minute, int second);

    struct Palette {
        lv_color_t face;
        lv_color_t numeral;
        lv_color_t hour_mark;
        lv_color_t minute_tick;
        lv_color_t hand;
        lv_color_t second_hand;
    };

private:
    static void DrawCb(lv_event_t* e);
    void Draw(lv_layer_t* layer, const lv_area_t& area) const;
    void ApplyPalette();

    lv_obj_t* root_ = nullptr;
    lv_obj_t* canvas_ = nullptr;  // ticks + hands, custom-drawn, above the numerals
    lv_obj_t* numerals_[4] = {};

    int radius_ = 0;          // panel radius
    int numeral_r_ = 0;       // radius of the numeral ring centres
    int glyph_h_ = 0;

    bool night_ = true;
    const Palette* palette_ = nullptr;

    int hour_ = -1;
    int minute_ = -1;
    int second_ = -1;
};

#endif  // CLOCK_FACE_H
