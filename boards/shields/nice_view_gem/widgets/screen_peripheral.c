#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/display.h>
#include <zmk/usb.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/event_manager.h>

#include "animation.h"
#include "battery.h"
#include "output.h"
#include "screen_peripheral.h"

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

#if IS_ENABLED(CONFIG_USB_DEVICE_STACK) && IS_ENABLED(CONFIG_NICE_VIEW_GEM_ANIMATION)
// Work queue item to periodically ensure animation is active when USB is connected
static struct k_work_delayable animation_check_work;
// Work queue item to prevent display blanking when USB is connected
static struct k_work_delayable keep_display_active_work;

static void ensure_animation_active_work(struct k_work *work) {
    struct zmk_widget_screen *widget;
    bool usb_powered = zmk_usb_is_powered();
    
    if (usb_powered) {
        // USB is connected - check if animation needs to be recreated
        // Only recreate if missing or static - don't touch running animations to avoid hiccups
        SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
            lv_obj_t *anim = find_animation_object(widget->obj);
            bool is_animated = (anim != NULL && lv_obj_check_type(anim, &lv_animimg_class));
            
            if (!is_animated) {
                // Animation doesn't exist or is static - recreate animated version
                // This handles cases where display blanking deleted or switched the animation
                update_animation_based_on_usb(widget->obj, true);
            }
            // If animation exists and is animated, leave it completely alone
            // Don't restart it even if paused - that causes hiccups
            // The animation should resume automatically when display wakes
        }
        
        // Schedule next check in 1 second - frequent enough to catch deletions quickly
        k_work_schedule(&animation_check_work, K_SECONDS(1));
    }
}

// Keep display active when USB is connected by reporting activity
static void keep_display_active_handler(struct k_work *work) {
    bool usb_powered = zmk_usb_is_powered();
    
    if (usb_powered) {
        // Report activity to prevent display blanking when USB is connected
        // This keeps the display active and animations running smoothly
        // We publish an activity_state_changed event to reset the idle timer
        struct zmk_activity_state_changed *ev = new_zmk_activity_state_changed();
        ev->state = ZMK_ACTIVITY_ACTIVE;
        ZMK_EVENT_RAISE(ev);
        
        // Schedule next activity report in 10 seconds
        // This is frequent enough to prevent blanking but not too frequent
        k_work_schedule(&keep_display_active_work, K_SECONDS(10));
    }
}
#endif

/**
 * Draw buffers
 **/

static void draw_top(lv_obj_t *widget, const struct status_state *state) {
    lv_obj_t *canvas = lv_obj_get_child(widget, 0);
    fill_background(canvas);

    // Draw widgets
    draw_output_status(canvas, state);
    draw_battery_status(canvas, state);

    // Rotate for horizontal display
    rotate_canvas(canvas);
    
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK) && IS_ENABLED(CONFIG_NICE_VIEW_GEM_ANIMATION)
    // When display refreshes (e.g., wakes from blanking), restart animation if USB connected
    // This handles cases where display blanking paused the animation
    if (zmk_usb_is_powered()) {
        lv_obj_t *anim = find_animation_object(widget);
        if (anim != NULL && lv_obj_check_type(anim, &lv_animimg_class)) {
            // Animation exists - restart it to resume if paused
            // This only happens on display refresh, not periodically, so shouldn't cause hiccups
            lv_animimg_start(anim);
        }
    }
#endif
}

/**
 * Battery status
 **/

static void set_battery_status(struct zmk_widget_screen *widget,
                               struct battery_status_state state) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    widget->state.charging = state.usb_present;
    // Update animation based on USB power state
    update_animation_based_on_usb(widget->obj, state.usb_present);
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */

    widget->state.battery = state.level;

    draw_top(widget->obj, &widget->state);
}

static void battery_status_update_cb(struct battery_status_state state) {
    struct zmk_widget_screen *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_battery_status(widget, state); }
}

