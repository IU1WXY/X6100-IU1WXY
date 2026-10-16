/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "dialog_settings.h"

#include "dsp.h"
#include "wifi.h"
#include "btaudio.h"
#include "bt_dev.h"
#include "cfg/settings_manager.h"
#include "cfg/settings_manager_api.h"
#include <vector>
#include <thread>
#include <string>
#include <cstdio>
#include <cstring>
#include <functional>

extern "C" {

    #include "dialog.h"
    #include "styles.h"
    #include "theme.h"
    #include "fonts/font_select.h"
    #include "params/params.h"
    #include "backlight.h"
    #include "radio.h"
    #include "events.h"
    #include "keyboard.h"
    #include "clock.h"
    #include "audio.h"
    #include "meter.h"
    #include "main.h"
    #include "msg.h"
    #include "textarea_window.h"
    #include "station.h"
    #include "wfview_server.h"
    #include "cluster/client.h"
    #include "cluster/spots.h"

    #include "lvgl/lvgl.h"
    #include <sys/time.h>
    #include <time.h>
    #include <fcntl.h>
    #include <stdlib.h>
    #include <unistd.h>
    #include <sys/ioctl.h>
    #include <linux/rtc.h>
    #include <errno.h>

}

#define SMALL_PAD   5
#define ROW_H       44  /* option row and control height */
/* Settings text two sizes over the sony_N it names (readability); VISION already scales it up */
#define SF(n)       font_down(&sony_##n, theme_shape.vision ? 0 : -2)

#define SMALL_1     57
#define SMALL_2     (SMALL_1 * 2 + SMALL_PAD * 1)
#define SMALL_3     (SMALL_1 * 3 + SMALL_PAD * 2)
#define SMALL_4     (SMALL_1 * 4 + SMALL_PAD * 3)
#define SMALL_5     (SMALL_1 * 5 + SMALL_PAD * 4)
#define SMALL_6     (SMALL_1 * 6 + SMALL_PAD * 5)

#define SMALL_WIDTH 57

static void make_general_page();
static void make_display_page();
static void make_audio_page();
static void make_wireless_page();
static void make_bluetooth_page();
static void eth_draft_load();
static void make_info_page();
static void make_keys_page();
static void make_cluster_page();
static void make_txcal_page();
static void make_walkie_page();

static void load_general_page(button_data_t *btn_data);
static void load_display_page(button_data_t *btn_data);
static void load_audio_page(button_data_t *btn_data);
static void load_wireless_page(button_data_t *btn_data);
static void load_bluetooth_page(button_data_t *btn_data);
static void load_info_page(button_data_t *btn_data);
static void load_keys_page(button_data_t *btn_data);
static void load_cluster_page(button_data_t *btn_data);
static void load_txcal_page(button_data_t *btn_data);
static void load_walkie_page(button_data_t *btn_data);

static void construct_cb(lv_obj_t *parent);
static void destruct_cb();
static void key_cb(lv_event_t * e);

static lv_obj_t     *grid;
static lv_obj_t     *page;      /* scrolling column of cards */
static lv_coord_t   col_dsc[] = { LV_GRID_FR(1), SMALL_1, SMALL_1, SMALL_1, SMALL_1, SMALL_1, SMALL_1, LV_GRID_TEMPLATE_LAST };
static lv_coord_t   col_station[] = { LV_GRID_FR(1), 150, LV_GRID_TEMPLATE_LAST };
static lv_coord_t   col_datetime[] = { LV_GRID_FR(1), 78, 78, 100, LV_GRID_TEMPLATE_LAST };
static lv_coord_t   row_pool[8][64];    /* row descriptors: one per card of the page */
static lv_coord_t   *row_dsc = row_pool[0];
static int          card_n;

static bool         datetime_changed;
static time_t       now;
struct tm           ts;         /* local time fields */
struct tm           ts_utc;     /* same instant in UTC, for the RTC */

static lv_obj_t     *day;
static lv_obj_t     *month;
static lv_obj_t     *year;
static lv_obj_t     *hour;
static lv_obj_t     *min;
static lv_obj_t     *sec;

static std::vector<Subscription> observers;

static button_data_t btn_p1 = {
    .type  = BTN_TEXT,
    .label = "(SETTINGS\n1:3)",
    .press = button_next_page_cb,
};

static button_data_t btn_p2 = {
    .type  = BTN_TEXT,
    .label = "(SETTINGS\n2:3)",
    .press = button_next_page_cb,
};

static button_data_t btn_p3 = {
    .type  = BTN_TEXT,
    .label = "(SETTINGS\n3:3)",
    .press = button_next_page_cb,
};

static button_data_t btn_general = {
    .type  = BTN_TEXT,
    .label = "General",
    .press = load_general_page,
};

static button_data_t btn_display = {
    .type  = BTN_TEXT,
    .label = "Display",
    .press = load_display_page,
};

static button_data_t btn_audio = {
    .type  = BTN_TEXT,
    .label = "Audio",
    .press = load_audio_page,
};

static button_data_t btn_wireless = {
    .type  = BTN_TEXT,
    .label = "Wireless",
    .press = load_wireless_page,
};

static button_data_t btn_bluetooth = {
    .type  = BTN_TEXT,
    .label = "Bluetooth",
    .press = load_bluetooth_page,
};

static button_data_t btn_keys = {
    .type  = BTN_TEXT,
    .label = "Keys",
    .press = load_keys_page,
};

static button_data_t btn_cluster = {
    .type  = BTN_TEXT,
    .label = "Cluster",
    .press = load_cluster_page,
};

static button_data_t btn_txcal = {
    .type  = BTN_TEXT,
    .label = "TX Cal",
    .press = load_txcal_page,
};

static button_data_t btn_info = {
    .type  = BTN_TEXT,
    .label = "Info",
    .press = load_info_page,
};

static button_data_t btn_walkie = {
    .type  = BTN_TEXT,
    .label = "Walkie",
    .press = load_walkie_page,
};

buttons_page_t btn_page = {
    {
     &btn_p1,
     &btn_general,
     &btn_display,
     &btn_audio,
     &btn_wireless,
     }
};

static buttons_page_t btn_page_2 = {
    {
     &btn_p2,
     &btn_bluetooth,
     &btn_keys,
     &btn_cluster,
     &btn_txcal,
     }
};

static buttons_page_t btn_page_3 = {
    {
     &btn_p3,
     &btn_info,
     &btn_walkie,
     }
};

static dialog_t     dialog = {
    .construct_cb = construct_cb,
    .destruct_cb = destruct_cb,
    .audio_cb = NULL,
    .btn_page = &btn_page,
    .key_cb = key_cb,
    .run = false,
};

dialog_t            *dialog_settings = &dialog;

/* Left rail: the tabs of both button pages, numbered, the current one marked */

#define RAIL_W      150
#define RAIL_N      10

static button_data_t *rail_tabs[RAIL_N] = {
    &btn_general, &btn_display, &btn_audio, &btn_wireless, &btn_bluetooth, &btn_keys, &btn_cluster, &btn_txcal,
    &btn_info, &btn_walkie,
};
static lv_obj_t      *rail_items[RAIL_N];

static void rail_click_cb(lv_event_t *e) {
    int             i  = (int)(intptr_t)lv_event_get_user_data(e);
    buttons_page_t *pg = i < 4 ? &btn_page : i < 8 ? &btn_page_2 : &btn_page_3;

    if (buttons_get_cur_page() != pg) {
        buttons_unload_page();
        buttons_load_page(pg);
    }
    rail_tabs[i]->press(rail_tabs[i]);
}

static void rail_create() {
    lv_obj_t  *rail = lv_obj_create(dialog.obj);
    lv_coord_t h    = (DIALOG_H - 8) / RAIL_N;

    lv_obj_remove_style_all(rail);
    lv_obj_set_size(rail, RAIL_W, DIALOG_H - 8);
    lv_obj_align(rail, LV_ALIGN_LEFT_MID, 4, 0);
    lv_obj_set_flex_flow(rail, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_border_side(rail, LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_border_width(rail, 1, 0);
    lv_obj_set_style_border_color(rail, TC(dialog_border), 0);
    lv_obj_clear_flag(rail, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < RAIL_N; i++) {
        lv_obj_t *item = lv_obj_create(rail);
        char      name[16];
        int       k;

        lv_obj_remove_style_all(item);
        lv_obj_set_size(item, RAIL_W - 4, h);
        lv_obj_clear_flag(item, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(item, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(item, TC(sel_bg), 0);
        lv_obj_set_style_border_side(item, LV_BORDER_SIDE_LEFT, 0);
        lv_obj_set_style_border_color(item, TC(accent), 0);
        lv_obj_add_event_cb(item, rail_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *num = lv_label_create(item);
        lv_obj_set_style_text_font(num, SF(14), 0);
        lv_label_set_text_fmt(num, "%02d", i + 1);
        lv_obj_align(num, LV_ALIGN_LEFT_MID, 10, 0);

        for (k = 0; rail_tabs[i]->label[k] && k < 15; k++) name[k] = toupper(rail_tabs[i]->label[k]);
        name[k] = 0;
        lv_obj_t *lbl = lv_label_create(item);
        /* BLUETOOTH inside the rail: a size down, two in VISION */
        const lv_font_t *fonts[] = {SF(18), SF(16), SF(14)};
        lv_point_t       size;
        int              f = 0;

        do {
            lv_txt_get_size(&size, name, fonts[f], 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        } while (size.x > RAIL_W - 4 - 38 - 2 && ++f < 2);
        lv_obj_set_style_text_font(lbl, fonts[f], 0);
        lv_label_set_text(lbl, name);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 38, 0);
        rail_items[i] = item;
    }
}

static void rail_mark(button_data_t *btn_data) {
    for (int i = 0; i < RAIL_N; i++) {
        lv_obj_t *item = rail_items[i];
        bool      on   = rail_tabs[i] == btn_data;

        lv_obj_set_style_bg_opa(item, on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(item, on ? 4 : 0, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(item, 0), on ? TC(sel_text) : TC(text_faint), 0);
        lv_obj_set_style_text_color(lv_obj_get_child(item, 1), on ? TC(sel_text) : TC(text_dim), 0);
    }
}

static void mark_tab(button_data_t *btn_data) {
    for (buttons_page_t *page : {&btn_page, &btn_page_2, &btn_page_3}) {
        for (auto &&btn : page->items) {
            buttons_mark(btn, false);
        }
    }
    buttons_mark(btn_data, true);
    rail_mark(btn_data);
}

static void load_general_page(button_data_t *btn_data) {
    make_general_page();
    mark_tab(btn_data);
}

static void load_display_page(button_data_t *btn_data) {
    make_display_page();
    mark_tab(btn_data);
}

static void load_audio_page(button_data_t *btn_data) {
    make_audio_page();
    mark_tab(btn_data);
}

static void load_wireless_page(button_data_t *btn_data) {
    eth_draft_load();
    make_wireless_page();
    mark_tab(btn_data);
}

static void load_bluetooth_page(button_data_t *btn_data) {
    make_bluetooth_page();
    mark_tab(btn_data);
}

static void load_info_page(button_data_t *btn_data) {
    make_info_page();
    mark_tab(btn_data);
}

static void load_keys_page(button_data_t *btn_data) {
    make_keys_page();
    mark_tab(btn_data);
}

static void load_cluster_page(button_data_t *btn_data) {
    make_cluster_page();
    mark_tab(btn_data);
}

static void load_walkie_page(button_data_t *btn_data) {
    make_walkie_page();
    mark_tab(btn_data);
}

static void load_txcal_page(button_data_t *btn_data) {
    make_txcal_page();
    mark_tab(btn_data);
}

/* Shared update */

static void bool_update_cb(lv_event_t * e) {
    lv_obj_t        *obj = lv_event_get_target(e);
    params_bool_t   *var = (params_bool_t*)lv_event_get_user_data(e);

    params_bool_set(var, lv_obj_has_state(obj, LV_STATE_CHECKED));
}

static void bool_update_subj_cb(lv_event_t *e) {
    lv_obj_t *obj  = lv_event_get_target(e);
    ParamInt *param = (ParamInt *)lv_event_get_user_data(e);

    param->set(lv_obj_has_state(obj, LV_STATE_CHECKED));
}

static void uint8_dropdown_update_cb(lv_event_t * e) {
    lv_obj_t        *obj = lv_event_get_target(e);
    params_uint8_t  *var = (params_uint8_t*)lv_event_get_user_data(e);

    params_uint8_set(var, lv_dropdown_get_selected(obj));
}

/* Themes and fonts are applied live: every window takes its colours and fonts when
 * built, so the main screen is built again (main_screen_set_theme()) */
static lv_obj_t *theme_dd;
static lv_obj_t *font_dd;

/* Settings goes with the rest of the screen, then opens again on Display, on the
 * dropdown that was changed (arg) */
static void theme_apply(void *arg) {
    bool font = arg != NULL;

    main_screen_set_theme(params.theme_name.x);
    main_screen_start_app(ACTION_APP_SETTINGS);
    load_display_page(&btn_display);
    lv_group_focus_obj(font ? font_dd : theme_dd);
}

static void font_update_cb(lv_event_t * e) {
    uint8_dropdown_update_cb(e);
    if (theme_shape.font >= 0) {
        msg_update_text_fmt("Font: this theme has its own");   /* RS, Black Metal, Total Black */
        return;
    }
    params_flush();
    lv_async_call(theme_apply, (void *)1);   /* not from inside the dropdown's own event */
}

static void theme_update_cb(lv_event_t * e) {
    char name[32];

    lv_dropdown_get_selected_str(lv_event_get_target(e), name, sizeof(name));
    params_str_set(&params.theme_name, name);
    params_flush();
    cfg_api_flush_all();
    lv_async_call(theme_apply, NULL);   /* not from inside the dropdown's own event */
}

/* Shared create */
static lv_obj_t * create_switch(lv_obj_t *parent){
    lv_obj_t *obj = lv_switch_create(parent);
    dialog_item(&dialog, obj);
    lv_obj_set_style_bg_color(obj, TC(switch_off), LV_PART_MAIN);
    lv_obj_center(obj);
    return obj;
}

static lv_obj_t * switch_bool(lv_obj_t *parent, params_bool_t *var) {
    lv_obj_t *obj = create_switch(parent);
    lv_obj_add_event_cb(obj, bool_update_cb, LV_EVENT_VALUE_CHANGED, var);
    if (var->x) {
        lv_obj_add_state(obj, LV_STATE_CHECKED);
    }
    return obj;
}

static lv_obj_t * switch_bool(lv_obj_t *parent, ParamInt &param) {
    lv_obj_t *obj = create_switch(parent);
    lv_obj_add_event_cb(obj, bool_update_subj_cb, LV_EVENT_VALUE_CHANGED, &param);

    if (param.get()) {
        lv_obj_add_state(obj, LV_STATE_CHECKED);
    }
    return obj;
}

static lv_obj_t * dropdown_uint8(lv_obj_t *parent, params_uint8_t *var, const char *options, lv_event_cb_t cb=uint8_dropdown_update_cb) {
    lv_obj_t *obj = lv_dropdown_create(parent);

    dialog_item(&dialog, obj);

    lv_obj_add_event_cb(obj, cb, LV_EVENT_VALUE_CHANGED, var);

    lv_obj_t *list = lv_dropdown_get_list(obj);
    styles_dropdown_list(list);

    lv_dropdown_set_options(obj, options);
    lv_dropdown_set_symbol(obj, NULL);

    lv_dropdown_set_selected(obj, var->x);

    return obj;
}

template <typename T>
lv_obj_t *slider_with_text(lv_obj_t *cell, T val, T min, T max, T step, size_t width, const char *fmt,
                           lv_event_cb_t event_cb, void *cb_user_data = nullptr) {
    lv_obj_t *obj;
    obj = lv_slider_create(cell);

    dialog_item(&dialog, obj);

    lv_slider_set_mode(obj, LV_SLIDER_MODE_NORMAL);
    lv_slider_set_range(obj, min / step, max / step);
    lv_slider_set_value(obj, val / step, LV_ANIM_OFF);
    lv_obj_set_width(obj, width);
    lv_obj_align(obj, LV_ALIGN_LEFT_MID, 0, 0);   /* in the middle of the box, as its value */

    /*Create a label below the slider*/
    lv_obj_t *slider_label = lv_label_create(cell);
    lv_obj_set_user_data(slider_label, (void *)fmt);
    lv_label_set_text_fmt(slider_label, fmt, val);
    lv_obj_align(slider_label, LV_ALIGN_RIGHT_MID, 12, 0);
    lv_obj_set_style_text_color(slider_label, TC(text), 0);

    lv_obj_set_user_data(obj, slider_label);

    lv_obj_add_event_cb(obj, event_cb, LV_EVENT_VALUE_CHANGED, cb_user_data);
    return obj;
}

/**
 * Update bg opacity for some UI elements to preview changes
 */
static void change_bg_opa_cb(lv_event_t *e) {
    lv_obj_t       *obj  = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);

    /* A theme's window image (RS) is the whole background: faded, the window turns see-through */
    if (lv_obj_get_style_bg_img_src(dialog.obj, 0)) {
        return;
    }

    lv_anim_t bg_fade_anim;
    lv_anim_init(&bg_fade_anim);
    lv_anim_set_exec_cb(&bg_fade_anim,
                        [](void *var, int32_t value) { lv_obj_set_style_bg_img_opa((lv_obj_t *)var, value, 0); });
    lv_anim_set_var(&bg_fade_anim, dialog.obj);
    lv_anim_set_time(&bg_fade_anim, 500);
    lv_opa_t cur_opa = lv_obj_get_style_bg_img_opa(dialog.obj, 0);

    if (((lv_obj_check_type(obj, &lv_slider_class) || lv_obj_check_type(obj, &lv_spinbox_class)) &&
         lv_obj_has_state(obj, LV_STATE_EDITED)) ||
        (lv_obj_check_type(obj, &lv_switch_class) && (code == LV_EVENT_FOCUSED))) {
        if (cur_opa != LV_OPA_60) {
            lv_anim_set_values(&bg_fade_anim, cur_opa, LV_OPA_60);
            lv_anim_start(&bg_fade_anim);
        }
    } else {
        if (cur_opa != LV_OPA_COVER) {
            lv_anim_set_values(&bg_fade_anim, cur_opa, LV_OPA_COVER);
            lv_anim_start(&bg_fade_anim);
        }
    }
}

/* Datetime */

static bool zone_refresh;   /* spinboxes being reloaded: not a user edit */

static void datetime_update_cb(lv_event_t * e) {
    if (zone_refresh) return;
    ts.tm_mday = lv_spinbox_get_value(day);
    ts.tm_mon = lv_spinbox_get_value(month) - 1;
    ts.tm_year = lv_spinbox_get_value(year) - 1900;
    ts.tm_hour = lv_spinbox_get_value(hour);
    ts.tm_min = lv_spinbox_get_value(min);
    ts.tm_sec = lv_spinbox_get_value(sec);

    datetime_changed = true;

    /* Set system */

    struct timespec tp;

    ts.tm_isdst = -1;
    tp.tv_sec = mktime(&ts);   /* fields are local time, system clock is UTC */
    tp.tv_nsec = 0;

    int res = clock_settime(CLOCK_REALTIME, &tp);
    if (res != 0)
    {
        LV_LOG_ERROR("Can't set system time: %s\n", strerror(errno));
        return;
    }
    gmtime_r(&tp.tv_sec, &ts_utc);
}

static void datetime_set_rtc_cb(lv_event_t * e) {
    lv_obj_t * obj = lv_event_get_target(e);
    if (!lv_obj_has_state(obj, LV_STATE_EDITED) && datetime_changed)
    {
        /* Set RTC */

        int rtc = open("/dev/rtc1", O_WRONLY);

        if (rtc > 0) {
            int res = ioctl(rtc, RTC_SET_TIME, &ts_utc);
            if (res != 0)
            {
                LV_LOG_ERROR("Can't set RTC time: %s\n", strerror(errno));
                return;
            }
            close(rtc);
        } else {
            LV_LOG_ERROR("Can't open /dev/rtc1: %s\n", strerror(errno));
        }
        datetime_changed = false;
    }

}

static uint8_t make_date(uint8_t row) {
    lv_obj_t    *obj;
    uint8_t     col = 0;

    /* Label */

    obj = lv_label_create(grid);

    lv_label_set_text(obj, "Date");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col++, 1, LV_GRID_ALIGN_CENTER, row, 1);

    /* Day */

    obj = lv_spinbox_create(grid);
    day = obj;

    dialog_item(&dialog, obj);

    lv_spinbox_set_value(obj, ts.tm_mday);
    lv_spinbox_set_range(obj, 1, 31);
    lv_spinbox_set_digit_format(obj, 2, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, 78, ROW_H);

    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 1, LV_GRID_ALIGN_CENTER, row, 1);   col++;
    lv_obj_add_event_cb(obj, datetime_update_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(obj, datetime_set_rtc_cb, LV_EVENT_FOCUSED, NULL);

    /* Month */

    obj = lv_spinbox_create(grid);
    month = obj;

    dialog_item(&dialog, obj);

    lv_spinbox_set_value(obj, ts.tm_mon + 1);
    lv_spinbox_set_range(obj, 1, 12);
    lv_spinbox_set_digit_format(obj, 2, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, 78, ROW_H);

    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 1, LV_GRID_ALIGN_CENTER, row, 1);   col++;
    lv_obj_add_event_cb(obj, datetime_update_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(obj, datetime_set_rtc_cb, LV_EVENT_FOCUSED, NULL);

    /* Year */

    obj = lv_spinbox_create(grid);
    year = obj;

    dialog_item(&dialog, obj);

    lv_spinbox_set_value(obj, ts.tm_year + 1900);
    lv_spinbox_set_range(obj, 2020, 2038);
    lv_spinbox_set_digit_format(obj, 4, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, 100, ROW_H);

    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 1, LV_GRID_ALIGN_CENTER, row, 1);   col++;
    lv_obj_add_event_cb(obj, datetime_update_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(obj, datetime_set_rtc_cb, LV_EVENT_FOCUSED, NULL);

    return row + 1;
}

static uint8_t make_time(uint8_t row) {
    lv_obj_t    *obj;
    uint8_t     col = 0;

    /* Label */

    obj = lv_label_create(grid);

    lv_label_set_text(obj, "Time");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col++, 1, LV_GRID_ALIGN_CENTER, row, 1);

    /* Hour */

    obj = lv_spinbox_create(grid);
    hour = obj;

    dialog_item(&dialog, obj);

    lv_spinbox_set_value(obj, ts.tm_hour);
    lv_spinbox_set_range(obj, 0, 23);
    lv_spinbox_set_digit_format(obj, 2, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, 78, ROW_H);

    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 1, LV_GRID_ALIGN_CENTER, row, 1);   col++;
    lv_obj_add_event_cb(obj, datetime_update_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(obj, datetime_set_rtc_cb, LV_EVENT_FOCUSED, NULL);

    /* Min */

    obj = lv_spinbox_create(grid);
    min = obj;

    dialog_item(&dialog, obj);

    lv_spinbox_set_value(obj, ts.tm_min);
    lv_spinbox_set_range(obj, 0, 59);
    lv_spinbox_set_digit_format(obj, 2, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, 78, ROW_H);

    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 1, LV_GRID_ALIGN_CENTER, row, 1);   col++;
    lv_obj_add_event_cb(obj, datetime_update_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(obj, datetime_set_rtc_cb, LV_EVENT_FOCUSED, NULL);

    /* Sec */

    obj = lv_spinbox_create(grid);
    sec = obj;

    dialog_item(&dialog, obj);

    lv_spinbox_set_value(obj, ts.tm_sec);
    lv_spinbox_set_range(obj, 0, 59);
    lv_spinbox_set_digit_format(obj, 2, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, 78, ROW_H);

    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 1, LV_GRID_ALIGN_CENTER, row, 1);   col++;
    lv_obj_add_event_cb(obj, datetime_update_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(obj, datetime_set_rtc_cb, LV_EVENT_FOCUSED, NULL);

    return row + 1;
}

/* Backlight */

static void backlight_timeout_update_cb(lv_event_t * e) {
    lv_obj_t *obj = lv_event_get_target(e);

    params_lock();
    params.brightness_timeout = lv_spinbox_get_value(obj);
    params_unlock(&params.dirty.brightness_timeout);

    backlight_tick();
}

static void backlight_brightness_update_cb(lv_event_t * e) {
    lv_obj_t *obj = lv_event_get_target(e);

    params_lock();
    params.brightness_normal = lv_slider_get_value(obj);
    params_unlock(&params.dirty.brightness_normal);

    params_lock();
    params.brightness_idle = lv_slider_get_left_value(obj);
    params_unlock(&params.dirty.brightness_idle);

    backlight_set_brightness(params.brightness_normal);
}

static void backlight_buttons_update_cb(lv_event_t * e) {
    lv_obj_t *obj = lv_event_get_target(e);

    backlight_set_buttons((buttons_light_t)lv_dropdown_get_selected(obj));
}

static uint8_t make_backlight(uint8_t row) {
    lv_obj_t    *obj;
    uint8_t     col = 0;

    /* Label */

    obj = lv_label_create(grid);

    lv_label_set_text(obj, "Timeout, Brightness");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col++, 1, LV_GRID_ALIGN_CENTER, row, 1);

    /* Timeout */

    obj = lv_spinbox_create(grid);

    dialog_item(&dialog, obj);

    lv_spinbox_set_value(obj, params.brightness_timeout);
    lv_spinbox_set_range(obj, 5, 120);
    lv_spinbox_set_digit_format(obj, 3, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, SMALL_2, ROW_H);

    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 2, LV_GRID_ALIGN_CENTER, row, 1);   col += 2;
    lv_obj_add_event_cb(obj, backlight_timeout_update_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Brightness */

    obj = lv_obj_create(grid);

    lv_obj_set_size(obj, SMALL_4, ROW_H);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 4, LV_GRID_ALIGN_CENTER, row, 1);   col += 4;
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(obj);

    obj = lv_slider_create(obj);

    dialog_item(&dialog, obj);

    lv_slider_set_mode(obj, LV_SLIDER_MODE_RANGE);
    lv_slider_set_value(obj, params.brightness_normal, LV_ANIM_OFF);
    lv_slider_set_left_value(obj, params.brightness_idle, LV_ANIM_OFF);
    lv_slider_set_range(obj, -1, 9);
    lv_obj_set_width(obj, SMALL_4 - 30);
    lv_obj_center(obj);

    lv_obj_add_event_cb(obj, backlight_brightness_update_cb, LV_EVENT_VALUE_CHANGED, NULL);

    row++;
    obj = lv_label_create(grid);

    lv_label_set_text(obj, "Buttons brightness");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

    obj = lv_dropdown_create(grid);

    dialog_item(&dialog, obj);

    lv_obj_set_size(obj, SMALL_6, ROW_H);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 6, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_center(obj);

    lv_obj_t *list = lv_dropdown_get_list(obj);
    styles_dropdown_list(list);

    lv_dropdown_set_options(obj, " Always Off \n Always On \n Temporarily On ");
    lv_dropdown_set_symbol(obj, NULL);
    lv_dropdown_set_selected(obj, params.brightness_buttons);
    lv_obj_add_event_cb(obj, backlight_buttons_update_cb, LV_EVENT_VALUE_CHANGED, NULL);

    return row + 1;
}

/* Line-in, Line-out */

static void line_in_out_update_cb(lv_event_t * e) {
    lv_obj_t *obj = lv_event_get_target(e);
    void (*fn)(uint8_t) = (void (*)(uint8_t))lv_event_get_user_data(e);
    lv_obj_t *slider_label = (lv_obj_t *)lv_obj_get_user_data(obj);
    char *fmt = (char *)lv_obj_get_user_data(slider_label);
    int32_t val = lv_slider_get_value(obj);
    fn(val);
    lv_label_set_text_fmt(slider_label, fmt, val);
}

static uint8_t make_line_gain(uint8_t row) {
    lv_obj_t    *obj;
    lv_obj_t    *cell;

    cell = lv_label_create(grid);

    lv_label_set_text(cell, "Line-in, Line-out");
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

    cell = lv_obj_create(grid);

    lv_obj_set_size(cell, SMALL_3, ROW_H);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 1, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(cell);

    slider_with_text(cell, (int)params.line_in, 0, 36, 1, SMALL_3 - 30 - 60, "%d", line_in_out_update_cb, (void*)radio_set_line_in);

    cell = lv_obj_create(grid);

    lv_obj_set_size(cell, SMALL_3, ROW_H);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 4, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(cell);

    slider_with_text(cell, (int)params.line_out, 0, 36, 1, SMALL_3 - 30 - 60, "%d", line_in_out_update_cb, (void*)radio_set_line_out);

    return row + 1;
}

/* Mag Freq, Info, ALC */

static uint8_t make_mag(uint8_t row) {
    lv_obj_t    *obj;
    uint8_t     col = 0;

    obj = lv_label_create(grid);

    lv_label_set_text(obj, "Popups Freq/Info/ALC");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col++, 1, LV_GRID_ALIGN_CENTER, row, 1);

    /* Freq */

    obj = lv_obj_create(grid);

    lv_obj_set_size(obj, SMALL_2, ROW_H);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 3, LV_GRID_ALIGN_CENTER, row, 1);   col += 2;
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(obj);

    obj = switch_bool(obj, &params.mag_freq);

    lv_obj_set_width(obj, SMALL_2 - 30);

    /* Info */

    obj = lv_obj_create(grid);

    lv_obj_set_size(obj, SMALL_2, ROW_H);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 3, LV_GRID_ALIGN_CENTER, row, 1);   col += 2;
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(obj);

    obj = switch_bool(obj, &params.mag_info);

    lv_obj_set_width(obj, SMALL_2 - 30);

    /* ALC */

    obj = lv_obj_create(grid);

    lv_obj_set_size(obj, SMALL_2, ROW_H);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 3, LV_GRID_ALIGN_CENTER, row, 1);   col += 2;
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(obj);

    obj = switch_bool(obj, &params.mag_alc);

    lv_obj_set_width(obj, SMALL_2 - 30);

    return row + 1;
}

