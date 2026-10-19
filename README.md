<h1 align="center">X6100 custom firmware</h1>

<p align="center"><i>by Giovanni Ricciardi IU1WXY</i> · <a href="https://x6100-link.it"><b>x6100-link.it</b></a></p>

<p align="center">Heartfelt thanks to <b>Oleg Belousov R1CBU</b>, <b>Georgy Dyuldin R2RFE</b>,
<b>1KO125</b> by <b>Hany El Imam / TheMurusTeam</b> and the whole <b>X6100 community</b>.<br>
Thanks to <b>Fabio IN3FOB</b> for the beta testing, but above all
to <b>Leo CT7AUE</b>, <b>Angelo IU2VWK</b>, <b>Gianluca IW5DUA</b> and <b>Emilio IU8QTM</b>
for their neverending patience, time and advices.</p>

A complete microSD firmware for the Xiegu X6100: every digital mode on the
radio (FT8, FT4, FT2, JS8, WSPR, PSK31, PSK63, Olivia, JTTY, RTTY, SSTV,
WeFax, NavTex), a CW decoder and keyboard, DX cluster spots on the waterfall,
Field Ops for SOTA and POTA activations, themes, Bluetooth headphones, CAT and
keyboards, and remote control from the X6100 Link app or a browser.

This repository holds the releases only: the microSD image, the GPL sources
of each version, the terms of use and the third-party licences.

## Install

1. Download `X6100-IU1WXY-v<version>.img` from the
   **[latest release](https://github.com/IU1WXY/X6100-IU1WXY/releases/latest)**
   and check it against its `.sha256` file.
2. Write it to a microSD card with [balenaEtcher](https://etcher.balena.io/) or Rufus.
3. Put the card in the radio and switch on.

Settings and logs live on the card's DATA partition. Writing a new image
erases them: copy them to your PC first and back after the first start (the
manual lists the files). Updates of the radio apps that need no new image
are installed from the radio itself, in Settings > Update.

## Manual

Every app and setting, the web pages, the CAT commands and the files on the
card: **[x6100-link.it/guide.html](https://x6100-link.it/guide.html)**
(in Italian: [x6100-link.it/guida.html](https://x6100-link.it/guida.html)).

Problems and ideas: [x6100-link.it/report.php](https://x6100-link.it/report.php).

## Releases

All versions, with their release notes, are on the
[Releases page](https://github.com/IU1WXY/X6100-IU1WXY/releases).

## Licence

From version 0.128 the IU1WXY software (the radio interface program, the web
configurator, their scripts and data) is Copyright (c) 2026 Giovanni Ricciardi
IU1WXY, all rights reserved, and is used under the [terms of use](LICENSE).
In short: you may install and use it free of charge on your X6100, keep copies
and pass the unmodified image to other X6100 owners free of charge. You may not
sell it, modify it or present it as your own work. It comes without any
warranty. Versions up to 0.127 stay under the licences they were published with.

The image also holds software by other authors under their own licences
(Linux, BusyBox, U-Boot, the radio control library by Georgy Dyuldin, LVGL,
ft8_lib, liquid-dsp, the JS8, JTTY and WSPR helper programs, the fonts and
more). Those licences apply in full: the list and the texts are in
[THIRD_PARTY_LICENSES](THIRD_PARTY_LICENSES), and on the radio in
`/usr/share/x6100/licenses/`.

## GPL sources

Each release has the file `X6100-IU1WXY-v<version>-gpl-sources.tar.gz` among
its assets: the source of the GPL and LGPL parts of that image (the helper
programs x6100-js8d, x6100-jttyd and x6100-wsprd, the LGPL files of the
interface, the kernel configuration and patches, the WiFi driver patches, the
buildroot tree and its legal-info). Its README says how each part maps to the
image. The same source can be requested at info@x6100-link.it.

## Credits

**Giovanni Ricciardi IU1WXY** designed and wrote this firmware: the rewrite of
the GUI and the theme system, the X6100 Link app and the remote control,
Field Ops and Bands, the DX Cluster, WSPR, SSTV TX, RTTY and WeFax TX, the CW
Keyboard, the integration of JS8, SSTV, WeFax and NavTex, Bluetooth CAT,
headphones and keyboards, the wfview server, and the microSD image.

Standing on the work of: the **R1CBU** GUI by Oleg Belousov R1CBU and Georgy
Dyuldin R2RFE ([gdyuldin/x6100_gui](https://github.com/gdyuldin/x6100_gui)),
**1KO125** by Hany El Imam / TheMurusTeam (WeFax, NavTex, SSTV decoders),
SP2KMO and **randal007** (openx6100: Bluetooth devices, PSK31 and Olivia; the
X6100 JS8 app), **JS8** on the JS8Call engine (Jordan Sherer KN4CRD,
JS8Call-improved), **wsprd** from WSJT-X (K1JT, K9AN), **JTTY** from WSJT-X
3.2 (K1JT and the WSJT-X team), the radio control library and the
AetherX6100Buildroot image by Georgy Dyuldin, **OH1W** (the idea behind the
birdie calibration), and the **EiBi**
broadcast list by Eike Bierwirth. Libraries: LVGL, cJSON, liquid-dsp, PFFFT,
ft8_lib, libpng, bottle, PulseAudio, BlueZ. Fonts: Roboto, DejaVu, Share Tech,
Arial. Skimmer spots come from the Reverse Beacon Network and its skimmer
operators. Hardware: Xiegu.

## Privacy

What the radio sends, to whom and why is in the privacy notice:
[x6100-link.it/privacy.php](https://x6100-link.it/privacy.php).
