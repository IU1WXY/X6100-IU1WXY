/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "spectrum.h"

#include "dsp.h"
#include "dialog_navtex.h"
#include "dialog_wefax.h"
#include "events.h"
#include "meter.h"
#include "params/params.h"
#include "cfg/cfg_api.h"
#include "pubsub_ids.h"
#include "radio.h"
#include "recorder.h"
#include "waterfall.h"
#include "rtty.h"
#include "scheduler.h"
#include "dialog_birdie_cal.h"
#include "styles.h"
#include "theme.h"
#include "util.h"

#include "lv_drivers/display/drm.h"

#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_MIN S4
#define DEFAULT_MAX S9_20
#define VISOR_HEIGHT_TX (100 - 61)
#define VISOR_HEIGHT_RX 100
#define SPECTRUM_SIZE 800

typedef struct {
    float    val;
    uint64_t time;
} peak_t;

static float grid_min = DEFAULT_MIN;
static float grid_max = DEFAULT_MAX;

static lv_obj_t *obj;

static int32_t width_hz     = 100000;
static int16_t visor_height = 100;

static float   spectrum_buf[SPECTRUM_SIZE];
static peak_t  spectrum_peak[SPECTRUM_SIZE];
static uint8_t zoom_factor = 1;

static bool spectrum_tx = false;

static int32_t filter_from = 0;
static int32_t filter_to   = 3000;
static int32_t fg_freq;
static int32_t rit;
static int32_t mode_lo_offset;
static int32_t if_shift;

static int32_t dnf_show = false;
static int32_t dnf_center;
static int32_t dnf_width;

static bool center_line_show = true;

static int32_t cur_base_lo_freq;
static uint8_t prev_fft_dec = 1;
static int16_t freq_mod;


static pthread_mutex_t data_mux;

/* Direct mode (DRM planes, spectrum shown): the trace is drawn on the primary
 * plane under the GUI, like the waterfall; LVGL draws only the markers over
 * it, redrawn when they change. sbuf is the portrait image of the strip:
 * landscape x is row 799 - x, landscape y (from the top) is column y */
static bool     direct = false;
static uint32_t *sbuf;
static int32_t  sbuf_h;
static int32_t  markers_sig[12];

static void on_zoom_changed(Subject *subj, void *user_data);
static void update_filters(Subject *subj, void *user_data);
static void update_dnf(Subject *subj, void *user_data);
static void update_center_line(Subject *subj, void *user_data);
static void on_mode_lo_offset_change(Subject *subj, void *user_data);
static void on_if_shift_change(Subject *subj, void *user_data);
static void on_grid_min_change(Subject *subj, void *user_data);
static void on_grid_max_change(Subject *subj, void *user_data);
static void on_cur_base_lo_freq_change(Subject *subj, void *user_data);
static void on_rit_change(Subject *subj, void *user_data);
static void shift_peaks(int32_t df);

/* Shift for filter data, notch, etc. Fixed waterfall mode: the whole view is
 * shifted against the received data */
static lv_coord_t markers_offset_get(lv_coord_t w) {
    return ((mode_lo_offset + if_shift + waterfall_view_shift()) * zoom_factor * w + width_hz / 2) / width_hz;
}

/* Shift for the spectrum data */
static lv_coord_t spectrum_offset_get(lv_coord_t w) {
    if (spectrum_tx) {
        return markers_offset_get(w);
    }
    lv_coord_t data_shift = 0;
    if (cur_base_lo_freq) {
        // Handle delay between sending new settings to base and new data flow
        data_shift = cur_base_lo_freq - (fg_freq + mode_lo_offset - if_shift + rit);
    }
    return ((mode_lo_offset + data_shift + waterfall_view_shift()) * zoom_factor * w + width_hz / 2) / width_hz;
}

static void levels_get(float *min, float *max) {
    if (spectrum_tx) {
        *min = DEFAULT_MIN;
        *max = DEFAULT_MAX;
    } else {
        *min = grid_min;
        *max = grid_max;
    }
}

/* Settings (web Display page): the trace and the fill in their own colours, else the theme's */
static lv_color_t trace_color(void) {
    int32_t c = param_i_get(cfg_spectrum_line_color);
    return c < 0 ? TC(line) : lv_color_hex(c);
}

/* The fill in its own colour: the trace is drawn over it */
static bool fill_own(void) {
    return params.spectrum_filled.x && param_i_get(cfg_spectrum_fill_color) >= 0;
}