/* Long press actions */

typedef struct {
    const char                *label;
    press_action_t      action;
} action_items_t;

static action_items_t long_action_items[] = {
    { .label = " None ", .action = ACTION_NONE },
    { .label = " Screenshot ", .action = ACTION_SCREENSHOT },
    { .label = " RX recorder on/off ", .action = ACTION_RECORDER },
    { .label = " Screen rec on/off ", .action = ACTION_SCREEN_REC },
    { .label = " Mute ", .action = ACTION_MUTE },
    { .label = " WF mode ", .action = ACTION_WF_MODE },
    { .label = " ATT on/off ", .action = ACTION_ATT_TOGGLE },
    { .label = " PRE on/off ", .action = ACTION_PRE_TOGGLE },
    { .label = " Tuner on/off ", .action = ACTION_ATU_TOGGLE },
    { .label = " Tuner: tune ", .action = ACTION_ATU_TUNE },
    /* Digital modes, in the order of the Modes menu */
    { .label = " APP RTTY ", .action = ACTION_APP_RTTY },
    { .label = " APP FT8 ", .action = ACTION_APP_FT8 },
    { .label = " APP WSPR ", .action = ACTION_APP_WSPR },
    { .label = " APP JS8 ", .action = ACTION_APP_JS8 },
    { .label = " APP SSTV ", .action = ACTION_APP_SSTV },
    { .label = " APP WeFax ", .action = ACTION_APP_WEFAX },
    { .label = " APP NavTex ", .action = ACTION_APP_NAVTEX },
    /* Tools */
    { .label = " APP Cluster ", .action = ACTION_APP_CLUSTER },
    { .label = " APP CW Trainer ", .action = ACTION_APP_CWTRAINER },
    { .label = " APP Field Ops ", .action = ACTION_APP_FIELD },
    { .label = " APP Bands ", .action = ACTION_APP_BANDS },
    { .label = " APP RX recorder ", .action = ACTION_APP_RECORDER },
    { .label = " APP SWR Scan ", .action = ACTION_APP_SWRSCAN },
    { .label = " APP GPS ", .action = ACTION_APP_GPS },
    { .label = " APP QTH grid ", .action = ACTION_APP_QTH },
    { .label = " APP Settings ", .action = ACTION_APP_SETTINGS },
    { .label = NULL, .action = ACTION_NONE }
};

static void long_action_update_cb(lv_event_t * e) {
    lv_obj_t    *obj = lv_event_get_target(e);
    uint32_t    *i = (uint32_t*)lv_event_get_user_data(e);
    uint8_t     val = long_action_items[lv_dropdown_get_selected(obj)].action;

    params_lock();

    switch (*i) {
        case 0:
            params.long_gen = val;
            params_unlock(&params.dirty.long_gen);
            break;

        case 1:
            params.long_app = val;
            params_unlock(&params.dirty.long_app);
            break;

        case 2:
            params.long_key = val;
            params_unlock(&params.dirty.long_key);
            break;

        case 3:
            params.long_msg = val;
            params_unlock(&params.dirty.long_msg);
            break;

        case 4:
            params.long_dfn = val;
            params_unlock(&params.dirty.long_dfn);
            break;

        case 5:
            params.long_dfl = val;
            params_unlock(&params.dirty.long_dfl);
            break;
    }
}

static uint8_t make_long_action(uint8_t row) {
    const char        *labels[] = {
        "GEN",
        "APP",
        "KEY",
        "MSG",
        "DFN",
        "DFL",
    };
    lv_obj_t    *obj;

    for (uint8_t i = 0; i < 6; i++) {

        obj = lv_label_create(grid);

        lv_label_set_text(obj, labels[i]);
        lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

        obj = lv_dropdown_create(grid);

        dialog_item(&dialog, obj);

        lv_obj_set_size(obj, SMALL_6, ROW_H);
        lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 6, LV_GRID_ALIGN_CENTER, row, 1);
        lv_obj_center(obj);

        lv_obj_t *list = lv_dropdown_get_list(obj);
        styles_dropdown_list(list);

        lv_dropdown_set_symbol(obj, NULL);

        uint8_t x;

        switch (i) {
            case 0: x = params.long_gen;    break;
            case 1: x = params.long_app;    break;
            case 2: x = params.long_key;    break;
            case 3: x = params.long_msg;    break;
            case 4: x = params.long_dfn;    break;
            case 5: x = params.long_dfl;    break;

            default:
                x = ACTION_NONE;
                break;
        }

        lv_dropdown_clear_options(obj);

        uint8_t n = 0;

        while (long_action_items[n].label) {
            lv_dropdown_add_option(obj, long_action_items[n].label, LV_DROPDOWN_POS_LAST);

            if (long_action_items[n].action == x) {
                lv_dropdown_set_selected(obj, n);
            }

            n++;
        }

        uint32_t *param = (uint32_t*)malloc(sizeof(uint32_t));
        *param = i;

        lv_obj_add_event_cb(obj, long_action_update_cb, LV_EVENT_VALUE_CHANGED, param);

        row++;
    }

    return row;
}

/* HMic F1, F2 actions */

static action_items_t hmic_action_items[] = {
    { .label = " None ", .action = ACTION_NONE },
    { .label = " RX recorder on/off ", .action = ACTION_RECORDER },
    { .label = " Screen rec on/off ", .action = ACTION_SCREEN_REC },
    { .label = " Mute ", .action = ACTION_MUTE },
    { .label = " Step up ", .action = ACTION_STEP_UP },
    { .label = " Step down ", .action = ACTION_STEP_DOWN },
    { .label = " NR toggle ", .action = ACTION_NR_TOGGLE },
    { .label = " NB toggle ", .action = ACTION_NB_TOGGLE },
    { .label = " WF mode ", .action = ACTION_WF_MODE },
    { .label = " ATT on/off ", .action = ACTION_ATT_TOGGLE },
    { .label = " PRE on/off ", .action = ACTION_PRE_TOGGLE },
    { .label = " Tuner on/off ", .action = ACTION_ATU_TOGGLE },
    { .label = " Tuner: tune ", .action = ACTION_ATU_TUNE },
    { .label = NULL, .action = ACTION_NONE }
};

static void hmic_action_update_cb(lv_event_t * e) {
    lv_obj_t    *obj = lv_event_get_target(e);
    uint8_t     *i = (uint8_t*)lv_event_get_user_data(e);
    uint8_t     val = hmic_action_items[lv_dropdown_get_selected(obj)].action;

    params_lock();

    switch (*i) {
        case 0:
            params.press_f1 = val;
            params_unlock(&params.dirty.press_f1);
            break;

        case 1:
            params.press_f2 = val;
            params_unlock(&params.dirty.press_f2);
            break;

        case 2:
            params.long_f1 = val;
            params_unlock(&params.dirty.long_f1);
            break;

        case 3:
            params.long_f2 = val;
            params_unlock(&params.dirty.long_f2);
            break;
    }
}

static uint8_t make_hmic_action(uint8_t row) {
    struct {const char *label; uint8_t param;} items[] = {
        {"F1", params.press_f1},
        {"F2", params.press_f2},
        {"F1 long", params.long_f1},
        {"F2 long", params.long_f2}
    };
    size_t items_len = sizeof(items) / sizeof(items[0]);
    lv_obj_t    *obj;

    for (uint8_t i = 0; i < items_len; i++) {

        obj = lv_label_create(grid);

        lv_label_set_text(obj, items[i].label);
        lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

        obj = lv_dropdown_create(grid);

        dialog_item(&dialog, obj);

        lv_obj_set_size(obj, SMALL_6, ROW_H);
        lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 6, LV_GRID_ALIGN_CENTER, row, 1);
        lv_obj_center(obj);

        lv_obj_t *list = lv_dropdown_get_list(obj);
        styles_dropdown_list(list);

        lv_dropdown_set_symbol(obj, NULL);

        lv_dropdown_clear_options(obj);

        uint8_t n = 0;

        while (hmic_action_items[n].label) {
            lv_dropdown_add_option(obj, hmic_action_items[n].label, LV_DROPDOWN_POS_LAST);

            if (hmic_action_items[n].action == items[i].param) {
                lv_dropdown_set_selected(obj, n);
            }

            n++;
        }

        uint8_t *param = (uint8_t*)malloc(sizeof(uint8_t));
        *param = i;

        lv_obj_add_event_cb(obj, hmic_action_update_cb, LV_EVENT_VALUE_CHANGED, param);

        row++;
    }

    return row;
}

/* Play,Rec gain */

static void play_gain_update_cb(lv_event_t * e) {
    lv_obj_t *obj = lv_event_get_target(e);
    float val = audio_set_play_vol(lv_slider_get_value(obj));

    params_float_set(&params.play_gain_db_f, val);

    lv_obj_t *slider_label = (lv_obj_t *)lv_obj_get_user_data(obj);
    char *fmt = (char *)lv_obj_get_user_data(slider_label);
    radio_set_line_in(val);
    lv_label_set_text_fmt(slider_label, fmt, val);
}

static void rec_gain_update_cb(lv_event_t * e) {
    lv_obj_t *obj = lv_event_get_target(e);
    float val = audio_set_rec_vol(lv_slider_get_value(obj));

    params_float_set(&params.rec_gain_db_f, val);
    lv_obj_t *slider_label = (lv_obj_t *)lv_obj_get_user_data(obj);
    char *fmt = (char *)lv_obj_get_user_data(slider_label);
    radio_set_line_in(val);
    lv_label_set_text_fmt(slider_label, fmt, val);
}

static uint8_t make_audio_gain(uint8_t row) {
    lv_obj_t    *obj;
    lv_obj_t    *cell;

    cell = lv_label_create(grid);

    lv_label_set_text(cell, "Play, Rec gain");
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

    cell = lv_obj_create(grid);

    lv_obj_set_size(cell, SMALL_3, ROW_H);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 1, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(cell);

    slider_with_text(cell, params.play_gain_db_f.x, -10.0f, 10.0f, 1.0f, SMALL_3 - 30 - 65, "%.1f", play_gain_update_cb);

    cell = lv_obj_create(grid);

    lv_obj_set_size(cell, SMALL_3, ROW_H);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 4, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(cell);

    slider_with_text(cell, params.rec_gain_db_f.x, -10.0f, 10.0f, 1.0f, SMALL_3 - 30 - 65, "%.1f", rec_gain_update_cb);

    return row + 1;
}

/* Transverter */

static void transverter_update_cb(lv_event_t * e) {
    lv_obj_t        *obj = lv_event_get_target(e);
    ParamInt        *param = (ParamInt*)lv_event_get_user_data(e);

    param->set(lv_spinbox_get_value(obj) * 1000000L);
}

static uint8_t make_transverter(uint8_t row, ParamInt &from, ParamInt &to, ParamInt &shift) {
    lv_obj_t        *obj;
    uint8_t         col = 0;

    /* Label */

    obj = lv_label_create(grid);

    lv_label_set_text_fmt(obj, "%i: from, to, shift", from.context_id() + 1);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col++, 1, LV_GRID_ALIGN_CENTER, row, 1);

    /* From */

    obj = lv_spinbox_create(grid);

    dialog_item(&dialog, obj);

    lv_spinbox_set_value(obj, from.get() / 1000000L);
    lv_spinbox_set_range(obj, 70, 500);
    lv_spinbox_set_digit_format(obj, 3, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, SMALL_2, ROW_H);

    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 2, LV_GRID_ALIGN_CENTER, row, 1);   col += 2;
    lv_obj_add_event_cb(obj, transverter_update_cb, LV_EVENT_VALUE_CHANGED, &from);

    /* To */

    obj = lv_spinbox_create(grid);

    dialog_item(&dialog, obj);

    lv_spinbox_set_value(obj, to.get() / 1000000L);
    lv_spinbox_set_range(obj, 70, 500);
    lv_spinbox_set_digit_format(obj, 3, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, SMALL_2, ROW_H);

    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 2, LV_GRID_ALIGN_CENTER, row, 1);   col += 2;
    lv_obj_add_event_cb(obj, transverter_update_cb, LV_EVENT_VALUE_CHANGED, &to);

    /* Shift */

    obj = lv_spinbox_create(grid);

    dialog_item(&dialog, obj);

    lv_spinbox_set_value(obj, shift.get() / 1000000L);
    lv_spinbox_set_range(obj, 42, 500);
    lv_spinbox_set_digit_format(obj,3, 0);
    lv_spinbox_set_digit_step_direction(obj, LV_DIR_LEFT);
    lv_obj_set_size(obj, SMALL_2, ROW_H);

    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 2, LV_GRID_ALIGN_CENTER, row, 1);   col += 2;
    lv_obj_add_event_cb(obj, transverter_update_cb, LV_EVENT_VALUE_CHANGED, &shift);

    return row + 1;
}

/* Spectrum and waterfall auto */

#define AUTO_LEVEL_STEP 0.2f

/* Spectrum above the waterfall on/off; the waterfall takes its place */

/* The main screen follows it live (main_screen.c); the page again, with or without the spectrum
 * rows, after the event: the switch that got it is deleted */
static lv_obj_t *spectrum_show_sw;

static void spectrum_show_page(void *arg) {
    if (!page) {
        return;
    }
    make_display_page();
    lv_group_focus_obj(spectrum_show_sw);
}

static void spectrum_show_cb(lv_event_t *e) {
    lv_async_call(spectrum_show_page, NULL);
}

