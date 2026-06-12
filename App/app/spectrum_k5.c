/* Original work Copyright 2023 fagci
 * https://github.com/fagci
 *
 * Modified work Copyright 2024 kamilsss655
 * https://github.com/kamilsss655
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *     Unless required by applicable law or agreed to in writing, software
 *     distributed under the License is distributed on an "AS IS" BASIS,
 *     WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *     See the License for the specific language governing permissions and
 *     limitations under the License.
 */

// Port of the kamilsss655 (fagci-derived) spectrum analyzer to the
// armel UV-K1 firmware. Differences from the K5 original:
// - channel scan mode reads each channel frequency from flash on the
//   fly (the K5 keeps a 200-channel RAM cache, the K1 has 1024 flash
//   channels and no RAM to spare); the scan list is capped at
//   MAX_SCAN_CHANNELS valid channels, and the scan-list filter toggles
//   between ALL and the radio's current scan list setting
// - every symbol is static: the F4HWN bandscope (app/spectrum.c) is
//   compiled in the same image and shares most fagci symbol names
// - K5-only helpers (RX_OFFSET, RX_AGC, SETTINGS_SetVfoFrequency,
//   GetSLevelAttributes, DrawVLine) inlined or re-implemented below

#ifdef ENABLE_SPECTRUM_K5

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "app/spectrum_k5.h"

#include "audio.h"
#include "bitmaps.h"
#include "board.h"
#include "driver/backlight.h"
#include "driver/bk4819-regs.h"
#include "driver/bk4819.h"
#include "driver/gpio.h"
#include "driver/keyboard.h"
#include "driver/st7565.h"
#include "driver/system.h"
#include "driver/systick.h"
#include "external/printf/printf.h"
#include "font.h"
#include "frequencies.h"
#include "helper/battery.h"
#include "misc.h"
#include "radio.h"
#include "settings.h"
#include "ui/helper.h"

#ifdef ENABLE_SCAN_RANGES
#include "app/chFrScanner.h"
#endif

// ---------------------------------------------------------------- types

typedef enum State {
  SPECTRUM,
  FREQ_INPUT,
  STILL,
} State;

typedef enum Mode {
  FREQUENCY_MODE,
  CHANNEL_MODE,
  SCAN_RANGE_MODE,
} Mode;

typedef enum StepsCount {
  STEPS_128,
  STEPS_64,
  STEPS_32,
  STEPS_16,
} StepsCount;

typedef enum ScanStep {
  S_STEP_0_01kHz,
  S_STEP_0_1kHz,
  S_STEP_0_5kHz,
  S_STEP_1_0kHz,

  S_STEP_2_5kHz,
  S_STEP_5_0kHz,
  S_STEP_6_25kHz,
  S_STEP_8_33kHz,
  S_STEP_10_0kHz,
  S_STEP_12_5kHz,
  S_STEP_25_0kHz,
  S_STEP_100_0kHz,
} ScanStep;

typedef struct SpectrumSettings {
  uint32_t frequencyChangeStep;
  StepsCount stepsCount;
  ScanStep scanStepIndex;
  uint16_t scanDelay;
  uint16_t rssiTriggerLevel;
  BK4819_FilterBandwidth_t bw;
  BK4819_FilterBandwidth_t listenBw;
  int dbMin;
  int dbMax;
  ModulationMode_t modulationType;
  bool backlightState;
} SpectrumSettings;

typedef struct KeyboardState {
  KEY_Code_t current;
  KEY_Code_t prev;
  uint8_t counter;
} KeyboardState;

typedef struct ScanInfo {
  uint16_t rssi, rssiMin, rssiMax;
  uint16_t i, iPeak;
  uint32_t f, fPeak;
  uint16_t scanStep;
  uint16_t measurementsCount;
} ScanInfo;

typedef struct PeakInfo {
  uint16_t t;
  uint16_t rssi;
  uint32_t f;
  uint16_t i;
} PeakInfo;

// RegisterSpec comes from driver/bk4819-regs.h

typedef struct {
  uint8_t sLevel;
  uint8_t over;
  int dBmRssi;
} sLevelAttributes;

// --------------------------------------------------------------- tables

static const uint8_t DrawingEndY = 40;

static const uint16_t scanStepValues[] = {
    1,   10,  50,  100,

    250, 500, 625, 833, 1000, 1250, 2500, 10000,
};

static const uint16_t scanStepBWRegValues[] = {
    //     RX  RXw TX  BW
    // 0b0 000 000 001 01 1000
    // 1
    0b0000000001011000, // 6.25
    // 10
    0b0000000001011000, // 6.25
    // 50
    0b0000000001011000, // 6.25
    // 100
    0b0000000001011000, // 6.25
    // 250
    0b0000000001011000, // 6.25
    // 500
    0b0010010001011000, // 6.25
    // 625
    0b0100100001011000, // 6.25
    // 833
    0b0110110001001000, // 6.25
    // 1000
    0b0110110001001000, // 6.25
    // 1250
    0b0111111100001000, // 6.25
    // 2500
    0b0011011000101000, // 25
    // 10000
    0b0011011000101000, // 25
};

#define F_MAX frequencyBandTable[BAND_N_ELEM - 1].upper
#define MAX_ATTENUATION 160
#define ATTENUATE_STEP 10
#define SQUELCH_OFF_DELAY 10
// 30 MHz, in 10Hz units
#define HF_FREQUENCY 3000000
// same cap as the K5 RAM cache; rssiHistory compresses >128 anyway
#define MAX_SCAN_CHANNELS 200
#define UHF_NOISE_FLOOR 40

static const uint16_t RSSI_MAX_VALUE = 65535;

// -------------------------------------------------------------- statics

static Mode appMode;
static bool gTailFound;
static bool isBlacklistApplied;
static bool isNormalizationApplied;
static bool isAttenuationApplied;
static uint8_t gainOffset[129];
static uint8_t attenuationOffset[129];

// channel scan mode
static uint16_t scanChannel[MAX_SCAN_CHANNELS];
static uint8_t scanChannelsCount;
static bool scanListAll = true;
static uint32_t lastPeakFrequency;
static bool isKnownChannel = false;
static int peakChannel = -1;
static char channelName[12];
static void LoadValidMemoryChannels(void);
static void AutoAdjustResolution(void);
static void LookupChannelInfo(void);

static uint16_t R30, R37, R3D, R43, R47, R48, R7E, R02, R3F;
static uint32_t initialFreq;
static char String[32];

static bool isInitialized = false;
static bool isListening = true;
static bool monitorMode = false;
static bool redrawStatus = true;
static bool redrawScreen = false;
static bool newScanStart = true;
static bool preventKeypress = true;
static bool audioState = true;

static State currentState = SPECTRUM, previousState = SPECTRUM;

static PeakInfo peak;
static ScanInfo scanInfo;
static KeyboardState kbd = {KEY_INVALID, KEY_INVALID, 0};