static lv_color_t fill_color(void) {
    return fill_own() ? lv_color_hex(param_i_get(cfg_spectrum_fill_color)) : trace_color();
}

/* The fill fading from its colour at the trace to the background at the bottom, the trace drawn
 * over it (after R1CBU's gradient fill, OpenX6100) */
static bool fill_grad(void) {
    return params.spectrum_filled.x && (param_i_get(cfg_spectrum_fill_gradient) || theme_shape.deck);   /* deck: always */
}

/* The trace is on the primary plane: the main screen's spectrum in direct mode, not the walkie's */
static bool on_plane(lv_obj_t *o) {
    return direct && o == obj;
}

void spectrum_draw_cb(lv_event_t *e) {
    lv_obj_t          *obj      = lv_event_get_target(e);
    lv_draw_ctx_t     *draw_ctx = lv_event_get_draw_ctx(e);
    lv_draw_line_dsc_t main_line_dsc;
    lv_draw_line_dsc_t peak_line_dsc;
    lv_draw_line_dsc_t trace_dsc;
    lv_draw_rect_dsc_t grad_dsc;
    lv_point_t         trace_prev = {0, 0};
    bool               grad  = fill_grad();
    bool               trace = fill_own() || grad;

    float min, max;
    levels_get(&min, &max);

    /* Lines */

    lv_draw_line_dsc_init(&main_line_dsc);

    main_line_dsc.color = params.spectrum_filled.x ? fill_color() : trace_color();
    main_line_dsc.width = 1;
    lv_draw_line_dsc_init(&trace_dsc);
    trace_dsc.color = trace_color();
    trace_dsc.width = 1;
    lv_draw_rect_dsc_init(&grad_dsc);
    grad_dsc.bg_grad.dir = LV_GRAD_DIR_VER;
    grad_dsc.bg_grad.stops[0].color = fill_color();
    grad_dsc.bg_grad.stops[1].color = lv_obj_get_style_bg_color(obj, LV_PART_MAIN);

    lv_draw_line_dsc_init(&peak_line_dsc);

    peak_line_dsc.color = TC(off);
    peak_line_dsc.width = 1;

    lv_coord_t x1 = obj->coords.x1;
    lv_coord_t y1 = obj->coords.y1;

    lv_coord_t w = lv_obj_get_width(obj);
    lv_coord_t h = lv_obj_get_height(obj);

    lv_coord_t spectrum_offset = spectrum_offset_get(w);
    lv_coord_t markers_offset = markers_offset_get(w);

    lv_point_t main_a, main_b;
    lv_point_t peak_a, peak_b;

    if (!params.spectrum_filled.x) {
        main_b.x = x1;
        main_b.y = y1 + h;
    }

    peak_b.x = x1;
    peak_b.y = y1 + h;

    /* Direct mode: the trace is on the primary plane, only the markers here */
    for (uint16_t i = 0; i < SPECTRUM_SIZE && !on_plane(obj); i++) {
        float    v = (spectrum_buf[i] - min) / (max - min);
        uint16_t x = i * w / SPECTRUM_SIZE;

        /* Peak */

        if (params.spectrum_peak.x && !spectrum_tx) {
            float v_peak = (spectrum_peak[i].val - min) / (max - min);

            peak_a.x = x1 + spectrum_offset + x;
            peak_a.y = y1 + (1.0f - v_peak) * h;

            lv_draw_line(draw_ctx, &peak_line_dsc, &peak_a, &peak_b);

            peak_b = peak_a;
        }

        /* Main */

        main_a.x = x1 + spectrum_offset + x;
        main_a.y = y1 + (1.0f - v) * h;

        if (params.spectrum_filled.x) {
            main_b.x = main_a.x;
            main_b.y = y1 + h;
        }

        if (grad) {
            lv_area_t col = {main_a.x, main_a.y, main_a.x, main_b.y};

            lv_draw_rect(draw_ctx, &grad_dsc, &col);
        } else {
            lv_draw_line(draw_ctx, &main_line_dsc, &main_a, &main_b);
        }
        if (trace) {
            if (i) lv_draw_line(draw_ctx, &trace_dsc, &trace_prev, &main_a);
            trace_prev = main_a;
        }

        if (!params.spectrum_filled.x) {
            main_b = main_a;
        }
    }

    /* Filter */

    lv_draw_rect_dsc_t rect_dsc;
    lv_area_t          area;

    lv_draw_rect_dsc_init(&rect_dsc);

    rect_dsc.bg_color = theme_shape.deck ? TC(vfo_bg) : bg_color;   /* deck: shaded on the black */
    rect_dsc.bg_opa   = LV_OPA_50;

    int32_t w_hz = width_hz / zoom_factor;

    int16_t sign_from = (filter_from > 0) ? 1 : -1;
    int16_t sign_to   = (filter_to > 0) ? 1 : -1;

    int32_t f1 = (float)(w * filter_from) / w_hz + 1.0f;
    int32_t f2 = (float)(w * filter_to) / w_hz + 1.0f;


    area.x1 = x1 + markers_offset + w / 2 + f1;
    area.y1 = y1;
    area.x2 = x1 + markers_offset + w / 2 + f2;
    area.y2 = y1 + h;

    lv_draw_rect(draw_ctx, &rect_dsc, &area);

    /* Notch */
    if (dnf_show) {
        int32_t from, to;

        rect_dsc.bg_color = TC(text);
        rect_dsc.bg_opa   = LV_OPA_50;

        /* dnf_center already carries the sideband's sign (update_dnf): no second flip, or LSB lands on the USB side */
        from = dnf_center - dnf_width;
        to   = dnf_center + dnf_width;

        if (from < to) {
            f1 = (w * from) / w_hz;
            f2 = (w * to) / w_hz;
        } else {
            f1 = (w * to) / w_hz;
            f2 = (w * from) / w_hz;
        }

        area.x1 = x1 + markers_offset + w / 2 + f1;
        area.y1 = y1;
        area.x2 = x1 + markers_offset + w / 2 + f2;
        area.y2 = y1 + h;

        lv_draw_rect(draw_ctx, &rect_dsc, &area);
    }

    if (rtty_get_state() != RTTY_OFF) {
        int32_t from, to;

        from = sign_from * (params.rtty_center - params.rtty_shift / 2);
        to   = sign_to * (params.rtty_center + params.rtty_shift / 2);

        f1 = (int64_t)(w * from) / w_hz;
        f2 = (int64_t)(w * to) / w_hz;

        main_a.x = x1 + markers_offset + w / 2 + f1;
        main_a.y = y1;
        main_b.x = main_a.x;
        main_b.y = y1 + h;
        lv_draw_line(draw_ctx, &main_line_dsc, &main_a, &main_b);

        main_a.x = x1 + markers_offset + w / 2 + f2;
        main_b.x = main_a.x;
        lv_draw_line(draw_ctx, &main_line_dsc, &main_a, &main_b);
    }

    /*
     * NAVTEX tuning markers.
     * Decoder center = 1000 Hz, shift = 170 Hz -> 915 / 1085 Hz.
     * Use the exact same Hz-to-pixel mapping and line style as RTTY.
     */
    if (dialog_navtex_is_active()) {
        int32_t from, to;

        from = sign_from * 915;
        to   = sign_to * 1085;

        f1 = (int64_t)(w * from) / w_hz;
        f2 = (int64_t)(w * to) / w_hz;

        main_a.x = x1 + markers_offset + w / 2 + f1;
        main_a.y = y1;
        main_b.x = main_a.x;
        main_b.y = y1 + h;
        lv_draw_line(draw_ctx, &main_line_dsc, &main_a, &main_b);

        main_a.x = x1 + markers_offset + w / 2 + f2;
        main_b.x = main_a.x;
        lv_draw_line(draw_ctx, &main_line_dsc, &main_a, &main_b);
    }

    /*
     * WEFAX tuning markers.
     * Decoder center = 1500 Hz, shift = 850 Hz -> 1075 / 1925 Hz.
     * Use the exact same Hz-to-pixel mapping and line style as RTTY/NAVTEX.
     * dialog_wefax_is_active() follows dialog.run, so the markers disappear
     * regardless of how the WEFAX dialog is closed.
     */
    if (dialog_wefax_is_active()) {
        int32_t from, to;

        from = sign_from * 1075;
        to   = sign_to * 1925;

        f1 = (int64_t)(w * from) / w_hz;
        f2 = (int64_t)(w * to) / w_hz;

        main_a.x = x1 + markers_offset + w / 2 + f1;
        main_a.y = y1;
        main_b.x = main_a.x;
        main_b.y = y1 + h;
        lv_draw_line(draw_ctx, &main_line_dsc, &main_a, &main_b);

        main_a.x = x1 + markers_offset + w / 2 + f2;
        main_b.x = main_a.x;
        lv_draw_line(draw_ctx, &main_line_dsc, &main_a, &main_b);
    }

    /* Center */

    main_line_dsc.width = 3;
    main_line_dsc.color = TC(vfo_line);   /* VFO line, as the waterfall one */

    main_a.x = x1 + markers_offset + w / 2;
    main_a.y = y1;
    main_b.x = main_a.x;
    main_b.y = y1 + h;

    if (recorder_is_on()) {
        main_line_dsc.color = TC(err_strong);
    }
    if (center_line_show) {
        lv_draw_line(draw_ctx, &main_line_dsc, &main_a, &main_b);
    }

}