static uint8_t make_spectrum_show(uint8_t row) {
    lv_obj_t *obj;

    obj = lv_label_create(grid);
    lv_label_set_text(obj, "Spectrum");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

    obj = lv_obj_create(grid);
    lv_obj_set_size(obj, SMALL_3, ROW_H);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(obj);
    obj = switch_bool(obj, cfg_sm.p_spectrum_show);
    spectrum_show_sw = obj;
    lv_obj_add_event_cb(obj, spectrum_show_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_width(obj, SMALL_3 - 30);

    return row + 1;
}

/* Walkie Talkie portrait screen: on at once (walkie.c follows the setting), Settings closes */
static uint8_t make_walkie(uint8_t row) {
    lv_obj_t *obj;

    obj = lv_label_create(grid);
    lv_label_set_text(obj, "Walkie Talkie");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

    obj = lv_obj_create(grid);
    lv_obj_set_size(obj, SMALL_3, ROW_H);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(obj);
    obj = switch_bool(obj, cfg_sm.p_walkie);
    lv_obj_set_width(obj, SMALL_3 - 30);

    return row + 1;
}

/* Spectrum fill, peaks */
static uint8_t make_spectrum_fill_peak(uint8_t row) {
    lv_obj_t *obj;

    obj = lv_label_create(grid);
    lv_label_set_text(obj, "Fill, peaks");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

    for (int i = 0; i < 2; i++) {
        obj = lv_obj_create(grid);
        lv_obj_set_size(obj, SMALL_3, ROW_H);
        lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, i ? 4 : 1, 3, LV_GRID_ALIGN_CENTER, row, 1);
        lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_center(obj);
        obj = switch_bool(obj, i ? &params.spectrum_peak : &params.spectrum_filled);
        lv_obj_add_event_cb(obj, change_bg_opa_cb, LV_EVENT_FOCUSED, NULL);
        lv_obj_add_event_cb(obj, change_bg_opa_cb, LV_EVENT_DEFOCUSED, NULL);
        lv_obj_set_width(obj, SMALL_3 - 30);
    }
    return row + 1;
}

static void birdie_filter_update_cb(lv_event_t *e) {
    radio_update_birdie_notch();
}

static uint8_t make_birdie_filter(uint8_t row) {
    lv_obj_t *obj = lv_label_create(grid);
    lv_label_set_text(obj, "Birdie filter");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

    obj = lv_obj_create(grid);
    lv_obj_set_size(obj, SMALL_3, ROW_H);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 4, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(obj);

    obj = switch_bool(obj, &params.birdie_filter);
    lv_obj_add_event_cb(obj, birdie_filter_update_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(obj, change_bg_opa_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(obj, change_bg_opa_cb, LV_EVENT_DEFOCUSED, NULL);
    lv_obj_set_width(obj, SMALL_3 - 30);
    return row + 1;
}

static uint8_t make_cluster_spots_wf(uint8_t row) {
    lv_obj_t *obj;

    obj = lv_label_create(grid);
    lv_label_set_text(obj, "Cluster spots");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

    obj = lv_obj_create(grid);
    lv_obj_set_size(obj, SMALL_3, ROW_H);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(obj);
    obj = switch_bool(obj, cfg_sm.p_cluster_spots_wf);
    lv_obj_set_width(obj, SMALL_3 - 30);

    return row + 1;
}

static uint8_t make_band_plan(uint8_t row) {
    lv_obj_t *obj;

    obj = lv_label_create(grid);
    lv_label_set_text(obj, "Band plan");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

    obj = lv_obj_create(grid);
    lv_obj_set_size(obj, SMALL_3, ROW_H);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(obj);
    obj = switch_bool(obj, cfg_sm.p_band_plan);
    lv_obj_set_width(obj, SMALL_3 - 30);

    return row + 1;
}

/* A label and an on/off switch for a 0/1 parameter */
static uint8_t make_switch_row(uint8_t row, const char *text, ParamInt &param) {
    lv_obj_t *obj;

    obj = lv_label_create(grid);
    lv_label_set_text(obj, text);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

    obj = lv_obj_create(grid);
    lv_obj_set_size(obj, SMALL_3, ROW_H);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(obj);
    obj = switch_bool(obj, param);
    lv_obj_set_width(obj, SMALL_3 - 30);

    return row + 1;
}

static void smeter_type_update_cb(lv_event_t *e) {
    cfg_sm.p_smeter_needle.set(lv_dropdown_get_selected(lv_event_get_target(e)));
}

/* Bars, or a needle on a dial */
static uint8_t make_smeter_needle(uint8_t row) {
    lv_obj_t *obj;

    obj = lv_label_create(grid);
    lv_label_set_text(obj, "S-meter type");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

    obj = lv_dropdown_create(grid);
    dialog_item(&dialog, obj);
    styles_dropdown_list(lv_dropdown_get_list(obj));
    lv_dropdown_set_options(obj, " Bars \n Needle ");
    lv_dropdown_set_symbol(obj, NULL);
    lv_dropdown_set_selected(obj, cfg_sm.p_smeter_needle.get());
    lv_obj_add_event_cb(obj, smeter_type_update_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_size(obj, SMALL_3, ROW_H);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 3, LV_GRID_ALIGN_CENTER, row, 1);

    return row + 1;
}

static void smeter_color_update_cb(lv_event_t *e) {
    cfg_sm.p_smeter_color.set(lv_dropdown_get_selected(lv_event_get_target(e)));
}

/* Theme: as the theme draws it. Scale: green, yellow, red along the scale. Level: the lit bars all
 * in the colour of the signal */
static uint8_t make_smeter_color(uint8_t row) {
    lv_obj_t *obj;

    obj = lv_label_create(grid);
    lv_label_set_text(obj, "S-meter colours");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

    obj = lv_dropdown_create(grid);
    dialog_item(&dialog, obj);
    styles_dropdown_list(lv_dropdown_get_list(obj));
    lv_dropdown_set_options(obj, " Theme \n Scale \n Level ");
    lv_dropdown_set_symbol(obj, NULL);
    lv_dropdown_set_selected(obj, cfg_sm.p_smeter_color.get());
    lv_obj_add_event_cb(obj, smeter_color_update_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_size(obj, SMALL_3, ROW_H);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 3, LV_GRID_ALIGN_CENTER, row, 1);

    return row + 1;
}

static void digi_text_update_cb(lv_event_t *e) {
    cfg_sm.p_digi_text_compact.set(lv_dropdown_get_selected(lv_event_get_target(e)));
}

/* CW / RTTY decoder text: Large or Compact (more lines), from SP2KMO's upstream c2fbe40c */
static uint8_t make_digi_text(uint8_t row) {
    lv_obj_t *obj;

    obj = lv_label_create(grid);
    lv_label_set_text(obj, "Decoder text");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

    obj = lv_dropdown_create(grid);
    dialog_item(&dialog, obj);
    styles_dropdown_list(lv_dropdown_get_list(obj));
    lv_dropdown_set_options(obj, " Large \n Compact ");
    lv_dropdown_set_symbol(obj, NULL);
    lv_dropdown_set_selected(obj, cfg_sm.p_digi_text_compact.get());
    lv_obj_add_event_cb(obj, digi_text_update_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_size(obj, SMALL_3, ROW_H);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 3, LV_GRID_ALIGN_CENTER, row, 1);

    return row + 1;
}

static void ft8_info_update_cb(lv_event_t *e) {
    cfg_sm.p_ft8_info.set(lv_dropdown_get_selected(lv_event_get_target(e)));
}

/* FT8/FT4 table: distance, country or both, from SP2KMO's upstream 3074180e */
static uint8_t make_ft8_info(uint8_t row) {
    lv_obj_t *obj;

    obj = lv_label_create(grid);
    lv_label_set_text(obj, "FT8/FT4 info");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

    obj = lv_dropdown_create(grid);
    dialog_item(&dialog, obj);
    styles_dropdown_list(lv_dropdown_get_list(obj));
    lv_dropdown_set_options(obj, " km \n mi \n Country \n Country + km \n Country + mi ");
    lv_dropdown_set_symbol(obj, NULL);
    lv_dropdown_set_selected(obj, cfg_sm.p_ft8_info.get());
    lv_obj_add_event_cb(obj, ft8_info_update_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_size(obj, SMALL_3, ROW_H);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 3, LV_GRID_ALIGN_CENTER, row, 1);

    return row + 1;
}

static uint8_t make_cluster_column(uint8_t row) {
    return make_switch_row(row, "Cluster column", cfg_sm.p_cluster_column);
}

static void auto_level_offset_update_cb(lv_event_t * e) {
    lv_obj_t *obj = lv_event_get_target(e);
    float val = lv_slider_get_value(obj) * AUTO_LEVEL_STEP;

    lv_obj_t *slider_label = (lv_obj_t *)lv_obj_get_user_data(obj);
    char *fmt = (char *)lv_obj_get_user_data(slider_label);
    lv_label_set_text_fmt(slider_label, fmt, val);
    cfg_sm.p_auto_level_offset.set(val);
}

static uint8_t make_auto_offset(uint8_t row) {
    lv_obj_t *obj;

    obj = lv_label_create(grid);

    lv_label_set_text(obj, "Auto level, offset");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

    /* On/off */
    obj = lv_obj_create(grid);
    lv_obj_set_size(obj, SMALL_3, ROW_H);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(obj);
    obj = switch_bool(obj, cfg_sm.p_auto_level_enabled);
    lv_obj_set_width(obj, SMALL_3 - 30);

    /* Offset */
    obj = lv_obj_create(grid);
    lv_obj_set_size(obj, SMALL_3, ROW_H);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 4, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(obj);
    obj = slider_with_text(obj, cfg_sm.p_auto_level_offset.get(), -15.0f, 15.0f, AUTO_LEVEL_STEP,
                           SMALL_3 - 120, "%0.1f", auto_level_offset_update_cb);
    lv_obj_add_event_cb(obj, change_bg_opa_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(obj, change_bg_opa_cb, LV_EVENT_DEFOCUSED, NULL);

    return row + 1;
}

/* Spectrum  min, max (when no auto)*/

static void grid_min_max_update_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    int       val = lv_slider_get_value(obj);

    lv_obj_t *slider_label = (lv_obj_t *)lv_obj_get_user_data(obj);
    char     *fmt          = (char *)lv_obj_get_user_data(slider_label);
    lv_label_set_text_fmt(slider_label, fmt, val);
    auto param = static_cast<ParamInt*>(lv_event_get_user_data(e));
    param->set(val);
}

uint8_t make_spectrum_min_max(uint8_t row) {
    lv_obj_t *obj;
    lv_obj_t *cell;

    cell = lv_label_create(grid);

    lv_label_set_text(cell, "Manual min, max");
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

    cell = lv_obj_create(grid);

    lv_obj_set_size(cell, SMALL_3, ROW_H);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 1, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(cell);

    obj = slider_with_text(cell, cfg_sm.p_band_grid_min.get(), S_MIN, S7, 1, SMALL_3 - 120, "%d",
                           grid_min_max_update_cb, (void*)&cfg_sm.p_band_grid_min);

    lv_obj_add_event_cb(obj, change_bg_opa_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(obj, change_bg_opa_cb, LV_EVENT_DEFOCUSED, NULL);

    cell = lv_obj_create(grid);

    lv_obj_set_size(cell, SMALL_3, ROW_H);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 4, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(cell);

    obj = slider_with_text(cell, cfg_sm.p_band_grid_max.get(), S8, S9_40, 1, SMALL_3 - 120, "%d",
                           grid_min_max_update_cb, (void*)&cfg_sm.p_band_grid_max);

    lv_obj_add_event_cb(obj, change_bg_opa_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(obj, change_bg_opa_cb, LV_EVENT_DEFOCUSED, NULL);

    return row + 1;
}

/* Waterfall center line and zoom */

static uint8_t make_waterfall_line_zoom(uint8_t row) {
    lv_obj_t    *obj;
    uint8_t     col = 0;

    obj = lv_label_create(grid);

    lv_label_set_text(obj, "Line, zoom");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col++, 1, LV_GRID_ALIGN_CENTER, row, 1);

    obj = lv_obj_create(grid);

    lv_obj_set_size(obj, SMALL_3, ROW_H);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(obj);

    obj = switch_bool(obj, &params.waterfall_center_line);
    lv_obj_add_event_cb(obj, change_bg_opa_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(obj, change_bg_opa_cb, LV_EVENT_DEFOCUSED, NULL);
    lv_obj_set_width(obj, SMALL_3 - 30);

    obj = lv_obj_create(grid);

    lv_obj_set_size(obj, SMALL_3, ROW_H);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 4, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(obj);

    obj = switch_bool(obj, &params.waterfall_zoom);

    lv_obj_set_width(obj, SMALL_3 - 30);

    return row + 1;
}

static void wf_mode_update_cb(lv_event_t *e) {
    cfg_sm.p_wf_mode.set(lv_dropdown_get_selected(lv_event_get_target(e)));
}

/* Centered: the VFO stays in the middle. Fixed: the span stays put, the VFO line moves */
static uint8_t make_waterfall_mode(uint8_t row) {
    lv_obj_t *obj;

    obj = lv_label_create(grid);
    lv_label_set_text(obj, "Mode");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

    obj = lv_dropdown_create(grid);
    dialog_item(&dialog, obj);
    styles_dropdown_list(lv_dropdown_get_list(obj));
    lv_dropdown_set_options(obj, " Centered \n Fixed ");
    lv_dropdown_set_symbol(obj, NULL);
    lv_dropdown_set_selected(obj, cfg_sm.p_wf_mode.get());
    lv_obj_add_event_cb(obj, wf_mode_update_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_size(obj, SMALL_3, ROW_H);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 3, LV_GRID_ALIGN_CENTER, row, 1);

    return row + 1;
}

static uint8_t make_waterfall_smooth_scroll(uint8_t row) {
    lv_obj_t    *obj;
    uint8_t     col = 0;

    obj = lv_label_create(grid);

    lv_label_set_text(obj, "Smooth scroll");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col++, 1, LV_GRID_ALIGN_CENTER, row, 1);

    obj = lv_obj_create(grid);

    lv_obj_set_size(obj, SMALL_3, ROW_H);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 4, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(obj);

    obj = switch_bool(obj, &params.waterfall_smooth_scroll);

    lv_obj_set_width(obj, SMALL_3 - 30);

    return row + 1;
}

/* A message at start when x6100-link.it has a newer release (update_check.c) */
static uint8_t make_update_alert(uint8_t row) {
    lv_obj_t *obj = lv_label_create(grid);

    lv_label_set_text(obj, "Update alert");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

    obj = lv_obj_create(grid);
    lv_obj_set_size(obj, SMALL_3, ROW_H);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 4, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(obj);
    obj = switch_bool(obj, &params.update_alert);
    lv_obj_set_width(obj, SMALL_3 - 30);
    return row + 1;
}

static uint8_t make_knob_info(uint8_t row) {
    lv_obj_t    *obj;
    uint8_t     col = 0;

    obj = lv_label_create(grid);

    lv_label_set_text(obj, "Knob info");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col++, 1, LV_GRID_ALIGN_CENTER, row, 1);

    obj = lv_obj_create(grid);

    lv_obj_set_size(obj, SMALL_3, ROW_H);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 4, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(obj);

    obj = switch_bool(obj, cfg_sm.p_knob_info);

    lv_obj_set_width(obj, SMALL_3 - 30);

    return row + 1;
}

static void sp_mode_update_cb(lv_event_t * e) {
    lv_obj_t *obj = lv_event_get_target(e);

    if (lv_obj_has_state(obj, LV_STATE_CHECKED)) {
        radio_change_spmode(-1);
    } else {
        radio_change_spmode(1);
    }
}

static uint8_t make_sp_mode(uint8_t row) {
    lv_obj_t    *obj;
    uint8_t     col = 0;

    obj = lv_label_create(grid);

    lv_label_set_text(obj, "Speaker mode");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col++, 1, LV_GRID_ALIGN_CENTER, row, 1);

    obj = lv_obj_create(grid);

    lv_obj_set_size(obj, SMALL_3, ROW_H);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 4, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(obj);

    obj = lv_switch_create(obj);

    dialog_item(&dialog, obj);

    lv_obj_center(obj);
    lv_obj_add_event_cb(obj, sp_mode_update_cb, LV_EVENT_VALUE_CHANGED, NULL);

    if (!params.spmode.x) {
        lv_obj_add_state(obj, LV_STATE_CHECKED);
    }

    lv_obj_set_width(obj, SMALL_3 - 30);

    return row + 1;
}

/* Compressor threshold, makeup */
#define COMP_TH_MAKEUP_STEP 0.5f

static void comp_update_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    float val = lv_slider_get_value(obj) * COMP_TH_MAKEUP_STEP;

    lv_obj_t *slider_label = (lv_obj_t *)lv_obj_get_user_data(obj);
    char *fmt = (char *)lv_obj_get_user_data(slider_label);
    lv_label_set_text_fmt(slider_label, fmt, val);

    ParamFloat *param = (ParamFloat *)lv_event_get_user_data(e);
    param->set(val);
}


static uint8_t make_comp_th_makeup(uint8_t row) {
    lv_obj_t    *obj;
    lv_obj_t    *cell;

    cell = lv_label_create(grid);

    lv_label_set_text(cell, "Compressor, makeup");
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

    cell = lv_obj_create(grid);

    lv_obj_set_size(cell, SMALL_3, ROW_H);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 1, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(cell);

    slider_with_text(cell, cfg_sm.p_comp_threshold_offset.get(),
        -15.0f, 15.0f, COMP_TH_MAKEUP_STEP,
        SMALL_3 - 120, "%0.1f", comp_update_cb,
        (void *)&cfg_sm.p_comp_threshold_offset);

    cell = lv_obj_create(grid);

    lv_obj_set_size(cell, SMALL_3, ROW_H);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 4, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(cell);

    slider_with_text(cell, cfg_sm.p_comp_makeup_offset.get(),
        -15.0f, 15.0f, COMP_TH_MAKEUP_STEP,
        SMALL_3 - 120, "%0.1f", comp_update_cb,
        (void *)&cfg_sm.p_comp_makeup_offset);

    return row + 1;
}

/* TX offset */
#define TX_OFFSET_SCALE 50

static void tx_offset_update_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    int32_t val = lv_slider_get_value(obj);

    lv_obj_t *slider_label = (lv_obj_t *)lv_obj_get_user_data(obj);
    char *fmt = (char *)lv_obj_get_user_data(slider_label);
    lv_label_set_text_fmt(slider_label, fmt, val);

    ParamInt *param = (ParamInt *)lv_event_get_user_data(e);
    param->set(val * TX_OFFSET_SCALE);
}

static void on_tx_offset_change(Subject *subj, void *user_data) {
    lv_obj_t *slider = (lv_obj_t *)user_data;
    auto *subj_t = static_cast<SubjectInt *>(subj);
    lv_slider_set_value(slider, subj_t->get() / TX_OFFSET_SCALE, LV_ANIM_OFF);
    lv_event_send(slider, LV_EVENT_VALUE_CHANGED, NULL);
}

static uint8_t make_tx_offset(uint8_t row) {
    lv_obj_t    *obj;
    lv_obj_t    *cell;
    lv_obj_t    *slider;
    Observer    *observer;

    cell = lv_label_create(grid);

    lv_label_set_text(cell, "IQ offsets");
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

    cell = lv_obj_create(grid);

    lv_obj_set_size(cell, SMALL_3, ROW_H);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 1, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(cell);

    slider = slider_with_text(cell, cfg_sm.p_band_tx_i_offset.get() / TX_OFFSET_SCALE,
        -10000 / TX_OFFSET_SCALE, 10000 / TX_OFFSET_SCALE, 1,
        SMALL_3 - 110, "%d", tx_offset_update_cb,
        (void *)&cfg_sm.p_band_tx_i_offset);

    observer = cfg_sm.p_band_tx_i_offset.subscribe(on_tx_offset_change, slider);
    observers.emplace_back(observer);

    cell = lv_obj_create(grid);

    lv_obj_set_size(cell, SMALL_3, ROW_H);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 4, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(cell);

    slider = slider_with_text(cell, cfg_sm.p_band_tx_q_offset.get() / TX_OFFSET_SCALE,
        -10000 / TX_OFFSET_SCALE, 10000 / TX_OFFSET_SCALE, 1,
        SMALL_3 - 110, "%d", tx_offset_update_cb,
        (void *)&cfg_sm.p_band_tx_q_offset);

    observer = cfg_sm.p_band_tx_q_offset.subscribe(on_tx_offset_change, slider);
    observers.emplace_back(observer);

    return row + 1;
}

/* TX codec gain */
#define OUTPUT_GAIN_STEP 0.2f

static void codec_gain_update_cb(lv_event_t * e) {
    lv_obj_t *obj = lv_event_get_target(e);
    float val = (float)lv_slider_get_value(obj) * OUTPUT_GAIN_STEP;

    lv_obj_t *slider_label = (lv_obj_t *)lv_obj_get_user_data(obj);
    char *fmt = (char *)lv_obj_get_user_data(slider_label);
    lv_label_set_text_fmt(slider_label, fmt, val);
    cfg_sm.p_output_gain.set(val);
}

static uint8_t make_codec_gain(uint8_t row) {
    lv_obj_t    *obj;
    lv_obj_t    *cell;

    cell = lv_label_create(grid);

    lv_label_set_text(cell, "Codec gain");
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

    cell = lv_obj_create(grid);

    lv_obj_set_size(cell, SMALL_6, ROW_H);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 1, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(cell);

    slider_with_text(cell, cfg_sm.p_output_gain.get(),
        -25.0f, 25.0f, OUTPUT_GAIN_STEP,
        SMALL_6 - 120, "%0.1f", codec_gain_update_cb);

    return row + 1;
}

/* TX codec DAC gain */

static void band_out_gain_update_cb(lv_event_t * e) {
    lv_obj_t *obj = lv_event_get_target(e);
    float val = (float)lv_slider_get_value(obj) * OUTPUT_GAIN_STEP;

    lv_obj_t *slider_label = (lv_obj_t *)lv_obj_get_user_data(obj);
    char *fmt = (char *)lv_obj_get_user_data(slider_label);
    lv_label_set_text_fmt(slider_label, fmt, val);
    cfg_sm.p_band_dac_offset.set(val);
}

static void on_dac_gain_change(Subject *subj, void *user_data) {
    lv_obj_t *slider = (lv_obj_t*)user_data;
    lv_slider_set_value(slider, cfg_sm.p_band_dac_offset.get() / OUTPUT_GAIN_STEP, LV_ANIM_OFF);
    lv_event_send(slider, LV_EVENT_VALUE_CHANGED, NULL);
}