static struct battery_status_state battery_status_get_state(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);

    return (struct battery_status_state){
        .level = (ev != NULL) ? ev->state_of_charge : zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_battery_status, struct battery_status_state,
                            battery_status_update_cb, battery_status_get_state);

ZMK_SUBSCRIPTION(widget_battery_status, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_usb_conn_state_changed);

/**
 * USB connection state handler - directly updates animation immediately
 * Reuse battery_status_state since it has usb_present field
 */
static struct battery_status_state usb_conn_get_state(const zmk_event_t *_eh) {
    return (struct battery_status_state){
        .level = zmk_battery_state_of_charge(),
        .usb_present = zmk_usb_is_powered(),
    };
}

static void usb_conn_update_cb(struct battery_status_state state) {
    struct zmk_widget_screen *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        // Immediately update animation when USB state changes
        update_animation_based_on_usb(widget->obj, state.usb_present);
        
        // If USB is connected, start periodic checks and keep display active
#if IS_ENABLED(CONFIG_NICE_VIEW_GEM_ANIMATION)
        if (state.usb_present) {
            // Start check immediately to verify animation state
            k_work_schedule(&animation_check_work, K_MSEC(50));
            // Start keeping display active to prevent blanking
            k_work_schedule(&keep_display_active_work, K_SECONDS(1));
        } else {
            k_work_cancel_delayable(&animation_check_work);
            k_work_cancel_delayable(&keep_display_active_work);
        }
#endif
    }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_usb_conn_status, struct battery_status_state,
                            usb_conn_update_cb, usb_conn_get_state);
ZMK_SUBSCRIPTION(widget_usb_conn_status, zmk_usb_conn_state_changed);
#endif /* IS_ENABLED(CONFIG_USB_DEVICE_STACK) */

/**
 * Peripheral status
 **/

static struct peripheral_status_state get_state(const zmk_event_t *_eh) {
    return (struct peripheral_status_state){.connected = zmk_split_bt_peripheral_is_connected()};
}

static void set_connection_status(struct zmk_widget_screen *widget,
                                  struct peripheral_status_state state) {
    widget->state.connected = state.connected;

    draw_top(widget->obj, &widget->state);
}

static void output_status_update_cb(struct peripheral_status_state state) {
    struct zmk_widget_screen *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_connection_status(widget, state); }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_peripheral_status, struct peripheral_status_state,
                            output_status_update_cb, get_state)
ZMK_SUBSCRIPTION(widget_peripheral_status, zmk_split_peripheral_status_changed);

/**
 * Initialization
 **/

int zmk_widget_screen_init(struct zmk_widget_screen *widget, lv_obj_t *parent) {
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, SCREEN_HEIGHT, SCREEN_WIDTH);
    lv_obj_set_style_bg_color(widget->obj, LVGL_BACKGROUND, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(widget->obj, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *top = lv_canvas_create(widget->obj);
    lv_obj_align(top, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_canvas_set_buffer(top, widget->cbuf, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);

    draw_animation(widget->obj);

    sys_slist_append(&widgets, &widget->node);
    widget_battery_status_init();
    widget_peripheral_status_init();
    
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK) && IS_ENABLED(CONFIG_NICE_VIEW_GEM_ANIMATION)
    // Initialize work queue for periodic animation checks
    k_work_init_delayable(&animation_check_work, ensure_animation_active_work);
    // Initialize work queue for keeping display active when USB connected
    k_work_init_delayable(&keep_display_active_work, keep_display_active_handler);
    
    // If USB is already connected at init, start the periodic checks
    if (zmk_usb_is_powered()) {
        k_work_schedule(&animation_check_work, K_MSEC(50));
        k_work_schedule(&keep_display_active_work, K_SECONDS(1));
    }
    
    widget_usb_conn_status_init();
#endif

    return 0;
}

lv_obj_t *zmk_widget_screen_obj(struct zmk_widget_screen *widget) { return widget->obj; }