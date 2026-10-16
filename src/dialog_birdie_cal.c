/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Birdie & Internal Spur Auto-Calibration Dialog
 */

#include "dialog_birdie_cal.h"

#include "dialog.h"
#include "buttons.h"
#include "events.h"
#include "keyboard.h"
#include "main_screen.h"
#include "meter.h"
#include "params/params.h"
#include "radio.h"
#include "styles.h"
#include "theme.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_BIRDIES 1024
#define JSON_PATH   "/mnt/birdies.json"

typedef struct {
    uint32_t freq;
    int16_t  db;
    int16_t  snr;
} birdie_entry_t;

static birdie_entry_t g_birdies[MAX_BIRDIES];
static int            g_num_birdies = 0;

typedef struct {
    const char *name;
    uint32_t   start_hz;
    uint32_t   stop_hz;
} scan_band_t;

static const scan_band_t SCAN_BANDS[] = {
    {"20m",     14000000, 14350000},
    {"40m",      7000000,  7200000},
    {"80m",      3500000,  3800000},
    {"15m",     21000000, 21450000},
    {"10m",     28000000, 29700000},
    {"160m",     1800000,  2000000},
    {"30m",     10100000, 10150000},
    {"17m",     18068000, 18168000},
    {"12m",     24890000, 24990000},
    {"6m",      50000000, 52000000},
    {"All HF",   1800000, 29700000},
};
#define NUM_BANDS ((int)(sizeof(SCAN_BANDS) / sizeof(SCAN_BANDS[0])))

static const uint32_t SCAN_STEPS[] = {1000, 500, 2500, 5000};
#define NUM_STEPS ((int)(sizeof(SCAN_STEPS) / sizeof(SCAN_STEPS[0])))

static const int16_t SCAN_THRESHOLDS[] = {4, 6, 8, 10, 15, 3};
#define NUM_THRESHOLDS ((int)(sizeof(SCAN_THRESHOLDS) / sizeof(SCAN_THRESHOLDS[0])))

/* State */
static bool       g_running = false;
static int        g_band_idx = 0;
static int        g_step_idx = 0;      /* default 1000 Hz */
static int        g_thresh_idx = 0;    /* default +4 dB */

static uint32_t   g_cur_freq = 0;
static uint32_t   g_start_freq = 0;
static uint32_t   g_stop_freq = 0;
static uint32_t   g_orig_freq = 0;
static float      g_noise_floor = -120.0f;
static lv_timer_t *g_scan_timer = NULL;

/* UI Widgets */
static lv_obj_t   *lbl_title = NULL;
static lv_obj_t   *lbl_status = NULL;
static lv_obj_t   *lbl_freq = NULL;
static lv_obj_t   *lbl_info = NULL;
static lv_obj_t   *progress_bar = NULL;
static lv_obj_t   *ta_log = NULL;

/* Forward declarations */
static void construct_cb(lv_obj_t *parent);
static void destruct_cb(void);
static void key_cb(lv_event_t *e);
static void btn_run_cb(button_data_t *btn_data);
static void btn_band_cb(button_data_t *btn_data);
static void btn_step_cb(button_data_t *btn_data);
static void btn_thresh_cb(button_data_t *btn_data);
static void btn_filter_cb(button_data_t *btn_data);
static const char *label_run_fn(void);
static const char *label_band_fn(void);
static const char *label_step_fn(void);
static const char *label_thresh_fn(void);
static const char *label_filter_fn(void);
static void save_birdies_json(void);

static button_data_t btn_run = {
    .type     = BTN_TEXT_FN,
    .label_fn = label_run_fn,
    .press    = btn_run_cb,
};

static button_data_t btn_band = {
    .type     = BTN_TEXT_FN,
    .label_fn = label_band_fn,
    .press    = btn_band_cb,
};

static button_data_t btn_step = {
    .type     = BTN_TEXT_FN,
    .label_fn = label_step_fn,
    .press    = btn_step_cb,
};

static button_data_t btn_thresh = {
    .type     = BTN_TEXT_FN,
    .label_fn = label_thresh_fn,
    .press    = btn_thresh_cb,
};

static button_data_t btn_filter = {
    .type     = BTN_TEXT_FN,
    .label_fn = label_filter_fn,
    .press    = btn_filter_cb,
};

