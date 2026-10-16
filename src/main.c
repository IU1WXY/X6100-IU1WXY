/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "update_check.h"
#include "lvgl/lvgl.h"
#include "lv_drivers/display/fbdev.h"
#include "lv_drivers/display/drm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>
#include <sys/time.h>

#include "main.h"
#include "main_screen.h"
#include "screen_rec.h"
#include "styles.h"
#include "fonts/font_select.h"
#include "radio.h"
#include "dsp.h"
#include "util.h"
#include "keyboard.h"
#include "spectrum.h"
#include "waterfall.h"
#include "keypad.h"
#include "params/params.h"
#include "dialog_birdie_cal.h"
#include "cfg/cfg_api.h"
#include "audio.h"
#include "cw.h"
#include "panel.h"
#include "cat.h"
#include "wfview_server.h"
#include "remote_server.h"
#include "clock.h"
#include "rtty.h"
#include "backlight.h"
#include "events.h"
#include "gps.h"
#include "mfk.h"
#include "vol.h"
#include "qso_log.h"
#include "scheduler.h"
#include "wifi.h"
#include "btaudio.h"
#include "bt_dev.h"
#include "usb_devices.h"
#include "dialog_cluster.h"
#include "sstv_resources.h"
#include "walkie.h"

#define DISP_BUF_SIZE (800 * 480 * 4)

rotary_t                    *vol;
encoder_t                   *mfk;

static lv_color_t           buf1[DISP_BUF_SIZE];
static lv_color_t           buf2[DISP_BUF_SIZE];
static lv_disp_draw_buf_t   disp_buf;
static lv_disp_drv_t        disp_drv;

void * tick_thread (void *args);

#if USE_DRM
/* Transparent screen: LVGL cleared the whole draw buffer (6 MB) before every area it redraws,
 * even the clock's seconds, ~30 times a second. The area is drawn packed: clear just that. */
static void clear_cb(lv_disp_drv_t *drv, uint8_t *buf, uint32_t size) {
    memset(buf, 0, lv_area_get_size(drv->draw_ctx->buf_area) * sizeof(lv_color_t));
}
#endif

/* Display settings that would break the screen are put right at every start
 * (an old or hand-edited params.db, a backup restored from another version):
 * the spectrum is disabled in this firmware, the font
 * must exist and the legacy spectrum values must stay in their ranges. */
static void fix_uint8(params_uint8_t *p) {
    if (p->max > p->min && (p->x < p->min || p->x > p->max)) {
        uint8_t v = p->x < p->min ? p->min : p->max;

        fprintf(stderr, "Settings: %s %u out of range, set to %u\n", p->name, p->x, v);
        params_uint8_set(p, v);
    }
}

static void display_settings_check(void) {
    if (params.font.x > FONT_DEJAVU) {
        fprintf(stderr, "Settings: font %u unknown, set to the default\n", params.font.x);
        params_uint8_set(&params.font, FONT_SONY);
    }
    fix_uint8(&params.spectrum_beta);
    fix_uint8(&params.spectrum_peak_hold);
    fix_uint8(&params.spectrum_peak_speed);
}

#if USE_DRM
/* SIGUSR1: the screen to /tmp/screen.raw (with DRM /dev/fb0 no longer shows it) */
static volatile sig_atomic_t dump_requested;

static void dump_signal(int sig) {
    dump_requested = 1;
}
#endif

/* The panel is portrait: the walkie screen goes to it turned 180, not 90. DRM only */
bool main_display_portrait(bool on) {
#if USE_DRM
    if (screen_rec_out != drm_flush) {
        return false;
    }
    drm_set_portrait(on);
    disp_drv.hor_res = on ? 480 : 800;
    disp_drv.ver_res = on ? 800 : 480;
    lv_disp_drv_update(lv_disp_get_default(), &disp_drv);
    return true;
#else
    return false;
#endif
}

