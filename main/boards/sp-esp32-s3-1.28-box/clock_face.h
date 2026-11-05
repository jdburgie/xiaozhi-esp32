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

    // 24 h local time. Cheap when nothing changed.
    void SetTime(int hour, int minute, int second);

private:
    lv_obj_t* root_ = nullptr;
    lv_obj_t* scale_ = nullptr;
    lv_obj_t* hour_hand_ = nullptr;
    lv_obj_t* minute_hand_ = nullptr;
    lv_obj_t* second_hand_ = nullptr;
    lv_obj_t* cap_ = nullptr;

    int hour_len_ = 0;
    int minute_len_ = 0;
    int second_len_ = 0;
    int last_hour_ = -1;
    int last_minute_ = -1;
    int last_second_ = -1;
};

#endif  // CLOCK_FACE_H
