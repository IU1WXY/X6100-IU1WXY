/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */
#include "buttons.h"

#include "controls.h"
#include "util.h"
#include "radio.h"
#include "tx_player.h"
#include "cfg/settings_manager.h"
#include "cfg/encoder_defaults.h"

#include <stdio.h>
#include <cstring>
#include <string>
#include <vector>
#include <array>

extern "C" {
    #include "styles.h"
    #include "theme.h"
    #include "main_screen.h"
    #include "screen_rec.h"
    #include "mfk.h"
    #include "vol.h"
    #include "msg.h"
    #include "panel.h"
    #include "params/params.h"
    #include "pubsub_ids.h"
    #include "walkie.h"
    #include "cw_field.h"
}

#define STATE_ASSIGNED LV_STATE_USER_1
#define STATE_ACTIVE   LV_STATE_USER_2  /* the encoder is on this control now */

struct _disp_button_t {
    lv_obj_t      *parent;
    lv_obj_t      *label;
    lv_obj_t      *value;     /* second line of a "Name:\nvalue" label */
    lv_obj_t      *vol_mark;
    lv_obj_t      *mfk_mark;
    button_data_t *data;  // Link to button data;
};

// Array of display buttons (size BUTTONS)
static disp_btn_t      disp_btns[BUTTONS];
static buttons_page_t *cur_page = NULL;
static std::array<char, CTRL_FAST_ACCESS_LAST> fast_binds;
static std::array<char, CTRL_LAST> binds;

static void disp_btn_refresh(disp_btn_t *b);
static void disp_btn_clear(disp_btn_t *b);
static void marks_refresh(disp_btn_t *b);

static void button_app_page_cb(button_data_t *data);
static void button_encoder_update_cb(button_data_t *data);
static void button_mem_load_cb(button_data_t *data);

static void encoder_binds_change_cb(Subject *subj, void *user_data);
static void label_update_cb(Subject *subj, void *user_data);
static void set_btn_text(disp_btn_t *b, const char *text);

static void button_encoder_hold_update_cb(button_data_t *data);
static void button_mem_save_cb(button_data_t *data);

// Label getters

static const char * vol_label_getter();
static const char * sql_label_getter();
static const char * rfg_label_getter();
static const char * tx_power_label_getter();

static const char * filter_low_label_getter();
static const char * filter_high_label_getter();
static const char * filter_bw_label_getter();

static const char * moni_level_label_getter();

static const char * rit_label_getter();
static const char * xit_label_getter();

static const char * agc_label_getter();
static const char * comp_label_getter();
static const char * if_shift_label_getter();
static const char * wf_ref_label_getter();
static const char * wf_mode_label_getter();
static const char * screen_rec_label_getter();
static const char * span_wider_label_getter();
static const char * span_narrower_label_getter();

static const char * vox_on_label_getter();

static const char * key_speed_label_getter();
static const char * key_volume_label_getter();
static const char * key_train_label_getter();
static const char * key_tone_label_getter();

static const char * key_mode_label_getter();
static const char * iambic_mode_label_getter();
static const char * qsk_time_label_getter();
static const char * key_ratio_label_getter();

static const char * cw_decoder_label_getter();
static const char * cw_tuner_label_getter();
static const char * cw_snr_label_getter();

static const char * cw_peak_on_label_getter();
static const char * cw_peak_q_label_getter();

static const char * dnf_label_getter();
static const char * dnf_center_label_getter();
static const char * dnf_width_label_getter();
static const char * dnf_auto_label_getter();

static const char * nb_label_getter();
static const char * nb_level_label_getter();
static const char * nb_width_label_getter();

static const char * nr_label_getter();
static const char * nr_level_label_getter();

static void button_agc_cb(button_data_t *data);

/* Make VOL/MFK button functions */
static button_data_t make_encoder_btn(const char *name, cfg_ctrl_t ctrl) {
    return button_data_t{.type            = BTN_TEXT,
                         .label           = name,
                         .press           = button_encoder_update_cb,
                         .hold            = button_encoder_hold_update_cb,
                         .ctrl            = ctrl,
                         .encoder_allowed = true};
}

static button_data_t make_encoder_btn(const char *(*label_fn)(), cfg_ctrl_t ctrl, Subject *subj = nullptr) {
    return button_data_t{.type            = BTN_TEXT_FN,
                         .label_fn        = label_fn,
                         .press           = button_encoder_update_cb,
                         .hold            = button_encoder_hold_update_cb,
                         .ctrl            = ctrl,
                         .encoder_allowed = true,
                         .subj            = subj};
}

/* Make MEM buttons functions */
static button_data_t make_mem_btn(const char *name, int32_t ctrl) {
    return button_data_t{
        .type = BTN_TEXT, .label = name, .press = button_mem_load_cb, .hold = button_mem_save_cb, .ctrl = ctrl};
}

static button_data_t make_app_btn(const char *name, press_action_t ctrl) {
    return button_data_t{.type = BTN_TEXT, .label = name, .press = button_app_page_cb, .hold = nullptr, .ctrl = ctrl};
}

static button_data_t make_page_btn(const char *name) {
    return button_data_t{
        .type = BTN_TEXT, .label = name, .press = button_next_page_cb, .hold = button_prev_page_cb};
}

/* VOL */

static button_data_t btn_vol = {
    .type            = BTN_TEXT_FN,
    .label_fn        = vol_label_getter,
    .press           = button_encoder_update_cb,
    .ctrl            = CTRL_VOL,
    .encoder_allowed = true,
    .subj            = (Subject*)&cfg_sm.p_volume,
};

static button_data_t btn_sql = make_encoder_btn(sql_label_getter, CTRL_SQL, (Subject*)&cfg_sm.p_squelch);
static button_data_t btn_rfg = make_encoder_btn(rfg_label_getter, CTRL_RFG, (Subject*)&cfg_sm.p_rfgain);
/* TX power in the digital apps: press for the next step, up to the apps' 7 W cap */
static const char *dig_pwr_label() {
    static char buf[20];
    snprintf(buf, sizeof(buf), "Power\n%g W", LV_MIN(cfg_sm.p_pwr.get(), TX_PLAYER_MAX_PWR_W));
    return buf;
}

/* The setting itself (the radio follows it): radio_set_pwr() alone left the label and the
 * next step on the old value, and the transmission levelled to it */
static void dig_pwr_cb(button_data_t *data) {
    static const float steps[] = {0.5f, 1.0f, 2.0f, 3.0f, 5.0f, TX_PLAYER_MAX_PWR_W};
    float p = LV_MIN(cfg_sm.p_pwr.get(), TX_PLAYER_MAX_PWR_W), next = steps[0];

    for (float s : steps) {
        if (s > p + 0.05f) {
            next = s;
            break;
        }
    }
    cfg_sm.p_pwr.set(next);
}

button_data_t btn_dig_pwr = {
    .type = BTN_TEXT_FN, .label_fn = dig_pwr_label, .press = dig_pwr_cb, .subj = (Subject *)&cfg_sm.p_pwr};

button_data_t btn_tx_pwr = make_encoder_btn(tx_power_label_getter, CTRL_PWR, (Subject*)&cfg_sm.p_pwr);
static button_data_t btn_flt_low  = make_encoder_btn(filter_low_label_getter, CTRL_FILTER_LOW, (Subject*)&cfg_sm.cp_cur_filter_low);
static button_data_t btn_flt_high = make_encoder_btn(filter_high_label_getter, CTRL_FILTER_HIGH, (Subject*)&cfg_sm.cp_cur_filter_high);
static button_data_t btn_flt_bw   = make_encoder_btn(filter_bw_label_getter, CTRL_FILTER_BW, (Subject*)&cfg_sm.cp_cur_filter_bw);
static button_data_t btn_moni_lvl  = make_encoder_btn(moni_level_label_getter, CTRL_MONI, (Subject*)&cfg_sm.p_moni);