static void tx_cb(lv_event_t *e) {
    visor_height = VISOR_HEIGHT_TX;
}

static void rx_cb(lv_event_t *e) {
    visor_height = VISOR_HEIGHT_RX;
}

static inline uint32_t blend_xrgb(uint32_t dst, uint32_t src, uint8_t a) {
    uint32_t rb = ((dst & 0x00FF00FFu) * (255u - a) + (src & 0x00FF00FFu) * a + 0x00800080u);
    uint32_t g  = ((dst & 0x0000FF00u) * (255u - a) + (src & 0x0000FF00u) * a + 0x00008000u);
    return ((rb >> 8) & 0x00FF00FFu) | ((g >> 8) & 0x0000FF00u) | 0xff000000u;
}

/* A landscape pixel of the strip, with coverage c (0..1) */
static void plot(int x, int y, float c, uint32_t color) {
    if (x < 0 || x >= SPECTRUM_SIZE || y < 0 || y >= sbuf_h || c <= 0.0f) {
        return;
    }
    uint32_t *p = &sbuf[(SPECTRUM_SIZE - 1 - x) * sbuf_h + y];

    *p = blend_xrgb(*p, color, c >= 1.0f ? 255 : (uint8_t)(c * 255.0f + 0.5f));
}

static float fpart(float v) {
    return v - floorf(v);
}

