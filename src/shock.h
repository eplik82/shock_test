// Löögi püüdmine (käivituslävi + eel/järelpuhver) ja poolsiinusimpulsi analüüs
#pragma once
#include <stdint.h>

#include <string>
#include <vector>

enum ShockState : uint8_t { SH_IDLE = 0, SH_ARMED, SH_CAPTURING, SH_DONE };

// Löögi tulemus
struct ShotResult {
    // nominaal ja piirid (otsuse hetkel)
    float a_nom, td_nom;
    float peak_min, peak_max, td_min, td_max, dv_nom, dv_min, dv_max;
    // mõõdetud
    float peak;        // g (interpoleeritud)
    float peak_raw;    // suurim valim
    float td;          // ms (baaskestus 10 % punktidest)
    float dv;          // m/s
    int axis;          // 0..2 (X,Y,Z)
    int polarity;      // +1 / -1
    bool saturated;
    bool shape_ok;
    int shape_viol;    // koridorist väljas valimeid
    float shape_worst; // suurim väljumine koridorist (g)
    int pulse_samples; // valimeid impulsi kohta
    float rate;        // Hz
    // otsus
    bool pass_peak, pass_td, pass_dv, pass;
    std::string reason;  // eestikeelne põhjus (tühi kui läbitud)
    // graafik: valitud telg (polaarsus normeeritud), aeg ms nominaalse impulsi algusest
    float t0_ms, dt_ms;
    std::vector<float> wave;
    // koridor samadel ajahetkedel
    std::vector<float> band_lo, band_hi, ideal;
};

typedef void (*shock_result_cb_t)(const ShotResult &r);

void shock_init(shock_result_cb_t cb);
void shock_arm(bool on);
ShockState shock_state(void);
// viimase ~150 ms suurim |a| g-des (eelpinge eemaldatud) — reaalajas näit
float shock_live_g(void);
// analüüs eraldi (testimiseks): a = valitud telje kiirendus g-des
void shock_analyze(const float *a, int n, float rate, float trig_index, ShotResult &r);
// nominaalne poolsiinus ja tolerantsikoridor (t ms nominaalse impulsi algusest; band = osa A-st, nt 0.2)
float shock_ideal(float t, float A, float T);
void shock_band(float t, float A, float T, float band, float &lo, float &hi);