/* MFK */

static button_data_t btn_zoom      = make_encoder_btn("Spectrum\nZoom", CTRL_SPECTRUM_FACTOR);
static button_data_t btn_rit       = make_encoder_btn(rit_label_getter, CTRL_RIT, (Subject*)&cfg_sm.p_rit);
static button_data_t btn_xit       = make_encoder_btn(xit_label_getter, CTRL_XIT, (Subject*)&cfg_sm.p_xit);
static button_data_t btn_agc       = {.type     = BTN_TEXT_FN,
                                      .label_fn = agc_label_getter,
                                      .press    = button_agc_cb,
                                      .subj     = (Subject*)&cfg_sm.cp_cur_agc};
static button_data_t btn_comp      = make_encoder_btn(comp_label_getter, CTRL_COMP, (Subject*)&cfg_sm.p_comp);
static button_data_t btn_if_shift  = make_encoder_btn(if_shift_label_getter, CTRL_IF_SHIFT, (Subject*)&cfg_sm.p_band_if_shift);
static button_data_t btn_wf_ref    = make_encoder_btn(wf_ref_label_getter, CTRL_WF_REF, (Subject*)&cfg_sm.p_waterfall_ref);

static button_data_t btn_wf_mode   = {.type            = BTN_TEXT_FN,
                                      .label_fn        = wf_mode_label_getter,
                                      .press           = controls_toggle_wf_mode,
                                      .hold            = button_encoder_hold_update_cb,
                                      .ctrl            = CTRL_WF_MODE,
                                      .encoder_allowed = true,
                                      .subj            = (Subject*)&cfg_sm.p_wf_mode};
static button_data_t btn_span_wider    = {.type     = BTN_TEXT_FN,
                                          .label_fn = span_wider_label_getter,
                                          .press    = controls_span_wider,
                                          .subj     = (Subject*)&cfg_sm.p_mode_zoom};
static button_data_t btn_span_narrower = {.type     = BTN_TEXT_FN,
                                          .label_fn = span_narrower_label_getter,
                                          .press    = controls_span_narrower,
                                          .subj     = (Subject*)&cfg_sm.p_mode_zoom};

static button_data_t btn_screen_rec = {.type     = BTN_TEXT_FN,
                                       .label_fn = screen_rec_label_getter,
                                       .press    = [](button_data_t *) { screen_rec_toggle(); }};

/* VOX */
static button_data_t btn_vox_on    = {.type            = BTN_TEXT_FN,
                                      .label_fn        = vox_on_label_getter,
                                      .press           = controls_toggle_vox,
                                      .hold            = button_encoder_hold_update_cb,
                                      .ctrl            = CTRL_VOX_ON,
                                      .encoder_allowed = true,
                                      .subj            = (Subject*)&cfg_sm.p_vox_en};

/* MEM */

static button_data_t btn_mem_1 = make_mem_btn("Set 1", 1);
static button_data_t btn_mem_2 = make_mem_btn("Set 2", 2);
static button_data_t btn_mem_3 = make_mem_btn("Set 3", 3);
static button_data_t btn_mem_4 = make_mem_btn("Set 4", 4);
static button_data_t btn_mem_5 = make_mem_btn("Set 5", 5);
static button_data_t btn_mem_6 = make_mem_btn("Set 6", 6);
static button_data_t btn_mem_7 = make_mem_btn("Set 7", 7);
static button_data_t btn_mem_8 = make_mem_btn("Set 8", 8);

/* CW */

static button_data_t btn_key_speed  = make_encoder_btn(key_speed_label_getter, CTRL_KEY_SPEED, (Subject*)&cfg_sm.p_key_speed);
static button_data_t btn_key_volume = make_encoder_btn(key_volume_label_getter, CTRL_KEY_VOL, (Subject*)&cfg_sm.p_key_vol);
static button_data_t btn_key_train  = {.type            = BTN_TEXT_FN,
                                       .label_fn        = key_train_label_getter,
                                       .press           = controls_toggle_key_train,
                                       .hold            = button_encoder_hold_update_cb,
                                       .ctrl            = CTRL_KEY_TRAIN,
                                       .encoder_allowed = true,
                                       .subj            = (Subject*)&cfg_sm.p_key_train};
static button_data_t btn_key_tone   = make_encoder_btn(key_tone_label_getter, CTRL_KEY_TONE, (Subject*)&cfg_sm.p_key_tone);

static button_data_t btn_key_mode        = make_encoder_btn(key_mode_label_getter, CTRL_KEY_MODE, (Subject*)&cfg_sm.p_key_mode);
static button_data_t btn_key_iambic_mode = {.type            = BTN_TEXT_FN,
                                            .label_fn        = iambic_mode_label_getter,
                                            .press           = controls_toggle_key_iambic_mode,
                                            .hold            = button_encoder_hold_update_cb,
                                            .ctrl            = CTRL_IAMBIC_MODE,
                                            .encoder_allowed = true,
                                            .subj            = (Subject*)&cfg_sm.p_iambic_mode};
static button_data_t btn_key_qsk_time    = make_encoder_btn(qsk_time_label_getter, CTRL_QSK_TIME, (Subject*)&cfg_sm.p_qsk_time);
static button_data_t btn_key_ratio       = make_encoder_btn(key_ratio_label_getter, CTRL_KEY_RATIO, (Subject*)&cfg_sm.p_key_ratio);

static button_data_t btn_cw_decoder = {.type            = BTN_TEXT_FN,
                                       .label_fn        = cw_decoder_label_getter,
                                       .press           = controls_toggle_cw_decoder,
                                       .hold            = button_encoder_hold_update_cb,
                                       .ctrl            = CTRL_CW_DECODER,
                                       .encoder_allowed = true,
                                       .subj            = (Subject*)&cfg_sm.p_cw_decoder};
static button_data_t btn_cw_tuner   = {.type            = BTN_TEXT_FN,
                                       .label_fn        = cw_tuner_label_getter,
                                       .press           = controls_toggle_cw_tuner,
                                       .hold            = button_encoder_hold_update_cb,
                                       .ctrl            = CTRL_CW_TUNE,
                                       .encoder_allowed = true,
                                       .subj            = (Subject*)&cfg_sm.p_cw_tune};
static button_data_t btn_cw_snr = make_encoder_btn(cw_snr_label_getter, CTRL_CW_DECODER_SNR, (Subject*)&cfg_sm.p_cw_decoder_snr);

static button_data_t btn_cw_peak_on = {.type            = BTN_TEXT_FN,
                                       .label_fn        = cw_peak_on_label_getter,
                                       .press           = controls_toggle_cw_peak,
                                       .hold            = button_encoder_hold_update_cb,
                                       .ctrl            = CTRL_CW_PEAK_ON,
                                       .encoder_allowed = true,
                                       .subj            = (Subject*)&cfg_sm.p_cw_peak_on};
static button_data_t btn_cw_peak_q = make_encoder_btn(cw_peak_q_label_getter, CTRL_CW_PEAK_Q, (Subject*)&cfg_sm.p_cw_peak_q);

static button_data_t btn_cw_zap = {
    .type = BTN_TEXT, .label = "CW\nZAP", .press = controls_cw_zap, .hold = NULL, .ctrl = CTRL_CW_ZAP};