#ifdef ENABLE_SCAN_RANGES
#define BLACKLIST_SIZE 200
static uint16_t blacklistFreqs[BLACKLIST_SIZE];
static uint8_t blacklistFreqsIdx;
static bool IsBlacklisted(uint16_t idx);
#endif
static uint8_t CurrentScanIndex(void);

static const char *bwNames[4] = {"  25k", "12.5k", "6.25k", "   AM"};
static const uint8_t modulationTypeTuneSteps[] = {100, 50, 10};

static SpectrumSettings settings = {
    .stepsCount = STEPS_128,
    .scanStepIndex = S_STEP_25_0kHz,
    .frequencyChangeStep = 80000,
    .rssiTriggerLevel = 150,
    .backlightState = true,
    .bw = BK4819_FILTER_BW_WIDE,
    .listenBw = BK4819_FILTER_BW_WIDE,
    .modulationType = MODULATION_FM,
    .dbMin = -130,
    .dbMax = -50,
};

static uint32_t fMeasure = 0;
static uint32_t currentFreq, tempFreq;
static uint16_t rssiHistory[128];

static uint8_t freqInputIndex = 0;
static uint8_t freqInputDotIndex = 0;
static KEY_Code_t freqInputArr[10];
static char freqInputString[11];

static uint8_t menuState = 0;
static uint16_t listenT = 0;

static RegisterSpec registerSpecs[] = {
    {},
    {"LNAs", BK4819_REG_13, 8, 0b11, 1},
    {"LNA", BK4819_REG_13, 5, 0b111, 1},
    {"PGA", BK4819_REG_13, 0, 0b111, 1},
    {"MIX", BK4819_REG_13, 3, 0b11, 1},
};

static uint16_t statuslineUpdateTimer = 0;

static void RelaunchScan(void);
static void ResetInterrupts(void);
static void ToggleNormalizeRssi(bool on);
static void ToggleScanList(void);
static void ResetModifiers(void);

// -------------------------------------------------- K5 helpers, inlined

// kamils' helper/measurements: rssi (half-dB units, 160dB offset) to dBm
static int Rssi2DBm(const uint16_t rssi) { return (rssi >> 1) - 160; }

static sLevelAttributes GetSLevelAttributes(const int16_t rssi,
                                            const uint32_t frequency) {
  sLevelAttributes att;
  int16_t s0_dBm = -130;

  // all S1 on max gain, no antenna
  static const int8_t dBmCorrTable[7] = {
      -5,  // band 1
      -38, // band 2
      -37, // band 3
      -20, // band 4
      -23, // band 5
      -23, // band 6
      -16  // band 7
  };

  // use UHF/VHF S-table for bands above HF
  if (frequency > HF_FREQUENCY)
    s0_dBm -= 20;

  att.dBmRssi = Rssi2DBm(rssi) + dBmCorrTable[FREQUENCY_GetBand(frequency)];
  att.sLevel = MIN(MAX((att.dBmRssi - s0_dBm) / 6, 0), 9);
  att.over = MIN(MAX(att.dBmRssi - (s0_dBm + 9 * 6), 0), 99);

  return att;
}

static uint32_t RX_freq_min(void) { return frequencyBandTable[0].lower; }

static void DrawVLine(int sy, int ey, int nx, bool fill) {
  for (int i = sy; i <= ey; i++) {
    if (i < 56 && nx < 128) {
      PutPixel(nx, i, fill);
    }
  }
}

static uint8_t GetStepIdxFromStepFrequency(uint16_t stepFrequency) {
  for (uint8_t i = 0; i < STEP_N_ELEM; i++)
    if (gStepFrequencyTable[i] == stepFrequency)
      return i;
  return STEP_25kHz;
}

static uint8_t GetScanStepFromStepFrequency(uint16_t stepFrequency) {
  for (uint8_t i = 0; i < ARRAY_SIZE(scanStepValues); i++)
    if (scanStepValues[i] == stepFrequency)
      return i;
  return S_STEP_25_0kHz;
}

// kamils' SETTINGS_SetVfoFrequency, minus the K5 RX_OFFSET handling
static void SetVfoFrequency(uint32_t frequency) {
  const uint8_t Vfo = gEeprom.TX_VFO;

  // clamp the frequency entered to some valid value
  if (frequency < RX_freq_min()) {
    frequency = RX_freq_min();
  } else if (frequency >= BX4819_band1.upper &&
             frequency < BX4819_band2.lower) {
    const uint32_t center = (BX4819_band1.upper + BX4819_band2.lower) / 2;
    frequency = (frequency < center) ? BX4819_band1.upper : BX4819_band2.lower;
  } else if (frequency > F_MAX) {
    frequency = F_MAX;
  }

  {
    const FREQUENCY_Band_t band = FREQUENCY_GetBand(frequency);

    if (gTxVfo->Band != band) {
      gTxVfo->Band = band;
      gEeprom.ScreenChannel[Vfo] = band + FREQ_CHANNEL_FIRST;
      gEeprom.FreqChannel[Vfo] = band + FREQ_CHANNEL_FIRST;

      SETTINGS_SaveVfoIndices();

      RADIO_ConfigureChannel(Vfo, VFO_CONFIGURE_RELOAD);
    }

    gTxVfo->StepFrequency = gStepFrequencyTable[gTxVfo->STEP_SETTING];

    frequency = FREQUENCY_RoundToStep(frequency, gTxVfo->StepFrequency);

    if (frequency >= BX4819_band1.upper && frequency < BX4819_band2.lower) {
      // clamp the frequency to the limit
      const uint32_t center = (BX4819_band1.upper + BX4819_band2.lower) / 2;
      frequency = (frequency < center)
                      ? BX4819_band1.upper - gTxVfo->StepFrequency
                      : BX4819_band2.lower;
    }

    gTxVfo->freq_config_RX.Frequency = frequency;
  }
}

// --------------------------------------------------------------- module

static uint16_t GetRegMenuValue(uint8_t st) {
  RegisterSpec s = registerSpecs[st];
  return (BK4819_ReadRegister(s.num) >> s.offset) & s.mask;
}

static void SetRegMenuValue(uint8_t st, bool add) {
  uint16_t v = GetRegMenuValue(st);
  RegisterSpec s = registerSpecs[st];

  uint16_t reg = BK4819_ReadRegister(s.num);
  if (add && v <= s.mask - s.inc) {
    v += s.inc;
  } else if (!add && v >= 0 + s.inc) {
    v -= s.inc;
  }
  reg &= ~(s.mask << s.offset);
  BK4819_WriteRegister(s.num, reg | (v << s.offset));
  redrawScreen = true;
}

static int clamp(int v, int min, int max) {
  return v <= min ? min : (v >= max ? max : v);
}

static uint8_t my_abs(signed v) { return v > 0 ? v : -v; }

static void SetState(State state) {
  previousState = currentState;
  currentState = state;
  redrawScreen = true;
  redrawStatus = true;
}

// Radio functions

