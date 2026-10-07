#include "store.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <mutex>

#include "adxl375.h"
#include "clock.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "nvs.h"
#include "settings.h"

static const char *TAG = "store";
#define ROOT "/lfs"

const char *const DIR_NAMES[6] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};

static std::recursive_mutex s_mtx;
static SeriesInfo s_open;
static bool s_has_open;
static bool s_ok;

static std::string sdir(uint32_t id)
{
    char b[32];
    snprintf(b, sizeof(b), ROOT "/t%04lu", (unsigned long)id);
    return b;
}

static bool write_info(const SeriesInfo &si)
{
    std::string p = sdir(si.id) + "/info.bin";
    FILE *f = fopen(p.c_str(), "wb");
    if (!f) return false;
    uint32_t ver = sizeof(SeriesInfo);
    fwrite(&ver, 4, 1, f);
    fwrite(&si, sizeof(si), 1, f);
    fclose(f);
    return true;
}

static bool read_info(uint32_t id, SeriesInfo &si)
{
    std::string p = sdir(id) + "/info.bin";
    FILE *f = fopen(p.c_str(), "rb");
    if (!f) return false;
    uint32_t ver = 0;
    bool ok = fread(&ver, 4, 1, f) == 1 && ver == sizeof(SeriesInfo) && fread(&si, sizeof(si), 1, f) == 1;
    fclose(f);
    return ok;
}

static void save_open_id(uint32_t id)
{
    nvs_handle_t h;
    if (nvs_open("store", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_u32(h, "open", id);
        nvs_commit(h);
        nvs_close(h);
    }
}

static uint32_t next_id(void)
{
    nvs_handle_t h;
    uint32_t id = 1;
    if (nvs_open("store", NVS_READWRITE, &h) == ESP_OK) {
        nvs_get_u32(h, "next", &id);
        nvs_set_u32(h, "next", id + 1);
        nvs_commit(h);
        nvs_close(h);
    }
    return id;
}

bool store_init(void)
{
    esp_vfs_littlefs_conf_t c = {};
    c.base_path = ROOT;
    c.partition_label = "storage";
    c.format_if_mount_failed = true;
    esp_err_t e = esp_vfs_littlefs_register(&c);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "LittleFS: %s", esp_err_to_name(e));
        return false;
    }
    s_ok = true;
    nvs_handle_t h;
    uint32_t id = 0;
    if (nvs_open("store", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u32(h, "open", &id);
        nvs_close(h);
    }
    if (id && read_info(id, s_open) && !s_open.closed) s_has_open = true;
    size_t u, t;
    store_usage(&u, &t);
    ESP_LOGI(TAG, "LittleFS %u / %u KB, avatud seeria %lu", (unsigned)(u / 1024), (unsigned)(t / 1024),
             (unsigned long)(s_has_open ? s_open.id : 0));
    return true;
}

void store_usage(size_t *used, size_t *total)
{
    *used = *total = 0;
    if (s_ok) esp_littlefs_info("storage", total, used);
}

bool store_has_open(void) { return s_has_open; }
SeriesInfo &store_open_series(void) { return s_open; }

uint32_t store_new_series(const char *object, const char *serial, const char *oper)
{
    std::lock_guard<std::recursive_mutex> g(s_mtx);
    if (s_has_open) store_close_series();
    SeriesInfo si;
    si.id = next_id();
    strlcpy(si.object, object, sizeof(si.object));
    strlcpy(si.serial, serial, sizeof(si.serial));
    strlcpy(si.oper, oper, sizeof(si.oper));
    const Settings &s = g_set;
    si.preset = s.preset;
    si.a_nom = s.peak_g;
    si.td_nom = s.td_ms;
    si.mass_kg = s.mass_kg;
    si.tol_peak = s.tol_peak;
    si.tol_td = s.tol_td;
    si.tol_dv = s.tol_dv;
    si.band = s.band;
    si.trig_pct = s.trig_pct;
    si.shape_check = s.shape_check;
    si.dv_check = s.dv_check;
    si.un_checks = s.un_checks;
    si.shots_per_dir = s.shots_per_dir;
    si.axis = s.axis;
    si.amax = s.amax;
    si.odr = adxl_odr();
    si.rate = adxl_measured_rate();
    si.start_epoch = clock_epoch();
    si.boot_id = clock_boot_id();
    si.start_uptime = clock_uptime_s();
    mkdir(sdir(si.id).c_str(), 0775);
    write_info(si);
    s_open = si;
    s_has_open = true;
    save_open_id(si.id);
    ESP_LOGI(TAG, "uus seeria %lu", (unsigned long)si.id);
    return si.id;
}