/* DSP */

static button_data_t btn_dnf        = {.type            = BTN_TEXT_FN,
                                       .label_fn        = dnf_label_getter,
                                       .press           = controls_toggle_dnf,
                                       .hold            = button_encoder_hold_update_cb,
                                       .ctrl            = CTRL_DNF,
                                       .encoder_allowed = true,
                                       .subj            = (Subject*)&cfg_sm.p_dnf};
static button_data_t btn_dnf_center = make_encoder_btn(dnf_center_label_getter, CTRL_DNF_CENTER, (Subject*)&cfg_sm.p_dnf_center);
static button_data_t btn_dnf_width  = make_encoder_btn(dnf_width_label_getter, CTRL_DNF_WIDTH, (Subject*)&cfg_sm.p_dnf_width);
static button_data_t btn_dnf_auto   = {.type            = BTN_TEXT_FN,
                                       .label_fn        = dnf_auto_label_getter,
                                       .press           = controls_toggle_dnf_auto,
                                       .hold            = button_encoder_hold_update_cb,
                                       .ctrl            = CTRL_DNF_AUTO,
                                       .encoder_allowed = true,
                                       .subj            = (Subject*)&cfg_sm.p_dnf_auto};

static button_data_t btn_nb       = {.type            = BTN_TEXT_FN,
                                     .label_fn        = nb_label_getter,
                                     .press           = controls_toggle_nb,
                                     .hold            = button_encoder_hold_update_cb,
                                     .ctrl            = CTRL_NB,
                                     .encoder_allowed = true,
                                     .subj            = (Subject*)&cfg_sm.p_nb};
static button_data_t btn_nb_level = make_encoder_btn(nb_level_label_getter, CTRL_NB_LEVEL, (Subject*)&cfg_sm.p_nb_level);
static button_data_t btn_nb_width = make_encoder_btn(nb_width_label_getter, CTRL_NB_WIDTH, (Subject*)&cfg_sm.p_nb_width);

static button_data_t btn_nr       = {.type            = BTN_TEXT_FN,
                                     .label_fn        = nr_label_getter,
                                     .press           = controls_toggle_nr,
                                     .hold            = button_encoder_hold_update_cb,
                                     .ctrl            = CTRL_NR,
                                     .encoder_allowed = true,
                                     .subj            = (Subject*)&cfg_sm.p_nr};
static button_data_t btn_nr_level = make_encoder_btn(nr_level_label_getter, CTRL_NR_LEVEL, (Subject*)&cfg_sm.p_nr_level);

/* APP */

static button_data_t btn_swr  = make_app_btn("SWR\nScan", ACTION_APP_SWRSCAN);
static void snake_cb(button_data_t *data) {
    main_screen_start_app(ACTION_APP_SNAKE);
}

/* A long press opens the hidden game */
static button_data_t btn_gps  = {.type = BTN_TEXT, .label = "GPS", .press = button_app_page_cb, .hold = snake_cb, .ctrl = ACTION_APP_GPS};

static button_data_t btn_rec      = make_app_btn("RX\nRecorder", ACTION_APP_RECORDER);
static button_data_t btn_settings = make_app_btn("Settings", ACTION_APP_SETTINGS);

static button_data_t btn_cluster = make_app_btn("Cluster", ACTION_APP_CLUSTER);
static button_data_t btn_modes   = make_app_btn("Modes", ACTION_APP_MODES);
static button_data_t btn_cwt     = make_app_btn("CW\nTrainer", ACTION_APP_CWTRAINER);
static button_data_t btn_field   = make_app_btn("Field\nOps", ACTION_APP_FIELD);
static button_data_t btn_bands   = make_app_btn("Bands", ACTION_APP_BANDS);
static button_data_t btn_birdie  = make_app_btn("Birdie\nCal", ACTION_APP_BIRDIE_CAL);

/* RTTY */
extern buttons_page_t buttons_page_rtty;
extern buttons_page_t buttons_page_rtty_2;
static button_data_t btn_rtty_p1 = {
    .type  = BTN_TEXT,
    .label = "(RTTY 1:2)",
    .press = button_next_page_cb,
    .hold  = button_prev_page_cb,
    .next  = &buttons_page_rtty_2,
    .prev  = &buttons_page_rtty_2,
};
static button_data_t btn_rtty_p2 = {
    .type  = BTN_TEXT,
    .label = "(RTTY 2:2)",
    .press = button_next_page_cb,
    .hold  = button_prev_page_cb,
    .next  = &buttons_page_rtty,
    .prev  = &buttons_page_rtty,
};
static button_data_t btn_rtty_rate = {
    .type  = BTN_TEXT,
    .label = "Rate",
    .press = button_encoder_update_cb,
    .ctrl  = CTRL_RTTY_RATE,
    .encoder_allowed = true,
};
static button_data_t btn_rtty_shift = {
    .type  = BTN_TEXT,
    .label = "Shift",
    .press = button_encoder_update_cb,
    .ctrl  = CTRL_RTTY_SHIFT,
    .encoder_allowed = true,
};
static button_data_t btn_rtty_center = {
    .type  = BTN_TEXT,
    .label = "Center",
    .press = button_encoder_update_cb,
    .ctrl  = CTRL_RTTY_CENTER,
    .encoder_allowed = true,
};
static button_data_t btn_rtty_reverse = {
    .type  = BTN_TEXT,
    .label = "Reverse",
    .press = button_encoder_update_cb,
    .ctrl  = CTRL_RTTY_REVERSE,
    .encoder_allowed = true,
};


/* VOL pages */
static button_data_t btn_vol_p1 = make_page_btn("(VOL 1:2)");
static button_data_t btn_vol_p2 = make_page_btn("(VOL 2:2)");

buttons_page_t buttons_page_vol_1 = {
    {&btn_vol_p1, &btn_vol, &btn_sql, &btn_rfg, &btn_tx_pwr}
};
static buttons_page_t page_vol_2 = {
    {&btn_vol_p2, &btn_moni_lvl}
};

/* MFK pages */
static button_data_t btn_mfk_p1 = make_page_btn("(MFK 1:3)");
static button_data_t btn_mfk_p2 = make_page_btn("(MFK 2:3)");
static button_data_t btn_mfk_p3 = make_page_btn("(MFK 3:3)");

static buttons_page_t page_mfk_1 = {
    {&btn_mfk_p1, &btn_rit, &btn_xit, &btn_zoom, &btn_wf_ref}
};
static buttons_page_t page_mfk_2 = {
    {&btn_mfk_p2, &btn_agc, &btn_comp, &btn_if_shift, &btn_vox_on}
};
static buttons_page_t page_mfk_3 = {
    {&btn_mfk_p3, &btn_wf_mode, &btn_span_wider, &btn_span_narrower, &btn_screen_rec}
};

/* MEM pages */

static button_data_t btn_mem_p1 = make_page_btn("(MEM 1:2)");
static button_data_t btn_mem_p2 = make_page_btn("(MEM 2:2)");

static buttons_page_t page_mem_1 = {
    {&btn_mem_p1, &btn_mem_1, &btn_mem_2, &btn_mem_3, &btn_mem_4}
};
static buttons_page_t page_mem_2 = {
    {&btn_mem_p2, &btn_mem_5, &btn_mem_6, &btn_mem_7, &btn_mem_8}
};

/* KEY pages */
static button_data_t btn_key_p1 = make_page_btn("(KEY 1:2)");
static button_data_t btn_key_p2 = make_page_btn("(KEY 2:2)");
static button_data_t btn_cw_p1  = make_page_btn("(CW 1:2)");
static button_data_t btn_cw_p2  = make_page_btn("(CW 2:2)");

