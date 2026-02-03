#pragma once

#include <lvgl.h>
#include "util.h"
#include "screen_peripheral.h"

void draw_animation(lv_obj_t *canvas);
void update_animation_based_on_usb(lv_obj_t *parent, bool usb_powered);