static void ToggleAFBit(bool on) {
  uint16_t reg = BK4819_ReadRegister(BK4819_REG_47);
  reg &= ~(1 << 8);
  if (on)
    reg |= on << 8;
  BK4819_WriteRegister(BK4819_REG_47, reg);
}

static void BackupRegisters(void) {
  R30 = BK4819_ReadRegister(BK4819_REG_30);
  R37 = BK4819_ReadRegister(BK4819_REG_37);
  R3D = BK4819_ReadRegister(BK4819_REG_3D);
  R43 = BK4819_ReadRegister(BK4819_REG_43);
  R47 = BK4819_ReadRegister(BK4819_REG_47);
  R48 = BK4819_ReadRegister(BK4819_REG_48);
  R7E = BK4819_ReadRegister(BK4819_REG_7E);
  R02 = BK4819_ReadRegister(BK4819_REG_02);
  R3F = BK4819_ReadRegister(BK4819_REG_3F);
}

static void RestoreRegisters(void) {
  BK4819_WriteRegister(BK4819_REG_30, R30);
  BK4819_WriteRegister(BK4819_REG_37, R37);
  BK4819_WriteRegister(BK4819_REG_3D, R3D);
  BK4819_WriteRegister(BK4819_REG_43, R43);
  BK4819_WriteRegister(BK4819_REG_47, R47);
  BK4819_WriteRegister(BK4819_REG_48, R48);
  BK4819_WriteRegister(BK4819_REG_7E, R7E);
  BK4819_WriteRegister(BK4819_REG_02, R02);
  BK4819_WriteRegister(BK4819_REG_3F, R3F);
}

static void ToggleAFDAC(bool on) {
  uint32_t Reg = BK4819_ReadRegister(BK4819_REG_30);
  Reg &= ~(1 << 9);
  if (on)
    Reg |= (1 << 9);
  BK4819_WriteRegister(BK4819_REG_30, Reg);
}

static void SetF(uint32_t f) {
  fMeasure = f;
  BK4819_SetFrequency(fMeasure);
  BK4819_PickRXFilterPathBasedOnFrequency(fMeasure);
  uint16_t reg = BK4819_ReadRegister(BK4819_REG_30);
  BK4819_WriteRegister(BK4819_REG_30, 0);
  BK4819_WriteRegister(BK4819_REG_30, reg);
}

// Spectrum related

static bool IsPeakOverLevel(void) {
  return peak.rssi >= settings.rssiTriggerLevel;
}

static void ResetInterrupts(void) {
  // disable interrupts
  BK4819_WriteRegister(BK4819_REG_3F, 0);
  // reset the interrupt
  BK4819_WriteRegister(BK4819_REG_02, 0);
}

static void CheckIfTailFound(void) {
  uint16_t interrupt_status_bits;
  // if interrupt waiting to be handled
  if (BK4819_ReadRegister(BK4819_REG_0C) & 1u) {
    // reset the interrupt
    BK4819_WriteRegister(BK4819_REG_02, 0);
    // fetch the interrupt status bits
    interrupt_status_bits = BK4819_ReadRegister(BK4819_REG_02);
    // if tail found interrupt
    if (interrupt_status_bits & BK4819_REG_02_MASK_CxCSS_TAIL) {
      gTailFound = true;
      listenT = 0;
      ResetInterrupts();
    }
  }
}

static void ResetPeak(void) {
  peak.t = 0;
  peak.rssi = 0;
}

static bool IsCenterMode(void) {
  return settings.scanStepIndex < S_STEP_2_5kHz;
}

// scan step in 0.01khz
static uint16_t GetScanStep(void) {
  return scanStepValues[settings.scanStepIndex];
}

static uint16_t GetStepsCount(void) {
  if (appMode == CHANNEL_MODE) {
    return scanChannelsCount;
  }
#ifdef ENABLE_SCAN_RANGES
  if (appMode == SCAN_RANGE_MODE) {
    return (gScanRangeStop - gScanRangeStart) / GetScanStep();
  }
#endif
  return 128 >> settings.stepsCount;
}

static uint32_t GetBW(void) { return GetStepsCount() * GetScanStep(); }
static uint32_t GetFStart(void) {
  return IsCenterMode() ? currentFreq - (GetBW() >> 1) : currentFreq;
}

static uint32_t GetFEnd(void) { return currentFreq + GetBW(); }

static void TuneToPeak(void) {
  scanInfo.f = peak.f;
  scanInfo.rssi = peak.rssi;
  scanInfo.i = peak.i;
  SetF(scanInfo.f);
}

static void ExitAndCopyToVfo(void) {
  RestoreRegisters();

  if (appMode == CHANNEL_MODE && peak.i >= 1 &&
      peak.i <= scanChannelsCount) {
    // jump to the memory channel of the current peak
    gEeprom.MrChannel[gEeprom.TX_VFO] = scanChannel[peak.i - 1];
    gEeprom.ScreenChannel[gEeprom.TX_VFO] = scanChannel[peak.i - 1];

    gRequestSaveVFO = true;
    gVfoConfigureMode = VFO_CONFIGURE_RELOAD;
  } else {
    gTxVfo->STEP_SETTING = GetStepIdxFromStepFrequency(GetScanStep());
    gTxVfo->Modulation = settings.modulationType;
    gTxVfo->CHANNEL_BANDWIDTH = settings.listenBw;

    SetVfoFrequency(peak.f);

    gRequestSaveChannel = 1;
  }

  // Additional delay to debounce keys
  SYSTEM_DelayMs(200);

  isInitialized = false;
}

static void DeInitSpectrum(void) {
  SetF(initialFreq);
  RestoreRegisters();
  gVfoConfigureMode = VFO_CONFIGURE;
  isInitialized = false;
}

static uint8_t GetBWRegValueForScan(void) {
  return scanStepBWRegValues[settings.scanStepIndex];
}

static uint16_t GetRssi(void) {
  uint16_t rssi;
  // testing resolution to sticky squelch issue
  while ((BK4819_ReadRegister(0x63) & 0b11111111) >= 255) {
    SYSTICK_DelayUs(100);
  }
  rssi = BK4819_GetRSSI();

  if (appMode == CHANNEL_MODE && FREQUENCY_GetBand(fMeasure) > BAND4_174MHz) {
    // Increase perceived RSSI for UHF bands to imitate radio squelch
    rssi += UHF_NOISE_FLOOR;
  }

  rssi += gainOffset[CurrentScanIndex()];
  rssi -= attenuationOffset[CurrentScanIndex()];

  return rssi;
}

static void ToggleAudio(bool on) {
  if (on == audioState) {
    return;
  }
  audioState = on;
  if (on) {
    AUDIO_AudioPathOn();
  } else {
    AUDIO_AudioPathOff();
  }
}