void store_save_open(void)
{
    std::lock_guard<std::recursive_mutex> g(s_mtx);
    if (s_has_open) write_info(s_open);
}

void store_close_series(void)
{
    std::lock_guard<std::recursive_mutex> g(s_mtx);
    if (!s_has_open) return;
    s_open.closed = true;
    write_info(s_open);
    s_has_open = false;
    save_open_id(0);
}

void store_next_pos(int *dir, int *num)
{
    int spd = s_open.shots_per_dir ? s_open.shots_per_dir : 1;
    int k = s_open.shots;
    *dir = (k / spd) % 6;
    *num = k % spd + 1;
}

bool store_series_complete(void)
{
    return s_has_open && s_open.shots >= s_open.shots_per_dir * 6;
}

ShotRec store_add_shot(const ShotResult &r)
{
    std::lock_guard<std::recursive_mutex> g(s_mtx);
    ShotRec rec = {};
    if (!s_has_open) return rec;
    int dir, num;
    store_next_pos(&dir, &num);
    rec.index = s_open.shots;
    rec.dir = dir;
    rec.num = num;
    rec.epoch = clock_epoch();
    rec.boot_id = clock_boot_id();
    rec.uptime = clock_uptime_s();
    rec.axis = r.axis;
    rec.polarity = r.polarity;
    rec.saturated = r.saturated;
    rec.shape_ok = r.shape_ok;
    rec.pass_peak = r.pass_peak;
    rec.pass_td = r.pass_td;
    rec.pass_dv = r.pass_dv;
    rec.pass = r.pass;
    rec.peak = r.peak;
    rec.peak_raw = r.peak_raw;
    rec.td = r.td;
    rec.dv = r.dv;
    rec.shape_worst = r.shape_worst;
    rec.rate = r.rate;
    rec.shape_viol = r.shape_viol;
    rec.pulse_samples = r.pulse_samples;
    strlcpy(rec.reason, r.reason.c_str(), sizeof(rec.reason));

    std::string d = sdir(s_open.id);
    FILE *f = fopen((d + "/shots.bin").c_str(), "ab");
    if (f) {
        fwrite(&rec, sizeof(rec), 1, f);
        fclose(f);
    }
    char wn[64];
    snprintf(wn, sizeof(wn), "/w%04u.bin", rec.index);
    f = fopen((d + wn).c_str(), "wb");
    if (f) {
        uint32_t n = r.wave.size();
        fwrite(&r.t0_ms, 4, 1, f);
        fwrite(&r.dt_ms, 4, 1, f);
        fwrite(&n, 4, 1, f);
        fwrite(r.wave.data(), 4, n, f);
        fclose(f);
    }
    s_open.shots++;
    if (r.pass) s_open.passed++;
    s_open.rate = r.rate;
    write_info(s_open);
    return rec;
}

bool store_undo_last(void)
{
    std::lock_guard<std::recursive_mutex> g(s_mtx);
    if (!s_has_open || s_open.shots == 0) return false;
    std::string d = sdir(s_open.id);
    std::string p = d + "/shots.bin";
    FILE *f = fopen(p.c_str(), "rb");
    if (!f) return false;
    std::vector<ShotRec> v;
    ShotRec r;
    while (fread(&r, sizeof(r), 1, f) == 1) v.push_back(r);
    fclose(f);
    if (v.empty()) return false;
    bool was_pass = v.back().pass;
    char wn[64];
    snprintf(wn, sizeof(wn), "/w%04u.bin", v.back().index);
    unlink((d + wn).c_str());
    v.pop_back();
    f = fopen(p.c_str(), "wb");
    if (f) {
        if (!v.empty()) fwrite(v.data(), sizeof(ShotRec), v.size(), f);
        fclose(f);
    }
    s_open.shots = v.size();
    if (was_pass && s_open.passed) s_open.passed--;
    write_info(s_open);
    return true;
}

