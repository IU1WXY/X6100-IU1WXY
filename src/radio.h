/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include <stdbool.h>
#include <aether_radio/x6100_control/control.h>

#include "lvgl/lvgl.h"
#include "audio.h"

#define RADIO_SAMPLES   (512)

typedef enum {
    RADIO_RX = 0,
    RADIO_TX,
    RADIO_ATU_START,
    RADIO_ATU_WAIT,
    RADIO_ATU_RUN,
    RADIO_SWRSCAN,

    RADIO_POWEROFF,
    RADIO_OFF
} radio_state_t;

typedef enum {
    RADIO_CHARGER_OFF = 0,
    RADIO_CHARGER_ON,
    RADIO_CHARGER_SHADOW
} radio_charger_t;

typedef void (*radio_rx_tx_change_t) (bool tx);

void radio_init();
void radio_start();
void radio_set_rx_tx_notify_fn(radio_rx_tx_change_t cb);
void radio_set_low_power_cb(void(*cb)(bool));
void radio_bb_reset();
radio_state_t radio_get_state();

/**
 * Set freq for radio without updating corresponding subject.
 * Useful for FT8 TX freq change and SWR scan
 */
void radio_set_freq(int32_t freq);
bool radio_check_freq(int32_t freq);
void radio_update_birdie_notch(void);

x6100_vfo_t radio_toggle_vfo();

uint16_t radio_change_vol(int16_t df);
bool radio_change_spmode(int16_t df);

void radio_change_mute();
void radio_vol_refresh();

void radio_set_pwr(float d);
/* While a digital app is open the radio never goes above TX_PLAYER_MAX_PWR_W */
void radio_set_digi_cap(bool on);
/* Radio only, not saved: settings keep cfg_tx_filter_low/high. */
void radio_set_tx_filter(uint16_t low, uint16_t high);
/* Noise reduction, noise blanker, notch and auto-notch off (the base
 * applies them in every mode, DIGI included), or back to the settings.
 * Radio only, not saved. */
void radio_set_rx_dsp_off(bool off);

void radio_set_charger(bool on);

void radio_start_atu();

bool radio_start_swrscan();
void radio_stop_swrscan();

void radio_poweroff();
void radio_set_ptt(bool tx);
/* The mic of the baseband without saving it (x6100_mic_sel_t), -1: back to cfg_mic */
void radio_set_mic_override(int mic);
/* The mic in use: the override, else cfg_mic */
int radio_get_mic(void);
void radio_set_modem(bool tx);
bool radio_get_modem(void);   /* the TX audio from the GUI (apps, recordings), not the mic */
/* The base plays this app's audio on the speaker (as voice prompts and the
 * recorder do) - and while it does, the audio it sends us isn't the
 * receiver's. Off puts the mics back. */
void radio_speaker_play(bool on);
/* audio_set_play_mode() with the radio lock: use this one */
void radio_set_play_mode(audio_play_mode_t mode);

void radio_set_line_in(uint8_t d);
void radio_set_line_out(uint8_t d);

void radio_set_morse_key(bool on);
/* Break-in delay of the base, ms (cfg_qsk_time is the setting; the CW encoder holds TX longer) */
void radio_set_qsk_time(uint16_t ms);
/* The TX voice reaches the screen recording only through the monitor: at least 30 while recording */
void radio_set_rec_moni(bool on);

int8_t radio_get_audio_in_lvl();