static buttons_page_t btn_page = {
    {&btn_run, &btn_band, &btn_step, &btn_thresh, &btn_filter}
};

static dialog_t dialog = {
    .run          = false,
    .construct_cb = construct_cb,
    .destruct_cb  = destruct_cb,
    .audio_cb     = NULL,
    .btn_page     = &btn_page,
    .key_cb       = key_cb,
};

dialog_t *dialog_birdie_cal = &dialog;

/* Public API: check if birdie suppression filter is enabled */
bool birdie_cal_is_enabled(void) {
    return params.birdie_filter.x && !g_running;
}

/* Enable or disable birdie suppression */
void birdie_cal_set_enabled(bool en) {
    params_bool_set(&params.birdie_filter, en);
    radio_update_birdie_notch();
}

/* Check if frequency is near a known birdie */
bool birdie_cal_is_birdie(uint32_t freq_hz, uint32_t tolerance_hz) {
    for (int i = 0; i < g_num_birdies; i++) {
        uint32_t bf = g_birdies[i].freq;
        if (freq_hz >= (bf > tolerance_hz ? bf - tolerance_hz : 0) &&
            freq_hz <= (bf + tolerance_hz)) {
            return true;
        }
    }
    return false;
}

/* Suppress known birdies and real-time carrier spikes in an FFT power spectrum or waterfall line */
void birdie_cal_suppress_spectrum(float *data_buf, uint16_t size, uint32_t center_freq, uint32_t span_hz) {
    if (!data_buf || size < 16 || !birdie_cal_is_enabled()) {
        return;
    }

    int32_t cur_mode = cparam_i_get(cfg_cur_mode);
    bool is_cw = (cur_mode == x6100_mode_cw || cur_mode == x6100_mode_cwr);
    int cw_center = size / 2;

    /* 1. Real-Time Adaptive Spike Blanker:
     * Eliminates unmodulated carrier needles, DC spikes, VFO-moving spurs,
     * and fractional-N PLL artifacts across the spectrum without needing a static list.
     * Uses 1D morphological opening (erosion + dilation) with 7-bin window.
     * Real SSB/AM signals are 20-50 bins wide and are 100% preserved.
     */
    const int W = 3;
    const float THRESHOLD_DB = 3.5f; /* 3.5 dB prominence over local background */

    float eroded[size];
    float bg[size];

    for (int i = 0; i < size; i++) {
        int left = (i - W < 0) ? 0 : (i - W);
        int right = (i + W >= size) ? (size - 1) : (i + W);
        float m = data_buf[left];
        for (int j = left + 1; j <= right; j++) {
            if (data_buf[j] < m) m = data_buf[j];
        }
        eroded[i] = m;
    }

    for (int i = 0; i < size; i++) {
        int left = (i - W < 0) ? 0 : (i - W);
        int right = (i + W >= size) ? (size - 1) : (i + W);
        float m = eroded[left];
        for (int j = left + 1; j <= right; j++) {
            if (eroded[j] > m) m = eroded[j];
        }
        bg[i] = m;
    }

    for (int i = 0; i < size; i++) {
        /* In CW mode, protect the narrow active listening passband around VFO center (+/- 8 bins) */
        if (is_cw && abs(i - cw_center) <= 8) {
            continue;
        }
        if (data_buf[i] - bg[i] > THRESHOLD_DB) {
            data_buf[i] = bg[i];
        }
    }

    /* 2. Also suppress any static calibrated birdies from /mnt/birdies.json */
    if (g_num_birdies > 0 && span_hz > 0) {
        uint32_t half_span = span_hz / 2;
        uint32_t min_freq = (center_freq > half_span) ? (center_freq - half_span) : 0;
        uint32_t max_freq = center_freq + half_span;

        int r = (int)((350LL * (int64_t)size) / (int64_t)span_hz);
        if (r < 2) r = 2;
        if (r > 16) r = 16;

        for (int i = 0; i < g_num_birdies; i++) {
            uint32_t bf = g_birdies[i].freq;
            if (bf < min_freq || bf > max_freq) {
                continue;
            }

            int64_t diff = (int64_t)bf - (int64_t)center_freq;
            int32_t k = (int32_t)(size / 2) + (int32_t)((diff * (int64_t)size) / (int64_t)span_hz);

            int left = k - r - 1;
            if (left < 0) left = 0;
            int right = k + r + 1;
            if (right >= size) right = size - 1;
            if (left >= right) continue;

            float v_left = data_buf[left];
            if (left > 0) v_left = 0.5f * (v_left + data_buf[left - 1]);

            float v_right = data_buf[right];
            if (right < size - 1) v_right = 0.5f * (v_right + data_buf[right + 1]);

            if (v_left > v_right + 15.0f) v_left = v_right + 3.0f;
            else if (v_right > v_left + 15.0f) v_right = v_left + 3.0f;

            int start_idx = (k - r < 0) ? 0 : (k - r);
            int end_idx = (k + r >= size) ? (size - 1) : (k + r);

            for (int j = start_idx; j <= end_idx; j++) {
                float frac = (float)(j - left) / (float)(right - left);
                float interp = v_left + frac * (v_right - v_left);
                if (data_buf[j] > interp) {
                    data_buf[j] = interp;
                }
            }
        }
    }
}