static uint8_t band_out_gain_correction(uint8_t row) {
    lv_obj_t    *obj;
    lv_obj_t    *cell;
    lv_obj_t    *slider;

    cell = lv_label_create(grid);

    lv_label_set_text(cell, "Band output gain");
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

    cell = lv_obj_create(grid);

    lv_obj_set_size(cell, SMALL_6, ROW_H);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 1, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(cell);

    slider = slider_with_text(cell, cfg_sm.p_band_dac_offset.get(),
        -6.0f, 6.0f, OUTPUT_GAIN_STEP,
        SMALL_6 - 120, "%0.1f", band_out_gain_update_cb);

    Observer *observer = cfg_sm.p_band_dac_offset.subscribe(on_dac_gain_change, slider);
    observers.emplace_back(observer);

    return row + 1;
}


uint8_t make_fm_emphasis(uint8_t row) {
    lv_obj_t    *obj;
    uint8_t     col = 0;

    obj = lv_label_create(grid);

    lv_label_set_text(obj, "FM pre/de-emphasis");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col++, 1, LV_GRID_ALIGN_CENTER, row, 1);

    obj = lv_obj_create(grid);

    lv_obj_set_size(obj, SMALL_3, ROW_H);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 4, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(obj);

    obj = switch_bool(obj, cfg_sm.p_fm_emphasis);

    lv_obj_set_width(obj, SMALL_3 - 30);

    return row + 1;
}

/* TX filter */

#define TX_FILTER_STEP 10

static void tx_filter_low_high_update_cb(lv_event_t * e) {
    lv_obj_t *obj = lv_event_get_target(e);
    uint32_t val = lv_slider_get_value(obj) * TX_FILTER_STEP;

    lv_obj_t *slider_label = (lv_obj_t *)lv_obj_get_user_data(obj);
    char *fmt = (char *)lv_obj_get_user_data(slider_label);
    lv_label_set_text_fmt(slider_label, fmt, val);
    ParamInt *param = (ParamInt *)lv_event_get_user_data(e);
    param->set(val);
}

uint8_t make_tx_filter(uint8_t row) {
    lv_obj_t    *obj;
    lv_obj_t    *cell;

    cell = lv_label_create(grid);

    lv_label_set_text(cell, "Filter low");
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

    cell = lv_obj_create(grid);

    lv_obj_set_size(cell, SMALL_6, ROW_H);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 1, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(cell);

    slider_with_text(cell, cfg_sm.p_tx_filter_low.get(),
        50, 400, TX_FILTER_STEP,
        SMALL_6 - 150, "%d", tx_filter_low_high_update_cb, (void*)&cfg_sm.p_tx_filter_low);
    row++;

    cell = lv_label_create(grid);

    lv_label_set_text(cell, "Filter high");
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

    cell = lv_obj_create(grid);

    lv_obj_set_size(cell, SMALL_6, ROW_H);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 1, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(cell);

    slider_with_text(cell, cfg_sm.p_tx_filter_high.get(),
        2000, 4000, TX_FILTER_STEP,
        SMALL_6 - 150, "%d", tx_filter_low_high_update_cb, (void*)&cfg_sm.p_tx_filter_high);
    row++;

    return row;
}

/* CESSB */

#define CESSB_POWER_UP_STEP 0.1f

static void cessb_power_up_update_cb(lv_event_t * e) {
    lv_obj_t *obj = lv_event_get_target(e);
    float val = lv_slider_get_value(obj) * CESSB_POWER_UP_STEP;

    lv_obj_t *slider_label = (lv_obj_t *)lv_obj_get_user_data(obj);
    char *fmt = (char *)lv_obj_get_user_data(slider_label);
    lv_label_set_text_fmt(slider_label, fmt, val);
    cfg_sm.p_cessb_power_up.set(val);
}

uint8_t make_cessb(uint8_t row) {
    lv_obj_t    *obj;
    lv_obj_t    *cell;


    /* Label */
    cell = lv_label_create(grid);

    lv_label_set_text(cell, "CESSB on, level");
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);


    /* on */
    cell = lv_obj_create(grid);

    lv_obj_set_size(cell, SMALL_2, ROW_H);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 1, 2, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(cell);

    obj = switch_bool(cell, cfg_sm.p_cessb_on);
    lv_obj_set_width(obj, SMALL_2 - 30);


    /* level */
    cell = lv_obj_create(grid);

    lv_obj_set_size(cell, SMALL_4, ROW_H);
    lv_obj_set_grid_cell(cell, LV_GRID_ALIGN_START, 3, 4, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(cell, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(cell);

    obj = slider_with_text(cell, cfg_sm.p_cessb_power_up.get(), 0.0f, 5.0f, CESSB_POWER_UP_STEP,
                           SMALL_3 - 40, "%0.1f", cessb_power_up_update_cb);

    row++;

    return row;
}

/* Charger */

/* Bluetooth CAT (btcat) */

#define BTCAT_CTL "/usr/sbin/btcat-ctl"

static lv_obj_t *bt_visible_sw = NULL;
static lv_obj_t *bt_visible_label;
static lv_timer_t *bt_visible_timer;
static uint32_t bt_visible_tick;     /* last tap: btcat-ctl gets 3 s to write the state */

static bool btcat_read(const char *key, bool def) {
    const char *files[] = { "/etc/default/btcat", "/mnt/btcat.conf" };
    bool val = def;
    size_t klen = strlen(key);

    for (auto f : files) {
        FILE *fp = fopen(f, "r");
        if (!fp) continue;
        char line[128];
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, key, klen) == 0 && line[klen] == '=') {
                const char *v = line + klen + 1;
                if (*v == '"') v++;
                val = strncmp(v, "yes", 3) == 0;
            }
        }
        fclose(fp);
    }
    return val;
}

static void btcat_run(const char *what, bool on) {
    std::string cmd = std::string(BTCAT_CTL " ") + what + (on ? " on" : " off") + " >/dev/null 2>&1";
    std::thread([cmd]() { system(cmd.c_str()); }).detach();
}

static void bt_power_update_cb(lv_event_t * e) {
    lv_obj_t *obj = lv_event_get_target(e);
    bool on = lv_obj_has_state(obj, LV_STATE_CHECKED);

    btcat_run("bt", on);
    if (bt_visible_sw) {
        if (on) lv_obj_clear_state(bt_visible_sw, LV_STATE_DISABLED);
        else    lv_obj_add_state(bt_visible_sw, LV_STATE_DISABLED);
    }
}

static void bt_visible_update_cb(lv_event_t * e) {
    lv_obj_t *obj = lv_event_get_target(e);
    btcat_run("discoverable", lv_obj_has_state(obj, LV_STATE_CHECKED));
    bt_visible_tick = lv_tick_get();
}

/* The pairing window (3 min) as it really is: btcat-ctl keeps its end, in s
 * of uptime, in /tmp/btcat-pairing while it is open */
static void bt_visible_timer_cb(lv_timer_t *t) {
    int left = bt_dev_visible_s();

    if (lv_tick_elaps(bt_visible_tick) < 3000) return;
    if (left > 0) {
        lv_label_set_text_fmt(bt_visible_label, "Pairing %i:%02i", left / 60, left % 60);
        lv_obj_add_state(bt_visible_sw, LV_STATE_CHECKED);
    } else {
        lv_label_set_text(bt_visible_label, "Pairing (3 min)");
        lv_obj_clear_state(bt_visible_sw, LV_STATE_CHECKED);
    }
}

static void bt_visible_delete_cb(lv_event_t * e) {
    bt_visible_sw = NULL;
    lv_timer_del(bt_visible_timer);
}

static lv_obj_t * bt_switch_cell(uint8_t row, uint8_t col, bool state, lv_event_cb_t cb) {
    lv_obj_t *obj = lv_obj_create(grid);

    lv_obj_set_size(obj, SMALL_3, ROW_H);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(obj);

    obj = create_switch(obj);
    lv_obj_set_width(obj, SMALL_3 - 30);
    if (state) lv_obj_add_state(obj, LV_STATE_CHECKED);
    lv_obj_add_event_cb(obj, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return obj;
}

static uint8_t make_bt_power(uint8_t row) {
    lv_obj_t *obj = lv_label_create(grid);

    lv_label_set_text(obj, "Bluetooth");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);
    bt_switch_cell(row, 1, btcat_read("BTCAT_BT", true), bt_power_update_cb);
    return row + 1;
}

static uint8_t make_bt_pairing(uint8_t row) {
    lv_obj_t *obj;
    bool      power = btcat_read("BTCAT_BT", true);

    obj = lv_label_create(grid);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);
    bt_visible_label = obj;
    bt_visible_sw = bt_switch_cell(row, 1, false, bt_visible_update_cb);
    lv_obj_add_event_cb(bt_visible_sw, bt_visible_delete_cb, LV_EVENT_DELETE, NULL);
    bt_visible_tick = lv_tick_get() - 3000;
    bt_visible_timer = lv_timer_create(bt_visible_timer_cb, 1000, NULL);
    bt_visible_timer_cb(bt_visible_timer);
    if (!power) lv_obj_add_state(bt_visible_sw, LV_STATE_DISABLED);

    return row + 1;
}

uint8_t make_charger(uint8_t row) {
    lv_obj_t *obj;
    uint8_t   col = 0;

    obj = lv_label_create(grid);

    lv_label_set_text(obj, "Charger");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col++, 1, LV_GRID_ALIGN_CENTER, row, 1);

    obj = dropdown_uint8(grid, &params.charger, " Off \n On \n Shadow", [](lv_event_t *e) {
        uint8_dropdown_update_cb(e);
        radio_set_charger(params.charger.x == RADIO_CHARGER_ON);
    });

    lv_obj_set_size(obj, SMALL_6, ROW_H);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 6, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_center(obj);

    return row + 1;
}

static uint8_t make_freq_accel(uint8_t row) {
    lv_obj_t    *obj;
    uint8_t     col = 0;

    obj = lv_label_create(grid);

    lv_label_set_text(obj, "Freq acceleration");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col++, 1, LV_GRID_ALIGN_CENTER, row, 1);

    obj = dropdown_uint8(grid, &params.freq_accel, " None \n Lite \n Strong");

    lv_obj_set_size(obj, SMALL_6, ROW_H);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 6, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_center(obj);

    return row + 1;
}

/* The built-in themes and the files in /mnt/themes */
static uint8_t make_theme(uint8_t row) {
    lv_obj_t    *obj;
    char        names[16][32];
    int         n = theme_list(names, 16);
    std::string options;

    obj = lv_label_create(grid);

    lv_label_set_text(obj, "Theme");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

    obj = lv_dropdown_create(grid);
    dialog_item(&dialog, obj);
    styles_dropdown_list(lv_dropdown_get_list(obj));
    lv_dropdown_set_symbol(obj, NULL);
    for (int i = 0; i < n; i++) {
        options += std::string(i ? "\n" : "") + names[i];
    }
    lv_dropdown_set_options(obj, options.c_str());
    for (int i = 0; i < n; i++) {
        if (strcasecmp(names[i], params.theme_name.x) == 0) {
            lv_dropdown_set_selected(obj, i);
        }
    }
    lv_obj_add_event_cb(obj, theme_update_cb, LV_EVENT_VALUE_CHANGED, NULL);
    theme_dd = obj;

    lv_obj_set_size(obj, SMALL_6, ROW_H);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 6, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_center(obj);

    return row + 1;
}

static uint8_t make_font(uint8_t row) {
    lv_obj_t    *obj;

    obj = lv_label_create(grid);
    lv_label_set_text(obj, "Font");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);

    obj = dropdown_uint8(grid, &params.font, " Sony Sketch \n Roboto \n DejaVu", font_update_cb);
    font_dd = obj;

    lv_obj_set_size(obj, SMALL_6, ROW_H);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 6, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_center(obj);

    return row + 1;
}

/* Wireless (WiFi, Ethernet) and Bluetooth. The MFK walks the rows; a press
 * on a network or device opens its action menu. */

#define WL_ROW_H    40
#define WIFI_ROWS   8
#define BT_ROWS     8

typedef struct {
    lv_obj_t *obj;
    lv_obj_t *name;
    lv_obj_t *pill_1;
    lv_obj_t *pill_2;
    lv_obj_t *note;
    lv_obj_t *bars[4];
    lv_obj_t *lock;
} wl_row_t;

typedef struct {
    std::string           label;
    std::string           note;
    std::function<void()> fn;
} wl_item_t;

static int                    wl_gen;       /* results for a deleted page are dropped */
static lv_timer_t            *wl_timer;
static bool                   wl_kbd;       /* password keyboard open */
static wifi_ap_info_t         wl_kbd_ap;

static lv_obj_t              *wl_menu;      /* action menu */
static lv_obj_t              *wl_menu_back; /* row focused again when it closes */
static std::vector<wl_item_t> wl_items;
static std::vector<lv_obj_t *> wl_item_rows;
static int                    wl_sel;
static const char            *wl_tab = "Wireless";  /* its button closes the menu */

static struct {
    lv_obj_t      *status_pill;
    lv_obj_t      *status_ssid;
    lv_obj_t      *ip;
    lv_obj_t      *scan_btn;
    lv_obj_t      *scan_note;
    wl_row_t       rows[WIFI_ROWS];
    wifi_ap_info_t aps[WIFI_ROWS];
    bool           scanning;
    time_t         last_scan;
} wf;

static lv_timer_t *bt_timer;     /* Bluetooth page */
static int         bt_ticks;

static struct {
    lv_obj_t             *pill;     /* adapter */
    lv_obj_t             *state;
    lv_obj_t             *addr;
    lv_obj_t             *cat;
    lv_obj_t             *scan_btn;
    lv_obj_t             *scan_note;
    lv_obj_t             *rx;
    lv_obj_t             *vol;
    lv_obj_t             *mute;
    wl_row_t              rows[BT_ROWS];
    std::vector<bt_dev_t> devs;
    std::vector<bt_dev_t> found;    /* not paired, seen since the last Scan */
    std::string           note;     /* after a scan that found nothing */
    bool                  was_scanning;
    int                   vol_pending = -1;
    bool                  vol_busy;
} bt;

static lv_obj_t * wl_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, lv_coord_t x) {
    lv_obj_t *obj = lv_label_create(parent);

    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_align(obj, LV_ALIGN_LEFT_MID, x, 0);
    lv_label_set_text(obj, "");
    return obj;
}

static lv_obj_t * wl_pill_create(lv_obj_t *parent, lv_coord_t x) {
    lv_obj_t *obj = wl_label(parent, SF(18), theme_hex(TC_text), x);

    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, 10, 0);
    lv_obj_set_style_pad_hor(obj, 8, 0);
    lv_obj_set_style_pad_ver(obj, 2, 0);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    return obj;
}

static void wl_pill(lv_obj_t *obj, const char *text, uint32_t bg, uint32_t fg) {
    if (!text) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(obj, text);
    lv_obj_set_style_bg_color(obj, lv_color_hex(bg), 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(fg), 0);
}

#define PILL_OK     "connected", theme_hex(TC_ok_bg), theme_hex(TC_ok)
#define PILL_HP     "headphones", theme_hex(TC_pill_hp), theme_hex(TC_accent)
#define PILL_CAT    "CAT", theme_hex(TC_pill_cat), theme_hex(TC_pill_cat_text)
#define PILL_PHONE  "phone", theme_hex(TC_pill_cat), theme_hex(TC_pill_cat_text)
#define PILL_KBD    "keyboard", theme_hex(TC_pill_kbd), theme_hex(TC_pill_kbd_text)
#define PILL_NEW    "new", theme_hex(TC_chip), theme_hex(TC_text_value)

/* Sections: a spaced title with a line to the right edge, then the rows on
 * their own grid. `grid` is the section being filled, the rows go from row 1. */

#define CARD_HEAD_H     30
#define ROW_LABEL       LV_OBJ_FLAG_USER_1     /* a row's label, with a dotted leader to its control */

static lv_coord_t *card_cols;

static uint8_t card_begin(const char *title, lv_coord_t *cols = col_dsc) {
    lv_obj_t *obj;

    grid = lv_obj_create(page);
    lv_obj_remove_style_all(grid);
    lv_obj_set_layout(grid, LV_LAYOUT_GRID);
    lv_obj_set_size(grid, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_color(grid, TC(text), 0);
    lv_obj_set_style_pad_hor(grid, 4, 0);
    lv_obj_set_style_pad_column(grid, SMALL_PAD, 0);
    lv_obj_set_style_pad_row(grid, 5, 0);

    card_cols = cols;
    row_dsc = row_pool[card_n++];
    std::fill_n(row_dsc, 64, ROW_H - 1);
    row_dsc[0] = CARD_HEAD_H;

    int ncols = 0;
    while (cols[ncols] != LV_GRID_TEMPLATE_LAST) ncols++;

    obj = lv_obj_create(grid);
    lv_obj_remove_style_all(obj);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(obj, 10, 0);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_STRETCH, 0, ncols, LV_GRID_ALIGN_STRETCH, 0, 1);

    lv_obj_t *lbl = lv_label_create(obj);
    lv_obj_set_style_text_font(lbl, SF(18), 0);
    lv_obj_set_style_text_color(lbl, TC(text_head), 0);
    lv_obj_set_style_text_letter_space(lbl, 3, 0);
    lv_label_set_text(lbl, title);

    lv_obj_t *line = lv_obj_create(obj);
    lv_obj_remove_style_all(line);
    lv_obj_set_height(line, 1);
    lv_obj_set_flex_grow(line, 1);
    lv_obj_set_style_bg_color(line, TC(card), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
    return 1;
}

/* Dots every 7 px from each row label to the first thing on its right */
static void leader_draw_cb(lv_event_t *e) {
    lv_obj_t          *g   = lv_event_get_target(e);
    uint32_t           n   = lv_obj_get_child_cnt(g);
    lv_draw_rect_dsc_t dot;

    lv_draw_rect_dsc_init(&dot);
    dot.bg_color = TC(text_faint);

    for (uint32_t i = 1; i < n; i++) {
        lv_obj_t *lbl = lv_obj_get_child(g, i);

        if (!lv_obj_has_flag(lbl, ROW_LABEL)) continue;

        int        r  = lv_obj_get_style_grid_cell_row_pos(lbl, 0);
        lv_coord_t x2 = LV_COORD_MAX;

        for (uint32_t j = 1; j < n; j++) {
            lv_obj_t *c = lv_obj_get_child(g, j);

            if (lv_obj_get_style_grid_cell_row_pos(c, 0) == r && lv_obj_get_style_grid_cell_column_pos(c, 0) >= 1 &&
                !lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN)) {
                x2 = LV_MIN(x2, c->coords.x1);
            }
        }
        if (x2 == LV_COORD_MAX) continue;

        lv_coord_t y = lbl->coords.y2 - 7;

        for (lv_coord_t x = lbl->coords.x2 + 10; x + 10 < x2; x += 7) {
            lv_area_t a;

            lv_area_set(&a, x, y, x + 1, y + 1);
            lv_draw_rect(lv_event_get_draw_ctx(e), &dot, &a);
        }
    }
}

/* Four corner brackets around the focused control, the choices with a triangle each side */
static void bracket_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);

    if (lv_event_get_code(e) == LV_EVENT_REFR_EXT_DRAW_SIZE) {
        lv_event_set_ext_draw_size(e, 6);
        return;
    }
    if (lv_event_get_code(e) != LV_EVENT_DRAW_POST_END || !lv_obj_has_state(obj, LV_STATE_FOCUSED)) {
        return;
    }
    lv_draw_ctx_t     *ctx = lv_event_get_draw_ctx(e);
    lv_area_t          o   = obj->coords;
    bool               choice = lv_obj_check_type(obj, &lv_dropdown_class);
    lv_draw_rect_dsc_t dsc;

    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = TC(accent);
    lv_area_increase(&o, 4, 2);

    if (choice) {   /* tight around the value, right-aligned */
        char       buf[64];
        lv_point_t size;

        lv_dropdown_get_selected_str(obj, buf, sizeof(buf));
        lv_txt_get_size(&size, buf, lv_obj_get_style_text_font(obj, 0), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        o.x1 = LV_MAX(o.x1, o.x2 - size.x - 2 * 26);
    } else if (lv_obj_check_type(obj, &lv_btn_class) && !lv_obj_has_flag(obj, LV_OBJ_FLAG_CHECKABLE) &&
               lv_obj_get_child_cnt(obj)) {
        o.x1 = LV_MAX(o.x1, lv_obj_get_child(obj, 0)->coords.x1 - 12);
    }

    const int L = 9;
    const int bars[8][4] = {
        {o.x1, o.y1, o.x1 + L, o.y1 + 1}, {o.x1, o.y1, o.x1 + 1, o.y1 + L},
        {o.x2 - L, o.y1, o.x2, o.y1 + 1}, {o.x2 - 1, o.y1, o.x2, o.y1 + L},
        {o.x1, o.y2 - 1, o.x1 + L, o.y2}, {o.x1, o.y2 - L, o.x1 + 1, o.y2},
        {o.x2 - L, o.y2 - 1, o.x2, o.y2}, {o.x2 - 1, o.y2 - L, o.x2, o.y2},
    };
    for (auto &b : bars) {
        lv_area_t a;

        lv_area_set(&a, b[0], b[1], b[2], b[3]);
        lv_draw_rect(ctx, &dsc, &a);
    }
    if (choice) {
        int y = (o.y1 + o.y2) / 2;

        for (int side : {-1, 1}) {
            int        x = side < 0 ? o.x1 + 7 : o.x2 - 7;
            lv_point_t t[3];

            t[0].x = x;                 t[0].y = y;
            t[1].x = x - side * 7;      t[1].y = y - 6;
            t[2].x = x - side * 7;      t[2].y = y + 6;
            lv_draw_polygon(ctx, &dsc, t, 3);
        }
    }
}

/* Controls flat on the row: no frames, values as bright text, brackets for the focus.
 * Sliders and switches keep their look, toggles a thin frame (off would vanish). */