static void ToggleRX(bool on) {
  isListening = on;

  // turn on green led only if screen brightness is over 7
  if (gEeprom.BACKLIGHT_MAX > 7)
    BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, on);

  ToggleAudio(on);
  ToggleAFDAC(on);
  ToggleAFBit(on);

  if (on) {
    listenT = SQUELCH_OFF_DELAY;
    BK4819_SetFilterBandwidth(settings.listenBw, false);

    gTailFound = false;

    // turn on CSS tail found interrupt
    BK4819_WriteRegister(BK4819_REG_3F, BK4819_REG_02_MASK_CxCSS_TAIL);
  } else {
    if (appMode != CHANNEL_MODE)
      BK4819_WriteRegister(BK4819_REG_43, GetBWRegValueForScan());
  }
}

// Scan info

static void ResetScanStats(void) {
  scanInfo.rssi = 0;
  scanInfo.rssiMax = 0;
  scanInfo.iPeak = 0;
  scanInfo.fPeak = 0;
}

static void InitScan(void) {
  ResetScanStats();
  scanInfo.i = 0;
  scanInfo.f = GetFStart();

  scanInfo.scanStep = GetScanStep();
  scanInfo.measurementsCount = GetStepsCount();
  // prevents phantom channel bar
  if (appMode == CHANNEL_MODE)
    scanInfo.measurementsCount++;
}

// resets modifiers like blacklist, attenuation, normalization
static void ResetModifiers(void) {
  for (int i = 0; i < 128; ++i) {
    if (rssiHistory[i] == RSSI_MAX_VALUE)
      rssiHistory[i] = 0;
  }
#ifdef ENABLE_SCAN_RANGES
  memset(blacklistFreqs, 0, sizeof(blacklistFreqs));
  blacklistFreqsIdx = 0;
#endif
  if (appMode == CHANNEL_MODE) {
    LoadValidMemoryChannels();
    AutoAdjustResolution();
  }
  ToggleNormalizeRssi(false);
  memset(attenuationOffset, 0, sizeof(attenuationOffset));
  isAttenuationApplied = false;
  isBlacklistApplied = false;
  RelaunchScan();
}

static void RelaunchScan(void) {
  InitScan();
  ResetPeak();
  ToggleRX(false);
  preventKeypress = true;
  scanInfo.rssiMin = RSSI_MAX_VALUE;
}

static void UpdateScanInfo(void) {
  if (scanInfo.rssi > scanInfo.rssiMax) {
    scanInfo.rssiMax = scanInfo.rssi;
    scanInfo.fPeak = scanInfo.f;
    scanInfo.iPeak = scanInfo.i;
  }
  // add attenuation offset to prevent noise floor lowering when
  // attenuated rx is over; essentially we measure non-attenuated lowest
  // rssi
  if (scanInfo.rssi + attenuationOffset[CurrentScanIndex()] <
      scanInfo.rssiMin) {
    scanInfo.rssiMin = scanInfo.rssi;
    settings.dbMin = Rssi2DBm(scanInfo.rssiMin);
    redrawStatus = true;
  }
}

static void AutoTriggerLevel(void) {
  if (settings.rssiTriggerLevel == RSSI_MAX_VALUE) {
    settings.rssiTriggerLevel = clamp(scanInfo.rssiMax + 8, 0, RSSI_MAX_VALUE);
  }
}

static void UpdatePeakInfoForce(void) {
  peak.t = 0;
  peak.rssi = scanInfo.rssiMax;
  peak.f = scanInfo.fPeak;
  peak.i = scanInfo.iPeak;
  LookupChannelInfo();
  AutoTriggerLevel();
}

static void UpdatePeakInfo(void) {
  if (peak.f == 0 || peak.t >= 1024 || peak.rssi < scanInfo.rssiMax)
    UpdatePeakInfoForce();
}

static uint8_t CurrentScanIndex(void) {
  if (scanInfo.measurementsCount > 128) {
    uint8_t i = (uint32_t)ARRAY_SIZE(rssiHistory) * 1000 /
                scanInfo.measurementsCount * scanInfo.i / 1000;
    return i;
  } else {
    return scanInfo.i;
  }
}

static void Measure(void) {
  uint16_t rssi = scanInfo.rssi = GetRssi();
#ifdef ENABLE_SCAN_RANGES
  if (scanInfo.measurementsCount > 128) {
    uint8_t idx = CurrentScanIndex();
    if (rssiHistory[idx] < rssi || isListening)
      rssiHistory[idx] = rssi;
    rssiHistory[(idx + 1) % 128] = 0;
    return;
  }
#endif
  rssiHistory[scanInfo.i] = rssi;
}

// Update things by keypress

static uint16_t dbm2rssi(int dBm) { return (dBm + 160) * 2; }

static void ClampRssiTriggerLevel(void) {
  settings.rssiTriggerLevel =
      clamp(settings.rssiTriggerLevel, dbm2rssi(settings.dbMin),
            dbm2rssi(settings.dbMax));
}

static void UpdateRssiTriggerLevel(bool inc) {
  if (inc)
    settings.rssiTriggerLevel += 2;
  else
    settings.rssiTriggerLevel -= 2;

  ClampRssiTriggerLevel();

  redrawScreen = true;
  redrawStatus = true;
}

static void UpdateDBMax(bool inc) {
  if (inc && settings.dbMax < 10) {
    settings.dbMax += 1;
  } else if (!inc && settings.dbMax > settings.dbMin) {
    settings.dbMax -= 1;
  } else {
    return;
  }

  ClampRssiTriggerLevel();
  redrawStatus = true;
  redrawScreen = true;
  SYSTEM_DelayMs(20);
}

static void UpdateScanStep(bool inc) {
  if (inc && settings.scanStepIndex < S_STEP_100_0kHz) {
    settings.scanStepIndex++;
  } else if (!inc && settings.scanStepIndex > 0) {
    settings.scanStepIndex--;
  } else {
    return;
  }
  settings.frequencyChangeStep = GetBW() >> 1;
  ResetModifiers();
  redrawScreen = true;
}

static void UpdateCurrentFreq(bool inc) {
  if (inc && currentFreq < F_MAX) {
    currentFreq += settings.frequencyChangeStep;
  } else if (!inc && currentFreq > RX_freq_min() &&
             currentFreq > settings.frequencyChangeStep) {
    currentFreq -= settings.frequencyChangeStep;
  } else {
    return;
  }
  ResetModifiers();
  redrawScreen = true;
}

static void UpdateCurrentFreqStill(bool inc) {
  uint8_t offset = modulationTypeTuneSteps[settings.modulationType];
  uint32_t f = fMeasure;
  if (inc && f < F_MAX) {
    f += offset;
  } else if (!inc && f > RX_freq_min()) {
    f -= offset;
  }
  SetF(f);
  redrawScreen = true;
}

static void AutoAdjustFreqChangeStep(void) {
  settings.frequencyChangeStep = GetBW() >> 1;
}

static void ToggleModulation(void) {
  if (settings.modulationType < MODULATION_UKNOWN - 1) {
    settings.modulationType++;
  } else {
    settings.modulationType = MODULATION_FM;
  }
  RADIO_SetModulation(settings.modulationType);
  BK4819_InitAGC(settings.modulationType != MODULATION_FM);
  redrawScreen = true;
}