int main(void) {
    setenv("X6100_FW", strrchr(VERSION, 'v') + 1, 1);   /* the helpers' User-Agent (x6100-field, spots) */
    clock_tz_init();
    lv_init();
    // lv_png_init();

#if USE_DRM
    signal(SIGUSR1, dump_signal);
    if (drm_init()) {
        screen_rec_out = drm_flush;
        fclose(fopen("/tmp/x6100_gui.drm", "w"));  /* tells screenshot tools to use SIGUSR1 */
    } else {
        unlink("/tmp/x6100_gui.drm");
        fbdev_init();
    }
#else
    fbdev_init();
#endif
    audio_init();
    event_init();
    usb_devices_monitor_init();

    lv_disp_draw_buf_init(&disp_buf, buf1, NULL, DISP_BUF_SIZE);
    lv_disp_drv_init(&disp_drv);

    disp_drv.draw_buf   = &disp_buf;
    disp_drv.flush_cb   = screen_rec_flush;
    // disp_drv.hor_res    = 480;
    // disp_drv.ver_res    = 800;
    disp_drv.hor_res    = 800;
    disp_drv.ver_res    = 480;
#if USE_DRM
    /* GUI over the waterfall plane: see-through parts keep their alpha */
    disp_drv.screen_transp = drm_planes();
    disp_drv.clear_cb = clear_cb;
#endif
    // disp_drv.sw_rotate  = 1;
    // disp_drv.rotated    = LV_DISP_ROT_90;

    lv_disp_drv_register(&disp_drv);

    lv_disp_set_bg_color(lv_disp_get_default(), lv_color_black());
    lv_disp_set_bg_opa(lv_disp_get_default(), LV_OPA_COVER);

    keyboard_init();

    keypad_init("/dev/input/event0");
    keypad_init("/dev/input/event4");

    rotary_main_init("/dev/input/event1");

    vol = rotary_init("/dev/input/event2");
    mfk = encoder_init("/dev/input/event3");

    vol->left[VOL_STATE_EDIT] = KEY_VOL_LEFT_EDIT;
    vol->right[VOL_STATE_EDIT] = KEY_VOL_RIGHT_EDIT;

    vol->left[VOL_STATE_SELECT] = KEY_VOL_LEFT_SELECT;
    vol->right[VOL_STATE_SELECT] = KEY_VOL_RIGHT_SELECT;
    vol->state = VOL_STATE_EDIT;

    params_init();
    birdie_cal_load();
    sstv_resources_init();
    display_settings_check();
    audio_set_play_vol(params.play_gain_db_f.x);
    audio_set_rec_vol(params.rec_gain_db_f.x);
    mfk_init();
    vol_init();
    /* Bundled themes to the SD card once; one already there is never overwritten */
    if (system("mkdir -p /mnt/themes; for t in /usr/share/x6100/themes/*; do "
               "[ -e \"/mnt/themes/${t##*/}\" ] || cp -r \"$t\" /mnt/themes/; done") != 0)
        LV_LOG_WARN("Bundled themes not copied to /mnt/themes");
    styles_load(params.theme_name.x);

    radio_init();
    audio_mixer_setup(x6100_control_get_base_ver());
    dsp_init();
    lv_obj_t *main_obj = main_screen();
    radio_start();

    cw_init();
    rtty_init();
    radio_set_rx_tx_notify_fn(&main_screen_notify_rx_tx);
    radio_set_low_power_cb(&main_screen_notify_low_power);
    wifi_power_setup();
    btaudio_init();
    bt_dev_init();
    backlight_init();
    cat_init();
    wfview_server_init();
    remote_server_init();
    gps_init();
    if (!qso_log_init()) {
        LV_LOG_ERROR("Can't init QSO log");
    }
    qso_log_import_adif("/mnt/incoming_log.adi");
    dialog_cluster_start_client();
    update_check_start();

    pthread_t thread;
    pthread_create(&thread, NULL, tick_thread, NULL);
    pthread_detach(thread);

    lv_scr_load(main_obj);
    walkie_init(main_obj);      /* its screen instead, when on */
    if (!walkie_on() && param_i_get(cfg_field_boot)) {
        main_screen_start_app(ACTION_APP_FIELD);
    }

    int64_t next_loop_time, sleep_time, loop_start_time;
    while (1) {
        loop_start_time = get_time();
        // observer_delayed_notify_all();
        event_obj_check();
        scheduler_work();
        next_loop_time = lv_timer_handler() + loop_start_time;
#if USE_DRM
        drm_commit();
        if (dump_requested) {
            dump_requested = 0;
            drm_dump("/tmp/screen.raw.tmp");
            rename("/tmp/screen.raw.tmp", "/tmp/screen.raw");
        }
#endif
        sleep_time = next_loop_time - get_time();
        if (sleep_time > 0) {
            usleep(sleep_time * 1000);
        } else {
            /* Cap loop rate when LVGL reports no delay to reduce CPU. */
            usleep(5000);
        }
    }
    return 0;
}

void * tick_thread (void *args)
{
      while(1) {
        usleep(5 * 1000);
        lv_tick_inc(5);
    }
}