static void flat_ctl(lv_obj_t *obj) {
    bool text = lv_obj_check_type(obj, &lv_dropdown_class) || lv_obj_check_type(obj, &lv_spinbox_class) ||
                lv_obj_check_type(obj, &lv_btn_class);

    if (!text && !lv_obj_check_type(obj, &lv_slider_class) && !lv_obj_check_type(obj, &lv_switch_class)) {
        if (lv_obj_check_type(obj, &lv_obj_class) && !lv_obj_get_group(obj)) {    /* a cell: look inside */
            lv_obj_set_style_border_width(obj, 0, 0);
            for (uint32_t i = 0; i < lv_obj_get_child_cnt(obj); i++) flat_ctl(lv_obj_get_child(obj, i));
        }
        return;
    }
    lv_obj_add_event_cb(obj, bracket_cb, LV_EVENT_ALL, NULL);
    lv_obj_refresh_ext_draw_size(obj);
    if (!text) {    /* their cell no longer frames them: a track of their own */
        lv_obj_set_style_bg_color(obj, TC(track), 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
        return;
    }

    bool toggle = lv_obj_has_flag(obj, LV_OBJ_FLAG_CHECKABLE);

    lv_obj_set_style_border_width(obj, toggle ? 1 : 0, 0);
    lv_obj_set_style_border_color(obj, TC(card), 0);
    lv_obj_set_style_border_width(obj, toggle ? 1 : 0, LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(obj, TC(accent), LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(obj, TC(accent), LV_STATE_FOCUSED);

    if (lv_obj_check_type(obj, &lv_dropdown_class)) {   /* the value at the right, room for the triangle */
        lv_dropdown_set_symbol(obj, LV_SYMBOL_DUMMY);
        lv_obj_set_style_base_dir(obj, LV_BASE_DIR_RTL, 0);
        lv_obj_set_style_pad_right(obj, 26, 0);
    } else if (lv_obj_check_type(obj, &lv_spinbox_class)) {
        lv_obj_set_style_text_align(obj, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_color(obj, TC(accent), LV_PART_CURSOR | LV_STATE_FOCUSED);   /* no fill behind it now */
        lv_obj_set_style_text_color(obj, TC(focus_text), LV_PART_CURSOR | LV_STATE_EDITED);
    } else if (!toggle && lv_obj_get_child_cnt(obj)) {
        lv_obj_align(lv_obj_get_child(obj, 0), LV_ALIGN_RIGHT_MID, -6, 0);
    }
}

/* Rows go up to `row` (exclusive): put a lone control at the right edge, a dotted
 * leader from each row label to its control, the controls flat on the row. */
static void card_end(uint8_t row) {
    uint8_t  ctl[64] = {};
    int      ncols = 0;
    uint32_t n = lv_obj_get_child_cnt(grid);

    while (card_cols[ncols] != LV_GRID_TEMPLATE_LAST) ncols++;
    row_dsc[row] = LV_GRID_TEMPLATE_LAST;
    lv_obj_set_grid_dsc_array(grid, card_cols, row_dsc);

    for (uint32_t i = 1; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(grid, i);

        if (lv_obj_get_style_grid_cell_column_pos(c, 0) >= 1) {
            ctl[lv_obj_get_style_grid_cell_row_pos(c, 0)]++;
        }
    }
    for (uint32_t i = 1; i < n; i++) {
        lv_obj_t *c    = lv_obj_get_child(grid, i);
        int       pos  = lv_obj_get_style_grid_cell_column_pos(c, 0);
        int       span = lv_obj_get_style_grid_cell_column_span(c, 0);
        int       r    = lv_obj_get_style_grid_cell_row_pos(c, 0);
        lv_style_value_t v;

        if (pos == 0 && span == 1 && lv_obj_check_type(c, &lv_label_class) &&
            lv_obj_get_local_style_prop(c, LV_STYLE_TEXT_COLOR, &v, 0) != LV_RES_OK) {
            lv_obj_add_flag(c, ROW_LABEL);
            lv_obj_set_style_text_color(c, TC(text_value), 0);
        } else if (pos >= 1 && ctl[r] == 1 && pos + span < ncols) {
            lv_coord_t w = lv_obj_get_style_width(c, 0);

            if (LV_COORD_IS_PX(w) && w > span * (SMALL_1 + SMALL_PAD) - SMALL_PAD) {
                span = ncols - 1;   /* wider than its cells */
            }
            lv_obj_set_grid_cell(c, (lv_grid_align_t)lv_obj_get_style_grid_cell_x_align(c, 0), ncols - span, span,
                                 (lv_grid_align_t)lv_obj_get_style_grid_cell_y_align(c, 0), r, 1);
        }
        flat_ctl(c);
    }
    lv_obj_add_event_cb(grid, leader_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);
}

static void wl_row_label(uint8_t row, const char *text) {
    lv_obj_t *obj = lv_label_create(grid);

    lv_label_set_text(obj, text);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);
}

static lv_obj_t * wl_button(uint8_t row, const char *text, lv_event_cb_t cb) {
    lv_obj_t *obj = lv_btn_create(grid);

    lv_obj_remove_style_all(obj);
    dialog_item(&dialog, obj);
    lv_obj_set_style_radius(obj, 8, 0);
    lv_obj_set_style_border_width(obj, 2, 0);
    lv_obj_set_style_border_color(obj, TC(control_border), 0);
    lv_obj_set_size(obj, SMALL_3, ROW_H - 4);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(obj);
    lv_obj_set_style_text_font(label, SF(22), 0);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return obj;
}

/* Note to the right of a 3-column control */
static lv_obj_t * wl_note(uint8_t row) {
    lv_obj_t *obj = wl_label(grid, theme_shape.vision ? &sony_18 : &sony_20, theme_hex(TC_text_dim), 0);   /* one size up: two would overflow */

    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 4, 3, LV_GRID_ALIGN_CENTER, row, 1);
    return obj;
}

/* A list (all the columns) of hidden rows; the refresh fills and shows them */
static lv_obj_t * wl_list(uint8_t row) {
    lv_obj_t *obj = lv_obj_create(grid);

    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(obj, 2, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_STRETCH, 0, 7, LV_GRID_ALIGN_START, row, 1);
    row_dsc[row] = LV_GRID_CONTENT;
    return obj;
}

static void wl_row_create(wl_row_t *r, lv_obj_t *list, lv_event_cb_t cb, int i) {
    lv_obj_t *obj = lv_obj_create(list);

    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, lv_pct(100), WL_ROW_H);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);   /* a MFK press is a click */
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLL_ON_FOCUS | LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_radius(obj, 4, 0);
    lv_obj_set_style_bg_color(obj, TC(sel_bg), LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_STATE_FOCUSED);
    lv_obj_set_style_border_color(obj, TC(sel_border), LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(obj, 2, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    lv_obj_add_event_cb(obj, key_cb, LV_EVENT_KEY, NULL);
    lv_group_add_obj(keyboard_group, obj);

    r->obj = obj;
    r->name = wl_label(obj, SF(22), theme_hex(TC_text), 0);      /* lined up with the row labels */
    lv_obj_set_size(r->name, 270, lv_font_get_line_height(SF(22)));   /* one line, then dots */
    lv_label_set_long_mode(r->name, LV_LABEL_LONG_DOT);
}

/* Hidden rows can't have the focus: move it before hiding the focused one */
static void wl_row_show(wl_row_t *r, bool show, lv_obj_t *fallback) {
    if (!show && lv_group_get_focused(keyboard_group) == r->obj) {
        lv_group_focus_obj(fallback);
    }
    if (show) {
        lv_obj_clear_flag(r->obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(r->obj, LV_OBJ_FLAG_HIDDEN);
    }
}

/* Action menu */

static void wl_menu_show_sel() {
    for (size_t i = 0; i < wl_item_rows.size(); i++) {
        bool on = (int)i == wl_sel;

        lv_obj_set_style_bg_opa(wl_item_rows[i], on ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(wl_item_rows[i], on ? 2 : 0, 0);
        lv_obj_set_style_text_color(lv_obj_get_child(wl_item_rows[i], 0), on ? TC(sel_text) : TC(text), 0);
    }
}

static void wl_menu_close() {
    if (!wl_menu) {
        return;
    }
    lv_obj_del(wl_menu);
    wl_menu = NULL;
    wl_item_rows.clear();
    if (wl_menu_back) {
        lv_group_focus_obj(wl_menu_back);
    }
    lv_group_set_editing(keyboard_group, false);
}

/* Run after the event: closing deletes the menu that got it */
static void wl_menu_pick_async(void *arg) {
    if (!wl_menu) {
        return;
    }
    int sel = (int)(intptr_t)arg;
    auto fn = sel >= 0 ? wl_items[sel].fn : nullptr;

    wl_menu_close();
    if (fn) {
        fn();
    }
}

static void wl_menu_key_cb(lv_event_t *e) {
    switch (*((uint32_t *)lv_event_get_param(e))) {
        case LV_KEY_LEFT:
        case LV_KEY_UP:
            if (wl_sel > 0) wl_sel--;
            wl_menu_show_sel();
            break;

        case LV_KEY_RIGHT:
        case LV_KEY_DOWN:
            if (wl_sel < (int)wl_items.size() - 1) wl_sel++;
            wl_menu_show_sel();
            break;

        case LV_KEY_ENTER:
            lv_async_call(wl_menu_pick_async, (void *)(intptr_t)wl_sel);
            break;

        case LV_KEY_ESC:
            lv_async_call(wl_menu_pick_async, (void *)(intptr_t)-1);
            break;

        case KEY_VOL_LEFT_EDIT:
        case KEY_VOL_LEFT_SELECT:
            radio_change_vol(-1);
            break;

        case KEY_VOL_RIGHT_EDIT:
        case KEY_VOL_RIGHT_SELECT:
            radio_change_vol(1);
            break;
    }
}

static void wl_menu_click_cb(lv_event_t *e) {
    lv_obj_t   *target = lv_event_get_target(e);
    lv_indev_t *indev = lv_indev_get_act();

    /* On the menu itself only the MFK press counts, not a touch */
    if (target == wl_menu && !(indev && lv_indev_get_type(indev) == LV_INDEV_TYPE_ENCODER)) {
        return;
    }

    for (size_t i = 0; i < wl_item_rows.size(); i++) {
        if (wl_item_rows[i] == target) {
            wl_sel = i;
        }
    }
    lv_async_call(wl_menu_pick_async, (void *)(intptr_t)wl_sel);
}

static void wl_menu_open(const std::string &title, std::vector<wl_item_t> items) {
    const lv_coord_t w = theme_shape.vision ? 480 : 420, head_h = 36, item_h = 40, hint_h = 30;

    wl_menu_close();
    wl_items = std::move(items);
    wl_menu_back = lv_group_get_focused(keyboard_group);

    wl_menu = lv_obj_create(dialog.obj);
    lv_obj_remove_style_all(wl_menu);
    lv_obj_set_size(wl_menu, w, head_h + wl_items.size() * item_h + hint_h + 8);
    lv_obj_center(wl_menu);
    lv_obj_add_style(wl_menu, &popup_style, 0);
    lv_obj_clear_flag(wl_menu, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *obj = wl_label(wl_menu, SF(18), theme_hex(TC_text_head), 16);
    lv_obj_set_width(obj, w - 32);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
    lv_label_set_text(obj, title.c_str());
    lv_obj_align(obj, LV_ALIGN_TOP_LEFT, 16, 8);

    for (size_t i = 0; i < wl_items.size(); i++) {
        lv_obj_t *row = lv_obj_create(wl_menu);

        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, w - 4, item_h);
        lv_obj_set_pos(row, 0, head_h + i * item_h);
        lv_obj_set_style_bg_color(row, TC(sel_bg), 0);
        lv_obj_set_style_border_color(row, TC(sel_border), 0);
        lv_obj_set_style_border_side(row, (lv_border_side_t)(LV_BORDER_SIDE_TOP | LV_BORDER_SIDE_BOTTOM), 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(row, wl_menu_click_cb, LV_EVENT_CLICKED, NULL);

        obj = wl_label(row, SF(22), theme_hex(TC_text), 16);
        lv_label_set_text(obj, wl_items[i].label.c_str());

        obj = wl_label(row, SF(18), theme_hex(TC_text_dim), 0);
        lv_label_set_text(obj, wl_items[i].note.c_str());
        lv_obj_align(obj, LV_ALIGN_RIGHT_MID, -16, 0);

        wl_item_rows.push_back(row);
    }

    obj = wl_label(wl_menu, SF(16), theme_hex(TC_text_dim), 16);
    lv_label_set_text_fmt(obj, "MFK: choose - press: confirm - %s: close", wl_tab);
    lv_obj_align(obj, LV_ALIGN_BOTTOM_LEFT, 16, -8);

    wl_sel = 0;
    wl_menu_show_sel();

    lv_group_add_obj(keyboard_group, wl_menu);
    lv_obj_add_event_cb(wl_menu, wl_menu_key_cb, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(wl_menu, wl_menu_click_cb, LV_EVENT_CLICKED, NULL);
    lv_group_focus_obj(wl_menu);
    lv_group_set_editing(keyboard_group, true);
}

/* WiFi */

static int wifi_compare_aps(const void *a, const void *b) {
    const wifi_ap_info_t *ap_a = (const wifi_ap_info_t *)a;
    const wifi_ap_info_t *ap_b = (const wifi_ap_info_t *)b;
    int score_a = ap_a->is_connected * 2 + ap_a->known;
    int score_b = ap_b->is_connected * 2 + ap_b->known;

    if (score_a != score_b) {
        return score_b - score_a;
    }
    return ap_b->strength - ap_a->strength;
}

static void wifi_update() {
    static const char *status_names[] = { "off", "starting", "disconnected", "connecting", "connected" };
    wifi_status_t status = wifi_get_status();
    bool          on = status != WIFI_OFF && status != WIFI_STARTING;
    const char   *ssid = "";
    int           n = 0;

    if (status == WIFI_CONNECTED) {
        wl_pill(wf.status_pill, PILL_OK);
    } else {
        wl_pill(wf.status_pill, status_names[status], theme_hex(TC_chip), theme_hex(TC_text_value));
    }

    if (on) {
        wifi_ap_arr_t aps = wifi_get_available_access_points();

        if (aps.count) {
            qsort(aps.ap_arr, aps.count, sizeof(wifi_ap_info_t), wifi_compare_aps);
        }
        for (; n < aps.count && n < WIFI_ROWS; n++) {
            wf.aps[n] = aps.ap_arr[n];
        }
        wifi_aps_info_delete(aps);
    }

    for (int i = 0; i < WIFI_ROWS; i++) {
        wl_row_t       *r = &wf.rows[i];
        wifi_ap_info_t *ap = &wf.aps[i];

        wl_row_show(r, i < n, wf.scan_btn);
        if (i >= n) {
            continue;
        }
        lv_label_set_text(r->name, ap->ssid);
        for (int b = 0; b < 4; b++) {
            bool lit = b < (ap->strength + 24) / 25;

            lv_obj_set_style_bg_color(r->bars[b], lit ? TC(ok) : TC(off), 0);
        }
        if (ap->password_validator) {
            lv_obj_clear_flag(r->lock, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(r->lock, LV_OBJ_FLAG_HIDDEN);
        }
        if (ap->is_connected) {
            wl_pill(r->pill_1, PILL_OK);
            ssid = ap->ssid;
        } else {
            wl_pill(r->pill_1, NULL, 0, 0);
        }
        lv_label_set_text(r->note, ap->known ? "saved" : "");
    }
    lv_label_set_text(wf.status_ssid, ssid);

    char  ip[16] = "", gw[16] = "";
    char *ip_p = ip, *gw_p = gw;

    if (status == WIFI_CONNECTED && wifi_get_ipaddr(&ip_p, &gw_p)) {
        lv_label_set_text_fmt(wf.ip, "%s  /  %s", ip, gw);
    } else {
        lv_label_set_text(wf.ip, "-");
    }

    bool scanning = on && wifi_scanning();

    if (wf.scanning && !scanning) {
        wf.last_scan = time(NULL);
    }
    wf.scanning = scanning;
    if (scanning) {
        lv_label_set_text(wf.scan_note, "scanning...");
    } else if (on && wf.last_scan) {
        lv_label_set_text_fmt(wf.scan_note, "last scan %i s ago", (int)(time(NULL) - wf.last_scan));
    } else {
        lv_label_set_text(wf.scan_note, "");
    }
}

static void wifi_power_cb(lv_event_t *e) {
    if (lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED)) {
        wifi_power_on();
        msg_update_text_fmt("Turning on");
    } else {
        wifi_power_off();
    }
    wifi_update();
}

static void wifi_scan_cb(lv_event_t *e) {
    wifi_status_t status = wifi_get_status();

    if (status == WIFI_OFF || status == WIFI_STARTING || wifi_scanning()) {
        return;
    }
    wifi_start_scan();
}

static bool wl_kbd_close() {
    wl_kbd = false;
    textarea_window_close();
    make_wireless_page();
    return true;
}

static bool wl_kbd_ok() {
    const char *password = textarea_window_get();

    if (wl_kbd_ap.password_validator && !wl_kbd_ap.password_validator(password)) {
        msg_update_text_fmt("Incorrect password");
        return false;
    }
    if (wl_kbd_ap.known) {
        wifi_update_connection(wl_kbd_ap.ssid, password);
    } else {
        wifi_add_connection(wl_kbd_ap.ssid, password);
    }
    return wl_kbd_close();
}

/* The keyboard gets all the keys: the page is built again when it closes */
static void wl_kbd_open(const wifi_ap_info_t &ap) {
    wl_kbd_ap = ap;
    wl_kbd = true;
    lv_group_remove_all_objs(keyboard_group);
    textarea_window_open(wl_kbd_ok, wl_kbd_close);
    lv_textarea_set_placeholder_text(textarea_window_text(), " Password");
}

static void wifi_row_cb(lv_event_t *e) {
    wifi_ap_info_t         ap = wf.aps[(intptr_t)lv_event_get_user_data(e)];
    std::vector<wl_item_t> items;

    if (ap.is_connected) {
        items.push_back({"Disconnect", "", [] { wifi_disconnect(); }});
    } else {
        items.push_back({"Connect", "", [ap] {
            if (ap.known) {
                wifi_connect(ap.ssid);
            } else if (!ap.password_validator) {
                wifi_add_connection(ap.ssid, NULL);
            } else {
                wl_kbd_open(ap);
            }
        }});
    }
    if (ap.known && ap.password_validator) {
        items.push_back({"Change password", "", [ap] { wl_kbd_open(ap); }});
    }
    if (ap.known) {
        items.push_back({"Delete", "", [ap] {
            wifi_delete_connection(ap.ssid);
            msg_update_text_fmt("Connection deleted");
        }});
    }
    wl_menu_open(ap.ssid, items);
}

static uint8_t make_wifi(uint8_t row) {
    lv_obj_t *obj;

    wl_row_label(row, "WiFi/BT power");
    bt_switch_cell(row, 1, params.wifi_enabled.x, wifi_power_cb);
    row++;

    wl_row_label(row, "Status");
    obj = lv_obj_create(grid);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(obj, 12, 0);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 6, LV_GRID_ALIGN_CENTER, row, 1);
    wf.status_pill = wl_pill_create(obj, 0);
    wf.status_ssid = wl_label(obj, SF(22), theme_hex(TC_text), 0);
    row++;

    obj = wl_label(grid, SF(22), theme_hex(TC_text_dim), 0);
    lv_label_set_text(obj, "IP / Gateway");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);
    wf.ip = wl_label(grid, SF(22), theme_hex(TC_text_dim), 0);
    lv_obj_set_grid_cell(wf.ip, LV_GRID_ALIGN_START, 1, 6, LV_GRID_ALIGN_CENTER, row, 1);
    row++;

    wl_row_label(row, "Networks");
    wf.scan_btn = wl_button(row, "Scan", wifi_scan_cb);
    wf.scan_note = wl_note(row);
    row++;

    lv_obj_t *list = wl_list(row);

    for (int i = 0; i < WIFI_ROWS; i++) {
        wl_row_t *r = &wf.rows[i];

        wl_row_create(r, list, wifi_row_cb, i);
        for (int b = 0; b < 4; b++) {
            obj = lv_obj_create(r->obj);
            lv_obj_remove_style_all(obj);
            lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
            lv_obj_set_size(obj, 8, 6 + b * 5);
            lv_obj_align(obj, LV_ALIGN_BOTTOM_LEFT, 300 + b * 11, -9);
            r->bars[b] = obj;
        }
        /* padlock: shackle and body */
        r->lock = lv_obj_create(r->obj);
        lv_obj_remove_style_all(r->lock);
        lv_obj_set_size(r->lock, 14, 20);
        lv_obj_align(r->lock, LV_ALIGN_LEFT_MID, 380, 0);
        obj = lv_obj_create(r->lock);
        lv_obj_remove_style_all(obj);
        lv_obj_set_size(obj, 10, 12);
        lv_obj_align(obj, LV_ALIGN_TOP_MID, 0, 0);
        lv_obj_set_style_radius(obj, 5, 0);
        lv_obj_set_style_border_width(obj, 2, 0);
        lv_obj_set_style_border_color(obj, TC(lock), 0);
        obj = lv_obj_create(r->lock);
        lv_obj_remove_style_all(obj);
        lv_obj_set_size(obj, 14, 11);
        lv_obj_align(obj, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_set_style_radius(obj, 2, 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(obj, TC(lock), 0);

        r->pill_1 = wl_pill_create(r->obj, 440);
        r->note = wl_label(r->obj, SF(20), theme_hex(TC_text_dim), 620);
    }
    return row + 1;
}

/* Bluetooth devices (bt_dev.c) and headphones (btaudio-ctl) */

static std::vector<std::string> split(const std::string &s, char sep) {
    std::vector<std::string> out;
    size_t                   start = 0, pos;

    while ((pos = s.find(sep, start)) != std::string::npos) {
        out.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    out.push_back(s.substr(start));
    return out;
}

static void bt_update_view() {
    if (!bt.scan_btn) {
        return;     /* page gone */
    }
    size_t n = bt.devs.size();

    for (size_t i = 0; i < BT_ROWS; i++) {
        wl_row_t *r = &bt.rows[i];

        wl_row_show(r, i < n, bt.scan_btn);
        if (i >= n) {
            continue;
        }
        bt_dev_t *d = &bt.devs[i];

        lv_label_set_text(r->name, d->name);
        if (!d->paired) {
            wl_pill(r->pill_1, PILL_NEW);
        } else if (d->audio) {
            wl_pill(r->pill_1, PILL_HP);
        } else if (d->input) {
            wl_pill(r->pill_1, PILL_KBD);
        } else if (d->phone) {
            wl_pill(r->pill_1, PILL_PHONE);
        } else {
            wl_pill(r->pill_1, PILL_CAT);
        }
        wl_pill(r->pill_2, d->connected ? "connected" : NULL, theme_hex(TC_ok_bg), theme_hex(TC_ok));
        if (!d->paired) {
            lv_label_set_text(r->note, "press to pair");
        } else if (!d->connected) {
            lv_label_set_text(r->note, "not connected");
        } else {
            lv_label_set_text(r->note, "");
        }
        lv_obj_align(r->note, LV_ALIGN_LEFT_MID, d->connected ? 580 : 430, 0);
    }
}

/* Headphone volume and RX switch */
static void bt_status_done(const char *out, int status, void *user) {
    if ((intptr_t)user != wl_gen) {
        return;
    }
    for (auto &line : split(out, '\n')) {
        auto f = split(line, '\t');

        if (f[0] == "HP" && f.size() >= 3) {
            int vol = atoi(f[2].c_str());

            if (bt.vol && vol >= 0 && bt.vol_pending < 0 && !bt.vol_busy && !lv_obj_has_state(bt.vol, LV_STATE_EDITED)) {
                lv_slider_set_value(bt.vol, vol, LV_ANIM_OFF);
                lv_label_set_text_fmt((lv_obj_t *)lv_obj_get_user_data(bt.vol), "%i %%", vol);
            }
        } else if (bt.rx && f[0] == "RX" && f.size() >= 2) {
            if (f[1] == "yes") {
                lv_obj_add_state(bt.rx, LV_STATE_CHECKED);
            } else {
                lv_obj_clear_state(bt.rx, LV_STATE_CHECKED);
            }
        }
    }
}

/* Bluetooth page, every second: the list and what the worker does (bt_dev.c).
 * The note shows its activity, a keyboard's code to type included. */
static void bt_update() {
    bt_dev_t    d;
    char        activity[48];
    bool        scanning = bt_dev_scanning();
    std::string focused;    /* the list re-sorts while a scan finds devices: the focus stays on its device */

    for (size_t i = 0; i < bt.devs.size() && i < BT_ROWS; i++) {
        if (bt.scan_btn && lv_group_get_focused(keyboard_group) == bt.rows[i].obj) {
            focused = bt.devs[i].addr;
        }
    }
    bt.devs.clear();
    for (int i = 0; bt_dev_get(i, &d); i++) {
        bt.devs.push_back(d);
    }
    /* bluez forgets a scanned device ~30 s after the scan: it stays here until the next Scan */
    for (auto &f : bt.found) {
        bool listed = false;

        for (auto &p : bt.devs) {
            listed |= strcmp(p.addr, f.addr) == 0;
        }
        if (!listed) {
            bt.devs.push_back(f);
        }
    }
    bt.found.clear();
    for (auto &p : bt.devs) {
        if (!p.paired) {
            bt.found.push_back(p);
        }
    }
    if (bt.was_scanning && !scanning && bt.found.empty()) {
        bt.note = "no headphones or keyboards found";
    }
    bt.was_scanning = scanning;

    bt_dev_activity(activity, sizeof(activity));
    if (bt.scan_note) {
        lv_label_set_text(bt.scan_note, activity[0] ? activity : bt.note.empty() ? "-" : bt.note.c_str());
    }
    bt_update_view();
    for (size_t i = 0; !focused.empty() && i < bt.devs.size() && i < BT_ROWS; i++) {
        if (focused == bt.devs[i].addr) {
            if (lv_group_get_focused(keyboard_group) != bt.rows[i].obj) lv_group_focus_obj(bt.rows[i].obj);
            break;
        }
    }
}

static void bt_scan_cb(lv_event_t *e) {
    if (bt_dev_busy() || bt_dev_scanning()) {
        return;
    }
    bt.found.clear();
    bt.note.clear();
    bt_dev_scan();
}

/* "Connect at startup" list, kept by btaudio-ctl */
static bool bt_autoconn(const char *addr) {
    char  line[512];
    bool  on = false;
    FILE *f = fopen("/mnt/btaudio.conf", "r");

    if (!f) {
        return false;
    }
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "BTAUDIO_AUTOCONNECT=", 20) == 0) {
            on = strstr(line, addr) != NULL;
        }
    }
    fclose(f);
    return on;
}

static void bt_row_cb(lv_event_t *e) {
    size_t i = (intptr_t)lv_event_get_user_data(e);

    if (i >= bt.devs.size() || bt_dev_busy()) {
        return;
    }
    bt_dev_t d = bt.devs[i];

    bt.note.clear();
    if (!d.paired) {
        bt_dev_connect(d.addr);     /* pairs first */
        return;
    }

    std::vector<wl_item_t> items;

    if (d.connected) {
        items.push_back({"Disconnect", "", [d] { bt_dev_disconnect(d.addr); }});
    } else if (d.audio || d.input) {
        items.push_back({"Connect", "", [d] { bt_dev_connect(d.addr); }});
    }
    if (d.audio) {
        bool on = bt_autoconn(d.addr);

        items.push_back({"Connect at startup", on ? "on" : "off", [d, on] {
            btaudio_run((std::string("autoconnect ") + d.addr + (on ? " off" : " on")).c_str(), NULL, NULL);
        }});
    }
    /* Forget asks once more, Cancel first */
    items.push_back({"Forget", "", [d] {
        wl_menu_open(std::string("Forget ") + d.name + "?", {
            {"Cancel", "", nullptr},
            {"Forget", "pair again to use it", [d] { bt_dev_forget(d.addr); }},
        });
    }});
    wl_menu_open(std::string(d.name) + " - " + d.addr, items);
}

static void bt_rx_cb(lv_event_t *e) {
    bool on = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);

    btaudio_run(on ? "rx on" : "rx off", NULL, NULL);
}

static void bt_vol_send();

static void bt_vol_done(const char *out, int status, void *user) {
    bt.vol_busy = false;
    bt_vol_send();
}

/* One volume command at a time, the last value wins */
static void bt_vol_send() {
    if (bt.vol_busy || bt.vol_pending < 0) {
        return;
    }
    std::string args = "volume " + std::to_string(bt.vol_pending);

    bt.vol_pending = -1;
    bt.vol_busy = true;
    btaudio_run(args.c_str(), bt_vol_done, NULL);
}

static void bt_vol_cb(lv_event_t *e) {
    lv_obj_t *obj = lv_event_get_target(e);
    int       vol = lv_slider_get_value(obj);

    lv_label_set_text_fmt((lv_obj_t *)lv_obj_get_user_data(obj), "%i %%", vol);
    bt.vol_pending = vol;
    bt_vol_send();
}

static void bt_mute_cb(lv_event_t *e) {
    btaudio_mute_set(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
}

static uint8_t make_bt_devices(uint8_t row) {
    lv_obj_t *obj;

    wl_row_label(row, "Headphones, keyboards");
    bt.scan_btn = wl_button(row, "Scan", bt_scan_cb);
    lv_label_set_text(wl_note(row), "16 s");
    row++;

    /* What the worker does, a keyboard's code to type included: the whole row */
    obj = wl_label(grid, SF(22), theme_hex(TC_text_dim), 0);   /* own colour: no leader dots */
    lv_label_set_text(obj, "Activity");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);
    bt.scan_note = wl_label(grid, theme_shape.vision ? &sony_18 : &sony_20, theme_hex(TC_text), 0);
    lv_obj_set_grid_cell(bt.scan_note, LV_GRID_ALIGN_END, 1, 6, LV_GRID_ALIGN_CENTER, row, 1);
    lv_label_set_long_mode(bt.scan_note, LV_LABEL_LONG_DOT);
    lv_obj_set_width(bt.scan_note, SMALL_6);
    lv_obj_set_style_text_align(bt.scan_note, LV_TEXT_ALIGN_RIGHT, 0);
    row++;

    lv_obj_t *list = wl_list(row);

    for (int i = 0; i < BT_ROWS; i++) {
        wl_row_t *r = &bt.rows[i];

        wl_row_create(r, list, bt_row_cb, i);
        r->pill_1 = wl_pill_create(r->obj, 300);
        r->pill_2 = wl_pill_create(r->obj, 430);
        r->note = wl_label(r->obj, SF(20), theme_hex(TC_text_dim), 430);
    }
    return row + 1;
}

static uint8_t make_headphones(uint8_t row) {
    lv_obj_t *obj;

    wl_row_label(row, "RX to headphones");
    bt.rx = bt_switch_cell(row, 1, false, bt_rx_cb);
    row++;

    wl_row_label(row, "Volume");
    obj = lv_obj_create(grid);
    lv_obj_set_size(obj, SMALL_6, ROW_H);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 6, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    bt.vol = slider_with_text(obj, 50, 0, 100, 1, SMALL_4, "%i %%", bt_vol_cb);
    lv_obj_align(bt.vol, LV_ALIGN_LEFT_MID, 10, 0);
    row++;

    wl_row_label(row, "Mute speaker");
    bt.mute = bt_switch_cell(row, 1, btaudio_mute_get(), bt_mute_cb);
    lv_label_set_text(wl_note(row), "AF 0 while connected");
    row++;
    return row;
}

/* WFView server: on/off, login (edited with the on-screen keyboard), client */

#define WV_USER 1
#define WV_PASS 2

static int wv_which;    /* keyboard open for WV_USER / WV_PASS */
static struct {
    lv_obj_t *sw;
    lv_obj_t *user;
    lv_obj_t *pass;
    lv_obj_t *pill;
    lv_obj_t *client;
} wv;

static void wv_update() {
    const char *name = wfview_server_client();

    /* The web configurator may change the settings while the page is open */
    if (!wl_kbd) {
        if (wfview_server_get_enabled() != lv_obj_has_state(wv.sw, LV_STATE_CHECKED)) {
            if (wfview_server_get_enabled()) lv_obj_add_state(wv.sw, LV_STATE_CHECKED);
            else lv_obj_clear_state(wv.sw, LV_STATE_CHECKED);
        }
        lv_label_set_text(lv_obj_get_child(wv.user, 0), wfview_server_user());
        lv_label_set_text(lv_obj_get_child(wv.pass, 0), std::string(strlen(wfview_server_pass()), '*').c_str());
    }

    wl_pill(wv.pill, name ? "connected" : NULL, theme_hex(TC_ok_bg), theme_hex(TC_ok));
    lv_label_set_text(wv.client, name ? name : "no client");
    lv_obj_set_style_text_color(wv.client, name ? TC(text) : TC(text_faint), 0);
}

static void wv_power_cb(lv_event_t *e) {
    wfview_server_set_enabled(lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED));
    wv_update();
}

/* The wfview card is on General: the keyboard gives the page back there */
static bool wv_kbd_close() {
    wl_kbd = false;
    textarea_window_close();
    make_general_page();
    return true;
}

static bool wv_kbd_ok() {
    const char *val = textarea_window_get();

    if (wv_which == WV_USER) {
        wfview_server_set_credentials(val, wfview_server_pass());
    } else {
        wfview_server_set_credentials(wfview_server_user(), val);
    }
    return wv_kbd_close();
}

static void wv_open_cb(lv_event_t *e) {
    wv_which = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target(e));
    wl_kbd = true;
    lv_group_remove_all_objs(keyboard_group);
    textarea_window_open(wv_kbd_ok, wv_kbd_close);
    lv_textarea_set_max_length(textarea_window_text(), 15);
    lv_textarea_set_placeholder_text(textarea_window_text(), wv_which == WV_USER ? "User" : "Password");
    textarea_window_set(wv_which == WV_USER ? wfview_server_user() : wfview_server_pass());
}

static uint8_t make_wfview_field(uint8_t row, const char *label, int which, lv_obj_t **btn) {
    std::string val = which == WV_USER ? wfview_server_user() : std::string(strlen(wfview_server_pass()), '*');

    wl_row_label(row, label);

    lv_obj_t *obj = wl_button(row, val.empty() ? "-" : val.c_str(), wv_open_cb);
    lv_obj_set_size(obj, 150, ROW_H - 4);
    lv_obj_set_user_data(obj, (void *)(intptr_t)which);
    *btn = obj;
    return row + 1;
}

static uint8_t make_wfview(uint8_t row) {
    lv_obj_t *obj;

    wl_row_label(row, "Server");
    wv.sw = bt_switch_cell(row, 1, wfview_server_get_enabled(), wv_power_cb);
    row++;

    row = make_wfview_field(row, "User", WV_USER, &wv.user);
    row = make_wfview_field(row, "Password", WV_PASS, &wv.pass);

    wl_row_label(row, "Port / Status");
    obj = lv_obj_create(grid);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(obj, 12, 0);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 6, LV_GRID_ALIGN_CENTER, row, 1);
    lv_label_set_text(wl_label(obj, SF(22), theme_hex(TC_text_dim), 0), "UDP 50001");
    wv.pill = wl_pill_create(obj, 0);
    wv.client = wl_label(obj, SF(22), theme_hex(TC_text), 0);
    lv_obj_set_size(wv.client, 130, lv_font_get_line_height(SF(22)));   /* one line, then dots */
    lv_label_set_long_mode(wv.client, LV_LABEL_LONG_DOT);
    return row + 1;
}

