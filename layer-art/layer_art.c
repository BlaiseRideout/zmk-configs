/*
 * nice!view status screen: a battery and connection status strip, plus pixel art.
 * The central shows a skull above an icon for the highest active layer, the
 * peripheral shows a scythe with the central's BLE profiles, which the central
 * relays by invoking the bt_prof behavior (behavior_profile.c) on it.
 * The layout matches the stock nice!view peripheral screen.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <lvgl.h>

#include <zmk/battery.h>
#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/usb.h>

#include "layer_art.h"
#include "util.h"

#define IS_CENTRAL (!IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL))

#if IS_CENTRAL
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/keymap.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/layer_state_changed.h>
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
#include <zephyr/bluetooth/conn.h>
#include <zmk/behavior.h>
#include <zmk/split/central.h>
#endif
#else
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk/events/split_peripheral_status_changed.h>
#endif

// zmk v0.3 (LVGL 8) and zmk main (Zephyr 4.1, LVGL 9) differ in the stock widget
// helper signatures and the endpoint API
#if LVGL_VERSION_MAJOR >= 9
#define ENDPOINT_SELECTED zmk_endpoint_get_selected
#define CANVAS_FORMAT CANVAS_COLOR_FORMAT
#define DRAW_RECT canvas_draw_rect
#define DRAW_TEXT canvas_draw_text
#define ROTATE(canvas, buf) rotate_canvas(canvas)
#define IMG_SET_SRC lv_image_set_src
#else
#define ENDPOINT_SELECTED zmk_endpoints_selected
#define CANVAS_FORMAT LV_IMG_CF_TRUE_COLOR
#define DRAW_RECT lv_canvas_draw_rect
#define DRAW_TEXT lv_canvas_draw_text
#define ROTATE(canvas, buf) rotate_canvas(canvas, buf)
#define IMG_SET_SRC lv_img_set_src
#endif

#if IS_CENTRAL
LV_IMG_DECLARE(art_aoeu);
LV_IMG_DECLARE(art_arrows);
LV_IMG_DECLARE(art_copland);
LV_IMG_DECLARE(art_empty);
LV_IMG_DECLARE(art_game);
LV_IMG_DECLARE(art_gears);
LV_IMG_DECLARE(art_mouse);
LV_IMG_DECLARE(art_num);
LV_IMG_DECLARE(art_wine);

// Keyed by layer display-name
static const struct {
    const char *layer;
    const lv_img_dsc_t *art;
} layer_art[] = {
    {"BASE", &art_wine},    {"NUM", &art_num},     {"NAV", &art_arrows},
    {"FUN", &art_copland},  {"MOUSE", &art_mouse}, {"BOARD", &art_gears},
    {"DVORAK", &art_aoeu},  {"GAME", &art_game},
};
#define INITIAL_ART art_empty
#else
LV_IMG_DECLARE(art_scythe);
extern const lv_img_dsc_t *const art_bt_profiles[5][2];
#endif

static struct status_state state;
static lv_color_t cbuf[CANVAS_SIZE * CANVAS_SIZE];
static lv_obj_t *top_canvas;
static lv_obj_t *art;

static const char *connection_symbol(void) {
#if IS_CENTRAL
    switch (state.selected_endpoint.transport) {
    case ZMK_TRANSPORT_USB:
        return LV_SYMBOL_USB;
    case ZMK_TRANSPORT_BLE:
        if (!state.active_profile_bonded) {
            return LV_SYMBOL_SETTINGS;
        }
        return state.active_profile_connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE;
    default:
        return "";
    }
#else
    return state.connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE;
#endif
}

static void draw_top(void) {
    lv_draw_label_dsc_t label_dsc;
    init_label_dsc(&label_dsc, LVGL_FOREGROUND, &lv_font_montserrat_16, LV_TEXT_ALIGN_RIGHT);
    lv_draw_rect_dsc_t rect_black_dsc;
    init_rect_dsc(&rect_black_dsc, LVGL_BACKGROUND);

    DRAW_RECT(top_canvas, 0, 0, CANVAS_SIZE, CANVAS_SIZE, &rect_black_dsc);
    draw_battery(top_canvas, &state);
    DRAW_TEXT(top_canvas, 0, 0, CANVAS_SIZE, &label_dsc, connection_symbol());
    ROTATE(top_canvas, cbuf);
}

static void battery_status_update_cb(struct battery_status_state bs) {
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
    state.charging = bs.usb_present;
#endif
    state.battery = bs.level;
    draw_top();
}

static struct battery_status_state battery_status_get_state(const zmk_event_t *eh) {
    return (struct battery_status_state){
        .level = zmk_battery_state_of_charge(),
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
        .usb_present = zmk_usb_is_powered(),
#endif
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_battery_status, struct battery_status_state,
                            battery_status_update_cb, battery_status_get_state)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_battery_state_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_battery_status, zmk_usb_conn_state_changed);
#endif

#if IS_CENTRAL
struct output_status_state {
    struct zmk_endpoint_instance selected_endpoint;
    bool active_profile_connected;
    bool active_profile_bonded;
};

static void output_status_update_cb(struct output_status_state os) {
    state.selected_endpoint = os.selected_endpoint;
    state.active_profile_connected = os.active_profile_connected;
    state.active_profile_bonded = os.active_profile_bonded;
    draw_top();
}

static struct output_status_state output_status_get_state(const zmk_event_t *eh) {
    return (struct output_status_state){
        .selected_endpoint = ENDPOINT_SELECTED(),
        .active_profile_connected = zmk_ble_active_profile_is_connected(),
        .active_profile_bonded = !zmk_ble_active_profile_is_open(),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_output_status, struct output_status_state,
                            output_status_update_cb, output_status_get_state)
ZMK_SUBSCRIPTION(widget_output_status, zmk_endpoint_changed);
#if IS_ENABLED(CONFIG_USB_DEVICE_STACK)
ZMK_SUBSCRIPTION(widget_output_status, zmk_usb_conn_state_changed);
#endif
#if defined(CONFIG_ZMK_BLE)
ZMK_SUBSCRIPTION(widget_output_status, zmk_ble_active_profile_changed);
#endif

struct layer_art_state {
    const lv_img_dsc_t *art;
};

static void layer_art_update_cb(struct layer_art_state ls) { IMG_SET_SRC(art, ls.art); }

static struct layer_art_state layer_art_get_state(const zmk_event_t *eh) {
    const char *name =
        zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(zmk_keymap_highest_layer_active()));

    for (int i = 0; name && i < ARRAY_SIZE(layer_art); i++) {
        if (strcmp(name, layer_art[i].layer) == 0) {
            return (struct layer_art_state){.art = layer_art[i].art};
        }
    }
    return (struct layer_art_state){.art = &art_empty};
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_layer_art, struct layer_art_state, layer_art_update_cb,
                            layer_art_get_state)
ZMK_SUBSCRIPTION(widget_layer_art, zmk_layer_state_changed);

#if IS_ENABLED(CONFIG_ZMK_SPLIT)
static void send_profile(void) {
    struct zmk_behavior_binding binding = {
        .behavior_dev = LAYER_ART_PROFILE_BEHAVIOR,
        .param1 = zmk_ble_active_profile_index(),
        .param2 = zmk_ble_active_profile_is_connected(),
    };
    struct zmk_behavior_binding_event event = {.timestamp = k_uptime_get()};

    for (int i = 0; i < ZMK_SPLIT_CENTRAL_PERIPHERAL_COUNT; i++) {
        zmk_split_central_invoke_behavior(i, &binding, event, true);
    }
}

static int profile_changed_listener(const zmk_event_t *eh) {
    send_profile();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(layer_art_profile, profile_changed_listener);
ZMK_SUBSCRIPTION(layer_art_profile, zmk_ble_active_profile_changed);

// Sends are dropped until a peripheral is connected and its services discovered,
// with no event when that happens, so resend a few times after any connection.
static int resends_left;

static void resend_work_cb(struct k_work *work) {
    send_profile();
    if (--resends_left > 0) {
        k_work_reschedule(k_work_delayable_from_work(work), K_SECONDS(2));
    }
}

static K_WORK_DELAYABLE_DEFINE(resend_work, resend_work_cb);

static void connected(struct bt_conn *conn, uint8_t err) {
    if (!err) {
        resends_left = 3;
        k_work_reschedule(&resend_work, K_SECONDS(1));
    }
}

BT_CONN_CB_DEFINE(layer_art_conn_callbacks) = {.connected = connected};
#endif
#else
// Central's active BLE profile: index * 2 + connected, or -1 until it is relayed
static atomic_t profile = ATOMIC_INIT(-1);

static const lv_img_dsc_t *peripheral_art(void) {
    atomic_val_t p = atomic_get(&profile);
    if (p < 0 || p / 2 >= ARRAY_SIZE(art_bt_profiles)) {
        return &art_scythe;
    }
    return art_bt_profiles[p / 2][p % 2];
}

static void profile_work_cb(struct k_work *work) {
    if (art) {
        IMG_SET_SRC(art, peripheral_art());
    }
}

static K_WORK_DEFINE(profile_work, profile_work_cb);

void layer_art_set_profile(uint32_t index, bool connected) {
    atomic_set(&profile, index * 2 + (connected ? 1 : 0));
    k_work_submit_to_queue(zmk_display_work_q(), &profile_work);
}

struct peripheral_status_state {
    bool connected;
};

static void peripheral_status_update_cb(struct peripheral_status_state ps) {
    state.connected = ps.connected;
    draw_top();
    if (!ps.connected) {
        atomic_set(&profile, -1);
        IMG_SET_SRC(art, peripheral_art());
    }
}

static struct peripheral_status_state peripheral_status_get_state(const zmk_event_t *eh) {
    return (struct peripheral_status_state){.connected = zmk_split_bt_peripheral_is_connected()};
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_peripheral_status, struct peripheral_status_state,
                            peripheral_status_update_cb, peripheral_status_get_state)
ZMK_SUBSCRIPTION(widget_peripheral_status, zmk_split_peripheral_status_changed);
#endif

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

    lv_obj_t *widget = lv_obj_create(screen);
    lv_obj_set_size(widget, 160, 68);
    lv_obj_align(widget, LV_ALIGN_TOP_LEFT, 0, 0);

    // The art covers all but the top (rightmost, before rotation) 20px of the canvas
    top_canvas = lv_canvas_create(widget);
    lv_obj_align(top_canvas, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_canvas_set_buffer(top_canvas, cbuf, CANVAS_SIZE, CANVAS_SIZE, CANVAS_FORMAT);

    art = lv_img_create(widget);
#if IS_CENTRAL
    IMG_SET_SRC(art, &art_empty);
#else
    IMG_SET_SRC(art, peripheral_art());
#endif
    lv_obj_align(art, LV_ALIGN_TOP_LEFT, 0, 0);

    widget_battery_status_init();
#if IS_CENTRAL
    widget_output_status_init();
    widget_layer_art_init();
#else
    widget_peripheral_status_init();
#endif

    return screen;
}