static buttons_page_t page_key_1 = {
    {&btn_key_p1, &btn_key_speed, &btn_key_volume, &btn_key_train, &btn_key_tone}
};
static buttons_page_t page_key_2 = {
    {&btn_key_p2, &btn_key_mode, &btn_key_iambic_mode, &btn_key_qsk_time, &btn_key_ratio}
};
static void cw_log_cb(button_data_t *data) {
    cw_field_log_open();
}

static button_data_t btn_cw_log = {.type = BTN_TEXT, .label = "Log\nQSO", .press = cw_log_cb};

static buttons_page_t page_cw_decoder_1 = {
    {&btn_cw_p1, &btn_cw_zap, &btn_cw_peak_on, &btn_cw_peak_q, &btn_cw_log}
};
static const char *cw_parse_label() {
    return cfg_sm.p_cw_parse.get() ? "Parse:\nOn" : "Parse:\nOff";
}

static void cw_parse_cb(button_data_t *data) {
    cfg_sm.p_cw_parse.set(!cfg_sm.p_cw_parse.get());
}

static button_data_t btn_cw_parse = {
    .type = BTN_TEXT_FN, .label_fn = cw_parse_label, .press = cw_parse_cb, .subj = (Subject *)&cfg_sm.p_cw_parse};

static buttons_page_t page_cw_decoder_2 = {
    {&btn_cw_p2, &btn_cw_decoder, &btn_cw_tuner, &btn_cw_snr, &btn_cw_parse}
};

/* DFN pages */
static button_data_t btn_dfn_p1 = make_page_btn("(DFN 1:3)");
static button_data_t btn_dfn_p2 = make_page_btn("(DFN 2:3)");
static button_data_t btn_dfn_p3 = make_page_btn("(DFN 3:3)");

static buttons_page_t page_dfn_1 = {
    {&btn_dfn_p1, &btn_dnf, &btn_dnf_center, &btn_dnf_width, &btn_dnf_auto}
};
static buttons_page_t page_dfn_2 = {
    {&btn_dfn_p2, &btn_nb, &btn_nb_level, &btn_nb_width}
};
static buttons_page_t page_dfn_3 = {
    {&btn_dfn_p3, &btn_nr, &btn_nr_level}
};

/* DFL pages */
static buttons_page_t page_dfl_1 = {
    {&btn_flt_low, &btn_flt_high, &btn_flt_bw}
};

/* App pages */
static button_data_t btn_app_p1 = make_page_btn("(APP 1:3)");
static button_data_t btn_app_p2 = make_page_btn("(APP 2:3)");
static button_data_t btn_app_p3 = make_page_btn("(APP 3:3)");

/* The digital mode apps are in the Modes menu. */
static buttons_page_t page_app_1 = {
    {&btn_app_p1, &btn_modes, &btn_cluster, &btn_swr, &btn_rec}
};
static buttons_page_t page_app_2 = {
    {&btn_app_p2, &btn_settings, &btn_cwt, &btn_field, &btn_birdie}
};
static buttons_page_t page_app_3 = {
    {&btn_app_p3, &btn_bands, &btn_gps, &btn_screen_rec}   /* the MFK 3:3 one too */
};

/* RTTY */

buttons_page_t buttons_page_rtty = {
    {&btn_rtty_p1, &btn_rtty_rate, &btn_rtty_shift, &btn_rtty_center, &btn_rtty_reverse}
};
buttons_page_t buttons_page_rtty_2 = {
    {&btn_rtty_p2, &btn_dig_pwr}
};

buttons_group_t buttons_group_gen = {
    &buttons_page_vol_1,
    &page_vol_2,
    &page_mfk_1,
    &page_mfk_2,
    &page_mfk_3,
};

buttons_group_t buttons_group_app = {
    &page_app_1,
    &page_app_2,
    &page_app_3,
};

buttons_group_t buttons_group_key = {
    &page_key_1,
    &page_key_2,
    &page_cw_decoder_1,
    &page_cw_decoder_2,
};

buttons_group_t buttons_group_dfn = {
    &page_dfn_1,
    &page_dfn_2,
    &page_dfn_3,
};

buttons_group_t buttons_group_dfl = {
    &page_dfl_1,
};

buttons_group_t buttons_group_vm = {
    &page_mem_1,
    &page_mem_2,
};

static struct {
    buttons_page_t **group;
    size_t           size;
} groups[] = {
    {buttons_group_gen, ARRAY_SIZE(buttons_group_gen)},
    {buttons_group_app, ARRAY_SIZE(buttons_group_app)},
    {buttons_group_key, ARRAY_SIZE(buttons_group_key)},
    {buttons_group_dfn, ARRAY_SIZE(buttons_group_dfn)},
    {buttons_group_dfl, ARRAY_SIZE(buttons_group_dfl)},
    {buttons_group_vm,  ARRAY_SIZE(buttons_group_vm) },
};

