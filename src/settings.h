// Seadistused (NVS) ja standardite eelseadistused
#pragma once
#include <stdint.h>

enum Preset : uint8_t {
    PRESET_CUSTOM = 0,
    PRESET_MIL_HSC1,      // MIL-STD-810H 516.8 Proc I HSC-I: 20 g / 23 ms
    PRESET_MIL_HSC2,      // HSC-II: 5 g / 23 ms
    PRESET_MIL_GROUND,    // maapealne, poolsiinus asendus: pi/4*40 g / 11 ms
    PRESET_UN_CELL,       // UN38.3 T.4 element: 150 g / 6 ms
    PRESET_UN_LARGE_CELL, // suur element: 50 g / 11 ms
    PRESET_UN_SMALL_BATT, // väike aku: min(150, sqrt(100850/m)) / 6 ms
    PRESET_UN_LARGE_BATT, // suur aku: min(50, sqrt(30000/m)) / 11 ms
    PRESET_COUNT
};

enum Axis : uint8_t { AXIS_AUTO = 0, AXIS_X, AXIS_Y, AXIS_Z };

struct Settings {
    uint8_t preset = PRESET_CUSTOM;
    float peak_g = 50.0f;      // nominaalne A
    float td_ms = 11.0f;       // nominaalne TD
    float mass_kg = 1.0f;      // UN38.3 aku mass
    float tol_peak = 20.0f;    // % (standard: 0,8A..1,2A)
    float tol_td = 10.0f;      // %
    float tol_dv = 20.0f;      // %
    float band = 20.0f;        // kuju koridor ±% A-st
    bool shape_check = true;   // kuju koridor mõjutab otsust
    bool dv_check = true;      // ΔV mõjutab otsust
    bool un_checks = false;    // UN38.3 operaatori kontrollid
    uint8_t amax = 200;        // A ülempiir: 200 või 166
    uint8_t axis = AXIS_AUTO;
    uint8_t shots_per_dir = 3;
    uint16_t odr = 3200;       // ADXL375 diskreetimissagedus (800/1600/3200)
    float trig_pct = 30.0f;    // käivituslävi % A-st
    float cal[3] = {1, 1, 1};  // telgede kalibreerimistegurid
    char ap_pass[33] = "shocktest";
    char sta_ssid[33] = "";    // valikuline: arendaja võrk (OTA)
    char sta_pass[65] = "";
};

extern Settings g_set;

void settings_load(void);
void settings_save(void);
// rakendab eelseadistuse (A, TD, löökide arv); UN akude puhul arvestab massi
void settings_apply_preset(Settings &s);
const char *preset_name(int p);
const char *preset_standard(int p);  // "MIL-STD-810H 516.8" / "UN38.3 T.4" / "Kasutaja"
bool preset_is_un(int p);
float settings_min_peak(void);  // 5 g