/* Antialiased line, Wu's algorithm (upstream spectrum_wu_line, landscape) */
static void wu_line(float x0, float y0, float x1, float y1, uint32_t color) {
    bool steep = fabsf(y1 - y0) > fabsf(x1 - x0);
    float t;

    if (steep) {
        t = x0; x0 = y0; y0 = t;
        t = x1; x1 = y1; y1 = t;
    }
    if (x0 > x1) {
        t = x0; x0 = x1; x1 = t;
        t = y0; y0 = y1; y1 = t;
    }
    float dx = x1 - x0;
    float gradient = (dx == 0.0f) ? 1.0f : (y1 - y0) / dx;

    int   xa = (int)roundf(x0), xb = (int)roundf(x1);
    float y  = y0 + gradient * (xa - x0);

    for (int x = xa; x <= xb; x++) {
        int   yi = (int)floorf(y);
        float f  = fpart(y);

        if (steep) {
            plot(yi, x, 1.0f - f, color);
            plot(yi + 1, x, f, color);
        } else {
            plot(x, yi, 1.0f - f, color);
            plot(x, yi + 1, f, color);
        }
        y += gradient;
    }
}

static void polyline(const float *v, float min, float max, lv_coord_t offset, uint32_t color) {
    float px = 0, py = 0;

    for (int i = 0; i < SPECTRUM_SIZE; i++) {
        float x = i + offset;
        float y = (1.0f - (v[i] - min) / (max - min)) * sbuf_h;

        y = LV_CLAMP(0.0f, y, (float)sbuf_h);
        if (i) {
            wu_line(px, py, x, y, color);
        }
        px = x;
        py = y;
    }
}

/* The markers drawn by LVGL: redrawn only when one of them changes */
static void markers_check(void) {
    int32_t sig[sizeof(markers_sig) / sizeof(markers_sig[0])] = {
        markers_offset_get(SPECTRUM_SIZE), filter_from, filter_to, dnf_show, dnf_center, dnf_width,
        rtty_get_state(), params.rtty_center, params.rtty_shift,
        dialog_navtex_is_active() | dialog_wefax_is_active() << 1, center_line_show, recorder_is_on()
    };

    if (memcmp(sig, markers_sig, sizeof(sig))) {
        memcpy(markers_sig, sig, sizeof(sig));
        lv_obj_invalidate(obj);
    }
}