/* Ethernet (wifi.cpp): the fields are a draft, typed one at a time with the keyboard;
 * Apply checks them all and saves. The draft lives across the page rebuilds the keyboard
 * makes, it is read again from the saved profile when the tab opens. */

static eth_cfg_t eth_draft;
static int       eth_kbd_field;
static struct {
    lv_obj_t *pill;
    lv_obj_t *iface;
    lv_obj_t *addr;
    lv_obj_t *gw;
    lv_obj_t *fields[ETH_FIELDS];
} eth;
static bool eth_shown;      /* the card is only there with an adapter plugged in */

static void eth_draft_load() {
    eth_info_t in;

    eth_get_info(&in);
    eth_draft = in.cfg;
}

static void eth_update() {
    eth_info_t in;

    if (!eth.pill) {
        return;
    }
    eth_get_info(&in);
    if (in.connected) {
        wl_pill(eth.pill, PILL_OK);
    } else {
        wl_pill(eth.pill, in.state, theme_hex(TC_chip), theme_hex(TC_text_value));
    }
    lv_label_set_text(eth.iface, in.iface);
    lv_label_set_text(eth.addr, in.addr[0] ? in.addr : "-");
    lv_label_set_text(eth.gw, in.gw[0] ? in.gw : "-");
}

static bool eth_kbd_ok() {
    char        val[32];
    const char *err;

    snprintf(val, sizeof(val), "%s", textarea_window_get());
    if (strlen(val) >= sizeof(eth_draft.field[0])) {
        err = "Too long";
    } else {
        err = eth_check_field(eth_kbd_field, val);
    }
    if (err) {
        msg_update_text_fmt("%s", err);
        return false;
    }
    strcpy(eth_draft.field[eth_kbd_field], val);
    return wl_kbd_close();
}

static void eth_field_cb(lv_event_t *e) {
    static const char *names[ETH_FIELDS] = {"IP address", "Netmask, 255.255.255.0 or /24", "Gateway (optional)", "DNS (optional)"};

    eth_kbd_field = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target(e));
    wl_kbd = true;
    lv_group_remove_all_objs(keyboard_group);
    textarea_window_open(eth_kbd_ok, wl_kbd_close);
    lv_textarea_set_max_length(textarea_window_text(), 15);
    lv_textarea_set_accepted_chars(textarea_window_text(), "0123456789./");     /* digits: the 1# key */
    lv_textarea_set_placeholder_text(textarea_window_text(), names[eth_kbd_field]);
    textarea_window_set(eth_draft.field[eth_kbd_field]);
}

static void eth_mode_cb(lv_event_t *e) {
    eth_draft.dhcp = lv_dropdown_get_selected(lv_event_get_target(e)) == 0;
}

static void eth_apply_cb(lv_event_t *e) {
    eth_cfg_t   cfg = eth_draft;
    const char *err = eth_check(&cfg);

    if (err) {
        msg_update_text_fmt("%s", err);
        return;
    }
    eth_draft = cfg;
    for (int i = 0; i < ETH_FIELDS; i++) {
        lv_label_set_text(lv_obj_get_child(eth.fields[i], 0), cfg.field[i][0] ? cfg.field[i] : "-");
    }
    eth_apply(&cfg);
    msg_update_text_fmt("Ethernet settings saved");
}

static uint8_t make_ethernet(uint8_t row) {
    static const char *labels[ETH_FIELDS] = {"IP address", "Netmask", "Gateway", "DNS"};
    lv_obj_t *obj;

    wl_row_label(row, "Status");
    obj = lv_obj_create(grid);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(obj, 12, 0);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 6, LV_GRID_ALIGN_CENTER, row, 1);
    eth.pill = wl_pill_create(obj, 0);
    eth.iface = wl_label(obj, SF(22), theme_hex(TC_text), 0);
    row++;

    for (int i = 0; i < 2; i++) {
        obj = wl_label(grid, SF(22), theme_hex(TC_text_dim), 0);
        lv_label_set_text(obj, i ? "Gateway in use" : "Address in use");
        lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);
        obj = wl_label(grid, SF(22), theme_hex(TC_text_dim), 0);
        lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 6, LV_GRID_ALIGN_CENTER, row, 1);
        *(i ? &eth.gw : &eth.addr) = obj;
        row++;
    }

    wl_row_label(row, "Mode");
    obj = lv_dropdown_create(grid);
    dialog_item(&dialog, obj);
    styles_dropdown_list(lv_dropdown_get_list(obj));
    lv_dropdown_set_options(obj, " DHCP \n Static ");
    lv_dropdown_set_symbol(obj, NULL);
    lv_dropdown_set_selected(obj, eth_draft.dhcp ? 0 : 1);
    lv_obj_add_event_cb(obj, eth_mode_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_size(obj, SMALL_3, ROW_H);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 3, LV_GRID_ALIGN_CENTER, row, 1);
    row++;

    for (int i = 0; i < ETH_FIELDS; i++) {
        wl_row_label(row, labels[i]);
        obj = wl_button(row, eth_draft.field[i][0] ? eth_draft.field[i] : "-", eth_field_cb);
        lv_obj_set_size(obj, SMALL_4, ROW_H - 4);
        lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 4, LV_GRID_ALIGN_CENTER, row, 1);
        lv_obj_set_user_data(obj, (void *)(intptr_t)i);
        eth.fields[i] = obj;
        row++;
    }

    obj = wl_label(grid, SF(22), theme_hex(TC_text), 0);   /* own colour: no leader dots */
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_CENTER, row, 1);
    wl_button(row, "Apply", eth_apply_cb);
    return row + 1;
}

/* An adapter plugged in or pulled out: the page again, with or without the card */
static void eth_rebuild(void *arg) {
    if (!wl_timer || wl_kbd || wl_menu) {
        return;     /* page gone, or busy: the keyboard and menu build it again when they close */
    }
    eth_draft_load();
    make_wireless_page();
}

static void wl_timer_cb(lv_timer_t *t) {
    eth_info_t in;

    eth_get_info(&in);
    if (in.present != eth_shown && !wl_kbd && !wl_menu) {
        lv_async_call(eth_rebuild, NULL);
    }
    wifi_update();
    eth_update();
}

/* Objects die with the page: late callbacks must not touch them */
static void bt_forget_objs() {
    bt.pill = bt.state = bt.addr = bt.cat = NULL;
    bt.scan_btn = bt.scan_note = bt.rx = bt.vol = bt.mute = NULL;
}

static void wl_delete_cb(lv_event_t *e) {
    wl_gen++;
    lv_timer_del(wl_timer);
    wl_timer = NULL;
    wl_menu_back = NULL;
    wl_menu_close();
    if (wl_kbd) {
        wl_kbd = false;
        textarea_window_close();
    }
}