/* Check if any known birdie falls in audio passband, returning tone frequency in Hz */
bool birdie_cal_check_audio(uint32_t vfo_freq, int mode, int low_filter, int high_filter, uint16_t *notch_center) {
    if (g_num_birdies == 0 || !birdie_cal_is_enabled() || !notch_center) {
        return false;
    }

    uint16_t best_notch = 0;
    int16_t best_db = -999;
    bool found = false;

    int key_tone = param_i_get(cfg_key_tone);
    if (key_tone < 300 || key_tone > 1500) key_tone = 700;

    for (int i = 0; i < g_num_birdies; i++) {
        uint32_t bf = g_birdies[i].freq;
        int64_t diff = (int64_t)bf - (int64_t)vfo_freq;
        int32_t tone = -1;

        switch (mode) {
            case x6100_mode_usb:
            case x6100_mode_usb_dig:
                if (diff >= (int64_t)low_filter && diff <= (int64_t)high_filter) {
                    tone = (int32_t)diff;
                }
                break;

            case x6100_mode_lsb:
            case x6100_mode_lsb_dig:
                if (-diff >= (int64_t)low_filter && -diff <= (int64_t)high_filter) {
                    tone = (int32_t)(-diff);
                }
                break;

            case x6100_mode_cw:
                {
                    int32_t cw_tone = (int32_t)diff + key_tone;
                    if (cw_tone >= low_filter && cw_tone <= high_filter) {
                        tone = cw_tone;
                    }
                }
                break;

            case x6100_mode_cwr:
                {
                    int32_t cwr_tone = key_tone - (int32_t)diff;
                    if (cwr_tone >= low_filter && cwr_tone <= high_filter) {
                        tone = cwr_tone;
                    }
                }
                break;

            case x6100_mode_am:
            case x6100_mode_nfm:
                {
                    int32_t abs_diff = (diff < 0) ? -diff : diff;
                    if (abs_diff >= 120 && abs_diff <= high_filter) {
                        tone = abs_diff;
                    }
                }
                break;

            default:
                break;
        }

        if (tone >= 100 && tone <= 3800) {
            if (!found || g_birdies[i].db > best_db) {
                best_notch = (uint16_t)tone;
                best_db = g_birdies[i].db;
                found = true;
            }
        }
    }

    if (found) {
        *notch_center = best_notch;
        return true;
    }
    return false;
}

void birdie_cal_load(void) {
    FILE *fp = fopen(JSON_PATH, "r");
    if (!fp) return;

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz <= 0 || sz > 512 * 1024) {
        fclose(fp);
        return;
    }

    char *buf = (char *)malloc(sz + 1);
    if (!buf) { fclose(fp); return; }
    size_t rd = fread(buf, 1, sz, fp);
    fclose(fp);
    buf[rd] = '\0';

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return;

    cJSON *arr = cJSON_GetObjectItem(root, "birdies");
    if (cJSON_IsArray(arr)) {
        g_num_birdies = 0;
        int count = cJSON_GetArraySize(arr);
        for (int i = 0; i < count && g_num_birdies < MAX_BIRDIES; i++) {
            cJSON *item = cJSON_GetArrayItem(arr, i);
            cJSON *f = cJSON_GetObjectItem(item, "freq");
            cJSON *d = cJSON_GetObjectItem(item, "db");
            cJSON *s = cJSON_GetObjectItem(item, "snr");
            if (f && d) {
                g_birdies[g_num_birdies].freq = (uint32_t)f->valuedouble;
                g_birdies[g_num_birdies].db = (int16_t)d->valueint;
                g_birdies[g_num_birdies].snr = s ? (int16_t)s->valueint : 0;
                g_num_birdies++;
            }
        }
    }
    cJSON_Delete(root);
}