static void direct_render(void) {
    lv_coord_t h = lv_obj_get_height(obj);
    float      min, max;

    markers_check();
    /* Spectrum off: transparent, under the top bars */
    if (!lv_obj_is_visible(obj) || h <= 0 || lv_obj_get_style_opa(obj, 0) < LV_OPA_MIN) {
        return;
    }
    if (h != sbuf_h) {
        free(sbuf);
        sbuf = malloc(SPECTRUM_SIZE * h * sizeof(uint32_t));
        sbuf_h = sbuf ? h : 0;
        if (!sbuf) {
            return;
        }
    }
    uint32_t bg = TC(panel).full | 0xff000000u;
    uint32_t line = trace_color().full | 0xff000000u;
    lv_color_t fc   = fill_color(), pc = TC(panel);
    uint32_t   fill = fc.full | 0xff000000u;
    bool       grad = fill_grad();

    for (int32_t i = 0; i < SPECTRUM_SIZE * h; i++) {
        sbuf[i] = bg;
    }
    levels_get(&min, &max);
    if (max <= min) {
        max = min + 1.0f;
    }
    lv_coord_t offset = spectrum_offset_get(SPECTRUM_SIZE);

    pthread_mutex_lock(&data_mux);
    if (params.spectrum_peak.x && !spectrum_tx) {
        float peak[SPECTRUM_SIZE];

        for (int i = 0; i < SPECTRUM_SIZE; i++) {
            peak[i] = spectrum_peak[i].val;
        }
        polyline(peak, min, max, offset, TC(off).full);
    }
    if (params.spectrum_filled.x) {
        for (int i = 0; i < SPECTRUM_SIZE; i++) {
            int x = i + offset;

            if (x < 0 || x >= SPECTRUM_SIZE) {
                continue;
            }
            int top = (1.0f - (spectrum_buf[i] - min) / (max - min)) * h;
            uint32_t *p = &sbuf[(SPECTRUM_SIZE - 1 - x) * h];

            top = LV_MAX(top, 0);
            for (int y = top; y < h; y++) {
                p[y] = grad ? lv_color_mix(fc, pc, (h - y) * 255 / (h - top)).full | 0xff000000u : fill;
            }
        }
        if (fill_own() || grad) polyline(spectrum_buf, min, max, offset, line);
    } else {
        polyline(spectrum_buf, min, max, offset, line);
    }
    pthread_mutex_unlock(&data_mux);

#if USE_DRM
    drm_wf_present(sbuf, obj->coords.y1, h);
#endif
}

static void spectrum_refresh(void *data) {
    if (direct) {
        if (lv_obj_get_screen(obj) == lv_scr_act())    /* not under the walkie screen */
            direct_render();
    } else {
        lv_obj_invalidate(obj);
    }
}

