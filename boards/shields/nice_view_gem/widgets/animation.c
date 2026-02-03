#include <stdlib.h>
#include <zephyr/kernel.h>
#include "animation.h"

#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
#include <zmk/usb.h>
#endif

LV_IMG_DECLARE(crystal_01);
LV_IMG_DECLARE(crystal_02);
LV_IMG_DECLARE(crystal_03);
LV_IMG_DECLARE(crystal_04);
LV_IMG_DECLARE(crystal_05);
LV_IMG_DECLARE(crystal_06);
LV_IMG_DECLARE(crystal_07);
LV_IMG_DECLARE(crystal_08);
LV_IMG_DECLARE(crystal_09);
LV_IMG_DECLARE(crystal_10);
LV_IMG_DECLARE(crystal_11);
LV_IMG_DECLARE(crystal_12);
LV_IMG_DECLARE(crystal_13);
LV_IMG_DECLARE(crystal_14);
LV_IMG_DECLARE(crystal_15);
LV_IMG_DECLARE(crystal_16);

const lv_img_dsc_t *anim_imgs[] = {
    &crystal_01, &crystal_02, &crystal_03, &crystal_04, &crystal_05, &crystal_06,
    &crystal_07, &crystal_08, &crystal_09, &crystal_10, &crystal_11, &crystal_12,
    &crystal_13, &crystal_14, &crystal_15, &crystal_16,
};

static lv_obj_t *find_animation_object(lv_obj_t *parent) {
    // Search for animation object in parent's children
    // Look for animimg first (animated), then img (static)
    uint32_t child_cnt = lv_obj_get_child_cnt(parent);
    for (int i = child_cnt - 1; i >= 0; i--) {
        lv_obj_t *child = lv_obj_get_child(parent, i);
        // Check if it's an animimg object (animated animation)
        if (lv_obj_check_type(child, &lv_animimg_class)) {
            return child;
        }
    }
    // If no animimg found, look for img object (static animation)
    for (int i = child_cnt - 1; i >= 0; i--) {
        lv_obj_t *child = lv_obj_get_child(parent, i);
        if (lv_obj_check_type(child, &lv_img_class)) {
            // Verify it's using one of our animation images
            const void *src = lv_img_get_src(child);
            for (int j = 0; j < 16; j++) {
                if (src == anim_imgs[j]) {
                    return child;
                }
            }
        }
    }
    return NULL;
}

static void create_static_animation(lv_obj_t *canvas) {
    lv_obj_t *art = lv_img_create(canvas);

    int length = sizeof(anim_imgs) / sizeof(anim_imgs[0]);
    srand(k_uptime_get_32());
    int random_index = rand() % length;
    int configured_index = (CONFIG_NICE_VIEW_GEM_ANIMATION_FRAME - 1) % length;
    int anim_imgs_index = CONFIG_NICE_VIEW_GEM_ANIMATION_FRAME > 0 ? configured_index : random_index;

    lv_img_set_src(art, anim_imgs[anim_imgs_index]);
    lv_obj_align(art, LV_ALIGN_TOP_LEFT, 36, 0);
}

static void create_animated_animation(lv_obj_t *canvas) {
    lv_obj_t *art = lv_animimg_create(canvas);
    lv_obj_center(art);

    lv_animimg_set_src(art, (const void **)anim_imgs, 16);
    lv_animimg_set_duration(art, CONFIG_NICE_VIEW_GEM_ANIMATION_MS);
    lv_animimg_set_repeat_count(art, LV_ANIM_REPEAT_INFINITE);
    lv_animimg_start(art);
    lv_obj_align(art, LV_ALIGN_TOP_LEFT, 36, 0);
}

void draw_animation(lv_obj_t *canvas) {
#if IS_ENABLED(CONFIG_NICE_VIEW_GEM_ANIMATION)
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    // Check USB power state - animate if USB is powered, static if on battery
    bool usb_powered = zmk_usb_is_powered();
    if (usb_powered) {
        create_animated_animation(canvas);
    } else {
        create_static_animation(canvas);
    }
#else
    // No USB support, always animate if enabled
    create_animated_animation(canvas);
#endif
#else
    // Animation disabled at compile time
    create_static_animation(canvas);
#endif
}

void update_animation_based_on_usb(lv_obj_t *parent, bool usb_powered) {
#if IS_ENABLED(CONFIG_NICE_VIEW_GEM_ANIMATION)
    // Find and remove existing animation object
    lv_obj_t *existing_anim = find_animation_object(parent);
    if (existing_anim != NULL) {
        lv_obj_del(existing_anim);
    }

    // Create new animation based on USB power state
    if (usb_powered) {
        create_animated_animation(parent);
    } else {
        create_static_animation(parent);
    }
#endif
}