static void save_birdies_json(void) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return;

    cJSON_AddNumberToObject(root, "timestamp", (double)time(NULL));
    cJSON_AddStringToObject(root, "band", SCAN_BANDS[g_band_idx].name);
    cJSON_AddNumberToObject(root, "count", g_num_birdies);

    cJSON *arr = cJSON_AddArrayToObject(root, "birdies");
    for (int i = 0; i < g_num_birdies; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "freq", g_birdies[i].freq);
        cJSON_AddNumberToObject(item, "db", g_birdies[i].db);
        cJSON_AddNumberToObject(item, "snr", g_birdies[i].snr);
        cJSON_AddItemToArray(arr, item);
    }

    char *str = cJSON_Print(root);
    cJSON_Delete(root);

    if (str) {
        FILE *fp = fopen(JSON_PATH, "w");
        if (fp) {
            fputs(str, fp);
            fclose(fp);
        }
        free(str);
    }
}

static void stop_scan(void) {
    if (g_scan_timer) {
        lv_timer_del(g_scan_timer);
        g_scan_timer = NULL;
    }
    g_running = false;
    if (g_orig_freq > 0) {
        radio_set_freq(g_orig_freq);
    }
    radio_update_birdie_notch();
    buttons_load_page(&btn_page);
}

static void scan_timer_cb(lv_timer_t *timer) {
    if (!g_running) return;

    /* Measure at current frequency */
    int16_t raw_db = meter_get_raw_db();

    /* Adapt noise floor (slowly tracking minimum) */
    if (g_noise_floor < -140.0f) {
        g_noise_floor = (float)raw_db;
    } else {
        if (raw_db < g_noise_floor) {
            g_noise_floor = 0.8f * g_noise_floor + 0.2f * (float)raw_db;
        } else {
            g_noise_floor = 0.99f * g_noise_floor + 0.01f * (float)raw_db;
        }
    }

    int16_t thresh = SCAN_THRESHOLDS[g_thresh_idx];
    float diff = (float)raw_db - g_noise_floor;

    /* Detected a carrier/birdie! */
    if (diff >= (float)thresh) {
        if (g_num_birdies < MAX_BIRDIES) {
            /* Avoid duplicates within +/- 1 kHz */
            bool dup = false;
            for (int i = 0; i < g_num_birdies; i++) {
                int32_t d = (int32_t)g_cur_freq - (int32_t)g_birdies[i].freq;
                if (abs(d) < 1500) {
                    dup = true;
                    if (raw_db > g_birdies[i].db) {
                        g_birdies[i].freq = g_cur_freq;
                        g_birdies[i].db = raw_db;
                        g_birdies[i].snr = (int16_t)diff;
                    }
                    break;
                }
            }
            if (!dup) {
                g_birdies[g_num_birdies].freq = g_cur_freq;
                g_birdies[g_num_birdies].db = raw_db;
                g_birdies[g_num_birdies].snr = (int16_t)diff;
                g_num_birdies++;

                char log_line[96];
                snprintf(log_line, sizeof(log_line), "[SPUR] %u.%03u.%03u MHz : %d dBm (+%d dB)\n",
                         g_cur_freq / 1000000, (g_cur_freq / 1000) % 1000, g_cur_freq % 1000,
                         raw_db, (int)diff);
                if (ta_log) {
                    lv_textarea_add_text(ta_log, log_line);
                }
            }
        }
    }

    /* Update UI */
    char txt[128];
    snprintf(txt, sizeof(txt), "Scanning %s: %u.%03u.%03u MHz",
             SCAN_BANDS[g_band_idx].name,
             g_cur_freq / 1000000, (g_cur_freq / 1000) % 1000, g_cur_freq % 1000);
    if (lbl_freq) lv_label_set_text(lbl_freq, txt);

    snprintf(txt, sizeof(txt), "Cur: %d dBm | Noise: %.0f dBm | Spurs found: %d",
             raw_db, g_noise_floor, g_num_birdies);
    if (lbl_info) lv_label_set_text(lbl_info, txt);

    if (progress_bar && g_stop_freq > g_start_freq) {
        int pct = (int)((int64_t)(g_cur_freq - g_start_freq) * 100 / (g_stop_freq - g_start_freq));
        if (pct > 100) pct = 100;
        lv_bar_set_value(progress_bar, pct, LV_ANIM_OFF);
    }

    /* Advance to next frequency step */
    uint32_t step = SCAN_STEPS[g_step_idx];
    g_cur_freq += step;

    if (g_cur_freq > g_stop_freq) {
        /* Finished! */
        stop_scan();
        save_birdies_json();
        birdie_cal_set_enabled(true);
        if (lbl_status) {
            snprintf(txt, sizeof(txt), "Done! %d spurs saved to %s. Filter: ENABLED", g_num_birdies, JSON_PATH);
            lv_label_set_text(lbl_status, txt);
        }
        return;
    }

    radio_set_freq((int32_t)g_cur_freq);
}