lv_obj_t *spectrum_init(lv_obj_t *parent) {
    if (!obj) pthread_mutex_init(&data_mux, NULL);   /* the DSP thread may hold it on a theme change */
    spectrum_min_max_reset();

    for (size_t i = 0; i < SPECTRUM_SIZE; i++) {
        spectrum_peak[i].val = S_MIN;
        spectrum_buf[i] = S_MIN;
    }

    obj = lv_obj_create(parent);

    lv_obj_add_style(obj, &spectrum_style, 0);
#if USE_DRM
    direct = drm_planes();      /* also when off: it can be shown live (main_screen.c) */
#endif
    if (direct) {
        /* Transparent, then the markers over the primary plane */
        lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
        lv_obj_add_event_cb(obj, direct_hole_cb, LV_EVENT_DRAW_MAIN_END, NULL);
    }
    lv_obj_add_event_cb(obj, spectrum_draw_cb, LV_EVENT_DRAW_MAIN_END, NULL);
    lv_obj_add_event_cb(obj, tx_cb, EVENT_RADIO_TX, NULL);
    lv_obj_add_event_cb(obj, rx_cb, EVENT_RADIO_RX, NULL);

    subject_subscribe_and_notify((Subject*)cfg_mode_zoom, on_zoom_changed, NULL);

    subject_subscribe((Subject*)cfg_cur_filter_low, update_filters, NULL);
    subject_subscribe((Subject*)cfg_cur_filter_high, update_filters, NULL);
    subject_subscribe_and_notify((Subject*)cfg_cur_mode, update_filters, NULL);

    subject_subscribe_and_notify((Subject*)cfg_cur_mode, update_center_line, NULL);
    subject_subscribe_and_notify((Subject*)cfg_mode_lo_offset, on_mode_lo_offset_change, NULL);
    subject_subscribe_and_notify((Subject*)cfg_band_if_shift, on_if_shift_change, NULL);

    subject_subscribe((Subject*)cfg_auto_level_enabled, on_grid_min_change, NULL);
    subject_subscribe_and_notify((Subject*)cfg_band_grid_min, on_grid_min_change, NULL);
    subject_subscribe((Subject*)cfg_auto_level_enabled, on_grid_max_change, NULL);
    subject_subscribe_and_notify((Subject*)cfg_band_grid_max, on_grid_max_change, NULL);

    subject_subscribe((Subject*)cfg_cur_mode, update_dnf, NULL);
    subject_subscribe((Subject*)cfg_dnf, update_dnf, NULL);
    subject_subscribe((Subject*)cfg_dnf_auto, update_dnf, NULL);
    subject_subscribe((Subject*)cfg_dnf_center, update_dnf, NULL);
    subject_subscribe_and_notify((Subject*)cfg_dnf_width, update_dnf, NULL);

    subject_subscribe_and_notify((Subject*)cfg_fg_freq, on_cur_base_lo_freq_change, NULL);
    subject_subscribe_and_notify((Subject*)cfg_rit, on_rit_change, NULL);
    return obj;
}

void spectrum_data(float *data_buf, uint16_t size, bool tx, uint32_t base_lo_freq, uint8_t fft_dec) {
    uint64_t now = get_time();

    if (base_lo_freq != cur_base_lo_freq) {
        int32_t df = base_lo_freq - cur_base_lo_freq;
        cur_base_lo_freq = base_lo_freq;
        shift_peaks(df);
    }

    if (!tx && birdie_cal_is_enabled()) {
        uint32_t eff_center = base_lo_freq ? base_lo_freq : (fg_freq + mode_lo_offset - if_shift + rit);
        uint32_t span = (zoom_factor > 0) ? (width_hz / zoom_factor) : width_hz;
        birdie_cal_suppress_spectrum(data_buf, size, eff_center, span);
    }

    pthread_mutex_lock(&data_mux);
    spectrum_tx = tx;
    for (uint16_t i = 0; i < size; i++) {
        spectrum_buf[i] = data_buf[i];

        if (params.spectrum_peak.x && !tx) {
            float   v    = spectrum_buf[i];
            peak_t *peak = &spectrum_peak[i];

            if ((v > peak->val) || (fft_dec != prev_fft_dec)) {
                peak->time = now;
                peak->val  = v;
            } else {
                if (now - peak->time > (int)params.spectrum_peak_hold.x * 1000) {
                    peak->val -= params.spectrum_peak_speed.x * 0.1f;
                }
            }
        }
    }
    prev_fft_dec = fft_dec;

    pthread_mutex_unlock(&data_mux);
    scheduler_put_noargs(spectrum_refresh);
}

int32_t spectrum_span(float *vfo_at) {
    *vfo_at = 0.5f + (float)markers_offset_get(SPECTRUM_SIZE) / SPECTRUM_SIZE;
    return width_hz / zoom_factor;
}

void spectrum_min_max_reset() {
    if (param_i_get(cfg_auto_level_enabled)) {
        grid_min = DEFAULT_MIN;
        grid_max = DEFAULT_MAX;
    } else {
        grid_min = param_i_get(cfg_band_grid_min);
        grid_max = param_i_get(cfg_band_grid_max);
    }
}

void spectrum_update_max(float db) {
    if (param_i_get(cfg_auto_level_enabled)) {
        grid_max = db - param_f_get(cfg_auto_level_offset);
    }
}

void spectrum_update_min(float db) {
    if (param_i_get(cfg_auto_level_enabled)) {
        grid_min = db - param_f_get(cfg_auto_level_offset);
    }
}

void spectrum_clear() {
    spectrum_min_max_reset();
    freq_mod = 0;
    uint64_t now = get_time();

    for (uint16_t i = 0; i < SPECTRUM_SIZE; i++) {
        spectrum_buf[i]       = S_MIN;
        spectrum_peak[i].val  = S_MIN;
        spectrum_peak[i].time = now;
    }
}