void store_backfill_time(void)
{
    std::lock_guard<std::recursive_mutex> g(s_mtx);
    if (!s_has_open || !clock_valid()) return;
    int64_t now = clock_epoch();
    uint32_t up = clock_uptime_s(), boot = clock_boot_id();
    bool changed = false;
    if (s_open.start_epoch == 0 && s_open.boot_id == boot) {
        s_open.start_epoch = now - (up - s_open.start_uptime);
        changed = true;
    }
    std::string p = sdir(s_open.id) + "/shots.bin";
    FILE *f = fopen(p.c_str(), "r+b");
    if (f) {
        ShotRec r;
        long pos = 0;
        while (fread(&r, sizeof(r), 1, f) == 1) {
            if (r.epoch == 0 && r.boot_id == boot) {
                r.epoch = now - (up - r.uptime);
                fseek(f, pos, SEEK_SET);
                fwrite(&r, sizeof(r), 1, f);
                fseek(f, pos + sizeof(r), SEEK_SET);
            }
            pos += sizeof(r);
        }
        fclose(f);
    }
    if (changed) write_info(s_open);
}

std::vector<SeriesInfo> store_list(void)
{
    std::lock_guard<std::recursive_mutex> g(s_mtx);
    std::vector<SeriesInfo> out;
    DIR *d = opendir(ROOT);
    if (!d) return out;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] != 't') continue;
        uint32_t id = strtoul(e->d_name + 1, nullptr, 10);
        SeriesInfo si;
        if (s_has_open && id == s_open.id) out.push_back(s_open);
        else if (read_info(id, si)) out.push_back(si);
    }
    closedir(d);
    std::sort(out.begin(), out.end(), [](const SeriesInfo &a, const SeriesInfo &b) { return a.id > b.id; });
    return out;
}

bool store_load(uint32_t id, SeriesInfo &info, std::vector<ShotRec> &shots)
{
    std::lock_guard<std::recursive_mutex> g(s_mtx);
    if (s_has_open && id == s_open.id) info = s_open;
    else if (!read_info(id, info)) return false;
    shots.clear();
    FILE *f = fopen((sdir(id) + "/shots.bin").c_str(), "rb");
    if (f) {
        ShotRec r;
        while (fread(&r, sizeof(r), 1, f) == 1) shots.push_back(r);
        fclose(f);
    }
    return true;
}

bool store_load_wave(uint32_t id, int index, float *t0, float *dt, std::vector<float> &w)
{
    char wn[64];
    snprintf(wn, sizeof(wn), "/w%04u.bin", index);
    FILE *f = fopen((sdir(id) + wn).c_str(), "rb");
    if (!f) return false;
    uint32_t n = 0;
    bool ok = fread(t0, 4, 1, f) == 1 && fread(dt, 4, 1, f) == 1 && fread(&n, 4, 1, f) == 1 && n < 100000;
    if (ok) {
        w.resize(n);
        ok = fread(w.data(), 4, n, f) == n;
    }
    fclose(f);
    return ok;
}

bool store_delete(uint32_t id)
{
    std::lock_guard<std::recursive_mutex> g(s_mtx);
    if (s_has_open && id == s_open.id) {
        s_has_open = false;
        save_open_id(0);
    }
    std::string d = sdir(id);
    DIR *dd = opendir(d.c_str());
    if (!dd) return false;
    struct dirent *e;
    std::vector<std::string> files;
    while ((e = readdir(dd))) files.push_back(d + "/" + e->d_name);
    closedir(dd);
    for (auto &p : files) unlink(p.c_str());
    return rmdir(d.c_str()) == 0;
}