static void ToggleListeningBW(void) {
  if (settings.listenBw == BK4819_FILTER_BW_NARROWER) {
    settings.listenBw = BK4819_FILTER_BW_WIDE;
  } else {
    settings.listenBw++;
  }
  redrawScreen = true;
}

static void ToggleBacklight(void) {
  settings.backlightState = !settings.backlightState;
  if (settings.backlightState) {
    BACKLIGHT_TurnOn();
  } else {
    BACKLIGHT_TurnOff();
  }
}

static void ToggleStepsCount(void) {
  if (settings.stepsCount == STEPS_128) {
    settings.stepsCount = STEPS_16;
  } else {
    settings.stepsCount--;
  }
  AutoAdjustFreqChangeStep();
  ResetModifiers();
  redrawScreen = true;
}

static void ResetFreqInput(void) {
  tempFreq = 0;
  for (int i = 0; i < 10; ++i) {
    freqInputString[i] = '-';
  }
}

static void FreqInput(void) {
  freqInputIndex = 0;
  freqInputDotIndex = 0;
  ResetFreqInput();
  SetState(FREQ_INPUT);
}

static void UpdateFreqInput(KEY_Code_t key) {
  if (key != KEY_EXIT && freqInputIndex >= 10) {
    return;
  }
  if (key == KEY_STAR) {
    if (freqInputIndex == 0 || freqInputDotIndex) {
      return;
    }
    freqInputDotIndex = freqInputIndex;
  }
  if (key == KEY_EXIT) {
    freqInputIndex--;
    if (freqInputDotIndex == freqInputIndex)
      freqInputDotIndex = 0;
  } else {
    freqInputArr[freqInputIndex++] = key;
  }

  ResetFreqInput();

  uint8_t dotIndex =
      freqInputDotIndex == 0 ? freqInputIndex : freqInputDotIndex;

  KEY_Code_t digitKey;
  for (int i = 0; i < 10; ++i) {
    if (i < freqInputIndex) {
      digitKey = freqInputArr[i];
      freqInputString[i] = digitKey <= KEY_9 ? '0' + digitKey - KEY_0 : '.';
    } else {
      freqInputString[i] = '-';
    }
  }

  uint32_t base = 100000; // 1MHz in BK units
  for (int i = dotIndex - 1; i >= 0; --i) {
    tempFreq += (freqInputArr[i] - KEY_0) * base;
    base *= 10;
  }

  base = 10000; // 0.1MHz in BK units
  if (dotIndex < freqInputIndex) {
    for (int i = dotIndex + 1; i < freqInputIndex; ++i) {
      tempFreq += (freqInputArr[i] - KEY_0) * base;
      base /= 10;
    }
  }
  redrawScreen = true;
}

static void Blacklist(void) {
#ifdef ENABLE_SCAN_RANGES
  blacklistFreqs[blacklistFreqsIdx++ % ARRAY_SIZE(blacklistFreqs)] = peak.i;
  rssiHistory[CurrentScanIndex()] = RSSI_MAX_VALUE;
#endif
  rssiHistory[peak.i] = RSSI_MAX_VALUE;
  isBlacklistApplied = true;
  ResetPeak();
  ToggleRX(false);
  ResetScanStats();
}

#ifdef ENABLE_SCAN_RANGES
static bool IsBlacklisted(uint16_t idx) {
  for (uint8_t i = 0; i < ARRAY_SIZE(blacklistFreqs); i++)
    if (blacklistFreqs[i] == idx)
      return true;
  return false;
}
#endif

// 2024 by kamilsss655 -> https://github.com/kamilsss655
// flattens spectrum by bringing all the rssi readings to the peak value
static void ToggleNormalizeRssi(bool on) {
  // we don't want to normalize when there is already active signal RX
  if (IsPeakOverLevel() && on)
    return;

  if (on) {
    for (uint8_t i = 0; i < ARRAY_SIZE(rssiHistory); i++) {
      gainOffset[i] = peak.rssi - rssiHistory[i];
    }
    isNormalizationApplied = true;
  } else {
    memset(gainOffset, 0, sizeof(gainOffset));
    isNormalizationApplied = false;
  }
  RelaunchScan();
}

// channel scan support: the valid-channel list holds channel indexes
// only; frequencies are fetched from flash while scanning
static void LoadValidMemoryChannels(void) {
  memset(scanChannel, 0, sizeof(scanChannel));
  scanChannelsCount = 0;
  for (uint16_t ch = MR_CHANNEL_FIRST;
       ch <= MR_CHANNEL_LAST && scanChannelsCount < MAX_SCAN_CHANNELS; ch++) {
    // ALL: any valid channel; SL: only the radio's current scan list
    if (RADIO_CheckValidChannel(ch, !scanListAll,
                                scanListAll ? 0 : gEeprom.SCAN_LIST_DEFAULT)) {
      scanChannel[scanChannelsCount++] = ch;
    }
  }
}

static void AutoAdjustResolution(void) {
  if (GetStepsCount() <= 64) {
    settings.stepsCount = STEPS_64;
  } else {
    settings.stepsCount = STEPS_128;
  }
}

static void ToggleScanList(void) {
  scanListAll = !scanListAll;
  LoadValidMemoryChannels();
  ResetModifiers();
  AutoAdjustResolution();
}

static void LookupChannelInfo(void) {
  if (appMode != CHANNEL_MODE) {
    isKnownChannel = false;
    return;
  }

  if (lastPeakFrequency == peak.f)
    return;

  lastPeakFrequency = peak.f;

  if (peak.i >= 1 && peak.i <= scanChannelsCount) {
    peakChannel = scanChannel[peak.i - 1];
    isKnownChannel = true;
    memset(channelName, 0, sizeof(channelName));
    SETTINGS_FetchChannelName(channelName, peakChannel);
  } else {
    peakChannel = -1;
    isKnownChannel = false;
  }

  redrawStatus = true;
}

static void Attenuate(uint8_t amount) {
  // attenuate doesn't work with more than 128 samples,
  // since we select max rssi in such mode ignoring attenuation
  if (scanInfo.measurementsCount > 128)
    return;

  if (attenuationOffset[peak.i] < MAX_ATTENUATION) {
    attenuationOffset[peak.i] += amount;
    isAttenuationApplied = true;

    ResetPeak();
    ResetScanStats();
  }
}

// Draw things

// applied x2 to prevent initial rounding
static uint8_t Rssi2PX(uint16_t rssi, uint8_t pxMin, uint8_t pxMax) {
  const int DB_MIN = settings.dbMin << 1;
  const int DB_MAX = settings.dbMax << 1;
  const int DB_RANGE = DB_MAX - DB_MIN;

  const uint8_t PX_RANGE = pxMax - pxMin;

  int dbm = clamp(rssi - (160 << 1), DB_MIN, DB_MAX);

  return ((dbm - DB_MIN) * PX_RANGE + DB_RANGE / 2) / DB_RANGE + pxMin;
}

