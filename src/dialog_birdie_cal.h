/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Birdie & Internal Spur Auto-Calibration Dialog
 */

#pragma once

#include "lvgl/lvgl.h"
#include "dialog.h"

#ifdef __cplusplus
extern "C" {
#endif

extern dialog_t *dialog_birdie_cal;

/* Check if birdie suppression filter is enabled */
bool birdie_cal_is_enabled(void);

/* Enable or disable birdie suppression */
void birdie_cal_set_enabled(bool en);

/* Check if a frequency is near a known calibrated birdie (+/- tolerance_hz) */
bool birdie_cal_is_birdie(uint32_t freq_hz, uint32_t tolerance_hz);

/* Suppress known birdies in an FFT power spectrum or waterfall line */
void birdie_cal_suppress_spectrum(float *data_buf, uint16_t size, uint32_t center_freq, uint32_t span_hz);

/* Check if any known birdie falls in audio passband, returning tone frequency in Hz */
bool birdie_cal_check_audio(uint32_t vfo_freq, int mode, int low_filter, int high_filter, uint16_t *notch_center);

/* Load calibrated birdies from /mnt/birdies.json */
void birdie_cal_load(void);

#ifdef __cplusplus
}
#endif