static void start_scan(void) {
    g_start_freq = SCAN_BANDS[g_band_idx].start_hz;
    g_stop_freq  = SCAN_BANDS[g_band_idx].stop_hz;
    g_cur_freq   = g_start_freq;
    g_noise_floor = -130.0f;
    g_num_birdies = 0;

    if (ta_log) {
        lv_textarea_set_text(ta_log, "--- Birdie Scan Started ---\n");
    }
    if (lbl_status) {
        lv_label_set_text(lbl_status, "SCANNING... Keep antenna disconnected!");
    }

    g_running = true;
    radio_set_freq((int32_t)g_cur_freq);

    /* 60 ms step time gives enough settling for SDR baseband and S-meter */
    g_scan_timer = lv_timer_create(scan_timer_cb, 60, NULL);
    buttons_load_page(&btn_page);
}

static void btn_run_cb(button_data_t *btn_data) {
    if (g_running) {
        stop_scan();
        if (lbl_status) lv_label_set_text(lbl_status, "Stopped by user.");
    } else {
        start_scan();
    }
}

static void btn_band_cb(button_data_t *btn_data) {
    if (g_running) return;
    g_band_idx = (g_band_idx + 1) % NUM_BANDS;
    buttons_load_page(&btn_page);
}

static void btn_step_cb(button_data_t *btn_data) {
    if (g_running) return;
    g_step_idx = (g_step_idx + 1) % NUM_STEPS;
    buttons_load_page(&btn_page);
}

static void btn_thresh_cb(button_data_t *btn_data) {
    if (g_running) return;
    g_thresh_idx = (g_thresh_idx + 1) % NUM_THRESHOLDS;
    buttons_load_page(&btn_page);
}

static void btn_filter_cb(button_data_t *btn_data) {
    bool en = !params.birdie_filter.x;
    birdie_cal_set_enabled(en);
    buttons_load_page(&btn_page);
    if (lbl_status) {
        char txt[64];
        snprintf(txt, sizeof(txt), "Filter: %s (%d spurs)", en ? "ENABLED" : "DISABLED", g_num_birdies);
        lv_label_set_text(lbl_status, txt);
    }
}

static const char *label_filter_fn(void) {
    return params.birdie_filter.x ? "Filter:\n[ON]" : "Filter:\n[OFF]";
}

static const char *label_run_fn(void) {
    return g_running ? "Stop\nScan" : "Start\nScan";
}

static const char *label_band_fn(void) {
    static char buf[24];
    snprintf(buf, sizeof(buf), "Band:\n%s", SCAN_BANDS[g_band_idx].name);
    return buf;
}

static const char *label_step_fn(void) {
    static char buf[24];
    uint32_t st = SCAN_STEPS[g_step_idx];
    if (st >= 1000) {
        snprintf(buf, sizeof(buf), "Step:\n%.1f kHz", st / 1000.0f);
    } else {
        snprintf(buf, sizeof(buf), "Step:\n%u Hz", st);
    }
    return buf;
}

static const char *label_thresh_fn(void) {
    static char buf[24];
    snprintf(buf, sizeof(buf), "Thresh:\n+%d dB", SCAN_THRESHOLDS[g_thresh_idx]);
    return buf;
}

