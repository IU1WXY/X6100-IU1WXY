/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "ft8/ft2.h"
#include "main_screen.h"

#include "styles.h"
#include "theme.h"
#include "spectrum.h"
#include "waterfall.h"
#include "util.h"
#include "radio.h"
#include "events.h"
#include "msg.h"
#include "msg_tiny.h"
#include "dsp.h"
#include "clock.h"
#include "cw_tune_ui.h"
#include "info.h"
#include "meter.h"
#include "band_info.h"
#include "tx_info.h"
#include "mfk.h"
#include "vol.h"
#include "main.h"
#include "panel.h"
#include "rtty.h"
#include "screenshot.h"
#include "screen_rec.h"
#include "keyboard.h"
#include "dialog.h"
#include "dialog_settings.h"
#include "dialog_freq.h"
#include "dialog_msg_cw.h"
#include "dialog_cw_kbd.h"
#include "dialog_msg_voice.h"
#include "dialog_swrscan.h"
#include "dialog_ft8.h"
#include "dialog_gps.h"
#include "dialog_field.h"
#include "dialog_bands.h"
#include "dialog_snake.h"
#include "dialog_birdie_cal.h"
#include "dialog_qth.h"
#include "dialog_recorder.h"
#include "dialog_wifi.h"
#include "dialog_wefax.h"
#include "dialog_navtex.h"
#include "dialog_js8.h"
#include "dialog_psk.h"
#include "dialog_olivia.h"
#include "dialog_jtty.h"
#include "dialog_sstv.h"
#include "dialog_cluster.h"
#include "dialog_modes.h"
#include "dialog_cw_trainer.h"
#include "dialog_wspr.h"
#include "freq_strip.h"
#include <ft8lib/constants.h>
#include "freq_scale.h"
#include "spot_marks.h"
#include "band_plan.h"
#include "cw_field.h"
#include "cluster_column.h"
#include "backlight.h"
#include "buttons.h"
#include "recorder.h"
#include "pubsub_ids.h"
#include "cfg/cfg_api.h"
#include "walkie.h"
#include "cfg/memory.h"
#include "knobs.h"
#include "dialog_channels.h"
#include "remote_server.h"
#include "channels.h"
#include "broadcast_db.h"

#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>


static uint16_t     spectrum_height;        /* Settings > Display > Spectrum height, 76 by default */
static uint16_t     freq_height = 36;
static lv_coord_t   wf_y, spot_tag_y;    /* waterfall top without the spot tags */
static lv_obj_t     *obj;
static SubjectInt   *freq_lock;
static bool         mode_lock = false;
static bool         ab_lock = false;
static bool         band_lock = false;

static lv_obj_t     *spectrum;
static lv_obj_t     *waterfall;
static lv_obj_t     *msg;
static lv_obj_t     *msg_tiny;
static lv_obj_t     *meter;
static lv_obj_t     *tx_info;
static lv_obj_t     *knobs;

/* Temporary Channel Memory overlay used by microphone FIL/GENE. */
static lv_obj_t     *channel_overlay = NULL;
static lv_obj_t     *channel_overlay_label = NULL;
static lv_timer_t   *channel_overlay_timer = NULL;

/* Temporary SW Broadcast Info overlay used by microphone GENE. */
static lv_obj_t     *broadcast_overlay = NULL;
static lv_obj_t     *broadcast_overlay_label = NULL;
static lv_obj_t     *broadcast_overlay_counter = NULL;
static lv_obj_t     *broadcast_overlay_name = NULL;
static lv_timer_t   *broadcast_overlay_timer = NULL;
static broadcast_match_t broadcast_matches[BROADCAST_MAX_MATCHES];
static size_t        broadcast_match_count = 0;
static size_t        broadcast_match_index = 0;
static int32_t       broadcast_search_frequency = 0;

// power off on low battery
static lv_timer_t *low_power_timer;

static void low_power_timer_cb(lv_timer_t * timer);

static void freq_shift(int16_t diff, uint16_t dt);
static void next_freq_step(bool up);
static void toggle_atu_enabled();
void channel_overlay_show(const char *name);
static void channel_overlay_timer_cb(lv_timer_t *timer);
static void broadcast_overlay_show(void);
static void broadcast_overlay_timer_cb(lv_timer_t *timer);


static void channel_overlay_timer_cb(lv_timer_t *timer) {

    if (channel_overlay) {
        lv_obj_del(channel_overlay);
        channel_overlay = NULL;
        channel_overlay_label = NULL;
    }

    channel_overlay_timer = NULL;
}


void channel_overlay_show(const char *name) {

    /*
     * Reuse the existing overlay when FIL/GENE are pressed rapidly.
     * Only the text changes and the one-second timeout starts again.
     */
    if (!channel_overlay) {

        channel_overlay = lv_obj_create(obj);

        lv_obj_set_size(channel_overlay, 700, 70);
        lv_obj_align(channel_overlay, LV_ALIGN_TOP_MID, 0, 75);

        lv_obj_set_style_bg_color(
            channel_overlay,
            TC(bg),
            LV_PART_MAIN
        );

        lv_obj_set_style_bg_opa(
            channel_overlay,
            LV_OPA_COVER,
            LV_PART_MAIN
        );

        lv_obj_set_style_border_width(
            channel_overlay,
            2,
            LV_PART_MAIN
        );

        lv_obj_set_style_border_color(
            channel_overlay,
            TC(text_mute),
            LV_PART_MAIN
        );

        lv_obj_set_style_radius(
            channel_overlay,
            8,
            LV_PART_MAIN
        );

        lv_obj_set_style_pad_all(
            channel_overlay,
            0,
            LV_PART_MAIN
        );

        lv_obj_clear_flag(
            channel_overlay,
            LV_OBJ_FLAG_SCROLLABLE
        );

        channel_overlay_label =
            lv_label_create(channel_overlay);

        lv_obj_set_width(
            channel_overlay_label,
            510
        );

        lv_label_set_long_mode(
            channel_overlay_label,
            LV_LABEL_LONG_DOT
        );

        lv_obj_set_style_text_align(
            channel_overlay_label,
            LV_TEXT_ALIGN_CENTER,
            0
        );

        lv_obj_set_style_text_color(
            channel_overlay_label,
            TC(text),
            0
        );

        lv_obj_set_style_text_font(
            channel_overlay_label,
            &sony_38,
            0
        );

        lv_obj_center(channel_overlay_label);
    }


    if (name && name[0] != '\0') {

        lv_label_set_text(
            channel_overlay_label,
            name
        );

    } else {

        lv_label_set_text(
            channel_overlay_label,
            "CHANNEL"
        );
    }


    lv_obj_move_foreground(channel_overlay);


    if (channel_overlay_timer) {

        /*
         * A new FIL/GENE press restarts the full one-second timeout.
         */
        lv_timer_reset(channel_overlay_timer);

    } else {

        channel_overlay_timer =
            lv_timer_create(
                channel_overlay_timer_cb,
                1000,
                NULL
            );

        lv_timer_set_repeat_count(
            channel_overlay_timer,
            1
        );
    }
}


static void broadcast_overlay_timer_cb(lv_timer_t *timer) {

    if (broadcast_overlay) {
        lv_obj_del(broadcast_overlay);
        broadcast_overlay = NULL;
        broadcast_overlay_label = NULL;
        broadcast_overlay_counter = NULL;
        broadcast_overlay_name = NULL;
    }

    broadcast_overlay_timer = NULL;
    broadcast_match_count = 0;
    broadcast_match_index = 0;
}