void buttons_init(lv_obj_t *parent) {

    if (x6100_control_get_base_ver().rev < 3) {
        // Hide DNF auto button
        page_dfn_1.items[4] = NULL;
    }

    /* Fill prev/next pointers */
    for (size_t i = 0; i < ARRAY_SIZE(groups); i++) {
        buttons_page_t **group = groups[i].group;
        for (size_t j = 0; j < groups[i].size; j++) {
            if (group[j]->items[0]->press == button_next_page_cb) {
                uint16_t next_id = (j + 1) % groups[i].size;
                group[j]->items[0]->next = group[next_id];
            } else {
                LV_LOG_USER("First button in page=%u, group=%u press cb is not next", j, i);
            }
            if (group[j]->items[0]->hold == button_prev_page_cb) {
                uint16_t prev_id = (groups[i].size + j - 1) % groups[i].size;
                group[j]->items[0]->prev = group[prev_id];
            } else {
                LV_LOG_USER("First button in page=%u, group=%u hold cb is not prev", j, i);
            }
        }
    }

    /* Update default binds */
    binds.fill(ENCODER_BIND_MFK);
    for (cfg_ctrl_t ctrl : kEncoderVolDefaults) {
        binds[ctrl] = ENCODER_BIND_VOL;
    }

    uint16_t y = 480 - BTN_HEIGHT;
    uint16_t x = 0;

    for (uint8_t i = 0; i < 5; i++) {
        lv_obj_t *f = lv_obj_create(parent);
        disp_btns[i].parent = f;
        lv_obj_add_flag(f, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

        lv_obj_remove_style_all(f);
        lv_obj_add_style(f, &btn_style, 0);
        lv_obj_add_style(f, &btn_active_style, LV_STATE_CHECKED);
        lv_obj_add_style(f, &btn_disabled_style, LV_STATE_DISABLED);
        // lv_obj_add_style(f, &btn_mark_assigned_style, STATE_ASSIGNED);

        /* Drawn buttons: 1 px apart on every side, more when the theme says (btn_gap) */
        lv_obj_set_pos(f, x + BTN_INSET, y + BTN_INSET);
        lv_obj_set_size(f, BTN_WIDTH - 2 * BTN_INSET, BTN_HEIGHT - 2 * BTN_INSET);
        x += BTN_WIDTH;

        /* Encoder marks */
        lv_obj_t *enc_mark;

        /* Encoder marks: a bar on the left edge, green VOL, blue MFK, or the theme's LED */
        lv_coord_t mark_x = theme_shape.mark[1] ? theme_shape.mark[0] : 0;
        lv_coord_t mark_y = theme_shape.mark[1] ? (BTN_HEIGHT - 2 - theme_shape.mark[2]) / 2 : theme_shape.deck ? 0 : 10;

        enc_mark = lv_obj_create(f);
        lv_obj_set_pos(enc_mark, mark_x, mark_y);
        lv_obj_clear_flag(enc_mark, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_style(enc_mark, &btn_mark_style, 0);
        lv_obj_add_style(enc_mark, &btn_mark_assigned_style, STATE_ASSIGNED);
        lv_obj_add_style(enc_mark, &btn_mark_active_style, STATE_ACTIVE);

        disp_btns[i].vol_mark = enc_mark;


        enc_mark = lv_obj_create(f);
        lv_obj_set_pos(enc_mark, mark_x, mark_y);
        lv_obj_set_style_bg_color(enc_mark, TC(info), 0);
        lv_obj_clear_flag(enc_mark, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_add_style(enc_mark, &btn_mark_style, 0);
        lv_obj_add_style(enc_mark, &btn_mark_assigned_style, STATE_ASSIGNED);
        lv_obj_add_style(enc_mark, &btn_mark_active_style, STATE_ACTIVE);
        disp_btns[i].mfk_mark = enc_mark;

        /* Label */
        lv_obj_t *label = lv_label_create(f);

        lv_obj_center(label);
        lv_obj_set_user_data(f, label);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

        lv_obj_t *value = lv_label_create(f);
        lv_obj_set_style_text_font(value, &sony_32, 0);
        lv_obj_set_style_text_color(value, TC(btn_text), 0);
        lv_obj_align(value, LV_ALIGN_BOTTOM_MID, 0, -5);
        lv_obj_add_flag(value, LV_OBJ_FLAG_HIDDEN);

        disp_btns[i].label = label;
        disp_btns[i].value = value;
    }

    cfg_sm.p_encoder_bind.subscribe_delayed_and_notify(encoder_binds_change_cb, NULL);
}

static uint32_t land_index[BUTTONS];   /* their place among the main screen's children */

void buttons_vertical(lv_obj_t *parent, lv_coord_t w, lv_coord_t h) {
    lv_coord_t bw = w - 2 * BTN_INSET, bh = h - 2 * BTN_INSET;

    for (uint8_t i = 0; i < BUTTONS; i++) {
        lv_obj_t           *f = disp_btns[i].parent;
        const lv_img_dsc_t *img;

        land_index[i] = lv_obj_get_index(f);
        lv_obj_set_parent(f, parent);
        lv_obj_set_pos(f, BTN_INSET, i * h + BTN_INSET);
        lv_obj_set_size(f, bw, bh);
        /* the state images at this size too (the normal one is set with the text) */
        if ((img = theme_image(TI_btn_active, bw, bh))) lv_obj_set_style_bg_img_src(f, img, LV_STATE_CHECKED);
        if ((img = theme_image(TI_btn_off, bw, bh))) lv_obj_set_style_bg_img_src(f, img, LV_STATE_DISABLED);
        if (disp_btns[i].data) disp_btn_refresh(&disp_btns[i]);
    }
}

void buttons_horizontal(lv_obj_t *parent) {
    for (uint8_t i = 0; i < BUTTONS; i++) {
        lv_obj_t *f = disp_btns[i].parent;

        lv_obj_set_parent(f, parent);
        lv_obj_move_to_index(f, land_index[i]);
        lv_obj_set_pos(f, i * BTN_WIDTH + BTN_INSET, 480 - BTN_HEIGHT + BTN_INSET);
        lv_obj_set_size(f, BTN_WIDTH - 2 * BTN_INSET, BTN_HEIGHT - 2 * BTN_INSET);
        lv_obj_remove_local_style_prop(f, LV_STYLE_BG_IMG_SRC, LV_STATE_CHECKED);
        lv_obj_remove_local_style_prop(f, LV_STYLE_BG_IMG_SRC, LV_STATE_DISABLED);
        if (disp_btns[i].data) disp_btn_refresh(&disp_btns[i]);
    }
}

void buttons_refresh(button_data_t *data) {
    if (data->disp_btn) {
        disp_btn_refresh(data->disp_btn);
    } else {
        LV_LOG_WARN("Button data label obj is null");
    }
}

void buttons_mark(button_data_t *data, bool val) {
    if (!data) {
        LV_LOG_INFO("Button data is null, skip mark");
        return;
    }
    data->mark = val;
    if (data->disp_btn) {
        disp_btn_refresh(data->disp_btn);
    }
}

void buttons_disabled(button_data_t *data, bool val) {
    data->disabled = val;
    if (data->disp_btn) {
        disp_btn_refresh(data->disp_btn);
    }
}

void buttons_load(uint8_t n, button_data_t *data) {
    button_data_t *prev_data = disp_btns[n].data;
    disp_btn_clear(disp_btns + n);

    lv_obj_t *label = disp_btns[n].label;
    disp_btns[n].data = data;
    if (data) {
        data->disp_btn = disp_btns + n;
        disp_btn_refresh(data->disp_btn);
    } else {
        set_btn_text(disp_btns + n, "");
    }
}

void buttons_load_page(buttons_page_t *page) {
    if (!page) {
        LV_LOG_ERROR("NULL pointer to buttons page");
        return;
    }
    if (cur_page) {
        buttons_unload_page();
    }
    cur_page = page;
    for (uint8_t i = 0; i < BUTTONS; i++) {
        buttons_load(i, page->items[i]);
    }
}

void buttons_unload_page() {
    cur_page = NULL;
    for (uint8_t i = 0; i < BUTTONS; i++) {
        disp_btn_clear(&disp_btns[i]);
    }
}

void button_next_page_cb(button_data_t *data) {
    buttons_unload_page();
    buttons_load_page(data->next);
}

void button_prev_page_cb(button_data_t *data) {
    buttons_unload_page();
    buttons_load_page(data->prev);
}

static void button_app_page_cb(button_data_t *data) {
    main_screen_start_app((press_action_t)data->ctrl);
}

static void button_agc_cb(button_data_t *data) {
    main_screen_agc_next();
}


static void button_encoder_update_cb(button_data_t *data) {
    // set corresponding encoder
    // if already bind - use corresponding encoder. If no - you default
    cfg_ctrl_t ctrl = (cfg_ctrl_t)data->ctrl;
    std::string binds_str = cfg_sm.p_encoder_bind.get();

    void (*set_fn)(cfg_ctrl_t) = NULL;
    if (binds_str.length() > ctrl){
        switch (binds[ctrl]) {
            case ENCODER_BIND_VOL:
                set_fn = vol_set_ctrl;
                break;
            case ENCODER_BIND_MFK:
                set_fn = mfk_set_ctrl;
                break;
        default: ;
            if (std::find(kEncoderVolDefaults.begin(), kEncoderVolDefaults.end(), ctrl)
                != kEncoderVolDefaults.end()) {
                set_fn = vol_set_ctrl;
            } else if (std::find(kEncoderMfkDefaults.begin(), kEncoderMfkDefaults.end(), ctrl)
                       != kEncoderMfkDefaults.end()) {
                set_fn = mfk_set_ctrl;
            }
            break;
        }
    } else {
        set_fn = mfk_set_ctrl;
    }
    if (walkie_on()) set_fn = mfk_set_ctrl;    /* the VOL knob stays the volume */
    if (set_fn) {
        set_fn(ctrl);
    }
}

static void button_encoder_hold_update_cb(button_data_t *data) {
    cfg_ctrl_t ctrl = (cfg_ctrl_t)data->ctrl;
    std::string binds = cfg_sm.p_encoder_bind.get();
    if (binds.length() > ctrl){
        switch (binds[ctrl]) {
            case ENCODER_BIND_NONE:
                msg_update_text_fmt("Added to VOL encoder");
                binds[ctrl] = ENCODER_BIND_VOL;
                break;
            case ENCODER_BIND_VOL:
                msg_update_text_fmt("Moved to MFK encoder");
                binds[ctrl] = ENCODER_BIND_MFK;
                break;
            case ENCODER_BIND_MFK:
                msg_update_text_fmt("Removed from MFK encoder");
                binds[ctrl] = ENCODER_BIND_NONE;
                break;
            default:
                binds[ctrl] = ENCODER_BIND_NONE;
                LV_LOG_WARN("Unexpected mode: %c for ctrl %d", binds[ctrl], ctrl);
                break;
        }
        cfg_sm.p_encoder_bind.set(binds);
    } else {
        LV_LOG_ERROR("Binds is too short (%d) for ctrl %c", binds.length(), binds.c_str());
    }
}

static void button_mem_load_cb(button_data_t *data) {
    mem_load(data->ctrl);
}

static void button_mem_save_cb(button_data_t *data) {
    mem_save(data->ctrl);
}

void buttons_press(uint8_t n, bool hold) {
    button_data_t *btn_data = disp_btns[n].data;
    if (btn_data == NULL) {
        LV_LOG_WARN("Button %u is NULL", n);
        return;
    }
    if (btn_data->disabled) {
        LV_LOG_USER("Button %s disabled", lv_label_get_text(btn_data->disp_btn->label));
        return;
    }
    if (hold) {
        if (btn_data->hold) {
            btn_data->hold(btn_data);
        } else {
            LV_LOG_USER("Button %u hold action is NULL", n);
        }
    } else {
        if (btn_data->press) {
            btn_data->press(btn_data);
        } else {
            LV_LOG_USER("Button %u press action is NULL", n);
        }
    }
}

void buttons_load_page_group(buttons_group_t group) {
    size_t group_size = 0;
    for (size_t i = 0; i < ARRAY_SIZE(groups); i++) {
        if (groups[i].group == group) {
            group_size = groups[i].size;
            break;
        }
    }
    if (group_size <= 0) {
        return;
    }
    for (size_t i = 0; i < group_size; i++) {
        if ((group[i] == cur_page) && (cur_page->items[0]->next)) {
            // load next
            cur_page->items[0]->press(cur_page->items[0]);
            return;
        }
    }
    // Load first
    buttons_load_page(group[0]);
}

buttons_page_t *buttons_get_cur_page() {
    return cur_page;
}

static const char * vol_label_getter() {
    static char buf[16];
    sprintf(buf, "Volume:\n%i", cfg_sm.p_volume.get());
    return buf;
}

static const char * sql_label_getter() {
    static char buf[16];
    sprintf(buf, "Squelch:\n%i", cfg_sm.p_squelch.get());
    return buf;
}

static const char * rfg_label_getter() {
    static char buf[16];
    sprintf(buf, "RF gain:\n%i", cfg_sm.p_rfgain.get());
    return buf;
}

static const char * tx_power_label_getter() {
    static char buf[20];
    sprintf(buf, "TX power:\n%0.1f W", cfg_sm.p_pwr.get());
    return buf;
}

static const char * filter_low_label_getter() {
    static char buf[22];
    sprintf(buf, "Filter low:\n%i Hz", cfg_sm.cp_cur_filter_low.get());
    return buf;
}
static const char * filter_high_label_getter() {
    static char buf[22];
    sprintf(buf, "Filter high:\n%i Hz", cfg_sm.cp_cur_filter_high.get());
    return buf;
}

static const char * filter_bw_label_getter() {
    static char buf[22];
    sprintf(buf, "Filter BW:\n%i Hz", cfg_sm.cp_cur_filter_bw.get());
    return buf;
}



static const char * moni_level_label_getter() {
    static char buf[22];
    sprintf(buf, "Moni level:\n%i", cfg_sm.p_moni.get());
    return buf;
}


static const char * rit_label_getter() {
    static char buf[22];
    sprintf(buf, "RIT:\n%+i", cfg_sm.p_rit.get());
    return buf;
}

static const char * xit_label_getter() {
    static char buf[22];
    sprintf(buf, "XIT:\n%+i", cfg_sm.p_xit.get());
    return buf;
}

static const char * agc_label_getter() {
    static const char *names[] = {"Off", "Slow", "Fast", "Auto"};
    static char buf[22];
    int32_t agc = cfg_sm.cp_cur_agc.get();
    sprintf(buf, "AGC:\n%s", (agc >= 0 && agc <= x6100_agc_auto) ? names[agc] : "?");
    return buf;
}

static const char * comp_label_getter() {
    static char buf[22];
    sprintf(buf, "Comp:\n%s", params_comp_str_get(cfg_sm.p_comp.get()));
    return buf;
}

static const char * if_shift_label_getter() {
    static char buf[22];
    sprintf(buf, "IF shift:\n%i", cfg_sm.p_band_if_shift.get());
    return buf;
}

static const char * wf_ref_label_getter() {
    static char buf[22];
    sprintf(buf, "WF ref:\n%+i dB", cfg_sm.p_waterfall_ref.get());
    return buf;
}

static const char * screen_rec_label_getter() {
    return screen_rec_is_on() ? "Stop\nrec" : "Screen\nrec";
}

void buttons_screen_rec_update() {
    if (btn_screen_rec.disp_btn) {
        buttons_refresh(&btn_screen_rec);
    }
}

static const char * wf_mode_label_getter() {
    return cfg_sm.p_wf_mode.get() ? "WF\nFixed" : "WF\nCenter";
}

/* Label shows the span the button selects */
static const char * span_label(int32_t zoom) {
    static char buf[2][24];
    static int  n;

    n ^= 1;
    snprintf(buf[n], sizeof buf[n], "Span %s\n%g kHz", zoom < cfg_sm.p_mode_zoom.get() ? "+" : "-", 100.0 / zoom);
    return buf[n];
}

static const char * span_wider_label_getter() {
    int32_t z = cfg_sm.p_mode_zoom.get();
    return z > 1 ? span_label(z >> 1) : "Span +\nmax";
}

static const char * span_narrower_label_getter() {
    int32_t z = cfg_sm.p_mode_zoom.get();
    return z < 8 ? span_label(z << 1) : "Span -\nmin";
}

const char *vox_on_label_getter() {
    static char buf[22];
    sprintf(buf, "VOX:\n%s", cfg_sm.p_vox_en.get() ? "On": "Off");
    return buf;
}

static const char * key_speed_label_getter() {
    static char buf[22];
    sprintf(buf, "Speed:\n%i wpm", cfg_sm.p_key_speed.get());
    return buf;
}

static const char * key_volume_label_getter() {
    static char buf[22];
    sprintf(buf, "Volume:\n%i", cfg_sm.p_key_vol.get());
    return buf;
}

static const char * key_train_label_getter() {
    static char buf[22];
    sprintf(buf, "Train:\n%s", cfg_sm.p_key_train.get() ? "On": "Off");
    return buf;
}

static const char * key_tone_label_getter() {
    static char buf[22];
    sprintf(buf, "Tone:\n%i Hz", cfg_sm.p_key_tone.get());
    return buf;
}

static const char * key_mode_label_getter() {
    static char buf[22];
    sprintf(buf, "Mode:\n%s", params_key_mode_str_get((x6100_key_mode_t)cfg_sm.p_key_mode.get()));
    return buf;
}

static const char * iambic_mode_label_getter() {
    static char buf[22];
    sprintf(buf, "Iambic:\n%s mode", params_iambic_mode_str_ger((x6100_iambic_mode_t)cfg_sm.p_iambic_mode.get()));
    return buf;
}

static const char * qsk_time_label_getter() {
    static char buf[22];
    sprintf(buf, "QSK time:\n%i ms", cfg_sm.p_qsk_time.get());
    return buf;
}

static const char * key_ratio_label_getter() {
    static char buf[22];
    sprintf(buf, "Ratio:\n%0.1f", cfg_sm.p_key_ratio.get());
    return buf;
}

static const char * cw_decoder_label_getter() {
    static char buf[22];
    sprintf(buf, "Decoder:\n%s", cfg_sm.p_cw_decoder.get() ? "On": "Off");
    return buf;
}

static const char * cw_tuner_label_getter() {
    static char buf[22];
    sprintf(buf, "Tuner:\n%s", cfg_sm.p_cw_tune.get() ? "On": "Off");
    return buf;
}

static const char * cw_snr_label_getter() {
    static char buf[22];
    sprintf(buf, "Dec SNR:\n%0.1f dB", cfg_sm.p_cw_decoder_snr.get());
    return buf;
}

static const char * cw_peak_on_label_getter() {
    static char buf[22];
    sprintf(buf, "CW peak:\n%s", cfg_sm.p_cw_peak_on.get() ? "On": "Off");
    return buf;
}

static const char * cw_peak_q_label_getter() {
    static char buf[22];
    sprintf(buf, "CW peak Q:\n%i", cfg_sm.p_cw_peak_q.get());
    return buf;
}

static const char * dnf_label_getter() {
    static char buf[22];
    sprintf(buf, "DNF:\n%s", cfg_sm.p_dnf.get() ? "On": "Off");
    return buf;
}

static const char * dnf_center_label_getter() {
    static char buf[22];
    sprintf(buf, "DNF freq:\n%i Hz", cfg_sm.p_dnf_center.get());
    return buf;
}

static const char * dnf_width_label_getter() {
    static char buf[22];
    sprintf(buf, "DNF width:\n%i Hz", cfg_sm.p_dnf_width.get());
    return buf;
}

static const char * dnf_auto_label_getter() {
    static char buf[22];
    sprintf(buf, "DNF auto:\n%s", cfg_sm.p_dnf_auto.get() ? "On": "Off");
    return buf;
}

static const char * nb_label_getter() {
    static char buf[22];
    sprintf(buf, "NB:\n%s", cfg_sm.p_nb.get() ? "On": "Off");
    return buf;
}

static const char * nb_level_label_getter() {
    static char buf[22];
    sprintf(buf, "NB level:\n%i", cfg_sm.p_nb_level.get());
    return buf;
}

static const char * nb_width_label_getter() {
    static char buf[22];
    sprintf(buf, "NB width:\n%i Hz", cfg_sm.p_nb_width.get());
    return buf;
}

static const char * nr_label_getter() {
    static char buf[22];
    sprintf(buf, "NR:\n%s", cfg_sm.p_nr.get() ? "On": "Off");
    return buf;
}

static const char * nr_level_label_getter() {
    static char buf[22];
    sprintf(buf, "NR level:\n%i", cfg_sm.p_nr_level.get());
    return buf;
}


static void encoder_binds_change_cb(Subject *subj, void *user_data) {
    std::string binds = cfg_sm.p_encoder_bind.get();

    for (size_t i = 0; i < binds.length(); i++) {
        fast_binds[i] = binds[i];
    }

    for (size_t i = 0; i < BUTTONS; i++)
    {
        disp_btn_t *b = disp_btns + i;
        disp_btn_refresh(b);
    }
}

/* "Name:\nvalue" shows a small name over a large value; "(PAGE 1:2)" is a
 * page button, drawn darker; anything else is an action, centred. */
/* VISION: the biggest size the label fits in, on one line or two ("Start\nAuto RX") */
/* The button's size as set (the walkie screen's are narrower and taller) */
static lv_coord_t btn_w(disp_btn_t *b) {
    return lv_obj_get_style_width(b->parent, 0);
}

static lv_coord_t btn_h(disp_btn_t *b) {
    return lv_obj_get_style_height(b->parent, 0);
}

static bool btn_narrow(disp_btn_t *b) {
    return btn_w(b) < BTN_WIDTH - 2 * BTN_INSET;
}

static const lv_font_t *vision_label_font(disp_btn_t *b, const char *text) {
    static const lv_font_t *const sizes[] = {&sony_28, &sony_24, &sony_20, &sony_16, &sony_14};
    lv_coord_t                    w = btn_w(b) - 8, h = btn_h(b);
    lv_point_t                    size;

    for (const lv_font_t *f : sizes) {
        lv_txt_get_size(&size, text, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (size.x <= w && size.y <= h) return f;
    }
    return &sony_14;
}

/* The value in a narrow button (walkie): the biggest size that fits its width ("3200 Hz") */
static const lv_font_t *narrow_value_font(disp_btn_t *b, const char *text) {
    static const lv_font_t *const sizes[] = {&sony_32, &sony_28, &sony_24, &sony_20};
    lv_point_t                    size;

    for (const lv_font_t *f : sizes) {
        lv_txt_get_size(&size, text, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (size.x <= btn_w(b) - 8) return f;
    }
    return &sony_18;
}

static void set_btn_text(disp_btn_t *b, const char *text) {
    const char *nl = strchr(text, '\n');

    if (nl && nl > text && nl[-1] == ':') {
        char name[32];
        snprintf(name, sizeof(name), "%.*s", (int)(nl - text - 1), text);
        /* Deck: "Volume 30" on one line, the name grey, the value white */
        if (theme_shape.deck && !btn_narrow(b)) {
            /* The biggest that fits on one line */
            static const lv_font_t *const fonts[] = {&sony_30, &sony_28, &sony_26, &sony_24, &sony_20};
            const lv_font_t *f;
            lv_point_t       ns, vs;

            for (size_t i = 0; i < sizeof(fonts) / sizeof(fonts[0]); i++) {
                f = fonts[i];
                lv_txt_get_size(&ns, name, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
                lv_txt_get_size(&vs, nl + 1, f, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
                if (ns.x + 6 + vs.x <= btn_w(b) - 8) break;
            }
            lv_label_set_text(b->label, name);
            lv_obj_set_style_text_font(b->label, f, 0);
            lv_obj_set_style_text_color(b->label, TC(text_dim), 0);
            lv_obj_align(b->label, LV_ALIGN_CENTER, -(vs.x + 6) / 2, 0);
            lv_label_set_text(b->value, nl + 1);
            lv_obj_set_style_text_font(b->value, f, 0);
            lv_obj_align(b->value, LV_ALIGN_CENTER, (ns.x + 6) / 2, 0);
            lv_obj_clear_flag(b->value, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(b->parent, TC(btn), 0);
            return;
        }
        /* Name over value; one size smaller when the two lines don't fit (taller fonts, btn_gap) */
        bool fit = lv_font_get_line_height(&sony_22) + lv_font_get_line_height(&sony_32) + 9
                   <= btn_h(b);
        lv_label_set_text(b->label, name);
        lv_obj_set_style_text_font(b->label, theme_shape.vision || btn_narrow(b) ? &sony_18 : fit ? &sony_22 : &sony_20, 0);
        lv_obj_set_style_text_color(b->label, TC(text_head), 0);
        lv_obj_align(b->label, LV_ALIGN_TOP_MID, 0, theme_shape.vision ? 0 : fit ? 4 : 2);
        /* VISION: the value as big as the button takes, the name over it (capitals and digits: no overlap) */
        lv_obj_set_style_text_font(b->value, btn_narrow(b)        ? narrow_value_font(b, nl + 1)
                                             : theme_shape.vision ? &sony_30
                                             : fit                ? &sony_32 : &sony_28, 0);
        lv_obj_align(b->value, LV_ALIGN_BOTTOM_MID, 0, theme_shape.vision ? 0 : fit ? -5 : -2);
        lv_label_set_text(b->value, nl + 1);
        lv_obj_clear_flag(b->value, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(b->parent, TC(btn), 0);
        return;
    }

    bool page = text[0] == '(';
    if (page) {
        char   t[32];
        size_t n = 0;
        for (const char *c = text; *c && n < sizeof(t) - 1; c++)
            if (*c != '(' && *c != ')') t[n++] = *c;
        t[n] = '\0';
        lv_label_set_text(b->label, t);
    } else {
        lv_label_set_text(b->label, text);
    }
    lv_obj_set_style_text_font(b->label, theme_shape.vision || btn_narrow(b) ? vision_label_font(b, lv_label_get_text(b->label))
                                                                              : theme_shape.deck ? &sony_30 : page ? &sony_28 : &sony_30, 0);
    if (page) lv_obj_set_style_text_color(b->label, TC(btn_page_text), 0);
    else lv_obj_remove_local_style_prop(b->label, LV_STYLE_TEXT_COLOR, 0);   /* the button's: btn_text, btn_active_text, btn_off_text */
    lv_obj_center(b->label);
    lv_obj_add_flag(b->value, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(b->parent, page ? TC(btn_page) : TC(btn), 0);
    /* Themes with images: the page button's own, else the button's */
    const lv_img_dsc_t *img = page ? theme_image(TI_btn_page, btn_w(b), btn_h(b)) : NULL;
    if (!img) img = theme_image(TI_btn, btn_w(b), btn_h(b));
    if (img) lv_obj_set_style_bg_img_src(b->parent, img, 0);
}

static void label_update_cb(Subject *subj, void *user_data) {
    button_data_t *btn_data = (button_data_t*)user_data;
    if (btn_data->disp_btn) {
        set_btn_text(btn_data->disp_btn, btn_data->label_fn());
    } else {
        LV_LOG_WARN("Can't update label: it's NULL");
    }
}


static void disp_btn_refresh(disp_btn_t *b) {
    button_data_t *btn_data = b->data;
    if (!btn_data) {
        LV_LOG_WARN("Button has no assigned data");
        return;
    }

    /* Label */

    if (btn_data->type == BTN_TEXT) {
        set_btn_text(b, btn_data->label);
    } else if (btn_data->type == BTN_TEXT_FN) {
        set_btn_text(b, btn_data->label_fn());
        if (btn_data->subj) {
            btn_data->observer = btn_data->subj->subscribe_delayed(label_update_cb, btn_data);
        } else {
            lv_obj_set_user_data(b->label, (void *)btn_data->label_fn);
        }
    } else {
        set_btn_text(b, "");
    }

    /* Marked button */

    if (btn_data->mark) {
        lv_obj_add_state(b->parent, LV_STATE_CHECKED);

    } else {
        lv_obj_clear_state(b->parent, LV_STATE_CHECKED);
    }

    /* Disabled button */

    if (btn_data->disabled) {
        lv_obj_add_state(b->parent, LV_STATE_DISABLED);

    } else {
        lv_obj_clear_state(b->parent, LV_STATE_DISABLED);
    }

    marks_refresh(b);
}

/* Encoder marks: which encoder the button's control is on, red (active) when
 * the encoder is on it now */
static void marks_refresh(disp_btn_t *b) {
    button_data_t *btn_data = b->data;

    if (btn_data && (btn_data->ctrl >= 0) && btn_data->encoder_allowed) {
        cfg_ctrl_t ctrl = (cfg_ctrl_t)btn_data->ctrl;
        encoder_binds_t encoder      = ENCODER_BIND_NONE,
                        fast_encoder = ENCODER_BIND_NONE;

        if (ctrl < std::size(fast_binds)) {
            fast_encoder = (encoder_binds_t)fast_binds[ctrl];
        }
        if (fast_encoder != ENCODER_BIND_NONE) {
            encoder = fast_encoder;
            lv_obj_add_state(b->vol_mark, STATE_ASSIGNED);
            lv_obj_add_state(b->mfk_mark, STATE_ASSIGNED);
        } else {
            lv_obj_clear_state(b->vol_mark, STATE_ASSIGNED);
            lv_obj_clear_state(b->mfk_mark, STATE_ASSIGNED);
            if ((ctrl < std::size(binds)) && (btn_data->press == button_encoder_update_cb)) {
                encoder = (encoder_binds_t)binds[ctrl];
            }
        }
        lv_obj_t *active_encoder_marker = NULL;
        bool      active = (encoder == ENCODER_BIND_VOL && ctrl == vol_get_ctrl()) ||
                           (encoder == ENCODER_BIND_MFK && ctrl == mfk_ctrl);
        for (lv_obj_t *m : {b->vol_mark, b->mfk_mark}) {
            if (active) lv_obj_add_state(m, STATE_ACTIVE);
            else lv_obj_clear_state(m, STATE_ACTIVE);
        }
        switch (encoder) {
            case ENCODER_BIND_VOL:
                lv_obj_clear_flag(b->vol_mark, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(b->mfk_mark, LV_OBJ_FLAG_HIDDEN);
                break;
            case ENCODER_BIND_MFK:
                lv_obj_add_flag(b->vol_mark, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_flag(b->mfk_mark, LV_OBJ_FLAG_HIDDEN);
                break;
            default:
                lv_obj_add_flag(b->vol_mark, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(b->mfk_mark, LV_OBJ_FLAG_HIDDEN);
                lv_obj_clear_state(b->vol_mark, STATE_ASSIGNED);
                lv_obj_clear_state(b->mfk_mark, STATE_ASSIGNED);
                break;
        }
    } else {
        lv_obj_add_flag(b->vol_mark, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(b->mfk_mark, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_state(b->vol_mark, STATE_ASSIGNED);
        lv_obj_clear_state(b->mfk_mark, STATE_ASSIGNED);
    }
}


/* An encoder moved to another control: its LED goes to that button */
void buttons_refresh_marks() {
    for (size_t i = 0; i < BUTTONS; i++) {
        if (disp_btns[i].parent) marks_refresh(&disp_btns[i]);   /* not before buttons_init() */
    }
}

static void disp_btn_clear(disp_btn_t *btn) {
    lv_obj_t *label = btn->label;

    set_btn_text(btn, "");
    lv_obj_set_user_data(label, NULL);
    lv_obj_clear_state(btn->parent, LV_STATE_CHECKED);
    lv_obj_clear_state(btn->parent, LV_STATE_DISABLED);

    lv_obj_clear_state(btn->vol_mark, STATE_ASSIGNED);
    lv_obj_clear_state(btn->mfk_mark, STATE_ASSIGNED);
    lv_obj_add_flag(btn->vol_mark, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(btn->mfk_mark, LV_OBJ_FLAG_HIDDEN);

    if (btn->data) {
        // Unbind data from display button
        btn->data->disp_btn = NULL;
        if (btn->data->observer) {
            btn->data->observer->unsubscribe();
            delete btn->data->observer;
            btn->data->observer = NULL;
        }
        btn->data = NULL;
    }
}
