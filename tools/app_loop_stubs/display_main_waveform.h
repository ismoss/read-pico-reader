/* 中文：调度测试替身。/ English: Scheduler test shim. */
#pragma once
#include "common.h"
const EpdWaveform *display_main_gray_waveform(void);
const EpdWaveform *display_main_shelf_exit_waveform(void);
const EpdWaveform *display_main_water_waveform(void);
const EpdWaveform *display_main_bw_waveform(void);
const EpdWaveform *display_main_normal_waveform(void);
const EpdWaveform *display_main_shelf_waveform(enum EpdDrawMode mode);
const EpdWaveform *display_settings_scroll_waveform(void);
const EpdWaveform *display_main_ui_waveform(void);