static uint8_t Rssi2Y(uint16_t rssi) {
  return DrawingEndY - Rssi2PX(rssi, 0, DrawingEndY);
}

static void DrawSpectrum(void) {
  for (uint8_t x = 0; x < 128; ++x) {
    uint16_t rssi = rssiHistory[x >> settings.stepsCount];
    if (rssi != RSSI_MAX_VALUE) {
      DrawVLine(Rssi2Y(rssi), DrawingEndY, x, true);
    }
  }
}

static void DrawStatus(void) {
  sprintf(String, "%d/%d", settings.dbMin, settings.dbMax);
  GUI_DisplaySmallest(String, 0, 1, true, true);

  BOARD_ADC_GetBatteryInfo(&gBatteryVoltages[gBatteryCheckCounter++ % 4],
                           &gBatteryCurrent);

  uint16_t voltage = (gBatteryVoltages[0] + gBatteryVoltages[1] +
                      gBatteryVoltages[2] + gBatteryVoltages[3]) /
                     4 * 760 / gBatteryCalibration[3];

  unsigned perc = BATTERY_VoltsToPercent(voltage);

  gStatusLine[116] = 0b00011100;
  gStatusLine[117] = 0b00111110;
  for (int i = 118; i <= 126; i++) {
    gStatusLine[i] = 0b00100010;
  }

  for (unsigned i = 127; i >= 118; i--) {
    if (127 - i <= (perc + 5) * 9 / 100) {
      gStatusLine[i] = 0b00111110;
    }
  }
}

static void DrawF(uint32_t f) {
  // channel mode: bold channel name above a big frequency, like the
  // kamilsss655 K5 UI; only in the SPECTRUM view (in STILL the big
  // font would overlap the S-meter row)
  if (currentState == SPECTRUM && appMode == CHANNEL_MODE && isKnownChannel &&
      channelName[0] != 0 && f == lastPeakFrequency) {
    UI_PrintStringSmallBold(channelName, 0, 127, 0);
    sprintf(String, "%u.%05u", f / 100000, f % 100000);
    UI_PrintString(String, 2, 127, 1, 8);
  } else {
    sprintf(String, "%u.%05u", f / 100000, f % 100000);
    UI_PrintStringSmallNormal(String, 8, 127, 0);
  }

  sprintf(String, "%3s", gModulationStr[settings.modulationType]);
  GUI_DisplaySmallest(String, 116, 1, false, true);
  sprintf(String, "%s", bwNames[settings.listenBw]);
  GUI_DisplaySmallest(String, 108, 7, false, true);
}

static void DrawNums(void) {

  if (currentState == SPECTRUM) {
    if (isNormalizationApplied) {
      sprintf(String, "N(%ux)", GetStepsCount());
    } else {
      sprintf(String, "%ux", GetStepsCount());
    }
    GUI_DisplaySmallest(String, 0, 1, false, true);

    if (appMode == CHANNEL_MODE) {
      sprintf(String, "%s", scanListAll ? "ALL" : "SL");
      GUI_DisplaySmallest(String, 0, 7, false, true);
      if (isKnownChannel) {
        // peak channel number, small on the left like the K5 UI
        sprintf(String, "M%i", peakChannel + 1);
        GUI_DisplaySmallest(String, 0, 13, false, true);
      }
    } else {
      sprintf(String, "%u.%02uk", GetScanStep() / 100, GetScanStep() % 100);
      GUI_DisplaySmallest(String, 0, 7, false, true);
    }
  }

  if (appMode == CHANNEL_MODE) {
    sprintf(String, "M:%d", scanChannel[0] + 1);
    GUI_DisplaySmallest(String, 0, 49, false, true);

    sprintf(String, "M:%d", scanChannel[GetStepsCount() - 1] + 1);
    GUI_DisplaySmallest(String, 108, 49, false, true);
  } else {
    sprintf(String, "%u.%05u", GetFStart() / 100000, GetFStart() % 100000);
    GUI_DisplaySmallest(String, 0, 49, false, true);

    sprintf(String, "%u.%05u", GetFEnd() / 100000, GetFEnd() % 100000);
    GUI_DisplaySmallest(String, 93, 49, false, true);
  }

  if (isAttenuationApplied) {
    sprintf(String, "ATT");
    GUI_DisplaySmallest(String, 52, 49, false, true);
  }

  if (isBlacklistApplied) {
    sprintf(String, "BL");
    GUI_DisplaySmallest(String, 67, 49, false, true);
  }
}

static void DrawRssiTriggerLevel(void) {
  if (settings.rssiTriggerLevel == RSSI_MAX_VALUE || monitorMode)
    return;
  uint8_t y = Rssi2Y(settings.rssiTriggerLevel);
  for (uint8_t x = 0; x < 128; x += 2) {
    PutPixel(x, y, true);
  }
}

static void DrawTicks(void) {
  uint32_t f = GetFStart();
  uint32_t span = GetFEnd() - GetFStart();
  uint32_t step = span / 128;
  for (uint8_t i = 0; i < 128; i += (1 << settings.stepsCount)) {
    f = GetFStart() + span * i / 128;
    uint8_t barValue = 0b00000001;
    (f % 10000) < step && (barValue |= 0b00000010);
    (f % 50000) < step && (barValue |= 0b00000100);
    (f % 100000) < step && (barValue |= 0b00011000);

    gFrameBuffer[5][i] |= barValue;
  }
  memset(gFrameBuffer[5] + 1, 0x80, 3);
  memset(gFrameBuffer[5] + 124, 0x80, 3);

  gFrameBuffer[5][0] = 0xff;
  gFrameBuffer[5][127] = 0xff;
}

static void DrawArrow(uint8_t x) {
  for (signed i = -2; i <= 2; ++i) {
    signed v = x + i;
    if (!(v & 128)) {
      gFrameBuffer[5][v] |= (0b01111000 << my_abs(i)) & 0b01111000;
    }
  }
}

static void OnKeyDown(uint8_t key) {
  switch (key) {
  case KEY_3:
    UpdateDBMax(true);
    break;
  case KEY_9:
    UpdateDBMax(false);
    break;
  case KEY_1:
    if (appMode != CHANNEL_MODE) {
      UpdateScanStep(true);
    }
    break;
  case KEY_7:
    if (appMode != CHANNEL_MODE) {
      UpdateScanStep(false);
    }
    break;
  case KEY_2:
    ToggleNormalizeRssi(!isNormalizationApplied);
    break;
  case KEY_8:
    ToggleBacklight();
    break;
  case KEY_UP:
    if (appMode == FREQUENCY_MODE) {
      UpdateCurrentFreq(true);
    } else {
      ResetModifiers();
    }
    break;
  case KEY_DOWN:
    if (appMode == FREQUENCY_MODE) {
      UpdateCurrentFreq(false);
    } else {
      ResetModifiers();
    }
    break;
  case KEY_SIDE1:
    Blacklist();
    break;
  case KEY_STAR:
    UpdateRssiTriggerLevel(true);
    break;
  case KEY_F:
    UpdateRssiTriggerLevel(false);
    break;
  case KEY_5:
    if (appMode == FREQUENCY_MODE)
      FreqInput();
    break;
  case KEY_0:
    ToggleModulation();
    break;
  case KEY_6:
    ToggleListeningBW();
    break;
  case KEY_4:
    if (appMode == CHANNEL_MODE) {
      ToggleScanList();
    } else if (appMode != SCAN_RANGE_MODE) {
      ToggleStepsCount();
    }
    break;
  case KEY_SIDE2:
    Attenuate(ATTENUATE_STEP);
    break;
  case KEY_PTT:
    ExitAndCopyToVfo();
    break;
  case KEY_MENU:
    SetState(STILL);
    TuneToPeak();
    break;
  case KEY_EXIT:
    if (menuState) {
      menuState = 0;
      break;
    }
    DeInitSpectrum();
    break;
  default:
    break;
  }
}

