// Testiseeriad ja löögid LittleFS-is (/lfs/tNNNN/...)
#pragma once
#include <stdint.h>

#include <string>
#include <vector>

#include "shock.h"

// 6 suunda: +X -X +Y -Y +Z -Z
extern const char *const DIR_NAMES[6];

struct SeriesInfo {
    uint32_t id = 0;
    char object[48] = "";
    char serial[32] = "";
    char oper[32] = "";
    uint8_t preset = 0;
    // seaded seeria alguses
    float a_nom, td_nom, mass_kg, tol_peak, tol_td, tol_dv, band, trig_pct;
    bool shape_check, dv_check, un_checks;
    uint8_t shots_per_dir, axis, amax;
    uint16_t odr;
    float rate;
    // aeg
    int64_t start_epoch = 0;  // 0 = kell seadistamata
    uint32_t boot_id = 0, start_uptime = 0;
    // UN38.3 operaatori kontrollid
    float mass_before = 0, mass_after = 0, ocv_before = 0, ocv_after = 0;
    bool checks_done = false;
    bool no_leak = false, no_vent = false, no_disasm = false, no_rupture = false, no_fire = false;
    bool closed = false;
    uint16_t shots = 0;
    uint16_t passed = 0;
};

// Löögi kokkuvõte (fikseeritud kirje shots.bin failis)
struct ShotRec {
    uint16_t index;     // 0..
    uint8_t dir;        // 0..5
    uint8_t num;        // 1..shots_per_dir
    int64_t epoch;      // 0 = kell seadistamata
    uint32_t boot_id, uptime;
    uint8_t axis;
    int8_t polarity;
    uint8_t saturated, shape_ok, pass_peak, pass_td, pass_dv, pass;
    float peak, peak_raw, td, dv, shape_worst, rate;
    uint16_t shape_viol, pulse_samples;
    char reason[96];
};

bool store_init(void);
void store_usage(size_t *used, size_t *total);

// avatud (pooleli) seeria
bool store_has_open(void);
SeriesInfo &store_open_series(void);
uint32_t store_new_series(const char *object, const char *serial, const char *oper);  // võtab seaded g_set-ist
void store_save_open(void);           // salvesta info (nt UN kontrollid)
void store_close_series(void);
// suund ja löögi nr järgmisele löögile
void store_next_pos(int *dir, int *num);
bool store_series_complete(void);
// lisa löök avatud seeriasse; tagastab kirje
ShotRec store_add_shot(const ShotResult &r);
bool store_undo_last(void);
// kella sünkroniseerimisel: dateeri selle käivituse kirjed tagantjärele
void store_backfill_time(void);

// kõik seeriad (uuemad eespool)
std::vector<SeriesInfo> store_list(void);
bool store_load(uint32_t id, SeriesInfo &info, std::vector<ShotRec> &shots);
// kõver: t0, dt (ms), valimid
bool store_load_wave(uint32_t id, int index, float *t0, float *dt, std::vector<float> &w);
bool store_delete(uint32_t id);