static void grid_create() {
    card_n = 0;
    page = lv_obj_create(dialog.obj);
    lv_obj_set_size(page, 796 - RAIL_W - 14, DIALOG_H - 8);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_text_color(page, TC(text), 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(page, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(page, 6, 0);
    lv_obj_set_style_pad_row(page, 8, 0);
    lv_obj_set_style_pad_bottom(page, 40, 0);   /* room under the last row */
    lv_obj_set_style_text_font(page, SF(22), 0);  /* dialogs use 36 */

    lv_obj_align(page, LV_ALIGN_RIGHT_MID, -4, 0);   /* right of the rail */
}

static lv_obj_t *focus_label;   /* the focused row's label, bright */

static void focus_row_label(lv_obj_t *obj) {
    if (focus_label) {
        lv_obj_set_style_text_color(focus_label, TC(text_value), 0);
        focus_label = NULL;
    }
    while (obj && lv_obj_get_parent(obj) && lv_obj_get_style_layout(lv_obj_get_parent(obj), 0) != LV_LAYOUT_GRID) {
        obj = lv_obj_get_parent(obj);
    }
    if (!obj || !lv_obj_get_parent(obj)) return;

    lv_obj_t *g = lv_obj_get_parent(obj);
    int       r = lv_obj_get_style_grid_cell_row_pos(obj, 0);

    for (uint32_t i = 0; i < lv_obj_get_child_cnt(g); i++) {
        lv_obj_t *c = lv_obj_get_child(g, i);

        if (lv_obj_has_flag(c, ROW_LABEL) && lv_obj_get_style_grid_cell_row_pos(c, 0) == r) {
            focus_label = c;
            lv_obj_set_style_text_color(c, TC(text), 0);
            break;
        }
    }
}

/* MFK focus: bring the whole row into view, with a margin (the default
 * scroll-on-focus left rows at the bottom edge half hidden) */
static void focus_cb(lv_group_t *g) {
    lv_obj_t *obj = lv_group_get_focused(g);

    focus_row_label(obj);
    if (!page || !obj || !lv_obj_has_flag(page, LV_OBJ_FLAG_SCROLLABLE)) {
        return;
    }
    lv_area_t  a, p;
    lv_coord_t margin = 24;

    lv_obj_update_layout(page);

    /* Rows without controls above the first or below the last one (status
     * lines, card ends) would never come into view: show the page ends */
    lv_obj_t **first = (lv_obj_t **)_lv_ll_get_head(&g->obj_ll);
    lv_obj_t **last  = (lv_obj_t **)_lv_ll_get_tail(&g->obj_ll);

    if (first && *first == obj) {   /* also the only one while the page is built */
        lv_obj_scroll_to_y(page, 0, LV_ANIM_OFF);
        return;
    }
    if (last && *last == obj) {
        lv_coord_t below = lv_obj_get_scroll_bottom(page);

        if (below > 0) lv_obj_scroll_by(page, 0, -below, LV_ANIM_OFF);
        return;
    }
    lv_obj_get_coords(obj, &a);
    lv_obj_get_coords(page, &p);
    if (a.y2 + margin > p.y2) {
        lv_obj_scroll_by(page, 0, p.y2 - (a.y2 + margin), LV_ANIM_OFF);
    } else if (a.y1 - margin < p.y1) {
        lv_obj_scroll_by(page, 0, p.y1 - (a.y1 - margin), LV_ANIM_OFF);
    }
}

static void grid_delete() {
    if (page) {
        lv_group_focus_cb_t cb = lv_group_get_focus_cb(keyboard_group);

        lv_group_set_editing(keyboard_group, false);
        // lv_group_remove_all_objs(keyboard_group);
        /* Not while the rows go: the focus steps back over them and focus_cb() would colour
         * a row label already deleted (Settings opened again on the theme, then a tab) */
        lv_group_set_focus_cb(keyboard_group, NULL);
        lv_obj_del(page);
        lv_group_set_focus_cb(keyboard_group, cb);
        page = NULL;
        grid = NULL;
        focus_label = NULL;
    }
}

/* Station: callsign and QTH locator, edited with the on-screen keyboard */

#define ST_CALLSIGN 1
#define ST_QTH      2

static int st_kbd;  /* keyboard open for ST_CALLSIGN / ST_QTH */

/* The keyboard gets all the keys: the page is built again when it closes */
static bool st_kbd_close() {
    st_kbd = 0;
    textarea_window_close();
    make_general_page();
    return true;
}

static bool st_kbd_ok() {
    const char *val = textarea_window_get();
    bool        ok  = st_kbd == ST_CALLSIGN ? station_callsign_save(val) : station_qth_save(val);

    return ok && st_kbd_close();
}

static void st_kbd_open_cb(lv_event_t *e) {
    st_kbd = (int)(intptr_t)lv_obj_get_user_data(lv_event_get_current_target(e));
    lv_group_remove_all_objs(keyboard_group);
    textarea_window_open(st_kbd_ok, st_kbd_close);
    if (st_kbd == ST_CALLSIGN) {
        station_callsign_edit(textarea_window_text());
    } else {
        station_qth_edit(textarea_window_text());
    }
}

static lv_timer_t *wv_timer;   /* the wfview client status on General */

static void wv_timer_cb(lv_timer_t *t) {
    wv_update();
}

static void st_delete_cb(lv_event_t *e) {
    if (st_kbd || wl_kbd) {
        st_kbd = 0;
        wl_kbd = false;
        textarea_window_close();
    }
    if (wv_timer) {
        lv_timer_del(wv_timer);
        wv_timer = NULL;
    }
}

static uint8_t make_station_row(uint8_t row, const char *label, const char *val, int which) {
    wl_row_label(row, label);

    lv_obj_t *obj = wl_button(row, val[0] ? val : "-", st_kbd_open_cb);
    lv_obj_set_size(obj, 150, ROW_H - 4);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 1, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_user_data(obj, (void *)(intptr_t)which);
    return row + 1;
}

static uint8_t make_station(uint8_t row) {
    row = make_station_row(row, "Callsign", params.callsign.x, ST_CALLSIGN);
    row = make_station_row(row, "QTH locator", params.qth.x, ST_QTH);
    lv_obj_add_event_cb(grid, st_delete_cb, LV_EVENT_DELETE, NULL);
    return row;
}

/* Audio: microphone, VOX, AGC */

typedef struct {
    ParamInt   *param;
    int32_t     min;
    int32_t     max;
    int32_t     step;
    const char *fmt;
} param_slider_t;

static param_slider_t hmic_slider      = {&cfg_sm.p_hmic, 0, 50, 1, "%i"};
static param_slider_t imic_slider      = {&cfg_sm.p_imic, 0, 50, 1, "%i"};
static param_slider_t vox_gain_slider  = {&cfg_sm.p_vox_gain, 0, 100, 1, "%i"};
static param_slider_t vox_ag_slider    = {&cfg_sm.p_vox_ag, 0, 100, 1, "%i"};
static param_slider_t vox_delay_slider = {&cfg_sm.p_vox_delay, 100, 2000, 50, "%i ms"};
static param_slider_t agc_knee_slider  = {&cfg_sm.p_agc_knee, -100, 0, 1, "%i dB"};
static param_slider_t agc_slope_slider = {&cfg_sm.p_agc_slope, 0, 10, 1, "%i dB"};
static param_slider_t smeter_cal_slider = {&cfg_sm.p_smeter_cal, -40, 20, 1, "%+i dB"};
static param_slider_t snap_hold_slider = {&cfg_sm.p_spot_snap_hold, 0, 5, 1, "%i"};
static param_slider_t snap_slow_slider = {&cfg_sm.p_spot_snap_slow, 1, 5, 1, "%i"};
static param_slider_t accel_onset_slider = {&cfg_sm.p_freq_accel_onset, 1, 5, 1, "%i"};
static param_slider_t wf_speed_slider = {&cfg_sm.p_waterfall_speed, 1, 5, 1, "%i"};
static param_slider_t spectrum_height_slider = {&cfg_sm.p_spectrum_height, 60, 160, 4, "%i px"};

static void param_slider_cb(lv_event_t *e) {
    lv_obj_t       *obj = lv_event_get_target(e);
    param_slider_t *p   = (param_slider_t *)lv_event_get_user_data(e);
    int32_t         val = lv_slider_get_value(obj) * p->step;

    lv_label_set_text_fmt((lv_obj_t *)lv_obj_get_user_data(obj), p->fmt, val);
    p->param->set(val);
}

/* S-meter cal, when its editing starts: what it is for, before the reading drifts from the truth.
 * Built as the cluster alert (theme roles, popup radius, sony_18 title), framed in the error colour */
static lv_obj_t   *smeter_warn;
static lv_timer_t *smeter_warn_timer;

static void smeter_warn_close(lv_timer_t *t) {
    if (smeter_warn) {
        lv_obj_del(smeter_warn);
        smeter_warn = NULL;
    }
    smeter_warn_timer = NULL;
}

extern "C" void smeter_warn_show(void) {   /* not static: the UI harness shows it in every theme */
    lv_obj_t *l;

    if (smeter_warn) {
        return;
    }
    smeter_warn = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(smeter_warn);
    lv_obj_set_size(smeter_warn, 560, LV_SIZE_CONTENT);
    lv_obj_align(smeter_warn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(smeter_warn, TC(popup), 0);
    lv_obj_set_style_bg_opa(smeter_warn, 240, 0);
    lv_obj_set_style_border_color(smeter_warn, TC(err), 0);
    lv_obj_set_style_border_width(smeter_warn, 3, 0);
    lv_obj_set_style_radius(smeter_warn, theme_shape.radius_popup, 0);
    lv_obj_set_style_pad_all(smeter_warn, 14, 0);
    lv_obj_set_style_pad_row(smeter_warn, 8, 0);
    lv_obj_set_flex_flow(smeter_warn, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(smeter_warn, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    l = lv_label_create(smeter_warn);
    lv_obj_set_style_text_font(l, &sony_18, 0);
    lv_obj_set_style_text_color(l, TC(err), 0);
    lv_label_set_text(l, "S-METER CALIBRATION");

    l = lv_label_create(smeter_warn);
    lv_obj_set_width(l, lv_pct(100));
    lv_obj_set_style_text_font(l, &sony_24, 0);
    lv_obj_set_style_text_color(l, TC(text), 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_label_set_text(l, "-22 dB matches an IC-7300. Change it only reading the same signal "
                         "on a calibrated reference (radio or generator).");

    smeter_warn_timer = lv_timer_create(smeter_warn_close, 5000, NULL);
    lv_timer_set_repeat_count(smeter_warn_timer, 1);
}

static void smeter_cal_warn_cb(lv_event_t *e) {
    lv_obj_t   *obj = lv_event_get_target(e);
    lv_indev_t *in  = lv_indev_get_act();
    bool        pointer = in && lv_indev_get_type(in) == LV_INDEV_TYPE_POINTER;
    lv_group_t *g   = (lv_group_t *)lv_obj_get_group(obj);

    /* Touch: pressing it changes it. Knob: the press that starts editing focuses it in edit
     * mode (it sends no PRESSED; the press that ends editing does) */
    if ((lv_event_get_code(e) == LV_EVENT_PRESSED && pointer) ||
        (lv_event_get_code(e) == LV_EVENT_FOCUSED && g && lv_group_get_editing(g))) {
        smeter_warn_show();
    }
}

static uint8_t make_param_slider(uint8_t row, const char *label, param_slider_t *p) {
    lv_obj_t *obj;

    wl_row_label(row, label);

    /* As the other sliders: the right half, value at the right */
    obj = lv_obj_create(grid);
    lv_obj_set_size(obj, SMALL_3, ROW_H);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 4, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(obj);
    obj = slider_with_text(obj, p->param->get(), p->min, p->max, p->step, SMALL_3 - 134, p->fmt, param_slider_cb, p);
    if (p == &smeter_cal_slider) {
        lv_obj_add_event_cb(obj, smeter_cal_warn_cb, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(obj, smeter_cal_warn_cb, LV_EVENT_FOCUSED, NULL);
    }

    return row + 1;
}

static uint8_t make_param_switch(uint8_t row, const char *label, ParamInt &param) {
    lv_obj_t *obj;

    wl_row_label(row, label);

    obj = lv_obj_create(grid);
    lv_obj_set_size(obj, SMALL_3, ROW_H);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 4, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(obj);
    obj = switch_bool(obj, param);
    lv_obj_set_width(obj, SMALL_3 - 30);

    return row + 1;
}

/* The microphone input: Audio > MICROPHONE (p_mic) and the walkie's own (p_walkie_mic) */
static void mic_update_cb(lv_event_t *e) {
    ((ParamInt *)lv_event_get_user_data(e))->set(lv_dropdown_get_selected(lv_event_get_target(e)));
}

static uint8_t make_mic(uint8_t row, const char *label, ParamInt &param) {
    lv_obj_t *obj;

    wl_row_label(row, label);

    obj = lv_dropdown_create(grid);
    dialog_item(&dialog, obj);
    styles_dropdown_list(lv_dropdown_get_list(obj));
    /* Same order as x6100_mic_builtin, x6100_mic_handle, x6100_mic_auto */
    lv_dropdown_set_options(obj, " Built-in \n Hand \n Auto ");
    lv_dropdown_set_symbol(obj, NULL);
    lv_dropdown_set_selected(obj, param.get());
    lv_obj_add_event_cb(obj, mic_update_cb, LV_EVENT_VALUE_CHANGED, &param);
    lv_obj_set_size(obj, SMALL_3, ROW_H);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 4, 3, LV_GRID_ALIGN_CENTER, row, 1);

    return row + 1;
}

/* Time zone: POSIX TZ strings, applied at once (see clock_tz_set) */

static const struct { const char *label; const char *posix; } zones[] = {
    { "UTC",                              "UTC0" },
    { "Europe/London (GMT/BST)",          "GMT0BST,M3.5.0/1,M10.5.0" },
    { "Europe/Rome (CET/CEST)",           "CET-1CEST,M3.5.0,M10.5.0/3" },
    { "Europe/Athens (EET/EEST)",         "EET-2EEST,M3.5.0/3,M10.5.0/4" },
    { "Europe/Moscow (MSK)",              "MSK-3" },
    { "America/New_York (EST/EDT)",       "EST5EDT,M3.2.0,M11.1.0" },
    { "America/Chicago (CST/CDT)",        "CST6CDT,M3.2.0,M11.1.0" },
    { "America/Denver (MST/MDT)",         "MST7MDT,M3.2.0,M11.1.0" },
    { "America/Los_Angeles (PST/PDT)",    "PST8PDT,M3.2.0,M11.1.0" },
    { "Asia/Tokyo (JST)",                 "JST-9" },
    { "Australia/Sydney (AEST/AEDT)",     "AEST-10AEDT,M10.1.0,M4.1.0/3" },
};
#define ZONES_NUM   (sizeof(zones) / sizeof(zones[0]))

static void zone_update_cb(lv_event_t *e) {
    clock_tz_set(zones[lv_dropdown_get_selected(lv_event_get_target(e))].posix);

    now = time(NULL);
    localtime_r(&now, &ts);
    zone_refresh = true;
    lv_spinbox_set_value(day, ts.tm_mday);
    lv_spinbox_set_value(month, ts.tm_mon + 1);
    lv_spinbox_set_value(year, ts.tm_year + 1900);
    lv_spinbox_set_value(hour, ts.tm_hour);
    lv_spinbox_set_value(min, ts.tm_min);
    lv_spinbox_set_value(sec, ts.tm_sec);
    zone_refresh = false;
}

static uint8_t make_zone(uint8_t row) {
    lv_obj_t    *obj;
    std::string  options;
    const char  *tz = getenv("TZ");
    uint16_t     sel = 0;

    wl_row_label(row, "Zone");

    for (size_t i = 0; i < ZONES_NUM; i++) {
        options += std::string(i ? "\n " : " ") + zones[i].label + " ";
        if (tz && !strcmp(tz, zones[i].posix)) {
            sel = i;
        }
    }

    obj = lv_dropdown_create(grid);
    dialog_item(&dialog, obj);
    lv_obj_set_style_text_font(obj, &sony_22, 0);   /* the long zone names fit at this size only */
    styles_dropdown_list(lv_dropdown_get_list(obj));
    lv_obj_set_style_text_font(lv_dropdown_get_list(obj), SF(22), 0);
    lv_dropdown_set_options(obj, options.c_str());
    lv_dropdown_set_symbol(obj, NULL);
    lv_dropdown_set_selected(obj, sel);
    lv_obj_add_event_cb(obj, zone_update_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_size(obj, 270, ROW_H);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 3, LV_GRID_ALIGN_CENTER, row, 1);

    return row + 1;
}

static void make_general_page() {
    grid_delete();
    grid_create();

    uint8_t row;

    now = time(NULL);
    struct tm *t = localtime(&now);

    memcpy(&ts, t, sizeof(ts));

    row = card_begin("STATION", col_station);
    row = make_station(row);
    card_end(row);

    row = card_begin("DATE / TIME", col_datetime);
    row = make_date(row);
    row = make_time(row);
    row = make_zone(row);
    card_end(row);

    row = card_begin("CONTROLS");
    row = make_freq_accel(row);
    row = make_param_slider(row, "Accel. onset", &accel_onset_slider);
    row = make_charger(row);
    row = make_param_slider(row, "S-meter cal", &smeter_cal_slider);
    row = make_walkie(row);
    card_end(row);

    row = card_begin("TRANSVERTERS");
    row = make_transverter(row, cfg_sm.p_transverter_0_from, cfg_sm.p_transverter_0_to, cfg_sm.p_transverter_0_shift);
    row = make_transverter(row, cfg_sm.p_transverter_1_from, cfg_sm.p_transverter_1_to, cfg_sm.p_transverter_1_shift);
    card_end(row);

    row = card_begin("WFVIEW SERVER");
    row = make_wfview(row);
    card_end(row);
    wv_update();
    wv_timer = lv_timer_create(wv_timer_cb, 1000, NULL);   /* deleted by st_delete_cb */
}

/* The Walkie Talkie screen: on (Settings closes, the portrait screen comes up), its knob, its mic */
static void make_walkie_page() {
    grid_delete();
    grid_create();

    uint8_t row = card_begin("WALKIE TALKIE");
    row = make_param_switch(row, "Walkie Talkie", cfg_sm.p_walkie);
    row = make_param_switch(row, "Main knob steps channels", cfg_sm.p_walkie_mem);
    row = make_mic(row, "Microphone", cfg_sm.p_walkie_mic);
    card_end(row);
}

/* What the front keys (long press) and the hand mic keys do */
static void make_keys_page() {
    grid_delete();
    grid_create();
    uint8_t row;

    row = card_begin("FRONT KEYS, LONG PRESS");
    row = make_long_action(row);
    card_end(row);

    row = card_begin("HAND MIC KEYS");
    row = make_hmic_action(row);
    card_end(row);
}

/* Cluster: which spots the app, the column and the waterfall show. Saved in
 * cluster.conf next to the web configurator's filters and alerts. */

#define CF_MODES 4

static const uint8_t cf_mode_bit[CF_MODES]  = { CLUSTER_FM_CW, CLUSTER_FM_SSB, CLUSTER_FM_DIG, CLUSTER_FM_AMFM };
static const char   *cf_mode_name[CF_MODES] = { "CW", "SSB", "DIG", "AM/FM" };

static struct {
    lv_obj_t *mode[CF_MODES];
    lv_obj_t *band[16];
    int       band_n;
    lv_obj_t *act[CLUSTER_ACT_COUNT];
} cf;

static void cf_save() {
    uint8_t  modes = 0;
    uint16_t bands = 0;
    uint8_t  act   = 0;
    int      on    = 0;

    for (int i = 0; i < CF_MODES; i++) {
        if (lv_obj_has_state(cf.mode[i], LV_STATE_CHECKED)) modes |= cf_mode_bit[i];
    }
    for (int b = 0; b < cf.band_n; b++) {
        if (lv_obj_has_state(cf.band[b], LV_STATE_CHECKED)) {
            bands |= 1u << b;
            on++;
        }
    }
    if (on == cf.band_n) bands = 0;     /* all of them: no filter */
    for (int i = 0; i < CLUSTER_ACT_COUNT; i++) {
        if (lv_obj_has_state(cf.act[i], LV_STATE_CHECKED)) act |= 1u << i;
    }
    cluster_filter_save(modes, bands, act);
}

static void cf_toggle_cb(lv_event_t *e) {
    lv_obj_t  *obj  = lv_event_get_target(e);
    lv_obj_t **list = (lv_obj_t **)lv_event_get_user_data(e);
    int        n    = list == cf.mode ? CF_MODES : list == cf.act ? CLUSTER_ACT_COUNT : cf.band_n;
    bool       any  = list == cf.act;  /* activity: none lit is all spots */

    for (int i = 0; i < n; i++) any |= lv_obj_has_state(list[i], LV_STATE_CHECKED);
    if (!any) {
        lv_obj_add_state(obj, LV_STATE_CHECKED);    /* one at least: none would hide every spot */
        return;
    }
    cf_save();
}

static lv_obj_t * cf_toggle(uint8_t row, int col, const char *text, bool on, lv_obj_t **list) {
    lv_obj_t *obj = lv_btn_create(grid);

    lv_obj_remove_style_all(obj);
    dialog_item(&dialog, obj);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_style_radius(obj, 8, 0);
    lv_obj_set_style_border_width(obj, 2, 0);
    lv_obj_set_style_border_color(obj, TC(control_border), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(obj, TC(toggle_on), LV_STATE_CHECKED);
    lv_obj_set_style_text_color(obj, TC(text), LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(obj, TC(toggle_focus), LV_STATE_CHECKED | LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(obj, TC(focus_text), LV_STATE_CHECKED | LV_STATE_FOCUSED);
    lv_obj_set_size(obj, SMALL_1, ROW_H - 4);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, col, 1, LV_GRID_ALIGN_CENTER, row, 1);
    if (on) lv_obj_add_state(obj, LV_STATE_CHECKED);
    lv_obj_add_event_cb(obj, cf_toggle_cb, LV_EVENT_VALUE_CHANGED, list);

    lv_obj_t *label = lv_label_create(obj);
    lv_obj_set_style_text_font(label, theme_shape.vision ? &sony_14 : &sony_18, 0);   /* VISION scales it to 20: AM/FM fits */
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return obj;
}

static void make_cluster_page() {
    const cluster_filter_t *f = cluster_filter();
    uint8_t                 row;
    lv_obj_t               *obj;

    grid_delete();
    grid_create();

    row = card_begin("CLUSTER FILTERS");

    wl_row_label(row, "Modes");
    for (int i = 0; i < CF_MODES; i++) {
        cf.mode[i] = cf_toggle(row, 3 + i, cf_mode_name[i], f->modes & cf_mode_bit[i], cf.mode);
    }
    row++;

    /* Two rows of bands, the second right-aligned */
    cf.band_n = 0;
    while (cf.band_n < 16 && cluster_band_name(cf.band_n)[0]) cf.band_n++;
    int first = (cf.band_n + 1) / 2;

    wl_row_label(row, "Bands (m)");
    for (int b = 0; b < cf.band_n; b++) {
        char name[8];
        int  r   = b < first ? row : row + 1;
        int  col = b < first ? 1 + b + (6 - first) : 1 + (b - first) + (6 - (cf.band_n - first));

        snprintf(name, sizeof(name), "%d", atoi(cluster_band_name(b)));
        cf.band[b] = cf_toggle(r, col, name, !f->bands || (f->bands & (1u << b)), cf.band);
    }
    row += 2;

    /* Two on a row, two columns each: WWBOTA is wider than a mode */
    wl_row_label(row, "Activity");
    for (int i = 0; i < CLUSTER_ACT_COUNT; i++) {
        cf.act[i] = cf_toggle(row + i / 2, 3 + i % 2 * 2, cluster_act_names[i], f->activity & (1u << i), cf.act);
        lv_obj_set_width(cf.act[i], SMALL_1 * 2 + SMALL_PAD);
        lv_obj_set_grid_cell(cf.act[i], LV_GRID_ALIGN_START, 3 + i % 2 * 2, 2, LV_GRID_ALIGN_CENTER, row + i / 2, 1);
    }
    row += 2;

    obj = wl_label(grid, SF(18), theme_hex(TC_text_dim), 0);
    lv_label_set_text(obj, "For the app, the column and the waterfall. Alerts always show.\n"
                           "Activity: none lit = all spots. Max age, spotters, alerts: web Cluster page.");
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 0, 7, LV_GRID_ALIGN_CENTER, row, 1);
    row_dsc[row] = LV_GRID_CONTENT;
    row++;

    card_end(row);

    /* The same settings as the Cluster app's Order and Band / All and the MFK items */
    row = card_begin("CLUSTER VIEW");
    row = make_switch_row(row, "Newest first", cfg_sm.p_cluster_order);
    row = make_switch_row(row, "All bands", cfg_sm.p_cluster_all_bands);
    row = make_param_slider(row, "Spot snap hold", &snap_hold_slider);
    row = make_param_slider(row, "Spot snap speed", &snap_slow_slider);
    card_end(row);

    /* The other way: what this radio decodes, reported to pskreporter.info */
    row = card_begin("PSK REPORTER");
    row = make_switch_row(row, "Report decodes", cfg_sm.p_pskreporter);   /* FT8 FT4 FT2 JS8 PSK31 PSK63 Olivia */
    card_end(row);
}

/* SWR protection: the most SWR a transmission keeps (tenths), 1.0 off; steps of 0.5 */
static void swr_protect_cb(lv_event_t *e) {
    lv_obj_t *obj   = lv_event_get_target(e);
    lv_obj_t *label = (lv_obj_t *)lv_obj_get_user_data(obj);
    int32_t   val   = lv_slider_get_value(obj) * 5;

    if (val <= 10) {
        lv_label_set_text(label, "Off");
    } else {
        lv_label_set_text_fmt(label, "%i.%i", val / 10, val % 10);
    }
    cfg_sm.p_swr_protect.set(val);
}

static uint8_t make_swr_protect(uint8_t row) {
    lv_obj_t *obj;

    wl_row_label(row, "SWR protection");

    obj = lv_obj_create(grid);
    lv_obj_set_size(obj, SMALL_3, ROW_H);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 4, 3, LV_GRID_ALIGN_CENTER, row, 1);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_center(obj);
    obj = slider_with_text(obj, cfg_sm.p_swr_protect.get(), 10, 50, 5, SMALL_3 - 120, "%i", swr_protect_cb);
    lv_event_send(obj, LV_EVENT_VALUE_CHANGED, NULL);   /* the label as 2.5 / Off */

    return row + 1;
}

/* TX CAL: the transmitter's calibration and CESSB, per band where the base keeps them per band */
static void make_txcal_page() {
    grid_delete();
    grid_create();
    uint8_t row;

    x6100_base_ver_t base_ver = x6100_control_get_base_ver();

    row = card_begin("TX CALIBRATION");
    row = make_tx_offset(row);
    if (base_ver.rev >= 8) {
        row = band_out_gain_correction(row);
    }
    if (base_ver.rev >= 3) {
        row = make_codec_gain(row);
    }
    card_end(row);

    if (base_ver.rev >= 12) {
        row = card_begin("CESSB");
        row = make_cessb(row);
        card_end(row);
    }

    row = card_begin("TX PROTECTION");
    row = make_swr_protect(row);
    card_end(row);
}

static void make_display_page() {
    grid_delete();
    grid_create();
    uint8_t row;

    row = card_begin("BACKLIGHT");
    row = make_backlight(row);
    card_end(row);

    row = card_begin("SPECTRUM");
    row = make_spectrum_show(row);
    if (cfg_sm.p_spectrum_show.get()) {
        row = make_spectrum_fill_peak(row);
        row = make_param_switch(row, "Fill gradient", cfg_sm.p_spectrum_fill_gradient);
        row = make_param_slider(row, "Height", &spectrum_height_slider);   /* live, at each step */
        row = make_birdie_filter(row);
    }
    card_end(row);

    row = card_begin("WATERFALL");
    row = make_waterfall_mode(row);
    row = make_auto_offset(row);
    row = make_spectrum_min_max(row);
    row = make_waterfall_line_zoom(row);
    row = make_waterfall_smooth_scroll(row);
    row = make_param_slider(row, "Speed", &wf_speed_slider);
    card_end(row);

    /* Overlays: shown with or without the spectrum */
    row = card_begin("MAIN SCREEN");
    row = make_band_plan(row);
    row = make_cluster_spots_wf(row);
    row = make_cluster_column(row);
    row = make_smeter_needle(row);
    row = make_smeter_color(row);
    row = make_param_switch(row, "Needle lamp", cfg_sm.p_smeter_lamp);
    card_end(row);

    row = card_begin("INTERFACE");
    row = make_mag(row);
    row = make_knob_info(row);
    row = make_theme(row);
    row = make_font(row);
    row = make_digi_text(row);
    row = make_ft8_info(row);
    card_end(row);
}

static void make_audio_page() {
    grid_delete();
    grid_create();
    uint8_t row;

    x6100_base_ver_t base_ver = x6100_control_get_base_ver();

    /* RX / speaker */
    row = card_begin("RX AUDIO");
    row = make_sp_mode(row);
    row = make_line_gain(row);
    row = make_audio_gain(row);
    card_end(row);

    /* TX audio */
    if (base_ver.rev >= 3) {
        row = card_begin("TX AUDIO");
        row = make_comp_th_makeup(row);
        if (base_ver.rev >= 9) {
            row = make_tx_filter(row);
        }
        if (base_ver.rev >= 8) {
            row = make_fm_emphasis(row);
        }
        card_end(row);
    }

    row = card_begin("MICROPHONE");
    row = make_mic(row, "Input", cfg_sm.p_mic);
    row = make_param_slider(row, "Hand mic", &hmic_slider);
    row = make_param_slider(row, "Built-in", &imic_slider);
    card_end(row);

    row = card_begin("VOX");
    row = make_param_switch(row, "On", cfg_sm.p_vox_en);
    row = make_param_slider(row, "Gain", &vox_gain_slider);
    row = make_param_slider(row, "Anti-VOX", &vox_ag_slider);
    row = make_param_slider(row, "Delay", &vox_delay_slider);
    card_end(row);

    row = card_begin("AGC");
    row = make_param_switch(row, "Hang", cfg_sm.p_agc_hang);
    row = make_param_slider(row, "Knee", &agc_knee_slider);
    row = make_param_slider(row, "Slope", &agc_slope_slider);
    card_end(row);

}

static void make_wireless_page() {
    grid_delete();
    grid_create();
    uint8_t row;

    wl_tab = "Wireless";
    row = card_begin("WIFI");
    row = make_wifi(row);
    card_end(row);

    eth_info_t in;

    eth_get_info(&in);
    eth = {};
    eth_shown = in.present;
    if (eth_shown) {
        row = card_begin("ETHERNET");
        row = make_ethernet(row);
        card_end(row);
    }

    lv_obj_add_event_cb(grid, wl_delete_cb, LV_EVENT_DELETE, NULL);
    wl_timer = lv_timer_create(wl_timer_cb, 1000, NULL);
    wifi_update();
    eth_update();
}

/* Bluetooth: the adapter, the devices (bt_dev.c), the headphone audio
 * (btaudio-ctl) and the CAT bridge (btcat). Nothing here waits: bt_dev's
 * worker talks to BlueZ, the page reads what it saw. */

static void bt_adapter_update() {
    bt_adapter_t a;
    int          left = bt_dev_visible_s();
    const char  *text = "";

    bt_dev_adapter(&a);
    if (!a.seen) {
        wl_pill(bt.pill, NULL, 0, 0);
        text = "-";
    } else if (!a.bluez) {
        wl_pill(bt.pill, "off", theme_hex(TC_chip), theme_hex(TC_text_value));
        text = "Bluetooth service not running";
    } else if (!a.present) {
        wl_pill(bt.pill, "off", theme_hex(TC_chip), theme_hex(TC_text_value));
        text = params.wifi_enabled.x ? "no adapter" : "WiFi/BT chip off (Wireless)";
    } else if (!a.powered) {
        wl_pill(bt.pill, "off", theme_hex(TC_chip), theme_hex(TC_text_value));
    } else if (left > 0) {
        wl_pill(bt.pill, "pairing", theme_hex(TC_pill_hp), theme_hex(TC_accent));
        text = "visible and pairable";
    } else {
        wl_pill(bt.pill, "on", theme_hex(TC_ok_bg), theme_hex(TC_ok));
        text = "hidden";
    }
    lv_label_set_text(bt.state, text);
    lv_label_set_text(bt.addr, a.present && a.addr[0] ? a.addr : "-");
}

/* btcat writes the number of its clients while it runs */
static void bt_cat_update() {
    int   n = -1;
    FILE *f = fopen("/tmp/btcat.clients", "r");

    if (f) {
        if (fscanf(f, "%d", &n) != 1) n = 0;
        fclose(f);
    }
    if (n < 0) {
        lv_label_set_text(bt.cat, "bridge not running");
    } else if (n == 0) {
        lv_label_set_text(bt.cat, "none connected");
    } else {
        lv_label_set_text_fmt(bt.cat, "%i connected", n);
    }
}

static void bt_timer_cb(lv_timer_t *t) {
    bt_update();
    bt_adapter_update();
    bt_cat_update();
    if (++bt_ticks % 2 == 0) {
        bt_dev_refresh();
    }
}

static void bt_page_delete_cb(lv_event_t *e) {
    wl_gen++;
    bt_forget_objs();
    lv_timer_del(bt_timer);
    bt_timer = NULL;
    wl_menu_back = NULL;
    wl_menu_close();
}

/* A value after a row label, as WiFi's Status: a pill and a text, or a text */
static lv_obj_t * bt_value_row(uint8_t row, const char *label, lv_obj_t **pill) {
    lv_obj_t *obj = lv_obj_create(grid);

    wl_row_label(row, label);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(obj, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(obj, 12, 0);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_START, 1, 6, LV_GRID_ALIGN_CENTER, row, 1);
    if (pill) *pill = wl_pill_create(obj, 0);
    return wl_label(obj, SF(22), theme_hex(TC_text), 0);
}

static void make_bluetooth_page() {
    grid_delete();
    grid_create();
    uint8_t row;

    wl_tab = "Bluetooth";
    bt.found.clear();
    bt.note.clear();
    bt.was_scanning = false;

    row = card_begin("ADAPTER");
    row = make_bt_power(row);
    bt.state = bt_value_row(row++, "Status", &bt.pill);
    bt.addr = bt_value_row(row++, "Address", NULL);
    row = make_bt_pairing(row);
    card_end(row);

    row = card_begin("DEVICES");
    row = make_bt_devices(row);
    card_end(row);

    row = card_begin("HEADPHONES");
    row = make_headphones(row);
    card_end(row);

    row = card_begin("BLUETOOTH CAT");
    bt.cat = bt_value_row(row++, "Clients", NULL);
    lv_label_set_text(bt_value_row(row++, "Port", NULL), "SPP, channel 1");
    card_end(row);

    lv_obj_add_event_cb(grid, bt_page_delete_cb, LV_EVENT_DELETE, NULL);
    bt_ticks = 0;
    bt_timer = lv_timer_create(bt_timer_cb, 1000, NULL);
    bt_dev_refresh();
    bt_update();
    bt_adapter_update();
    bt_cat_update();
    /* Headphone volume and RX switch once; wl_gen drops the answer if the page is left */
    btaudio_run("status", bt_status_done, (void *)(intptr_t)wl_gen);
}

static uint8_t info_row(uint8_t row, const char *label, const char *val) {
    wl_row_label(row, label);
    lv_obj_t *obj = lv_label_create(grid);
    lv_label_set_text(obj, val);
    lv_obj_set_style_text_color(obj, TC(text), 0);
    lv_obj_set_grid_cell(obj, LV_GRID_ALIGN_END, 1, 6, LV_GRID_ALIGN_CENTER, row, 1);
    return row + 1;
}

/* Diagnostics (Info): x6100-diag collects them, they are shown here and go to x6100-link.it only
 * with Send. The first time a short note says what they are for. */
#define DIAG_CTL "/usr/sbin/x6100-diag"
#define DIAG_ACK "/mnt/x6100-diag.ack"

static lv_obj_t *diag_note;     /* the row's note, NULL off the page */
static lv_obj_t *diag_btn;
static lv_obj_t *diag_panel;
static bool      diag_busy;

static void diag_ptr_clear_cb(lv_event_t *e) {
    *(lv_obj_t **)lv_event_get_user_data(e) = NULL;
}

static void diag_say(const char *s) {
    if (diag_note) lv_label_set_text(diag_note, s);
}

static void diag_close(void *arg) {
    if (!diag_panel) return;
    lv_obj_del(diag_panel);
    if (diag_btn) lv_group_focus_obj(diag_btn);
    lv_group_set_editing(keyboard_group, false);
}

static void diag_sent(const char *out, int status, void *user) {
    char s[64];

    diag_busy = false;
    if (status == 0 && *out) {
        snprintf(s, sizeof(s), "sent, code %.12s", out);
        diag_say(s);
    } else if (!strncmp(out, "CLOSED", 6)) {
        diag_say("not accepted now: ask IU1WXY first");
    } else if (!strncmp(out, "LIMIT", 5)) {
        diag_say("too many today, try tomorrow");
    } else {
        diag_say("not sent: no internet?");
    }
}

static void diag_send(void *arg) {
    FILE *f;

    diag_close(NULL);
    if ((f = fopen(DIAG_ACK, "w"))) fclose(f);
    diag_busy = true;
    diag_say("sending...");
    sh_run(DIAG_CTL " send", diag_sent, NULL);
}

static void diag_key_cb(lv_event_t *e) {
    lv_obj_t *box = (lv_obj_t *)lv_event_get_user_data(e);

    switch (*((uint32_t *)lv_event_get_param(e))) {
        case LV_KEY_LEFT:
        case LV_KEY_UP:     lv_obj_scroll_by(box, 0, 60, LV_ANIM_OFF); break;
        case LV_KEY_RIGHT:
        case LV_KEY_DOWN:   lv_obj_scroll_by(box, 0, -60, LV_ANIM_OFF); break;
        case LV_KEY_ENTER:  lv_async_call(diag_send, NULL); break;
        case LV_KEY_ESC:    lv_async_call(diag_close, NULL); break;
        case KEY_VOL_LEFT_EDIT:
        case KEY_VOL_LEFT_SELECT:   radio_change_vol(-1); break;
        case KEY_VOL_RIGHT_EDIT:
        case KEY_VOL_RIGHT_SELECT:  radio_change_vol(1); break;
    }
}

static void diag_click_cb(lv_event_t *e) {
    lv_async_call(lv_event_get_user_data(e) ? diag_send : diag_close, NULL);
}

/* MFK press on the panel: the encoder sends a click, not ENTER (the panel is neither
 * editable nor scrollable), so without this the press did nothing */
static void diag_panel_click_cb(lv_event_t *e) {
    lv_indev_t *indev = lv_indev_get_act();

    if (lv_event_get_target(e) == diag_panel && indev && lv_indev_get_type(indev) == LV_INDEV_TYPE_ENCODER) {
        lv_async_call(diag_send, NULL);
    }
}

static lv_obj_t * diag_panel_btn(lv_obj_t *parent, const char *text, bool send) {
    lv_obj_t *obj = lv_btn_create(parent);

    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, 160, 40);
    lv_obj_set_style_radius(obj, 8, 0);
    lv_obj_set_style_border_width(obj, 2, 0);
    lv_obj_set_style_border_color(obj, send ? TC(ok) : TC(control_border), 0);
    lv_obj_add_event_cb(obj, diag_click_cb, LV_EVENT_CLICKED, send ? (void *)1 : NULL);
    obj = wl_label(obj, SF(22), theme_hex(TC_text), 0);
    lv_label_set_text(obj, text);
    lv_obj_center(obj);
    return obj;
}

/* What will be sent, scrolled with the MFK; press or Send sends, ESC or Cancel closes */
static void diag_collected(const char *out, int status, void *user) {
    std::string text;
    char        buf[1024];
    size_t      n;
    FILE       *f = fopen("/tmp/x6100-diag.txt", "r");

    diag_busy = false;
    if (!f || !dialog.obj) {
        if (f) fclose(f);
        diag_say("could not collect them");
        return;
    }
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    fclose(f);
    diag_say("");

    diag_panel = lv_obj_create(dialog.obj);
    lv_obj_add_event_cb(diag_panel, diag_ptr_clear_cb, LV_EVENT_DELETE, &diag_panel);
    lv_obj_remove_style_all(diag_panel);
    lv_obj_set_size(diag_panel, lv_pct(94), lv_pct(92));
    lv_obj_center(diag_panel);
    lv_obj_add_style(diag_panel, &popup_style, 0);
    lv_obj_set_style_pad_all(diag_panel, 12, 0);
    lv_obj_set_style_pad_row(diag_panel, 8, 0);
    lv_obj_set_flex_flow(diag_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(diag_panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *obj = wl_label(diag_panel, SF(22), theme_hex(TC_text_head), 0);
    lv_label_set_text(obj, "Send these diagnostics to IU1WXY?");

    if (access(DIAG_ACK, F_OK) != 0) {     /* the first time only */
        obj = wl_label(diag_panel, SF(18), theme_hex(TC_text_value), 0);
        lv_obj_set_width(obj, lv_pct(100));
        lv_label_set_long_mode(obj, LV_LABEL_LONG_WRAP);
        lv_label_set_text(obj, "Thanks for helping! This is only read by IU1WXY to understand the problem, and "
                               "deleted within 90 days. It is a friendly, best-effort help: the firmware comes "
                               "as it is, with no warranty, and sending is always up to you.");
    }

    lv_obj_t *box = lv_obj_create(diag_panel);
    lv_obj_remove_style_all(box);
    lv_obj_set_width(box, lv_pct(100));
    lv_obj_set_flex_grow(box, 1);
    lv_obj_set_style_bg_color(box, TC(bg), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(box, 6, 0);
    obj = wl_label(box, SF(16), theme_hex(TC_text_dim), 0);
    lv_obj_set_width(obj, lv_pct(100));
    lv_label_set_long_mode(obj, LV_LABEL_LONG_WRAP);
    lv_label_set_text(obj, text.c_str());
    lv_obj_align(obj, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *row = lv_obj_create(diag_panel);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, lv_pct(100), 40);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 12, 0);
    obj = wl_label(row, SF(16), theme_hex(TC_text_dim), 0);
    lv_label_set_text(obj, "MFK: scroll - press: send - ESC: cancel");
    lv_obj_set_flex_grow(obj, 1);
    diag_panel_btn(row, "Cancel", false);
    diag_panel_btn(row, "Send", true);

    lv_group_add_obj(keyboard_group, diag_panel);
    lv_obj_add_event_cb(diag_panel, diag_key_cb, LV_EVENT_KEY, box);
    lv_obj_add_event_cb(diag_panel, diag_panel_click_cb, LV_EVENT_CLICKED, NULL);
    lv_group_focus_obj(diag_panel);
    lv_group_set_editing(keyboard_group, true);
}

static void diag_btn_cb(lv_event_t *e) {
    char cmd[160];

    if (diag_busy || diag_panel) return;
    diag_busy = true;
    diag_say("collecting...");
    snprintf(cmd, sizeof(cmd), DIAG_CTL " collect '%s' '%s'", strrchr(VERSION, ' ') + 1, x6100_control_get_fw_version_str());
    sh_run(cmd, diag_collected, NULL);
}

static uint8_t make_diag(uint8_t row) {
    wl_row_label(row, "Diagnostics");
    diag_btn = wl_button(row, "Send...", diag_btn_cb);
    lv_obj_add_event_cb(diag_btn, diag_ptr_clear_cb, LV_EVENT_DELETE, &diag_btn);
    diag_note = wl_note(row);
    lv_obj_add_event_cb(diag_note, diag_ptr_clear_cb, LV_EVENT_DELETE, &diag_note);
    return row + 1;
}

/* Info: cards and rows as the other tabs, in the same font */
static void make_info_page() {
    grid_delete();
    grid_create();
    uint8_t row;

    row = card_begin("FIRMWARE");
    row = info_row(row, "Version", strrchr(VERSION, ' ') + 1);
    row = info_row(row, "Author", "Giovanni Ricciardi IU1WXY");
    row = info_row(row, "Contact", "giovanni@iu1wxy.com");
    row = info_row(row, "Baseband", x6100_control_get_fw_version_str());
    row = make_update_alert(row);
    row = make_diag(row);
    card_end(row);
}

static void construct_cb(lv_obj_t *parent) {
    dialog.obj = dialog_init(parent);
    btn_p1.next = &btn_page_2;
    btn_p2.next = &btn_page_3;
    btn_p3.next = &btn_page;
    rail_create();
    make_general_page();
    mark_tab(&btn_general);
    lv_group_set_focus_cb(keyboard_group, focus_cb);
}

static void destruct_cb() {
    lv_group_set_focus_cb(keyboard_group, NULL);
    grid_delete();
    observers.clear();

}

static void key_cb(lv_event_t * e) {
    uint32_t    key = *((uint32_t *)lv_event_get_param(e));

    switch (key) {
        case HKEY_FINP:
             lv_group_set_editing(keyboard_group, !lv_group_get_editing((const lv_group_t*) keyboard_group));
             break;

        case LV_KEY_ESC:
            dialog_destruct();
            break;

        case KEY_VOL_LEFT_EDIT:
        case KEY_VOL_LEFT_SELECT:
            radio_change_vol(-1);
            break;

        case KEY_VOL_RIGHT_EDIT:
        case KEY_VOL_RIGHT_SELECT:
            radio_change_vol(1);
            break;
    }
}