static void OnKeyDownFreqInput(uint8_t key) {
  switch (key) {
  case KEY_0:
  case KEY_1:
  case KEY_2:
  case KEY_3:
  case KEY_4:
  case KEY_5:
  case KEY_6:
  case KEY_7:
  case KEY_8:
  case KEY_9:
  case KEY_STAR:
    UpdateFreqInput(key);
    break;
  case KEY_EXIT:
    if (freqInputIndex == 0) {
      SetState(previousState);
      break;
    }
    UpdateFreqInput(key);
    break;
  case KEY_MENU:
    if (tempFreq < RX_freq_min() || tempFreq > F_MAX) {
      break;
    }
    SetState(previousState);
    currentFreq = tempFreq;
    if (currentState == SPECTRUM) {
      ResetModifiers();
    } else {
      SetF(currentFreq);
    }
    break;
  default:
    break;
  }
}

static void OnKeyDownStill(KEY_Code_t key) {
  switch (key) {
  case KEY_3:
    UpdateDBMax(true);
    break;
  case KEY_9:
    UpdateDBMax(false);
    break;
  case KEY_UP:
    if (menuState) {
      SetRegMenuValue(menuState, true);
      break;
    }
    UpdateCurrentFreqStill(true);
    break;
  case KEY_DOWN:
    if (menuState) {
      SetRegMenuValue(menuState, false);
      break;
    }
    UpdateCurrentFreqStill(false);
    break;
  case KEY_STAR:
    UpdateRssiTriggerLevel(true);
    break;
  case KEY_F:
    UpdateRssiTriggerLevel(false);
    break;
  case KEY_5:
    FreqInput();
    break;
  case KEY_0:
    ToggleModulation();
    break;
  case KEY_6:
    ToggleListeningBW();
    break;
  case KEY_SIDE1:
    monitorMode = !monitorMode;
    break;
  case KEY_SIDE2:
    ToggleBacklight();
    break;
  case KEY_PTT:
    ExitAndCopyToVfo();
    break;
  case KEY_MENU:
    if (menuState == ARRAY_SIZE(registerSpecs) - 1) {
      menuState = 1;
    } else {
      menuState++;
    }
    redrawScreen = true;
    break;
  case KEY_EXIT:
    if (!menuState) {
      SetState(SPECTRUM);
      monitorMode = false;
      RelaunchScan();
      break;
    }
    menuState = 0;
    break;
  default:
    break;
  }
}

static void RenderFreqInput(void) {
  UI_PrintString(freqInputString, 2, 127, 0, 8);
}

static void RenderStatus(void) {
  memset(gStatusLine, 0, sizeof(gStatusLine));
  DrawStatus();
  ST7565_BlitStatusLine();
}

static void RenderSpectrum(void) {
  DrawTicks();
  if ((appMode == CHANNEL_MODE) && (GetStepsCount() < 128u)) {
    DrawArrow(peak.i * (settings.stepsCount + 1));
  } else {
    DrawArrow(128u * peak.i / GetStepsCount());
  }
  DrawSpectrum();
  DrawRssiTriggerLevel();
  DrawF(peak.f);
  DrawNums();
}

static void RenderStill(void) {
  DrawF(fMeasure);

  const uint8_t METER_PAD_LEFT = 3;

  for (int i = 0; i < 121; i++) {
    if (i % 10 == 0) {
      gFrameBuffer[2][i + METER_PAD_LEFT] = 0b01110000;
    } else if (i % 5 == 0) {
      gFrameBuffer[2][i + METER_PAD_LEFT] = 0b00110000;
    } else {
      gFrameBuffer[2][i + METER_PAD_LEFT] = 0b00010000;
    }
  }

  uint8_t x = Rssi2PX(scanInfo.rssi, 0, 121);
  for (int i = 0; i < x; ++i) {
    if (i % 5) {
      gFrameBuffer[2][i + METER_PAD_LEFT] |= 0b00000111;
    }
  }

  sLevelAttributes sLevelAtt;
  sLevelAtt = GetSLevelAttributes(scanInfo.rssi, fMeasure);

  if (sLevelAtt.over > 0) {
    sprintf(String, "S%2d+%2d", sLevelAtt.sLevel, sLevelAtt.over);
  } else {
    sprintf(String, "S%2d", sLevelAtt.sLevel);
  }

  GUI_DisplaySmallest(String, 4, 25, false, true);
  sprintf(String, "%d dBm", sLevelAtt.dBmRssi);
  GUI_DisplaySmallest(String, 40, 25, false, true);

  if (!monitorMode) {
    uint8_t x = Rssi2PX(settings.rssiTriggerLevel, 0, 121);
    gFrameBuffer[2][METER_PAD_LEFT + x] = 0b11111111;
  }

  const uint8_t PAD_LEFT = 4;
  const uint8_t CELL_WIDTH = 30;
  uint8_t offset = PAD_LEFT;
  uint8_t row = 4;

  for (int i = 0, idx = 1; idx <= 4; ++i, ++idx) {
    if (idx == 5) {
      row += 2;
      i = 0;
    }
    offset = PAD_LEFT + i * CELL_WIDTH;
    if (menuState == idx) {
      for (int j = 0; j < CELL_WIDTH; ++j) {
        gFrameBuffer[row][j + offset] = 0xFF;
        gFrameBuffer[row + 1][j + offset] = 0xFF;
      }
    }
    sprintf(String, "%s", registerSpecs[idx].name);
    GUI_DisplaySmallest(String, offset + 2, row * 8 + 2, false,
                        menuState != idx);
    sprintf(String, "%u", GetRegMenuValue(idx));
    GUI_DisplaySmallest(String, offset + 2, (row + 1) * 8 + 1, false,
                        menuState != idx);
  }
}

static void Render(void) {
  memset(gFrameBuffer, 0, sizeof(gFrameBuffer));

  switch (currentState) {
  case SPECTRUM:
    RenderSpectrum();
    break;
  case FREQ_INPUT:
    RenderFreqInput();
    break;
  case STILL:
    RenderStill();
    break;
  }

  ST7565_BlitFullScreen();
}

