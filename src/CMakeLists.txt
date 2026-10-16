add_executable(${PROJECT_NAME})

target_sources(${PROJECT_NAME} PUBLIC
    main.c main_screen.c walkie.c
    styles.c spectrum.c radio.c dsp.cpp util.cpp
    waterfall.c rotary.c keyboard.c kbd_rollover.c encoder.c
    events.c msg.c msg_tiny.c keypad.c
    hkey.c clock.c info.c
    meter.c band_info.cpp tx_info.c
    audio.c mfk.cpp cw.cpp cw_decoder.c cw_parse.c panel.cpp
    rtty.c screenshot.c screen_rec.c avi_writer.c backlight.c gps.c cat.cpp wfview_server.c remote_server.cpp
    dialog.c dialog_settings.cpp dialog_swrscan.c dialog_birdie_cal.c
    dialog_ft8.c dialog_freq.c dialog_gps.c dialog_msg_cw.c
    dialog_msg_voice.c dialog_recorder.c dialog_qth.c station.c
    dialog_channels.c
    dialog_wefax.c
    dialog_navtex.c
    dialog_js8.c
    dialog_psk.c psk_rx.c psk_tx.c psk_spec.c psk_macro.c digi_qso.c
    dialog_olivia.c olivia.c olivia_app.c olivia_demod.c olivia_macro.c olivia_mod.c olivia_resamp.c
    olivia_rx.c olivia_tx.c
    dialog_jtty.c jtty.c jtty_rx.c jtty_tx.c
    tx_player.c
    tx_level.c
    swr_guard.c
    base_guard.c
    dialog_sstv.c
    sstv_resources.c
    dialog_cluster.c
    freq_list.c
    js8_map.c map_geo.c
    ft8_udp.c
    pskreporter.c
    dxcc.c
    dialog_modes.c
    dialog_cw_trainer.c
    dialog_cw_kbd.c
    theme.c
    freq_strip.c
    freq_scale.c
    spot_marks.c
    band_plan.c
    cw_field.c
    paddle_keyer.c
    cluster_column.c
    sstv/decoder.c
    sstv/encoder.c
    sstv/vis.c
    sstv/linesync.c
    sstv/preview.c
    sstv/denoise.c
    wefax/decoder.c
    wefax/encoder.c
    navtex/decoder.cpp
    navtex/navtex_rx.cpp
    navtex/fftfilt.cxx
    navtex/dsc.c
    channels.c
    broadcast_db.c
    cJSON.c
    textarea_window.c cw_encoder.c buttons.cpp vol.cpp recorder.c
    cw_tune_ui.c adif.c qso_log.c scheduler.cpp
    dialog_wifi.c wifi.cpp btaudio.cpp bt_dev.c controls.cpp usb_devices.cpp
    knobs.cpp
    dialog_wspr.c app_frame.c update_check.c
    dialog_field.c
    dialog_bands.c
    dialog_snake.c
)

add_subdirectory(fonts)
add_subdirectory(ft8)
add_subdirectory(js8)
add_subdirectory(widgets)
add_subdirectory(params)
add_subdirectory(qth)
add_subdirectory(cluster)
add_subdirectory(field)
add_subdirectory(wspr)
add_subdirectory(cfg)
add_subdirectory(cw_decoder)

include_directories(${CMAKE_SYSROOT}/usr/include/ft8lib/)

set(THREADS_PREFER_PTHREAD_FLAG ON)

find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(deps REQUIRED IMPORTED_TARGET
    libnm dbus-glib-1 sqlite3 libpng libgps sndfile libudev libpulse libpulse-simple alsa libjpeg
)


target_link_libraries(${PROJECT_NAME} PRIVATE
PkgConfig::deps
FT8 QTH CLUSTER FIELD
WSPR
CW_DECODER
JS8
cfg
Threads::Threads
lvgl lvgl::drivers
aether_x6100_control
liquid
ft8
)

# string(TOUPPER "${CMAKE_BUILD_TYPE}" UPPERCASE_BUILD_TYPE)
if(UPPERCASE_BUILD_TYPE STREQUAL "DEBUG")
    target_compile_options(${PROJECT_NAME} PRIVATE -g -O0 -fno-omit-frame-pointer -fasynchronous-unwind-tables)
    target_link_options(${PROJECT_NAME} PRIVATE -g -rdynamic)


endif()

# target_compile_options(${PROJECT_NAME} PRIVATE -g -fno-omit-frame-pointer -fasynchronous-unwind-tables)
# target_link_options(${PROJECT_NAME} PRIVATE -g -rdynamic)

# target_compile_options(${PROJECT_NAME} PRIVATE -g -fsanitize=address -fsanitize=undefined  -fno-omit-frame-pointer -fno-sanitize-recover)
# target_link_options(${PROJECT_NAME} PRIVATE -g -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer -fno-sanitize-recover -static-libasan -static-libubsan)
