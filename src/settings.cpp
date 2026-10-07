#include "settings.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"

static const char *TAG = "settings";
static const char *NS = "shock";

Settings g_set;

void settings_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        // esimene käivitus: arendaja WiFi võetakse browser-püsivara seadetest, kui need on olemas
        if (nvs_open("browser", NVS_READONLY, &h) == ESP_OK) {
            size_t l = sizeof(g_set.sta_ssid);
            nvs_get_str(h, "ssid", g_set.sta_ssid, &l);
            l = sizeof(g_set.sta_pass);
            nvs_get_str(h, "pass", g_set.sta_pass, &l);
            nvs_close(h);
        }
        return;
    }
    // salvestis võib olla vanemast versioonist (lühem): uued väljad on struktuuri lõpus -> loe eesliide
    size_t len = 0;
    if (nvs_get_blob(h, "set", nullptr, &len) == ESP_OK && len > 0 && len <= sizeof(Settings)) {
        Settings s;
        uint8_t *buf = (uint8_t *)malloc(len);
        if (buf && nvs_get_blob(h, "set", buf, &len) == ESP_OK) {
            memcpy(&s, buf, len);
            g_set = s;
            if (len < sizeof(Settings)) ESP_LOGW(TAG, "seaded uuendatud (%u -> %u baiti)", (unsigned)len, (unsigned)sizeof(Settings));
        }
        free(buf);
    } else {
        ESP_LOGW(TAG, "seaded puuduvad/vigased, kasutan vaikeväärtusi");
    }
    nvs_close(h);
}

void settings_save(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u32(h, "ver", sizeof(Settings));
    nvs_set_blob(h, "set", &g_set, sizeof(Settings));
    nvs_commit(h);
    nvs_close(h);
}

float settings_min_peak(void) { return 5.0f; }

void settings_apply_preset(Settings &s)
{
    float m = s.mass_kg > 0.001f ? s.mass_kg : 0.001f;
    switch (s.preset) {
    case PRESET_MIL_HSC1: s.peak_g = 20; s.td_ms = 23; s.shots_per_dir = 3; break;
    case PRESET_MIL_HSC2: s.peak_g = 5; s.td_ms = 23; s.shots_per_dir = 3; break;
    case PRESET_MIL_GROUND: s.peak_g = roundf(M_PI / 4 * 40 * 10) / 10; s.td_ms = 11; s.shots_per_dir = 3; break;
    case PRESET_UN_CELL: s.peak_g = 150; s.td_ms = 6; s.shots_per_dir = 3; break;
    case PRESET_UN_LARGE_CELL: s.peak_g = 50; s.td_ms = 11; s.shots_per_dir = 3; break;
    case PRESET_UN_SMALL_BATT:
        s.peak_g = fminf(150.0f, sqrtf(100850.0f / m));
        s.td_ms = 6;
        s.shots_per_dir = 3;
        break;
    case PRESET_UN_LARGE_BATT:
        s.peak_g = fminf(50.0f, sqrtf(30000.0f / m));
        s.td_ms = 11;
        s.shots_per_dir = 3;
        break;
    default: return;
    }
    s.peak_g = roundf(s.peak_g * 10) / 10;
    // standardi tolerantsid
    s.tol_peak = 20;
    s.tol_td = 10;
    s.tol_dv = 20;
    s.band = 20;
    if (s.peak_g > s.amax) s.peak_g = s.amax;
}

const char *preset_name(int p)
{
    switch (p) {
    case PRESET_MIL_HSC1: return "MIL HSC-I 20 g / 23 ms";
    case PRESET_MIL_HSC2: return "MIL HSC-II 5 g / 23 ms";
    case PRESET_MIL_GROUND: return TR("MIL maapealne 31,4 g / 11 ms", "MIL ground 31.4 g / 11 ms");
    case PRESET_UN_CELL: return TR("UN38.3 element 150 g / 6 ms", "UN38.3 cell 150 g / 6 ms");
    case PRESET_UN_LARGE_CELL: return TR("UN38.3 suur element 50 g / 11 ms", "UN38.3 large cell 50 g / 11 ms");
    case PRESET_UN_SMALL_BATT: return TR("UN38.3 väike aku (mass)", "UN38.3 small battery (mass)");
    case PRESET_UN_LARGE_BATT: return TR("UN38.3 suur aku (mass)", "UN38.3 large battery (mass)");
    default: return TR("Kasutaja seaded", "User settings");
    }
}

const char *preset_standard(int p)
{
    if (p >= PRESET_MIL_HSC1 && p <= PRESET_MIL_GROUND) return "MIL-STD-810H Method 516.8";
    if (preset_is_un(p)) return "UN38.3 T.4 (Shock)";
    return TR("Kasutaja (516.8 tolerantsid)", "User (516.8 tolerances)");
}

bool preset_is_un(int p) { return p >= PRESET_UN_CELL && p <= PRESET_UN_LARGE_BATT; }