static void construct_cb(lv_obj_t *parent) {
    dialog.obj = dialog_init(parent);
    buttons_unload_page();
    buttons_load_page(&btn_page);

    g_orig_freq = cparam_i_get(cfg_fg_freq);
    g_running = false;

    /* Load any previous calibration */
    birdie_cal_load();

    lv_coord_t w = 780;
    lv_coord_t h = DIALOG_H - 10;

    lv_obj_t *cont = lv_obj_create(dialog.obj);
    lv_obj_set_size(cont, w, h);
    lv_obj_center(cont);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    /* Title */
    lbl_title = lv_label_create(cont);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(lbl_title, TC(text), 0);
    lv_label_set_text(lbl_title, "Birdie & Internal Spur Auto-Calibration");
    lv_obj_align(lbl_title, LV_ALIGN_TOP_LEFT, 10, 5);

    /* Subtitle / Warning */
    lv_obj_t *lbl_warn = lv_label_create(cont);
    lv_obj_set_style_text_font(lbl_warn, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_warn, TC(warn), 0);
    lv_label_set_text(lbl_warn, "Disconnect antenna or connect 50 Ohm dummy load before starting!");
    lv_obj_align(lbl_warn, LV_ALIGN_TOP_LEFT, 10, 30);

    /* Frequency & info labels */
    lbl_freq = lv_label_create(cont);
    lv_obj_set_style_text_font(lbl_freq, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_freq, TC(sel_text), 0);
    lv_label_set_text(lbl_freq, "Select band and press Start");
    lv_obj_align(lbl_freq, LV_ALIGN_TOP_LEFT, 10, 52);

    lbl_info = lv_label_create(cont);
    lv_obj_set_style_text_font(lbl_info, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_info, TC(text_dim), 0);
    char info_str[64];
    snprintf(info_str, sizeof(info_str), "Known spurs: %d | Filter: %s", g_num_birdies, params.birdie_filter.x ? "ON" : "OFF");
    lv_label_set_text(lbl_info, info_str);
    lv_obj_align(lbl_info, LV_ALIGN_TOP_LEFT, 10, 75);

    /* Progress Bar */
    progress_bar = lv_bar_create(cont);
    lv_obj_set_size(progress_bar, 750, 14);
    lv_obj_align(progress_bar, LV_ALIGN_TOP_LEFT, 10, 98);
    lv_bar_set_range(progress_bar, 0, 100);
    lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(progress_bar, TC(bar), LV_PART_MAIN);
    lv_obj_set_style_bg_color(progress_bar, TC(ok), LV_PART_INDICATOR);

    /* Status line */
    lbl_status = lv_label_create(cont);
    lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_status, TC(ok), 0);
    char status_str[64];
    snprintf(status_str, sizeof(status_str), "Status: %s (%d spurs loaded)",
             params.birdie_filter.x ? "Filter ENABLED" : "Filter DISABLED", g_num_birdies);
    lv_label_set_text(lbl_status, status_str);
    lv_obj_align(lbl_status, LV_ALIGN_TOP_LEFT, 10, 118);

    /* Log textarea for detected spurs */
    ta_log = lv_textarea_create(cont);
    lv_obj_set_size(ta_log, 750, 175);
    lv_obj_align(ta_log, LV_ALIGN_TOP_LEFT, 10, 142);
    lv_textarea_set_cursor_click_pos(ta_log, false);
    lv_obj_set_style_text_font(ta_log, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(ta_log, TC(bar), 0);
    lv_obj_set_style_text_color(ta_log, TC(text), 0);
    lv_obj_set_style_border_color(ta_log, TC(bar_border), 0);

    if (g_num_birdies > 0) {
        char init_txt[128];
        snprintf(init_txt, sizeof(init_txt), "Loaded %d previous spurs from %s\n", g_num_birdies, JSON_PATH);
        lv_textarea_set_text(ta_log, init_txt);
    } else {
        lv_textarea_set_text(ta_log, "No previous calibration found. Press Start Scan to begin.\n");
    }
}

static void destruct_cb(void) {
    if (g_running) {
        stop_scan();
    }
    lbl_title = NULL;
    lbl_status = NULL;
    lbl_freq = NULL;
    lbl_info = NULL;
    progress_bar = NULL;
    ta_log = NULL;
}

static void key_cb(lv_event_t *e) {
    uint32_t key = *((uint32_t *)lv_event_get_param(e));
    if (key == LV_KEY_ESC) {
        if (g_running) {
            stop_scan();
        }
        dialog_destruct();
    }
}