static void on_zoom_changed(Subject *subj, void *user_data) {
    zoom_factor = (uint8_t)subject_i_get((SubjectInt*)subj);
    spectrum_clear();
}

static void update_filters(Subject *subj, void *user_data) {
    int32_t low = subject_i_get((SubjectInt*)cfg_cur_filter_low);
    int32_t high = subject_i_get((SubjectInt*)cfg_cur_filter_high);
    x6100_mode_t mode = subject_i_get((SubjectInt*)cfg_cur_mode);
    switch (mode)
    {
    case x6100_mode_lsb:
    case x6100_mode_lsb_dig:
    case x6100_mode_cwr:
        filter_to = -low;
        filter_from = -high;
        break;
    case x6100_mode_am:
    case x6100_mode_nfm:
        filter_from = -high;
        filter_to = high;
        break;

    default:
        filter_from = low;
        filter_to = high;
        break;
    }
}

/* The manual notch on the spectrum: shown only when DNF is on, not automatic
 * and the mode has a sideband; centred on dnf_center (upstream 8e4f1d6) */
static void update_dnf(Subject *subj, void *user_data) {
    /* Redrawn on the next loop: the change may come from another thread (CAT) */
    lv_async_call((void (*)(void *))lv_obj_invalidate, obj);
    dnf_show = false;
    int32_t en = subject_i_get((SubjectInt*)cfg_dnf);
    if (!en) {
        return;
    }
    int32_t auto_ = subject_i_get((SubjectInt*)cfg_dnf_auto);
    if (auto_) {
        return;
    }
    x6100_mode_t mode = subject_i_get((SubjectInt*)cfg_cur_mode);
    if ((mode == x6100_mode_am) || (mode == x6100_mode_nfm)) {
        return;
    }
    dnf_show = true;
    dnf_width = subject_i_get((SubjectInt*)cfg_dnf_width);
    int32_t center = subject_i_get((SubjectInt*)cfg_dnf_center);
    switch (mode)
    {
    case x6100_mode_lsb:
    case x6100_mode_lsb_dig:
    case x6100_mode_cwr:
        dnf_center = -center;
        break;

    default:
        dnf_center = center;
        break;
    }
}


static void update_center_line(Subject *subj, void *user_data) {
    x6100_mode_t mode = (x6100_mode_t)subject_i_get((SubjectInt*)subj);
    center_line_show = (mode != x6100_mode_cw && mode != x6100_mode_cwr);
}

static void on_mode_lo_offset_change(Subject *subj, void *user_data) {
    mode_lo_offset = subject_i_get((SubjectInt*)subj);
}

static void on_if_shift_change(Subject *subj, void *user_data) {
    if_shift = subject_i_get((SubjectInt*)subj);
}

static void on_grid_min_change(Subject *subj, void *user_data) {
    if (!param_i_get(cfg_auto_level_enabled)) {
        grid_min = param_i_get(cfg_band_grid_min);
    }
}
static void on_grid_max_change(Subject *subj, void *user_data) {
    if (!param_i_get(cfg_auto_level_enabled)) {
        grid_max = param_i_get(cfg_band_grid_max);
    }
}

static void on_cur_base_lo_freq_change(Subject *subj, void *user_data) {
    fg_freq = cparam_i_get(cfg_fg_freq);
    scheduler_put_noargs(spectrum_refresh);
}

static void on_rit_change(Subject *subj, void *user_data) {
    rit = param_i_get(cfg_rit);
}

static void shift_peaks(int32_t df) {
    df += freq_mod;
    uint64_t time = get_time();

    uint16_t div     = width_hz / SPECTRUM_SIZE / zoom_factor;
    int32_t  delta   = (df + div / 2) / div;
    freq_mod = df - delta * div;

    if (delta == 0) {
        return;
    }
    peak_t  *to;
    for (int16_t i = 0; i < SPECTRUM_SIZE; i++) {
        int16_t dst_id = delta > 0 ? i : SPECTRUM_SIZE - i - 1;
        to = &spectrum_peak[dst_id];
        int16_t src_id = dst_id + delta;
        if ((src_id < 0) || (src_id >= SPECTRUM_SIZE)) {
            to->val = S_MIN;
            to->time = time;
        } else {
            *to = spectrum_peak[src_id];
        }
    }
}