static void broadcast_overlay_render(void) {

    if (!broadcast_overlay) {
        /*
         * Build the Broadcast Info panel exactly like the common
         * RTTY/NAVTEX panel: a label using panel_style.
         */
        broadcast_overlay = lv_label_create(obj);
        lv_label_set_text(broadcast_overlay, "");
        lv_obj_add_style(broadcast_overlay, &panel_style, 0);
        lv_obj_add_flag(broadcast_overlay, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
        lv_obj_clear_flag(broadcast_overlay, LV_OBJ_FLAG_SCROLLABLE);

        /* Result counter, only shown when more than one match exists. */
        broadcast_overlay_counter = lv_label_create(broadcast_overlay);
        lv_obj_set_pos(broadcast_overlay_counter, 8, 2);
        lv_obj_set_style_text_color(broadcast_overlay_counter, TC(text), 0);
        lv_obj_set_style_text_font(broadcast_overlay_counter, &sony_28, 0);

        /* Station name. */
        broadcast_overlay_name = lv_label_create(broadcast_overlay);
        lv_obj_set_width(broadcast_overlay_name, 735);
        lv_obj_set_pos(broadcast_overlay_name, 20, 4);
        lv_label_set_long_mode(broadcast_overlay_name, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(broadcast_overlay_name, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(broadcast_overlay_name, TC(text), 0);
        lv_obj_set_style_text_font(broadcast_overlay_name, &sony_38, 0);

        /* Broadcast details, fitted inside the standard 795 x 182 panel. */
        broadcast_overlay_label = lv_label_create(broadcast_overlay);
        lv_obj_set_width(broadcast_overlay_label, 755);
        lv_obj_set_pos(broadcast_overlay_label, 10, 48);
        lv_label_set_long_mode(broadcast_overlay_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_align(broadcast_overlay_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_color(broadcast_overlay_label, TC(text), 0);
        lv_obj_set_style_text_font(broadcast_overlay_label, &sony_28, 0);
    }

    if (broadcast_match_count > 0) {
        broadcast_match_t *m = &broadcast_matches[broadcast_match_index];
        int sh = m->start_minute / 60;
        int sm = m->start_minute % 60;
        int eh = m->stop_minute / 60;
        int em = m->stop_minute % 60;
        char lang[64];
        char target[80];

        snprintf(lang, sizeof(lang), "%s", m->language[0] ? m->language : "-");
        snprintf(target, sizeof(target), "%s", m->target[0] ? m->target : "-");

        if (broadcast_match_count > 1) {
            lv_label_set_text_fmt(
                broadcast_overlay_counter,
                "%u/%u",
                (unsigned)(broadcast_match_index + 1),
                (unsigned)broadcast_match_count
            );
        } else {
            lv_label_set_text(broadcast_overlay_counter, "");
        }

        lv_label_set_text(broadcast_overlay_name, m->station);

        lv_label_set_text_fmt(
            broadcast_overlay_label,
            "%.3f kHz     %02d:%02d-%02d:%02d UTC\n"
            "Language: %s     Target: %s\n"
            "ITU: %s     Days: %s",
            m->freq_khz, sh, sm, eh, em,
            lang, target,
            m->itu[0] ? m->itu : "-",
            m->days[0] ? m->days : "Daily"
        );
    } else {
        lv_label_set_text(broadcast_overlay_counter, "");
        lv_label_set_text(broadcast_overlay_name, "NO STATION SCHEDULED");
        lv_label_set_text_fmt(
            broadcast_overlay_label,
            "\n%.3f kHz",
            (double)broadcast_search_frequency / 1000.0
        );
    }

    lv_obj_move_foreground(broadcast_overlay);
}

static void broadcast_overlay_show(void) {

    /* If the panel is already visible, GENE means NEXT.  Do not repeat
     * the search: cycle the result set captured by the first press. */
    if (broadcast_overlay) {
        if (broadcast_match_count > 1) {
            broadcast_match_index = (broadcast_match_index + 1) % broadcast_match_count;
            broadcast_overlay_render();
        }
    } else {
        broadcast_search_frequency = cparam_i_get(cfg_fg_freq);
        broadcast_match_count = broadcast_db_find_matches(
            broadcast_search_frequency,
            2000,
            broadcast_matches,
            BROADCAST_MAX_MATCHES
        );
        broadcast_match_index = 0;
        broadcast_overlay_render();
    }

    if (broadcast_overlay_timer) {
        lv_timer_reset(broadcast_overlay_timer);
    } else {
        broadcast_overlay_timer = lv_timer_create(broadcast_overlay_timer_cb, 2000, NULL);
        lv_timer_set_repeat_count(broadcast_overlay_timer, 1);
    }
}


static void broadcast_overlay_save_channel(void) {

    broadcast_match_t match;
    bool have_match = false;

    /* If Broadcast Info is visible, save the station currently selected
     * in the panel.  Otherwise perform a fresh lookup at the current VFO
     * frequency and use the first matching station. */
    if (broadcast_overlay && broadcast_match_count > 0) {
        match = broadcast_matches[broadcast_match_index];
        have_match = true;
    } else if (!broadcast_overlay) {
        broadcast_match_t matches[BROADCAST_MAX_MATCHES];
        size_t count = broadcast_db_find_matches(
            cparam_i_get(cfg_fg_freq),
            2000,
            matches,
            BROADCAST_MAX_MATCHES
        );

        if (count > 0) {
            match = matches[0];
            have_match = true;
        }
    }

    /* Always reload the persistent channel database before modifying it.
     * After boot the in-memory channel list may still be empty if the
     * Channels dialog has never been opened. */
    if (!channels_load()) {
        if (broadcast_overlay) {
            lv_label_set_text(broadcast_overlay_counter, "");
            lv_label_set_text(broadcast_overlay_name, "SAVE ERROR");
            lv_label_set_text(broadcast_overlay_label, "Unable to load channels");
        } else {
            channel_overlay_show("Channel save error");
        }
        return;
    }

    if (!channels_add_current()) {
        if (broadcast_overlay) {
            lv_label_set_text(broadcast_overlay_counter, "");
            lv_label_set_text(broadcast_overlay_name, "SAVE ERROR");
            lv_label_set_text(broadcast_overlay_label, "Unable to add channel");
        } else {
            channel_overlay_show("Channel save error");
        }
        return;
    }

    uint16_t count = channels_count();

    if (have_match && count > 0) {
        if (!channels_set_name(count - 1, match.station)) {
            /* channels_add_current() has already saved the new record with
             * an empty name. Remove it again if assigning the station name
             * fails, so we don't leave a bogus channel behind. */
            channels_delete(count - 1);

            if (broadcast_overlay) {
                lv_label_set_text(broadcast_overlay_counter, "");
                lv_label_set_text(broadcast_overlay_name, "SAVE ERROR");
                lv_label_set_text(broadcast_overlay_label, "Unable to set channel name");
            } else {
                channel_overlay_show("Channel save error");
            }
            return;
        }
    }

    if (broadcast_overlay) {
        lv_label_set_text(broadcast_overlay_counter, "");
        lv_label_set_text(broadcast_overlay_name, "CHANNEL SAVED");
        lv_label_set_text(
            broadcast_overlay_label,
            have_match ? match.station : ""
        );

        if (broadcast_overlay_timer) {
            lv_timer_reset(broadcast_overlay_timer);
        } else {
            broadcast_overlay_timer = lv_timer_create(broadcast_overlay_timer_cb, 1000, NULL);
            lv_timer_set_repeat_count(broadcast_overlay_timer, 1);
        }

        lv_obj_move_foreground(broadcast_overlay);
    } else {
        char message[96];

        if (have_match && match.station[0]) {
            snprintf(message, sizeof(message), "Saved channel %s", match.station);
        } else {
            snprintf(message, sizeof(message), "Saved channel");
        }

        channel_overlay_show(message);
    }
}

// Observers functions

static void on_fg_freq_change(Subject *subj, void *user_data);
static void update_freq_boundaries(Subject *subj, void *user_data);
static void update_zoom_on_if_shift_change(Subject *subj, void *user_data);


void mem_load(uint16_t id) {
    if (!cfg_memory_load(id)) {
        msg_update_text_fmt("Nothing to load for memory %i", id);
    }
    if (id != MEM_BACKUP_ID) {
        msg_update_text_fmt("Loaded from memory %i", id);
    }
}

void mem_save(uint16_t id) {
    cfg_memory_save(id);

    if (id <= MEM_HKEY_MAX_ID) {
        msg_update_text_fmt("Saved in memory %i", id);
    }
}

static void low_power_timer_cb(lv_timer_t * timer) {
    low_power_timer = NULL;     /* one-shot: LVGL deletes it */
    msg_update_text_fmt("Power off");
    radio_set_charger(true);
    radio_poweroff();
}

/* AGC: Off -> Slow -> Fast -> Auto -> Off (AGC key, MFK "AGC" button) */
void main_screen_agc_next() {
    x6100_agc_t agc = cparam_i_get(cfg_cur_agc);
    switch (agc) {
        case x6100_agc_off:
            agc = x6100_agc_slow;
            break;

        case x6100_agc_slow:
            agc = x6100_agc_fast;
            break;

        case x6100_agc_fast:
            agc = x6100_agc_auto;
            break;

        case x6100_agc_auto:
            agc = x6100_agc_off;
            break;
    }
    cparam_i_set(cfg_cur_agc, agc);

    if (params.mag_info.x) {
        msg_tiny_set_text_fmt("AGC: %s", info_params_agc());
    }
}

static void toggle_atu_enabled() {
    bool new_atu_enabled = !param_i_get(cfg_atu_enabled);
    param_i_set(cfg_atu_enabled, new_atu_enabled);
}

static void next_freq_step(bool up) {
    uint16_t new_step = cfg_mode_change_freq_step(up);
    msg_update_text_fmt("Freq step: %i Hz", new_step);
}

/* Shown in the frequency strip while the app is open */
static const char *app_name(press_action_t a) {
    switch (a) {
        case ACTION_APP_RTTY:       return "RTTY";
        case ACTION_APP_FT8:        return ftx_protocol_name(param_i_get(cfg_ft8_protocol));
        case ACTION_APP_SWRSCAN:    return "SWR Scan";
        case ACTION_APP_GPS:        return "GPS";
        case ACTION_APP_SETTINGS:   return "Settings";
        case ACTION_APP_RECORDER:   return "Recorder";
        case ACTION_APP_WIFI:       return "WiFi";
        case ACTION_APP_WEFAX:      return "WeFax";
        case ACTION_APP_NAVTEX:     return "NavTex";
        case ACTION_APP_JS8:        return "JS8";
        case ACTION_APP_SSTV:       return "SSTV";
        case ACTION_APP_CLUSTER:    return "Cluster";
        case ACTION_APP_CWTRAINER:  return "CW Trainer";
        case ACTION_APP_WSPR:       return "WSPR";
        case ACTION_APP_CW_KBD:     return "CW Keyboard";
        case ACTION_APP_FIELD:      return "Field Ops";
        case ACTION_APP_BANDS:      return "Bands";
        case ACTION_APP_SNAKE:      return "Snake";
        case ACTION_APP_PSK:        return dialog_psk_is_63() ? "PSK63" : "PSK31";
        case ACTION_APP_OLIVIA:     return "Olivia";
        case ACTION_APP_JTTY:       return "JTTY";
        case ACTION_APP_BIRDIE_CAL: return "Birdie Cal";
        default:                    return NULL;    /* Modes menu: a menu, not an app */
    }
}

static void apps_disable() {
    dialog_destruct();
    freq_strip_set_app(NULL);

    if (rtty_get_state() != RTTY_OFF) {
        app_mem_save("RTTY");
        mem_load(MEM_BACKUP_ID);    /* the radio as before RTTY, as the other apps */
    }
    rtty_set_state(RTTY_OFF);
    radio_set_digi_cap(false);
    panel_update_visibility(false);
}

/* As the GEN key: the app closes, the GEN buttons come back */
void main_screen_close_app(void) {
    apps_disable();
    buttons_load_page_group(buttons_group_gen);
}

void main_screen_start_app(press_action_t app_action) {
    /* Modes again closes its menu. */
    if (app_action == ACTION_APP_MODES && dialog_type_is_run(dialog_modes)) {
        dialog_destruct();
        return;
    }
    apps_disable();

    /* The digital apps open where they were last used */
    switch (app_action) {
        case ACTION_APP_FT8: case ACTION_APP_JS8: case ACTION_APP_WSPR: case ACTION_APP_SSTV:
        case ACTION_APP_WEFAX: case ACTION_APP_NAVTEX:
            dialog_app_memory(app_name(app_action));
            radio_set_digi_cap(true);
            break;
        case ACTION_APP_PSK:    /* one memory for PSK31 and PSK63 */
            dialog_app_memory("PSK");
            radio_set_digi_cap(true);
            break;
        case ACTION_APP_OLIVIA:
            dialog_app_memory("Olivia");
            radio_set_digi_cap(true);
            break;
        case ACTION_APP_JTTY:
            dialog_app_memory("JTTY");
            radio_set_digi_cap(true);
            break;
        case ACTION_APP_RTTY:
            radio_set_digi_cap(true);
            break;
        default:
            break;
    }

    switch (app_action) {
        case ACTION_APP_RTTY: {
            int32_t freq, mode;
            mem_save(MEM_BACKUP_ID);
            if (app_mem_load("RTTY", &freq, &mode)) {
                main_screen_set_freq(freq);
                cparam_i_set(cfg_cur_mode, mode);
            }
        }
            buttons_load_page(&buttons_page_rtty);
            rtty_set_state(RTTY_RX);
            panel_update_visibility(true);
            break;

        case ACTION_APP_SETTINGS:
            dialog_construct(dialog_settings, obj);
            break;

        case ACTION_APP_MODES:
            dialog_construct(dialog_modes, obj);
            break;

        case ACTION_APP_SWRSCAN:
            dialog_construct(dialog_swrscan, obj);
            break;

        case ACTION_APP_FT8:
            dialog_construct(dialog_ft8, obj);
            break;

        case ACTION_APP_GPS:
            dialog_construct(dialog_gps, obj);
            break;

        case ACTION_APP_RECORDER:
            dialog_construct(dialog_recorder, obj);
            break;

        case ACTION_APP_WIFI:
            dialog_construct(dialog_wifi, obj);
            break;

        case ACTION_APP_WEFAX:
            dialog_construct(dialog_wefax, obj);
            break;

        case ACTION_APP_NAVTEX:
            dialog_construct(dialog_navtex, obj);
            break;

        case ACTION_APP_JS8:
            dialog_construct(dialog_js8, obj);
            break;

        case ACTION_APP_SSTV:
            dialog_construct(dialog_sstv, obj);
            break;

        case ACTION_APP_CLUSTER:
            dialog_construct(dialog_cluster, obj);
            break;

        case ACTION_APP_CWTRAINER:
            dialog_construct(dialog_cw_trainer, obj);
            break;

        case ACTION_APP_WSPR:
            dialog_construct(dialog_wspr, obj);
            break;

        case ACTION_APP_CW_KBD:
            dialog_construct(dialog_cw_kbd, obj);
            break;

        case ACTION_APP_FIELD:
            dialog_construct(dialog_field, obj);
            break;

        case ACTION_APP_BANDS:
            dialog_construct(dialog_bands, obj);
            break;

        case ACTION_APP_SNAKE:
            dialog_construct(dialog_snake, obj);
            break;

        case ACTION_APP_PSK:
            dialog_construct(dialog_psk, obj);
            break;

        case ACTION_APP_OLIVIA:
            dialog_construct(dialog_olivia, obj);
            break;

        case ACTION_APP_JTTY:
            dialog_construct(dialog_jtty, obj);
            break;

        case ACTION_APP_BIRDIE_CAL:
            dialog_construct(dialog_birdie_cal, obj);
            break;

        default:
            break;
    }
    freq_strip_set_app(app_name(app_action));
}

void main_screen_action(press_action_t action) {
    bool b;
    switch (action) {
        case ACTION_NONE:
            break;

        case ACTION_SCREENSHOT:
            screenshot_take();
            break;

        case ACTION_RECORDER:
            if (recorder_is_on()) {
                recorder_set_on(false);
            } else {
                recorder_set_on(true);
            }
            break;

        case ACTION_SCREEN_REC:
            screen_rec_toggle();
            break;

        case ACTION_MUTE:
            radio_change_mute();
            break;

        case ACTION_VOICE_MODE:     /* removed, may still be stored in settings */
        case ACTION_BAT_INFO:
            break;

        case ACTION_STEP_UP:
            next_freq_step(true);
            break;

        case ACTION_STEP_DOWN:
            next_freq_step(false);
            break;

        case ACTION_NR_TOGGLE:
            b = param_i_get(cfg_nr);
            b = !b;
            param_i_set(cfg_nr, b);
            msg_update_text_fmt("#%06x NR: %s", theme_hex(TC_text), b ? "On" : "Off");
            break;

        case ACTION_NB_TOGGLE:
            b = param_i_get(cfg_nb);
            b = !b;
            param_i_set(cfg_nb, b);
            msg_update_text_fmt("#%06x NB: %s", theme_hex(TC_text), b ? "On" : "Off");
            break;

        case ACTION_ATT_TOGGLE:
            b = !cparam_i_get(cfg_cur_att);
            cparam_i_set(cfg_cur_att, b);
            msg_update_text_fmt("#%06x ATT: %s", theme_hex(TC_text), b ? "On" : "Off");
            break;

        case ACTION_PRE_TOGGLE:
            b = !cparam_i_get(cfg_cur_pre);
            cparam_i_set(cfg_cur_pre, b);
            msg_update_text_fmt("#%06x PRE: %s", theme_hex(TC_text), b ? "On" : "Off");
            break;

        case ACTION_ATU_TOGGLE:   /* as the ATU key */
            toggle_atu_enabled();
            msg_update_text_fmt("#%06x ATU: %s", theme_hex(TC_text), param_i_get(cfg_atu_enabled) ? "On" : "Off");
            break;

        case ACTION_ATU_TUNE:     /* as a long press of the ATU key */
            radio_start_atu();
            break;

        case ACTION_WF_MODE:
            b = !param_i_get(cfg_wf_mode);
            param_i_set(cfg_wf_mode, b);
            msg_update_text_fmt("#%06x Waterfall: %s", theme_hex(TC_text), b ? "Fixed" : "Centered");
            break;

        case ACTION_APP_RTTY:
        case ACTION_APP_FT8:
        case ACTION_APP_SWRSCAN:
        case ACTION_APP_GPS:
        case ACTION_APP_SETTINGS:
        case ACTION_APP_RECORDER:
        case ACTION_APP_WIFI:
        case ACTION_APP_WEFAX:
        case ACTION_APP_NAVTEX:
        case ACTION_APP_JS8:
        case ACTION_APP_SSTV:
        case ACTION_APP_CLUSTER:
        case ACTION_APP_MODES:
        case ACTION_APP_CWTRAINER:
        case ACTION_APP_WSPR:
        case ACTION_APP_CW_KBD:
        case ACTION_APP_FIELD:
        case ACTION_APP_BANDS:
        case ACTION_APP_PSK:
        case ACTION_APP_OLIVIA:
        case ACTION_APP_JTTY:
            main_screen_start_app(action);
            break;

        case ACTION_APP_QTH:
            dialog_construct(dialog_qth, obj);
            break;

        case ACTION_APP_CALLSIGN:   /* Settings -> General */
            break;
    }
}

static x6100_mode_t get_next_mode_am_fm(bool long_press) {
    x6100_mode_t    mode = cparam_i_get(cfg_cur_mode);
    switch (mode) {
        case x6100_mode_am:
            mode = x6100_mode_nfm;
            break;
        case x6100_mode_nfm:
        default:
            mode = x6100_mode_am;
            break;
    }
    return mode;
}

static x6100_mode_t get_next_mode_cw(bool long_press) {
    x6100_mode_t    mode = cparam_i_get(cfg_cur_mode);
    switch (mode) {
        case x6100_mode_cw:
            mode = x6100_mode_cwr;
            break;
        case x6100_mode_cwr:
        default:
            mode = x6100_mode_cw;
            break;
    }
    return mode;
}

static x6100_mode_t get_next_mode_ssb(bool long_press) {
    x6100_mode_t    mode = cparam_i_get(cfg_cur_mode);
    switch (mode) {
        case x6100_mode_lsb_dig:
            if (long_press) {
                mode = x6100_mode_lsb;
            } else {
                mode = x6100_mode_usb_dig;
            }
            break;
        case x6100_mode_usb_dig:
            if (long_press) {
                mode = x6100_mode_usb;
            } else {
                mode = x6100_mode_lsb_dig;
            }
            break;
        case x6100_mode_lsb:
            if (long_press) {
                mode = x6100_mode_lsb_dig;
            } else {
                mode = x6100_mode_usb;
            }
            break;
        case x6100_mode_usb:
            if (long_press) {
                mode = x6100_mode_usb_dig;
            } else {
                mode = x6100_mode_lsb;
            }
            break;
        default:
            mode = x6100_mode_lsb;
            break;
    }
    return mode;
}

static void change_mode(keypad_key_t key, keypad_state_t state) {
    switch (state) {
        case KEYPAD_LONG:
        case KEYPAD_RELEASE:
            break;
        default:
            return;
    }

    // find next mode
    x6100_mode_t    next_mode;
    switch (key) {
        case KEYPAD_MODE_AM:
            next_mode = get_next_mode_am_fm(state == KEYPAD_LONG);
            break;
        case KEYPAD_MODE_CW:
            next_mode = get_next_mode_cw(state == KEYPAD_LONG);
            break;
        case KEYPAD_MODE_SSB:
            next_mode = get_next_mode_ssb(state == KEYPAD_LONG);
            break;
        default:
            break;
    }

    cparam_i_set(cfg_cur_mode, next_mode);
}

/* MSG opens the message dialog; pressed again it turns the page, like the other keys */
static void msg_dialog_open(dialog_t *dialog) {
    if (dialog_type_is_run(dialog)) {
        buttons_page_t *page = buttons_get_cur_page();

        if (page && page->items[0] && page->items[0]->press == button_next_page_cb) {
            buttons_press(0, false);
        }
        return;
    }
    apps_disable();
    panel_hide();
    dialog_construct(dialog, obj);
}

static void main_screen_keypad_cb(lv_event_t * e) {
    event_keypad_t *keypad = lv_event_get_param(e);

    switch (keypad->key) {
        case KEYPAD_PRE: ;
            int32_t pre = cparam_i_get(cfg_cur_pre);
            int32_t att = cparam_i_get(cfg_cur_att);
            if (keypad->state == KEYPAD_RELEASE) {
                pre = !pre;
                cparam_i_set(cfg_cur_pre, pre);

                if (params.mag_info.x) {
                    msg_tiny_set_text_fmt("Pre: %s", pre ? "On" : "Off");
                }
            } else if (keypad->state == KEYPAD_LONG) {
                att = !att;
                cparam_i_set(cfg_cur_att, att);

                if (params.mag_info.x) {
                    msg_tiny_set_text_fmt("Att: %s", att ? "On" : "Off");
                }
            }
            break;

        case KEYPAD_BAND_UP:
            if (keypad->state == KEYPAD_RELEASE) {
                if (!band_lock) {
                    cfg_band_load_next(true);
                }
                dialog_send(EVENT_BAND_UP, NULL);
            }
            break;

        case KEYPAD_BAND_DOWN:
            if (keypad->state == KEYPAD_RELEASE) {
                if (!band_lock) {
                    cfg_band_load_next(false);
                }
                dialog_send(EVENT_BAND_DOWN, NULL);
            }
            break;

        case KEYPAD_MODE_AM:
        case KEYPAD_MODE_CW:
        case KEYPAD_MODE_SSB:
            if (!mode_lock) {
                change_mode(keypad->key, keypad->state);
            }
            break;

        case KEYPAD_AGC:
            if (keypad->state == KEYPAD_RELEASE) {
                main_screen_agc_next();
            } else if (keypad->state == KEYPAD_LONG) {
                bool new_split = !param_i_get(cfg_band_split);
                param_i_set(cfg_band_split, new_split);

                spectrum_clear();

                if (params.mag_info.x) {
                    msg_tiny_set_text_fmt("%s", info_params_vfo_label_get());
                }
            }
            break;

        case KEYPAD_FST:
            if (keypad->state == KEYPAD_RELEASE) {
                next_freq_step(true);
            } else if (keypad->state == KEYPAD_LONG) {
                next_freq_step(false);
            }
            break;

        case KEYPAD_ATU:
            if (keypad->state == KEYPAD_RELEASE) {
                toggle_atu_enabled();

                if (params.mag_info.x) {
                    msg_tiny_set_text_fmt("ATU: %s", param_i_get(cfg_atu_enabled) ? "On" : "Off");
                }
            } else if (keypad->state == KEYPAD_LONG) {
                radio_start_atu();
            }
            break;

        case KEYPAD_F1:
            if (keypad->state == KEYPAD_RELEASE) {
                buttons_press(0, false);
            } else if (keypad->state == KEYPAD_LONG) {
                buttons_press(0, true);
            }
            break;

        case KEYPAD_F2:
            if (keypad->state == KEYPAD_RELEASE) {
                buttons_press(1, false);
            } else if (keypad->state == KEYPAD_LONG) {
                buttons_press(1, true);
            }
            break;

        case KEYPAD_F3:
            if (keypad->state == KEYPAD_RELEASE) {
                buttons_press(2, false);
            } else if (keypad->state == KEYPAD_LONG) {
                buttons_press(2, true);
            }
            break;

        case KEYPAD_F4:
            if (keypad->state == KEYPAD_RELEASE) {
                buttons_press(3, false);
            } else if (keypad->state == KEYPAD_LONG) {
                buttons_press(3, true);
            }
            break;

        case KEYPAD_F5:
            if (keypad->state == KEYPAD_RELEASE) {
                buttons_press(4, false);
            } else if (keypad->state == KEYPAD_LONG) {
                buttons_press(4, true);
            }
            break;

        case KEYPAD_GEN:
            if (keypad->state == KEYPAD_RELEASE) {
                apps_disable();
                buttons_load_page_group(buttons_group_gen);
            } else if (keypad->state == KEYPAD_LONG) {
                main_screen_action(params.long_gen);
            }
            break;

        case KEYPAD_APP:
            if (keypad->state == KEYPAD_RELEASE) {
                apps_disable();
                buttons_load_page_group(buttons_group_app);
            } else if (keypad->state == KEYPAD_LONG) {
                main_screen_action(params.long_app);
            }
            break;

        case KEYPAD_KEY:
            if (keypad->state == KEYPAD_RELEASE) {
                apps_disable();
                buttons_load_page_group(buttons_group_key);
            } else if (keypad->state == KEYPAD_LONG) {
                main_screen_action(params.long_key);
            }
            break;

        case KEYPAD_MSG:
            if (keypad->state == KEYPAD_RELEASE) {
                switch (cparam_i_get(cfg_cur_mode)) {
                    case x6100_mode_cw:
                    case x6100_mode_cwr:
                        msg_dialog_open(dialog_msg_cw);
                        break;

                    case x6100_mode_lsb:
                    case x6100_mode_usb:
                    case x6100_mode_am:
                    case x6100_mode_nfm:
                        msg_dialog_open(dialog_msg_voice);
                        break;

                    default:
                        msg_tiny_set_text_fmt("Not used in this mode");
                        break;
                }
            } else if (keypad->state == KEYPAD_LONG) {
                main_screen_action(params.long_msg);
            }
            break;

        case KEYPAD_DFN:
            if (keypad->state == KEYPAD_RELEASE) {
                apps_disable();
                buttons_load_page_group(buttons_group_dfn);
            } else if (keypad->state == KEYPAD_LONG) {
                main_screen_action(params.long_dfn);
            }
            break;

        case KEYPAD_DFL:
            if (keypad->state == KEYPAD_RELEASE) {
                apps_disable();
                buttons_load_page_group(buttons_group_dfl);
            } else if (keypad->state == KEYPAD_LONG) {
                main_screen_action(params.long_dfl);
            }
            break;

        case KEYPAD_AB:
            if (!ab_lock) {
                if (keypad->state == KEYPAD_RELEASE) {
                    radio_toggle_vfo();

                    spectrum_clear();

                    if (params.mag_info.x) {
                        msg_tiny_set_text_fmt("%s", info_params_vfo_label_get());
                    }
                } else if (keypad->state == KEYPAD_LONG) {
                    x6100_vfo_t cur_vfo = param_i_get(cfg_band_current_vfo);
                    cfg_band_vfo_copy();
                    // radio_vfo_set();
                    msg_update_text_fmt("Clone VFO %s", cur_vfo == X6100_VFO_A ? "A->B" : "B->A");
                }
            }
            break;

        case KEYPAD_POWER:
            if (keypad->state == KEYPAD_RELEASE) {
                backlight_switch();
            } else if (keypad->state == KEYPAD_LONG) {
                msg_update_text_fmt("Power off");
                radio_poweroff();
            }
            break;

        case KEYPAD_LOCK:
            if (keypad->state == KEYPAD_RELEASE) {
                subject_i_set(freq_lock, !subject_i_get(freq_lock));
            } else if (keypad->state == KEYPAD_LONG) {
                radio_bb_reset();
                exit(1);
            }
            break;

        case KEYPAD_PTT:
            switch (keypad->state) {
                case KEYPAD_PRESS:
                    dialog_msg_voice_ptt_pressed();
                    radio_set_ptt(true);

                    switch (cparam_i_get(cfg_cur_mode)) {
                        case x6100_mode_cw:
                        case x6100_mode_cwr:
                            radio_set_morse_key(true);
                            break;
                    }
                    break;

                case KEYPAD_RELEASE:
                case KEYPAD_LONG_RELEASE:
                    switch (cparam_i_get(cfg_cur_mode)) {
                        case x6100_mode_cw:
                        case x6100_mode_cwr:
                            radio_set_morse_key(false);
                            break;
                    }

                    radio_set_ptt(false);
                    break;
                default:
                    break;
            }
            break;

        case KEYPAD_VM:
            if ((keypad->state == KEYPAD_RELEASE) && !dialog_is_run()) {
                buttons_load_page_group(buttons_group_vm);
            }
            break;

        default:
            LV_LOG_WARN("Unsuported key: %u", keypad->key);
            break;
    }
}

static void main_screen_hkey_cb(lv_event_t * e) {
    event_hkey_t *hkey = lv_event_get_param(e);
    switch (hkey->key) {
        /* The band printed on the key; digits only in F-INP (dialog_freq.c). Long: memory 1-9 */
        case HKEY_0:
        case HKEY_1:
        case HKEY_2:
        case HKEY_3:
        case HKEY_4:
        case HKEY_5:
        case HKEY_6:
        case HKEY_7:
        case HKEY_8:
        case HKEY_9: {
            static const int16_t key_band_m[10] = {6, 160, 80, 40, 30, 20, 17, 15, 12, 10};

            if (hkey->state == HKEY_RELEASE) {
                if (!band_lock) {
                    remote_set_band(key_band_m[hkey->key - HKEY_0]);
                }
            } else if (hkey->state == HKEY_LONG && hkey->key != HKEY_0) {
                mem_load(hkey->key - HKEY_1 + 1);
            }
            break;
        }

        case HKEY_SPCH:
            if (hkey->state == HKEY_RELEASE) {
                subject_i_set(freq_lock, !subject_i_get(freq_lock));
            }
            break;

        case HKEY_TUNER:
            if (hkey->state == HKEY_RELEASE) {
                toggle_atu_enabled();

            } else if (hkey->state == HKEY_LONG) {
                radio_start_atu();
            }
            break;

        case HKEY_XFC:
            if (hkey->state == HKEY_RELEASE) {
                radio_toggle_vfo();

                spectrum_clear();
            }
            break;


        /*
        case HKEY_UP:
            if (hkey->state == HKEY_RELEASE) {
                if (!subject_i_get(freq_lock)) {
                    freq_shift(+1, 0);
                }
            } else if (hkey->state == HKEY_LONG) {
                if (!band_lock) {
                    cfg_band_load_next(true);
                }
                dialog_send(EVENT_BAND_UP, NULL);
            }
            break;


        
        case HKEY_DOWN:
            if (hkey->state == HKEY_RELEASE) {
                if (!subject_i_get(freq_lock)) {
                    freq_shift(-1, 0);
                }
            } else if (hkey->state == HKEY_LONG) {
                if (!band_lock) {
                    cfg_band_load_next(false);
                }
                dialog_send(EVENT_BAND_DOWN, NULL);
            }
            break; */

        case HKEY_UP:
    if (hkey->state == HKEY_RELEASE) {
        if (dialog_channels_recall_relative(-1)) {
            channel_overlay_show(
                dialog_channels_last_recalled_name()
            );
        }
    } else if (hkey->state == HKEY_LONG) {
        if (!band_lock) {
            cfg_band_load_next(true);
        }
        dialog_send(EVENT_BAND_UP, NULL);
    }
    break;


        case HKEY_DOWN:
    if (hkey->state == HKEY_RELEASE) {
        if (dialog_channels_recall_relative(+1)) {
            channel_overlay_show(
                dialog_channels_last_recalled_name()
            );
        }
    } else if (hkey->state == HKEY_LONG) {
        if (!band_lock) {
            cfg_band_load_next(false);
        }
        dialog_send(EVENT_BAND_DOWN, NULL);
    }
    break;


case HKEY_VM:
    if (dialog_wefax->run || dialog_navtex->run || dialog_sstv->run || dialog_js8->run || dialog_psk->run ||
        dialog_olivia->run || dialog_jtty->run) {
        break;
    }

    if (hkey->state == HKEY_RELEASE) {
        dialog_construct(dialog_channels, obj);
    }
    break;



/*
        case HKEY_VM:
            if (hkey->state == HKEY_RELEASE) {
                dialog_construct(dialog_channels, obj);
            } else if (hkey->state == HKEY_LONG) {
                dialog_channels_add_on_open();
                dialog_construct(dialog_channels, obj);
            }
            break;
*/


/*
        case HKEY_FIL:
            if (hkey->state == HKEY_RELEASE) {
                if (dialog_channels_recall_relative(-1)) {
                    channel_overlay_show(
                        dialog_channels_last_recalled_name()
                    );
                }
            }
            break;

        case HKEY_GENE:
            if (hkey->state == HKEY_RELEASE) {
                if (dialog_channels_recall_relative(+1)) {
                    channel_overlay_show(
                        dialog_channels_last_recalled_name()
                    );
                }
            }
            break;
*/

        /* Hand mic FIL: the front DFL key (filter pages; long press is its action) */
        case HKEY_FIL:
            if (hkey->state == HKEY_RELEASE) {
                apps_disable();
                buttons_load_page_group(buttons_group_dfl);
            } else if (hkey->state == HKEY_LONG) {
                main_screen_action(params.long_dfl);
            }
            break;

        /* Hand mic MODE: press steps LSB, USB, CW, CW-R, AM, NFM; long press is the SSB key's long press (DIG on/off) */
        case HKEY_MODE:
            if (!mode_lock && hkey->state == HKEY_RELEASE) {
                static const x6100_mode_t order[] = {x6100_mode_lsb, x6100_mode_usb, x6100_mode_cw,
                                                     x6100_mode_cwr, x6100_mode_am,  x6100_mode_nfm};
                x6100_mode_t mode = cparam_i_get(cfg_cur_mode);
                size_t       i    = 0;

                if (mode == x6100_mode_lsb_dig || mode == x6100_mode_usb_dig) {
                    mode = mode == x6100_mode_lsb_dig ? x6100_mode_lsb : x6100_mode_usb;
                }
                while (i < sizeof(order) / sizeof(order[0]) && order[i] != mode) {
                    i++;
                }
                cparam_i_set(cfg_cur_mode, order[(i + 1) % (sizeof(order) / sizeof(order[0]))]);
            } else if (!mode_lock && hkey->state == HKEY_LONG) {
                change_mode(KEYPAD_MODE_SSB, KEYPAD_LONG);
            }
            break;

        case HKEY_GENE:
            if (hkey->state == HKEY_RELEASE) {
                broadcast_overlay_show();
            }
            break;

        case HKEY_NW:
            if (hkey->state == HKEY_RELEASE) {
                broadcast_overlay_save_channel();
            }
            break;


        case HKEY_F1:
            if (hkey->state == HKEY_RELEASE) {
                main_screen_action(params.press_f1);
            } else if (hkey->state == HKEY_LONG) {
                main_screen_action(params.long_f1);
            }
            break;

        case HKEY_F2:
            if (hkey->state == HKEY_RELEASE) {
                main_screen_action(params.press_f2);
            } else if (hkey->state == HKEY_LONG) {
                main_screen_action(params.long_f2);
            }
            break;

        default:
            LV_LOG_WARN("Unsuported key: %u", hkey->key);
            break;
    }
}

static void main_screen_radio_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);

    lv_event_send(meter, code, NULL);
    lv_event_send(tx_info, code, NULL);
    lv_event_send(spectrum, code, NULL);

    dialog_send(code, NULL);
}

static void main_screen_update_cb(lv_event_t * e) {

    spectrum_clear();
}

/* Main knob acceleration from the time per detent (ms, 0: none): grows
 * smoothly with the speed, x2 at dt = k (Lite) or 2k (Strong), then half
 * as steep as upstream's 2^(k / dt), up to x6 (Lite) or x15 (Strong);
 * the onset setting moves k (upstream ae2af22 uses 20 and 40) */
static uint16_t freq_accel(uint16_t dt) {
    float k = 5.0f + 5.0f * param_i_get(cfg_freq_accel_onset);

    if (dt == 0) {
        return 1;
    }
    switch (params.freq_accel.x) {
        case FREQ_ACCEL_LITE:
            return LV_MIN(exp2f(0.5f + k / (2.0f * dt)), 6.0f);

        case FREQ_ACCEL_STRONG:
            return LV_MIN(exp2f(0.5f + k / dt), 15.0f);

        default:
            return 1;
    }
}

/* Cluster spots on the waterfall: turning slowly (at most spot_snap_slow
 * detents per 33 ms), the VFO stops on a spot it passes and stays there for
 * spot_snap_hold more detents, a light magnetic click (hold 0: off). Turning
 * fast goes straight past. Settings → Display → MAIN SCREEN. Not in CW: the
 * segment is thick with spots and a CW signal is tuned by ear, so the knob
 * kept sticking (ZAP and the app's tap centre a signal instead). */
static int32_t snap_hz;
static int     snap_hold;

static void freq_shift(int16_t diff, uint16_t dt) {
    if (subject_i_get(freq_lock)) {
        return;
    }

    int32_t old  = cparam_i_get(cfg_fg_freq);
    int32_t freq = old;
    int32_t spot;
    int32_t mode = cparam_i_get(cfg_cur_mode);
    int     hold = mode == x6100_mode_cw || mode == x6100_mode_cwr ? 0 : param_i_get(cfg_spot_snap_hold);
    bool    slow = hold > 0 && (dt == 0 || dt * param_i_get(cfg_spot_snap_slow) >= 33);

    if (slow && snap_hold > 0 && old == snap_hz) {
        snap_hold--;
        return;
    }
    snap_hold = 0;

    int32_t df = diff * param_i_get(cfg_mode_freq_step) * freq_accel(dt);
    freq = align_int(freq + df, abs(df));
    if (slow && spot_marks_crossed(old, freq, &spot)) {
        freq      = spot;
        snap_hz   = spot;
        snap_hold = hold;
    }
    cparam_i_set(cfg_fg_freq, freq);
}

static void main_screen_rotary_cb(lv_event_t * e) {
    rotary_data_t *data = (rotary_data_t *) lv_event_get_param(e);

    freq_shift(data->diff, data->dt);
    dialog_rotary(data->diff);
    free(data);
}

/* VOL press: turning changes the value, or chooses what VOL controls */
void main_screen_vol_press(void) {
    if (walkie_on()) return;    /* the volume, always */
    switch (vol->state) {
        case VOL_STATE_EDIT:
            vol->state = VOL_STATE_SELECT;
            knobs_set_vol_state(false);
            break;

        case VOL_STATE_SELECT:
            vol->state = VOL_STATE_EDIT;
            knobs_set_vol_state(true);
            break;
    }
    vol_update(0);
}

static void spectrum_key_cb(lv_event_t * e) {
    uint32_t key = *((uint32_t *)lv_event_get_param(e));

    if (cw_field_key(key)) {
        return;
    }
    switch (key) {
        case '-':
            if (!subject_i_get(freq_lock)) {
                freq_shift(-1, 0);
            }
            break;

        case '=':
            if (!subject_i_get(freq_lock)) {
                freq_shift(+1, 0);
            }
            break;

        case '_':
            next_freq_step(false);
            break;

        case '+':
            next_freq_step(true);
            break;

        case KEY_VOL_LEFT_EDIT:
        case '[':
            vol_update(-1);
            break;

        case KEY_VOL_RIGHT_EDIT:
        case ']':
            vol_update(+1);
            break;

        case KEY_VOL_LEFT_SELECT:
        case '{':
            vol_change_ctrl(-1);
            break;

        case KEY_VOL_RIGHT_SELECT:
        case '}':
            vol_change_ctrl(+1);
            break;

        case KEYBOARD_F9:
            dialog_construct(dialog_settings, obj);
            break;

        case LV_KEY_LEFT:
            switch (mfk_state) {
                case MFK_STATE_EDIT:
                    mfk_update(-1);
                    break;

                case MFK_STATE_SELECT:
                    mfk_change_ctrl(-1);
                    break;
            }
            break;

        case LV_KEY_RIGHT:
            switch (mfk_state) {
                case MFK_STATE_EDIT:
                    mfk_update(+1);
                    break;

                case MFK_STATE_SELECT:
                    mfk_change_ctrl(+1);
                    break;
            }
            break;

        case LV_KEY_ESC:
            // VOL press also
            if (!dialog_is_run()) {
                main_screen_vol_press();
            }
            break;

        case KEYBOARD_PRINT:
        case KEYBOARD_PRINT_SCR:
            screenshot_take();
            break;

        case KEYBOARD_SCRL_LOCK:
            subject_i_set(freq_lock, !subject_i_get(freq_lock));
            break;

        case KEYBOARD_PGUP:
            if (!band_lock) {
                cfg_band_load_next(true);
            }
            dialog_send(EVENT_BAND_UP, NULL);
            break;

        case KEYBOARD_PGDN:
            if (!band_lock) {
                cfg_band_load_next(false);
            }
            dialog_send(EVENT_BAND_DOWN, NULL);
            break;

        case HKEY_FINP:
        case 'f':
            if (!subject_i_get(freq_lock)) {
                dialog_construct(dialog_freq, obj);
            }
            break;

        default:
            break;
    }
}

/* MFK as on the main screen: dir -1 / +1 turns it, 0 is the press */
void main_screen_mfk(int dir) {
    if (dir) {
        if (mfk_state == MFK_STATE_EDIT)
            mfk_update(dir);
        else
            mfk_change_ctrl(dir);
        return;
    }
    switch (mfk_state) {
        case MFK_STATE_EDIT:
            mfk_state = MFK_STATE_SELECT;
            knobs_set_mfk_state(false);
            break;

        case MFK_STATE_SELECT:
            mfk_state = MFK_STATE_EDIT;
            knobs_set_mfk_state(true);
            break;
    }
    mfk_update(0);
}

static void spectrum_pressed_cb(lv_event_t * e) {
    lv_indev_t *indev = lv_indev_get_act();

    /* The keyboard Enter that sent the CW field line */
    if (indev && lv_indev_get_type(indev) == LV_INDEV_TYPE_KEYPAD && cw_field_visible()) return;
    if (dialog_cluster_alert_press()) return;
    switch (mfk_state) {
        case MFK_STATE_EDIT:
            mfk_state = MFK_STATE_SELECT;
            knobs_set_mfk_state(false);
            break;

        case MFK_STATE_SELECT:
            mfk_state = MFK_STATE_EDIT;
            knobs_set_mfk_state(true);
            break;
    }
    mfk_update(0);
}

/* Long MFK press: with the Cluster item active, tune to the highlighted spot.
 * The press already toggled the MFK state; toggle it back to keep browsing. */
static void spectrum_long_pressed_cb(lv_event_t * e) {
    if (mfk_ctrl == CTRL_CLUSTER) {
        cluster_column_tune();
        spectrum_pressed_cb(e);
    }
}

static lv_timer_t *keys_timer;   /* an enable on its way: a dialog opened meanwhile cancels it */

static void keys_enable_cb(lv_timer_t *t) {
    keys_timer = NULL;
    lv_group_add_obj(keyboard_group, spectrum);
    lv_group_set_editing(keyboard_group, true);
}

void main_screen_keys_enable(bool value) {
    if (value) {
        if (!keys_timer) {
            keys_timer = lv_timer_create(keys_enable_cb, 100, NULL);
            lv_timer_set_repeat_count(keys_timer, 1);
        }
    } else {
        if (keys_timer) {
            lv_timer_del(keys_timer);
            keys_timer = NULL;
        }
        lv_group_remove_obj(spectrum);
        lv_group_set_editing(keyboard_group, false);
    }
}

void main_screen_lock_freq(bool lock) {
    subject_i_set(freq_lock, lock);
}

void main_screen_lock_band(bool lock) {
    band_lock = lock;
}

void main_screen_lock_mode(bool lock) {
    mode_lock = lock;
    info_lock_mode(lock);
}

void main_screen_lock_ab(bool lock) {
    ab_lock = lock;
}

bool main_screen_freq_locked() {
    return subject_i_get(freq_lock);
}

bool main_screen_band_locked() {
    return band_lock;
}

bool main_screen_mode_locked() {
    return mode_lock;
}

void main_screen_set_freq(uint64_t freq) {
    cparam_i_set(cfg_fg_freq, freq);
    event_send(lv_scr_act(), EVENT_SCREEN_UPDATE, NULL);
}
/* With or without the spectrum, the band plan is a strip at the top of the
 * waterfall and the cluster spots go on the waterfall itself, a short line at
 * its top (across the strip) and the call under it; the waterfall stays where it
 * is (a band of their own for the tags was a tall grey bar that took its room). */
#define SPOT_LINE_H 8

static void on_cluster_spots_wf_change(Subject *subj, void *user_data) {
    lv_coord_t y = wf_y;

    lv_coord_t strip = param_i_get(cfg_band_plan) ? BAND_PLAN_H : 0;

    lv_obj_set_y(waterfall, y);
    freq_scale_lines_set_y(y);
    band_plan_set_y(y);
    spot_marks_layout(y, y + strip + SPOT_LINE_H);
    /* The cluster column starts under the band plan strip, not over it */
    y += strip;
    cluster_column_layout(y);
}

static void on_band_plan_change(Subject *subj, void *user_data) {
    band_plan_refresh();
    on_cluster_spots_wf_change(NULL, NULL);
}

static void on_cluster_column_change(Subject *subj, void *user_data) {
    if (!param_i_get(cfg_cluster_column)) {
        mfk_cluster_gone();
    }
    cluster_column_refresh();
}


/* Under the frequency strip. Spectrum off: it shrinks under the top bars,
 * invisible but still the key target, and the waterfall takes its place.
 * Returns the y under it (the frequency scale) */
static lv_coord_t spectrum_layout(void) {
    lv_coord_t y = 0;

    if (param_i_get(cfg_spectrum_show)) {
        y = FREQ_STRIP_Y + FREQ_STRIP_H;
        spectrum_height = param_i_get(cfg_spectrum_height);
        lv_obj_remove_local_style_prop(spectrum, LV_STYLE_OPA, 0);
    } else {
        spectrum_height = FREQ_STRIP_Y + FREQ_STRIP_H + METER_ROW_H;
        lv_obj_set_style_opa(spectrum, LV_OPA_TRANSP, 0);
    }
    lv_obj_set_y(spectrum, y);
    lv_obj_set_height(spectrum, spectrum_height);
    return y + spectrum_height;
}

/* Spectrum on/off or its height (Settings, the remote): what is under it moves, live */
static void on_spectrum_layout_change(Subject *subj, void *user_data) {
    lv_coord_t y = spectrum_layout();

    freq_scale_set_y(theme_shape.deck ? y - spectrum_height : y);   /* deck: the edges in the spectrum */
    y += freq_height;
    if (y == wf_y) {
        return;     /* the height of a spectrum that is off */
    }
    wf_y = y;
    waterfall_set_height(480 - y);
    on_cluster_spots_wf_change(NULL, NULL);     /* waterfall, its lines, band plan, spots, cluster column */
    panel_keep_below(wf_y - 8);
}

static void channels_poll_cb(lv_timer_t *t) {
    dialog_channels_poll();
}

static void build(void);

lv_obj_t * main_screen() {
    broadcast_db_init();
    channels_load();    /* creates channels.json with the defaults when missing */
    lv_timer_create(channels_poll_cb, 2000, NULL);

    freq_lock = subject_i_create(false);

    obj = lv_obj_create(NULL);

    lv_obj_add_event_cb(obj, main_screen_rotary_cb, EVENT_ROTARY, NULL);
    lv_obj_add_event_cb(obj, main_screen_keypad_cb, EVENT_KEYPAD, NULL);
    lv_obj_add_event_cb(obj, main_screen_hkey_cb, EVENT_HKEY, NULL);
    lv_obj_add_event_cb(obj, main_screen_radio_cb, EVENT_RADIO_TX, NULL);
    lv_obj_add_event_cb(obj, main_screen_radio_cb, EVENT_RADIO_RX, NULL);
    lv_obj_add_event_cb(obj, main_screen_update_cb, EVENT_SCREEN_UPDATE, NULL);

    lv_obj_add_style(obj, &background_style, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    /* On the top layer, over dialogs and the other top-layer items (Dyuldin, upstream da454b48) */
    msg = msg_init(lv_layer_top());
    msg_tiny = msg_tiny_init(lv_layer_top());

    build();
    buttons_load_page(&buttons_page_vol_1);
    return obj;
}

/* The widgets: at start, and again on a theme change (main_screen_set_theme()) */
static void build(void) {
    uint16_t y = 0;

    spectrum = spectrum_init(obj);
    main_screen_keys_enable(true);

    lv_obj_add_event_cb(spectrum, spectrum_key_cb, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(spectrum, spectrum_pressed_cb, LV_EVENT_PRESSED, NULL);
    /* Not scrollable: LVGL then sends LONG_PRESSED for the MFK button (it was a no-op before) */
    lv_obj_clear_flag(spectrum, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(spectrum, spectrum_long_pressed_cb, LV_EVENT_LONG_PRESSED, NULL);

    spectrum_min_max_reset();

    freq_height = theme_shape.deck ? 0 : 36;   /* deck: the edge frequencies go in the spectrum's top corners */
    y = spectrum_layout();

    freq_scale_init(obj, theme_shape.deck ? y - spectrum_height : y);

    spot_tag_y = y + 36;

    y += freq_height;
    wf_y = y;

    waterfall = waterfall_init(obj);

    waterfall_min_max_reset();

    lv_obj_set_y(waterfall, y);
    waterfall_set_height(480 - y);
    band_plan_init(obj, y);   /* the top of the waterfall */
    freq_scale_lines_init(obj, y, 480 - y);
    /* Cluster spots: tags under the frequency labels, lines at the bottom of
     * the spectrum, or at the top of the waterfall when it is off */
    spot_marks_init(obj, y, spot_tag_y);   /* laid out by on_cluster_spots_wf_change() */
    cluster_column_init(obj, wf_y);
    subject_subscribe_delayed_and_notify((Subject*)cfg_cluster_spots_wf, on_cluster_spots_wf_change, NULL);
    subject_subscribe_delayed((Subject*)cfg_band_plan, on_band_plan_change, NULL);
    subject_subscribe_delayed((Subject*)cfg_cluster_column, on_cluster_column_change, NULL);
    subject_subscribe_delayed((Subject*)cfg_spectrum_show, on_spectrum_layout_change, NULL);
    subject_subscribe_delayed((Subject*)cfg_spectrum_height, on_spectrum_layout_change, NULL);

    cw_field_init(obj);
    knobs_init(obj);

    buttons_init(obj);

    panel_init(obj);
    panel_keep_below(wf_y - 8);   /* as with the 76 px spectrum: on the scale's bottom edge */

    info_init(obj);
    clock_init(obj);    /* deck: over the info rows */

    freq_strip_init(obj);
    meter = meter_init(obj);    /* deck: on the frequency panel */
    tx_info = tx_info_init(obj);

    /* Holo: an accent line between the top bar and the frequency strip */
    if (theme_shape.rule) {
        lv_obj_t *rule = lv_obj_create(obj);
        lv_obj_remove_style_all(rule);
        lv_obj_set_pos(rule, 0, TOP_BAR_HEIGHT - TOP_BAR_MARGIN);
        lv_obj_set_size(rule, 800, TOP_BAR_MARGIN);
        lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(rule, TC(accent), 0);
    }

    cw_tune_init(obj);

    subject_subscribe_delayed((Subject*)freq_lock, on_fg_freq_change, NULL);
    subject_subscribe_delayed((Subject*)cfg_band_split, on_fg_freq_change, NULL);
    subject_subscribe_delayed((Subject*)cfg_fg_freq, on_fg_freq_change, NULL);

    subject_subscribe_delayed((Subject*)freq_lock, update_freq_boundaries, NULL);
    subject_subscribe_delayed((Subject*)cfg_fg_freq, update_freq_boundaries, NULL);
    subject_subscribe_delayed((Subject*)cfg_band_if_shift, update_freq_boundaries, NULL);
    subject_subscribe_delayed((Subject*)cfg_wf_mode, update_freq_boundaries, NULL);
    subject_subscribe_delayed_and_notify((Subject*)cfg_mode_zoom, update_freq_boundaries, NULL);

    subject_subscribe_delayed_and_notify((Subject*)cfg_bg_freq, on_fg_freq_change, NULL);

    subject_subscribe_delayed((Subject*)cfg_band_if_shift, update_zoom_on_if_shift_change, NULL);
    subject_subscribe_delayed((Subject*)cfg_mode_zoom, update_zoom_on_if_shift_change, NULL);
}

/* A theme change, live: the open app closes, the main screen is built again with the new
 * theme; the radio and the settings stay as they are. The modules keep their subscriptions,
 * timers and threads (subject_no_duplicates()): they find the new widgets through the same
 * pointers. Not in walkie mode (the buttons are on its screen) */
void main_screen_set_theme(const char *name) {
    apps_disable();
    buttons_page_t *page = buttons_get_cur_page();
    buttons_unload_page();
    /* The overlays go with the widgets: closed as their timers would */
    if (channel_overlay_timer) {
        lv_timer_del(channel_overlay_timer);
        channel_overlay_timer_cb(NULL);
    }
    if (broadcast_overlay_timer) {
        lv_timer_del(broadcast_overlay_timer);
        broadcast_overlay_timer_cb(NULL);
    }
    lv_async_call_cancel((lv_async_cb_t)lv_obj_invalidate, spectrum);   /* spectrum.c update_dnf() */
    lv_obj_clean(obj);
    walkie_drop();

    styles_load(name);
    subject_no_duplicates(true);
    build();
    subject_no_duplicates(false);
    buttons_load_page(page ? page : &buttons_page_vol_1);
    lv_obj_report_style_change(NULL);   /* what is left: the messages, the top layer */
    event_obj_drop_deleted();           /* what the threads queued for the old widgets */
}

void main_screen_notify_rx_tx(bool tx) {
    if (tx) {
        event_send(obj, EVENT_RADIO_TX, NULL);

    } else {
        event_send(obj, EVENT_RADIO_RX, NULL);
    }
}

void main_screen_notify_low_power(bool is_low) {
    if (is_low) {
        if (!low_power_timer) {
            low_power_timer = lv_timer_create(low_power_timer_cb, 30000, NULL);
            lv_timer_set_repeat_count(low_power_timer, 1);
            msg_schedule_long_text_fmt("Low battery! Turning off in 30s.");
        }
    } else {
        if (low_power_timer) {
            lv_timer_del(low_power_timer);
            low_power_timer = NULL;
        }
    }
}

static void on_fg_freq_change(Subject *subj, void *user_data) {
    int32_t    f;

    bool split = param_i_get(cfg_band_split);
    int32_t fg = cparam_i_get(cfg_fg_freq);
    int32_t bg = cparam_i_get(cfg_bg_freq);

    if (split && radio_get_state() == RADIO_TX) {
        f = bg;
    } else {
        f = fg;
    }

    uint16_t    mhz, khz, hz;

    split_freq(f, &mhz, &khz, &hz);

    if (params.mag_freq.x) {
        if (mhz < 100) {
            msg_tiny_set_text_fmt("%i.%03i.%03i", mhz, khz, hz);
        } else {
            msg_tiny_set_text_fmt("%i.%03i", mhz, khz);
        }
    }
}

static void update_freq_boundaries(Subject *subj, void *user_data) {
    bool split = param_i_get(cfg_band_split);
    int32_t fg = cparam_i_get(cfg_fg_freq);
    int32_t bg = cparam_i_get(cfg_bg_freq);
    int32_t f;

    if (split && radio_get_state() == RADIO_TX) {
        f = bg;
    } else {
        f = fg;
    }

    int32_t     half_width = 50000;
    uint32_t    color = subject_i_get(freq_lock) ? theme_hex(TC_text_soft) : theme_hex(TC_text);

    int32_t zoom = param_i_get(cfg_mode_zoom);

    if (params.waterfall_zoom.x) {
        half_width /= zoom;
    }

    /* The waterfall is centred on the VFO minus the IF shift */
    /* Fixed waterfall mode: the centre stays put while the VFO moves */
    int32_t center = f - param_i_get(cfg_band_if_shift) - waterfall_view_shift();
    freq_scale_update(center, half_width, color);
    spot_marks_update(center, half_width);
    band_plan_update(center, half_width);
}

static void update_zoom_on_if_shift_change(Subject *subj, void *user_data) {
    int32_t half_width = 40000;
    int32_t new_if_shift = param_i_get(cfg_band_if_shift);
    uint32_t zoom = param_i_get(cfg_mode_zoom);
    uint32_t new_zoom = zoom;
    while ((abs(new_if_shift) * new_zoom / half_width) && (new_zoom > 1))
    {
        new_zoom >>= 1;
    }
    if (new_zoom != zoom) {
        param_i_set(cfg_mode_zoom, new_zoom);
    }
}