static void HandleUserInput(void) {
  kbd.prev = kbd.current;
  kbd.current = KEYBOARD_GetKey();

  if (kbd.current != KEY_INVALID && kbd.current == kbd.prev) {
    if (kbd.counter < 16)
      kbd.counter++;
    else
      kbd.counter -= 3;
    SYSTEM_DelayMs(20);
  } else {
    kbd.counter = 0;
  }

  if (kbd.counter == 3 || kbd.counter == 16) {
    switch (currentState) {
    case SPECTRUM:
      OnKeyDown(kbd.current);
      break;
    case FREQ_INPUT:
      OnKeyDownFreqInput(kbd.current);
      break;
    case STILL:
      OnKeyDownStill(kbd.current);
      break;
    }
  }
}

static void Scan(void) {
  if (rssiHistory[scanInfo.i] != RSSI_MAX_VALUE
#ifdef ENABLE_SCAN_RANGES
      && !IsBlacklisted(scanInfo.i)
#endif
  ) {
    SetF(scanInfo.f);
    Measure();
    UpdateScanInfo();
  }
}

static void NextScanStep(void) {
  ++peak.t;
  if (appMode == CHANNEL_MODE) {
    // channel frequency is read from flash on the fly (no RAM cache)
    scanInfo.f = SETTINGS_FetchChannelFrequency(scanChannel[scanInfo.i]);
    ++scanInfo.i;
  } else {
    ++scanInfo.i;
    scanInfo.f += scanInfo.scanStep;
  }
}

static void UpdateScan(void) {
  Scan();

  if (scanInfo.i < GetStepsCount()) {
    NextScanStep();
    return;
  }

  if (scanInfo.measurementsCount < 128)
    memset(&rssiHistory[scanInfo.measurementsCount], 0,
           sizeof(rssiHistory) -
               scanInfo.measurementsCount * sizeof(rssiHistory[0]));

  redrawScreen = true;
  preventKeypress = false;

  UpdatePeakInfo();
  if (IsPeakOverLevel()) {
    ToggleRX(true);
    TuneToPeak();
    return;
  }

  newScanStart = true;
}

static void UpdateStill(void) {
  Measure();
  redrawScreen = true;
  preventKeypress = false;

  peak.rssi = scanInfo.rssi;
  AutoTriggerLevel();

  ToggleRX(IsPeakOverLevel() || monitorMode);
}

static void UpdateListening(void) {
  preventKeypress = false;
  if (currentState == STILL) {
    listenT = 0;
  }
  if (listenT) {
    listenT--;
    SYSTEM_DelayMs(1);
    return;
  }

  if (currentState == SPECTRUM) {
    if (appMode != CHANNEL_MODE)
      BK4819_WriteRegister(BK4819_REG_43, GetBWRegValueForScan());
    Measure();
    BK4819_SetFilterBandwidth(settings.listenBw, false);
  } else {
    Measure();
  }

  peak.rssi = scanInfo.rssi;
  redrawScreen = true;

  CheckIfTailFound();

  if ((IsPeakOverLevel() || monitorMode) && !gTailFound) {
    listenT = SQUELCH_OFF_DELAY;
    return;
  }

  ToggleRX(false);
  ResetScanStats();
}

static void Tick(void) {

#ifdef ENABLE_SCAN_RANGES
  if (gNextTimeslice_500ms) {
    gNextTimeslice_500ms = false;

    // if a lot of steps then it takes long time
    // we don't want to wait for whole scan
    // listening has it's own timer
    if (GetStepsCount() > 128 && !isListening) {
      UpdatePeakInfo();
      if (IsPeakOverLevel()) {
        ToggleRX(true);
        TuneToPeak();
        return;
      }
      redrawScreen = true;
      preventKeypress = false;
    }
  }
#endif

  if (!preventKeypress) {
    HandleUserInput();
  }
  if (newScanStart) {
    InitScan();
    newScanStart = false;
  }
  if (isListening && currentState != FREQ_INPUT) {
    UpdateListening();
  } else {
    if (currentState == SPECTRUM) {
      UpdateScan();
    } else if (currentState == STILL) {
      UpdateStill();
    }
  }
  if (redrawStatus || ++statuslineUpdateTimer > 4096) {
    RenderStatus();
    redrawStatus = false;
    statuslineUpdateTimer = 0;
  }
  if (redrawScreen) {
    Render();
    redrawScreen = false;
  }
}

void APP_RunSpectrumK5(void) {
  Mode mode = FREQUENCY_MODE;

  // memory channel on the active VFO -> scan the saved channels,
  // like the kamilsss655 K5 firmware does
  if (IS_MR_CHANNEL(gEeprom.ScreenChannel[gEeprom.TX_VFO])) {
    mode = CHANNEL_MODE;
  }
#ifdef ENABLE_SCAN_RANGES
  if (gScanRangeStart) {
    mode = SCAN_RANGE_MODE;
  }
#endif

  // reset modifiers if launched in a different mode than the previous run
  if (appMode != mode) {
    appMode = mode;
    ResetModifiers();
  }

  if (mode == CHANNEL_MODE) {
    LoadValidMemoryChannels();
    AutoAdjustResolution();
    if (scanChannelsCount == 0) {
      // no valid channels: fall back to frequency mode
      appMode = mode = FREQUENCY_MODE;
    }
  }

#ifdef ENABLE_SCAN_RANGES
  if (mode == SCAN_RANGE_MODE) {
    currentFreq = initialFreq = gScanRangeStart;
    for (uint8_t i = 0; i < ARRAY_SIZE(scanStepValues); i++) {
      if (scanStepValues[i] >= gTxVfo->StepFrequency) {
        settings.scanStepIndex = i;
        break;
      }
    }
    settings.stepsCount = STEPS_128;
  } else
#endif
    currentFreq = initialFreq = gTxVfo->pRX->Frequency;

  BackupRegisters();

  ResetInterrupts();

  // turn off GREEN LED if spectrum was started during active RX
  BK4819_ToggleGpioOut(BK4819_GPIO6_PIN2_GREEN, false);

  isListening = true; // to turn off RX later
  redrawStatus = true;
  redrawScreen = true;
  newScanStart = true;

  ToggleRX(true), ToggleRX(false); // hack to prevent noise when squelch off

  RADIO_SetModulation(settings.modulationType = gTxVfo->Modulation);
  BK4819_SetFilterBandwidth(settings.listenBw = gTxVfo->CHANNEL_BANDWIDTH,
                            false);
  settings.scanStepIndex = GetScanStepFromStepFrequency(gTxVfo->StepFrequency);

  AutoAdjustFreqChangeStep();

  RelaunchScan();

  for (int i = 0; i < 128; ++i) {
    rssiHistory[i] = 0;
  }

  isInitialized = true;

  while (isInitialized) {
    Tick();
  }
}

#endif // ENABLE_SPECTRUM_K5
