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

lv_obj_t *find_animation_object(lv_obj_t *parent) {
    // Search for animation object in parent's children
    // Look for animimg first (animated), then img/image (static)
    uint32_t child_cnt = lv_obj_get_child_cnt(parent);
    
    // First, try to find animimg (animated animation)
    for (int i = child_cnt - 1; i >= 0; i--) {
        lv_obj_t *child = lv_obj_get_child(parent, i);
        if (lv_obj_check_type(child, &lv_animimg_class)) {
            return child;
        }
    }
    
    // If no animimg found, look for img/image object (static animation)
    // Identify by checking if it uses one of our animation images and is positioned correctly
    for (int i = child_cnt - 1; i >= 0; i--) {
        lv_obj_t *child = lv_obj_get_child(parent, i);
        // Try to get source - this will work for image objects
        // Note: lv_img_get_src may return NULL or fail for non-image objects, which is fine
        const void *src = NULL;
        // Only try to get source if it's likely an image object
        // We check by trying to get the source - if it fails, src will be NULL
        src = lv_img_get_src(child);
        
        if (src != NULL) {
            // Verify it's using one of our animation images
            for (int j = 0; j < 16; j++) {
                if (src == anim_imgs[j]) {
                    // Additional check: verify it's positioned where we expect (optional safety check)
                    // Animation should be at ALIGN_TOP_LEFT with x=36, y=0
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
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    // Always check current USB power state directly, don't rely on parameter
    // This ensures we have the most up-to-date state
    bool current_usb_powered = zmk_usb_is_powered();
    
    // Find existing animation object
    lv_obj_t *existing_anim = find_animation_object(parent);
    
    // Check if we need to update
    bool needs_animated = current_usb_powered;
    bool is_currently_animated = (existing_anim != NULL && 
                                  lv_obj_check_type(existing_anim, &lv_animimg_class));
    
    if (needs_animated) {
        // USB is powered - ensure animation is active
        if (is_currently_animated) {
            // Animation exists and is animated - only restart if it's not running
            // Calling start() on a running animation shouldn't cause issues, but
            // to be safe, we'll only update if state actually changed
            // (The periodic check will handle restarting if paused)
        } else {
            // Need to create animated animation (either doesn't exist or is static)
            if (existing_anim != NULL) {
                lv_obj_del(existing_anim);
            }
            create_animated_animation(parent);
        }
    } else {
        // USB not powered - ensure animation is static
        if (is_currently_animated) {
            // Currently animated but USB is off - switch to static
            if (existing_anim != NULL) {
                lv_obj_del(existing_anim);
            }
            create_static_animation(parent);
        }
        // If already static, no change needed
    }
#else
    // No USB support, always animate if enabled
    lv_obj_t *existing_anim = find_animation_object(parent);
    if (existing_anim == NULL || !lv_obj_check_type(existing_anim, &lv_animimg_class)) {
        if (existing_anim != NULL) {
            lv_obj_del(existing_anim);
        }
        create_animated_animation(parent);
    }
#endif
#endif
}