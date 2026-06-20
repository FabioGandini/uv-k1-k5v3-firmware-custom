

# IU2VTM firmware for the UV-K1 and UV-K5 V3


This repository is a fork of the [F4HWN custom firmware](https://github.com/armel/uv-k5-firmware-custom), who was a fork of [Egzumer custom firmware](https://github.com/egzumer/uv-k5-firmware-custom). It extends the work done for the UV-K5 V1, based on the DP32G030 MCU, and adapts it to the newer UV-K1 and UV-K5 V3 built around the PY32F071 MCU. It is the result of the joint work of [@muzkr](https://github.com/muzkr) and [@armel](https://github.com/armel).

It adds a text **messenger** (with callsign ID, delivery ACK, range-check ping and optional encryption), a second **spectrum** analyzer with channel-scan, **aircopy** and a few extras. **Every feature is explained simply (ELI5) in the section below.**

**Download:** grab the latest `iu2vtm.bin` from the [Releases](https://github.com/FabioGandini/iu2vtm-uv-k1-k5v3-messenger-double-spectrum/releases) page.


# About this fork (feature_messenger branch)

This is the **IU2VTM** build (based on F4HWN/armel v5.6.0). It turns the little
UV-K1 into a tiny text-messaging radio, adds a second kind of spectrum display,
lets you clone radios over the air, and a few quality-of-life extras.

Below, every feature is explained **simply (ELI5 style)** — what it is and how
to use it — followed by the full button map and credits.

> Quick note on radios: most of these features were adapted for the **BK4829**
> radio chip inside the UV-K1, which behaves a bit differently from the BK4819
> in the older UV-K5.

---

## ✉️ 1. Text Messenger — send little text messages

**What it is (simply):** your radio can send and receive short text messages
(up to ~30 characters), like a walkie-talkie that can also "SMS". Two radios on
the **same frequency** with the messenger turned on can chat.

**How to use it:**
- Open the Messenger screen (assign it to a key, or use the programmable-key
  action **MESSENGER**).
- Type with the keypad like an old phone (**T9**): press a number key, and tap
  again to cycle through its letters. To type two letters on the **same** key,
  type the first, **wait ~1 second** (the cursor jumps forward), then type the
  next.
- **MENU** sends the message. The other radio shows it on its screen.

**Important:** for two radios to understand each other they must use the **same
"modulation" (speed)** and the **same frequency**.

## 🐢🐇 2. Modulation / speed (FSK450 / FSK700 / AFSK1200)

**What it is (simply):** the "how fast we talk" setting. Slower = more reliable
when the signal is weak; faster = quicker but needs a better signal.

- **FSK450** — slowest, most robust (bad conditions)
- **FSK700** — medium
- **AFSK1200** — fastest, uses the radio chip's built-in 1200-baud modem (good
  conditions). On the UV-K1 this mode is tuned to use the rock-solid factory
  "aircopy" radio settings, so it is the most reliable choice between two UV-K1.

**Both radios must be set to the same one** (menu: **MsgMod**). This is also how
a UV-K1 talks to a kamilsss655 UV-K5: pick the same modulation on both.

## ✅ 3. Delivery confirmation (ACK)

**What it is (simply):** a "read receipt". When you send a message and the other
radio received it, your screen shows a **`+`**. Turn it on with **MsgACK**.

## 🪪 4. Your callsign on every message (stay legal)

**What it is (simply):** ham-radio rules say you must say **who you are** on the
air. This feature automatically sticks your callsign in front of every message,
so a message becomes `IU2VTM01:hello`.

- The callsign is up to **8 characters** (e.g. 6 for your callsign + 2 to tell
  your radios apart: `IU2VTM01`, `IU2VTM02`...).
- **Set it on the radio:** in the Messenger screen, **hold DOWN** → the title
  changes to "Set Callsign" → type it → **MENU** to save (**EXIT** cancels). It
  is remembered after a reboot.
- **Or set it from a PC:** in the included CHIRP driver, field "Station callsign
  prefix".
- Leave it empty to turn the prefix off.

## 📡 5. Ping / Range check — "who can hear me, and how far?"

**What it is (simply):** like a sonar "ping". You send a ping, and every other
UV-K1 (with this firmware) that hears it answers automatically. For each answer
you see **who replied**, an estimated **distance**, and the **signal strength**:

```
IU2VTM02 1.5km -95
```

**How to use it:** in the Messenger screen, **hold UP** to send a ping. Replies
appear in the message list.

> ⚠️ **The distance is a rough estimate.** The radio has no GPS, so it guesses
> the distance from the signal strength using a simple radio-physics model.
> Walls, hills, trees and antennas all affect it, so treat it as a ballpark, not
> a GPS reading. It can be calibrated (see `MSG_DIST_REF_RSSI` /
> `MSG_DIST_DB_PER_2X` in `App/app/messenger.c`).
>
> Ping only works **between UV-K1 radios running this firmware** (a stock UV-K5
> doesn't know how to answer).

## 🔒 6. Encryption (ChaCha20) — scramble your messages

**What it is (simply):** locks your messages with a password so only radios with
the **same password** can read them. Turn on with **MsgEnc**; set the password
(**EncKey**) — only via CHIRP — and it must match on both radios.

> ⚠️ **Legal warning:** on **amateur (ham) radio bands, encryption is NOT
> allowed** (you may not hide the meaning of a transmission). Keep encryption
> **OFF** on ham frequencies and only send clear text there. The feature exists
> for study/experiments where it is permitted.

## 📈 7. Two spectrum analyzers (see the band)

**What it is (simply):** a "radio band visualizer" — bars showing where signals
are. This firmware has **two**:

- **F+5** — the F4HWN/Fagci **bandscope** (scans a frequency range).
- **F+7** — the kamilsss655 **spectrum with channel-scan mode**: open it while
  on a **memory channel** and it scans your saved channels instead of a
  frequency range, showing which ones are active. **KEY_4** toggles between all
  channels and your current scan list; **PTT** jumps to the strongest one.

## 🔁 8. Aircopy — clone one radio into another over the air

**What it is (simply):** copy all the settings/channels from one radio to
another **wirelessly**, no cable. Both radios must be close and on the same
frequency.

**How to use it:** **hold the SIDE2 button while turning the radio on** to enter
Aircopy mode. Works between UV-K1 radios (and other radios using the standard
Quansheng aircopy). It is a separate mode and does not interfere with the
messenger.

## 💻 9. CHIRP driver

A modified CHIRP driver (`chirp/iu2vtm.chirp.v5.5.0.messenger.py`) lets you set
the messenger options from a PC: receive on/off (**MsgRX**), ACK (**MsgACK**),
modulation (**MsgMod**), encryption (**MsgEnc**), the password (**EncKey**) and
your **callsign**. Load it in CHIRP, then "Download from radio".

---

## 🎛️ Messenger button map

| Button | What it does |
|---|---|
| **0–9** | type text (T9) |
| **`*`** | switch UPPER / lower / numbers |
| **F** (tap) | delete a character (backspace) |
| **F** (hold) | reset / clear the messenger |
| **UP / DOWN** (tap) | scroll the message history |
| **UP** (hold) | send a **ping** (range check) |
| **DOWN** (hold) | **set your callsign** (then MENU = save, EXIT = cancel) |
| **MENU** | send the message |
| **EXIT** | leave the messenger |

*(Tip: in the messenger, tapping a key does one thing, holding it does another.)*

---

All credit for the messenger, crypto, spectrum and aircopy code goes to their
original authors ([@joaquimorg](https://github.com/joaquimorg),
[@kamilsss655](https://github.com/kamilsss655), [@fagci](https://github.com/fagci),
[@egzumer](https://github.com/egzumer), [@armel](https://github.com/armel)); this
build only adapts and combines them for the UV-K1/PY32 platform and adds the
callsign, ping/range-check and distance-estimate features.

# A note for developers who intend to fork this project

This firmware is distributed under the Apache 2.0 License, carrying forward the original copyright of DualTachyon, whose work laid the foundation for the UV-K5 open-source ecosystem.
If you create a fork or a derived version, **we strongly encourage you to keep your work open source**.

Keeping your fork open:

- aligns with the intent and spirit of the Apache 2.0 License
- supports the amateur-radio and embedded-development community
- avoids unnecessary fragmentation
- allows others to study, audit and improve the firmware

It is also very much in line with the **ham spirit**: sharing knowledge, experimenting together and helping each other, rather than closing things off or claiming them as your own.

Maintaining an open-source fork is the best way to help build a healthy and sustainable ecosystem for everyone.

> [!WARNING]
> EN -  Use this firmware at your own risk. There is absolutely no guarantee that it will work in any way shape or form on your radio(s), it may even brick your radio(s), in which case, you'd need to buy another radio.
Anyway, have fun.




Special thanks to Jean-Cyrille F6IWW (3 times), Fabrice 14RC123, David F4BPP, Olivier 14RC206, Frédéric F4ESO, Stéphane F5LGW (2 times), Jorge Ornelas (4 times), Laurent F4AXK, Christophe Morel, Clayton W0LED, Pierre Antoine F6FWB, Jean-Claude 14FRS3306, Thierry F4GVO, Eric F1NOU, PricelessToolkit, Ady M6NYJ, Tom McGovern (4 times), Joseph Roth, Pierre-Yves Colin, Frank DJ7FG, Marcel Testaz, Brian Frobisher, Yannick F4JFO, Paolo Bussola, Dirk DL8DF, Levente Szőke (2 times), Bernard-Michel Herrera, Jérôme Saintespes, Paul Davies, RS (3 times), Johan F4WAT, Robert Wörle, Rafael Sundorf, Paul Harker, Peter Fintl, Pascal F4ICR (2 times), Mike DL2MF (3 times), Eric KI1C (2 times), Phil G0ELM, Jérôme Lambert, Eliot Vedel, Alfonso EA7KDF, Jean-François F1EVM, Robert DC1RDB (2 times), Ian KE2CHJ, Daryl VK3AWA, Roberto Brunelli, Robert Boardman, Stephen Oliver, Nicolas F4INE, William Bruno, Daniel OK2VLK, Tayler Chew, Peter DL7RFP, Philippe Kopp, Rune LA6YMA, Jeremy Luna, Steef Wagenaar (2 times), Zhuo BG7SGA, Jamie M0JLB, Antoine LIBERT, Vince K0DKR, Julia DF7JA, Ken 2E0UMK, Victor TI2SYS, Tobi DG9LAY, Deaglan K4DFQ, Catherine PALMER, Brian WA6JFK, Stéphane Hintzy, Roger F1HCN, Marcin Kusaj and Flavio Cottarelli for their [donations](https://www.paypal.com/paypalme/F4HWN). That’s so kind of them. Thanks so much 🙏🏻

## Table of Contents

* [My Features](#main-features)
* [Main Features from Egzumer](#main-features-from-egzumer)
* [Manual](#manual)
* [Compiling and Building from Docker](#compiling-and-Building-from-docker)
* [Flashing the Firmware with UVTools2](#flashing-the-firmware-with-uvtools2)
* [Credits](#credits)
* [Other sources of information](#other-sources-of-information)
* [License](#license)

## Main features and improvements from F4HWN:

* Fusion is now the reference edition of the project:
    * all-in-one firmware for UV-K1 and UV-K5 V3,
    * spectrum analyzer made by Fagci,
    * commercial FM radio support,
    * Vox and Aircopy support,
    * screenshots and K5Viewer support,
    * advanced RX audio profiles and Audio Scope,
    * first-responder oriented options,
    * small breakout game,
* improve default power settings level: 
    * Low1 to Low5 (<~20mW, ~125mW, ~250mW, ~500mW, ~1W), 
    * Mid ~2W, 
    * High ~5W,
    * User (see SetPwr),
* improve S-Meter (IARU Region 1 Technical Recommendation R.1 for VHF/UHF - [read more](https://hamwaves.com/decibel/en/)),
   * S-Meter (S0/S9) Level EEPROM settings that were introduced in the Egzumer firmware are now ignored and replaced by hardcoded values to comply with the IARU Recommendation.     
* improve bandscope (Spectrum Analyser):
    * add channel name,
    * add save of some spectrum parameters,
* improve UI: 
    * menu index is always visible, even if a menu is selected,
    * s-meter new design (Classic or Tiny), 
    * MAIN ONLY screen mode, 
    * DUAL and CROSS screen mode, 
    * RX blink on VFO RX, 
    * RX LED blink, 
    * Squelch level and Monitor,
    * Step value,
    * CTCSS or DCS value,
    * KeyLock message,
    * last RX,
    * move BatTxt menu from 34/63 to 30/63 (just after BatSave menu 29/63),
    * rename BackLt to BLTime,
    * rename BltTRX to BLTxRx,
    * improve memory channel input,
    * improve keyboard frequency input,
    * add percent and gauge to Air Copy,
    * improve audio bar,
    * add backlight fading,
    * add Audio Scope on TX,
    * and more...
* new menu entries and changes:
    * add SetPwr menu to set User power (<20mW, 125mW, 250mW, 500mW, 1W, 2W or 5W),
    * add SetPTT menu to set PTT mode (Classic or OnePush),
    * add SetTOT menu to set TOT alert (Off, Sound, Visual, All),
    * add SetCtr menu to set contrast (0 to 15),
    * add SetInv menu to set screen in invert mode (Off or On),
    * add SetEOT menu to set EOT (End Of Transmission) alert (Off, Sound, Visual, All),
    * add SetMet menu to set s-meter style (Classic or Tiny),
    * add SetLck menu to set what is locked (Keys or Keys + PTT),
    * add SetGUI menu to set font size on the VFO baseline (Classic or Tiny),
    * add SetRxA menu to select RX audio profiles,
    * add TXLock menu to open TX on channel,
    * add SetTmr menu to set RX and TX timers (Off or On),
    * add SetOff menu to set the delay before the transceiver goes into deep sleep (Off or 1 minute to 2 hours),
    * add SetNFM menu to set Narrow width (12.5kHz or 6.25kHz),
    * add SetVol menu to adjust RX audio volume,
    * add SetScn menu to set Scan mode
    * rename BatVol menu (52/63) to SysInf, which displays the firmware version in addition to the battery status,
    * improve PonMsg menu,
    * improve BackLt menu,
    * improve TxTOut menu,
    * improve ScnRev menu (CARRIER from 250ms to 20s, STOP, TIMEOUT from 5s to 2m)
    * improve KeyLck menu (OFF, delay from 15s to 10m)
    * add HAM CA F Lock band (for Canadian zone),
    * add PMR 446 F Lock band,
    * add FRS/GMRS/MURS F Lock band,
    * add SetNav hidden menu to select the navigation layout according to the radio model,
    * remove blink and SOS functionality, 
    * remove AM Fix menu (AM Fix is ENABLED by default),
    * add support of 3500mAh battery,
* improve status bar:
    * add SetPtt mode in status bar,
    * change font and bitmaps,
    * move USB icon to left of battery information,
    * add RX and TX timers,
* improve channel scanning:
    * support up to 24 scan lists,
    * each memory channel can be assigned to `OFF`, to one list (`01` to `24`), or to `ALL`,
    * `ALL` scans every channel except those set to `OFF`,
    * named scan lists are shown in the UI and status bar when available,
    * if the selected scan list is empty or invalid, the firmware automatically jumps to the next valid one,
    * very fast scan mode (around 150 freq/s),
    * frequencies exclusions,
* add resume mode on startup (scan, spectrum analyzer and FM radio),
* improve VFO persistence and restore behavior:
    * save the Squelch level adjusted with F + UP or F + DOWN,
    * restore the full VFO state on long press of EXIT,
* new actions:
    * RX MODE,
    * MAIN ONLY,
    * PTT, 
    * WIDE NARROW,
    * 1750Hz,
    * MUTE,
    * POWER HIGH,
    * REMOVE OFFSET,
    * BEAM,
* new key combinations:
    * add the F + UP or F + DOWN key combination to dynamically change the Squelch level,
    * add the F + F1 or F + F2 key combination to dynamically change the Step,
    * add F + 8 to quickly switch backlight between BLMin and BLMax on demand (this bypass BackLt strategy),
    * add F + 9 to return to BackLt strategy,
    * add long press on MENU, in * SCAN mode, to exclude the current memory channel,
    * add direct scan list selection while scanning with two digits (`00` = `ALL`, `01` to `24` = scan list).
* many fix:
    * squelch, 
    * s-meter,
    * DTMF overlaying, 
    * scan range limit,
    * clean display on startup,
    * no more PWM noise,
    * K5Viewer/serial key handling,
    * spectrum freeze on USB-C unplug,
    * Audio Scope behavior in OnePush mode and after DTMF/1750,
    * and more...
* enabled AIR COPY
* disabled ENABLE_DTMF_CALLING,
* disabled SCRAMBLER,
* remove 200Tx, 350Tx and 500Tx,
* unlock TX on all bands needs only to be repeat 3 times,
* code refactoring and many memory optimization,
* stream the live screen of the Quansheng K5 to K5Viewer and capture screenshots over a USB-to-Serial cable,
* and more...

## Main features from Egzumer:
* many of OneOfEleven mods:
   * AM fix, huge improvement in reception quality
   * long press buttons functions replicating F+ action
   * fast scanning
   * channel name editing in the menu
   * channel name + frequency display option
   * shortcut for scan-list assignment (long press `5 NOAA`)
   * scan-list toggle (long press `* Scan` while scanning)
   * configurable button function selectable from menu
   * battery percentage/voltage on status bar, selectable from menu
   * longer backlight times
   * mic bar
   * RSSI s-meter
   * more frequency steps
   * squelch more sensitive
* fagci spectrum analyzer (**F+5** to turn on)
* some other mods introduced by me:
   * SSB demodulation (adopted from fagci)
   * backlight dimming
   * battery voltage calibration from menu
   * better battery percentage calculation, selectable for 1600mAh or 2200mAh
   * more configurable button functions
   * long press MENU as another configurable button
   * better DCS/CTCSS scanning in the menu (`* SCAN` while in RX DCS/CTCSS menu item)
   * Piotr022 style s-meter
   * restore initial freq/channel when scanning stopped with EXIT, remember last found transmission with MENU button
   * reordered and renamed menu entries
   * LCD interference crash fix
   * many others...

 ## Manual

Up to date manual is available in the [Wiki section](https://github.com/armel/uv-k1-k5v3-firmware-custom/wiki)

## Radio performance

Please note that the Quansheng UV-Kx radios are not professional quality transceivers, their
performance is strictly limited. The RX front end has no track-tuned band pass filtering
at all, and so are wide band/wide open to any and all signals over a large frequency range.

Using the radio in high intensity RF environments will most likely make reception difficult,
especially in AM mode. The receiver simply does not have a great dynamic range, so stronger
signals can easily cause distortion, desensitization and poor AM audio.
This is fundamentally a hardware limitation: firmware can improve behavior at the margins, but
it cannot overcome the front-end design of the radio.
In practice, AM reception will degrade first and most severely, while FM reception is generally
more tolerant and should remain more usable.

But, they are nice toys for the price, fun to play with.

## Compiling and Building from Docker

This project provides a Docker-based build system to compile the Fusion firmware for the UV-K1 and UV-K5 V3. Everything is handled through the `compile-with-docker.sh` helper script.

The documented build output is generated inside `build/Fusion`, using the CMake presets defined in `CMakePresets.json`.

### Prerequisites

- Docker installed on your system
- Bash environment (Linux, macOS, WSL, Git Bash on Windows)

### Build Script Overview

The script `compile-with-docker.sh` performs the following actions:

1. Builds the Docker image (`uvk1-uvk5v3`) if it does not already exist.
2. Removes any previous `build` directory to ensure a clean configuration.
3. Runs CMake using the `Fusion` preset inside the Docker container.
4. Builds the firmware and outputs `.elf`, `.bin` and `.hex` files.

### Usage

```bash
./compile-with-docker.sh Fusion [extra CMake options]
```

### Documented Preset

- **Fusion**

### Examples

Build Fusion:

```bash
./compile-with-docker.sh Fusion
```

### Passing Additional CMake Options

You can pass extra configuration options after the preset name.  
These are forwarded directly to `cmake --preset` inside the container.

Examples:

```bash
./compile-with-docker.sh Fusion -DENABLE_SPECTRUM=ON
./compile-with-docker.sh Fusion -DENABLE_FEAT_F4HWN_GAME=ON -DENABLE_NOAA=ON
./compile-with-docker.sh Fusion -DSQL_TONE=600
```

### Notes

- The first run may take a few minutes while Docker builds the base image.
- Each build runs inside Docker, so your host environment remains clean.

## Flashing the Firmware with UVTools2

You can flash the UV-K5 V3 and UV-K1 directly from your web browser using the cross-platform WebSerial-based [UVTools2](https://armel.github.io/uvtools2/).

It works on Chrome, Chromium and Edge (desktop versions), and does not require installing any driver or software on your computer.

## Steps to flash the firmware

- Open UVTools2 in [flash](https://armel.github.io/uvtools2/?mode=flash) mode (or click the Flash Firmware tab).
- Connect your radio to your computer using a compatible USB programming cable (USB-C or Baofeng/Kenwood like double jack USB cable).
- Make sure your radio is in **DFU mode (flash mode)**.
- Select the firmware .bin file on your computer. 
- Click on `Flash Firmware`, then select the serial port associated with your radio.
- The progress bar will guide you through the flashing steps.

Once finished, your radio restart with the new firmware.

## Steps to dump or restore calibration data

[UVTools2](https://armel.github.io/uvtools2/) can also dump and restore calibration data, which is highly recommended. It’s best to create a dump right after installing F4HWN firmware, and to restore it before installing another firmware (or when returning to the stock firmware, for example).

### Dump

- Open UVTools2 in [dump](https://armel.github.io/uvtools2/?mode=dump) mode (or click the Dump Calib tab).
- Power on your radio in **normal mode**.
- Click `Dump Calibration Data`.

When the process is complete, click `Download calibration.dat` to save the file to your computer.

> [!NOTE]
> A good practice is to rename your calibration file using the serial number of your radio, which you can find on the label on the back of the device once you remove the battery. This helps avoid mixing up calibration files when you own multiple units.

### Restore

- Open UVTools2 in [restore](https://armel.github.io/uvtools2/?mode=restore) mode (or click the Restore Calib tab).
- Power on your radio in **normal mode**.
- Select your calibration.dat file on your computer.

Click `Restore Calibration Data` and wait until the process fully completes.

## Other sources of information

- [k1-teardown](https://github.com/armel/k1-teardown) 

## Credits

Many thanks to various people:

* [Muzkr](https://github.com/muzkr)
* [Mrkusypl](https://github.com/mrkusypl)
* [Andrej](https://github.com/Tunas1337)
* [Egzumer](https://github.com/egzumer)
* [OneOfEleven](https://github.com/OneOfEleven)
* [DualTachyon](https://github.com/DualTachyon)
* [Mikhail](https://github.com/fagci)
* [Manuel](https://github.com/manujedi)
* @wagner
* @Lohtse Shar
* [@Matoz](https://github.com/spm81)
* @Davide
* @Ismo OH2FTG
* [OneOfEleven](https://github.com/OneOfEleven)
* @d1ced95
* and others I forget

## License

Copyright 2023 Dual Tachyon
https://github.com/DualTachyon

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software

    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
