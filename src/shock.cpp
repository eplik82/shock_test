#include "shock.h"

#include <math.h>
#include <string.h>

#include "adxl375.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "settings.h"

static const char *TAG = "shock";

static shock_result_cb_t s_cb;
static volatile bool s_enabled;
static volatile ShockState s_state = SH_IDLE;
static volatile float s_live;

// 10 % punktide vahe -> poolsiinuse baaskestus: T10 = TD * (1 - 2*asin(0.1)/pi)
static const float K10 = 1.0f - 2.0f * 0.100167421f / (float)M_PI;
static const float G0 = 9.80665f;

ShockState shock_state(void) { return s_state; }
float shock_live_g(void) { return s_live; }

void shock_arm(bool on)
{
    s_enabled = on;
    if (!on) s_state = SH_IDLE;
}

// Ideaalne nominaalne poolsiinus
static inline float ideal_at(float t, float A, float T)
{
    return (t >= 0 && t <= T) ? A * sinf((float)M_PI * t / T) : 0.0f;
}

// Koridor: amplituud ±band·A, ajas ±0,1·TD (MIL-STD-810H joonis 516.8-5)
static void band_at(float t, float A, float T, float band, float &lo, float &hi)
{
    const float dt = 0.1f * T;
    float a = t - dt, b = t + dt;
    // max üle [a,b]: kui tipp (T/2) on vahemikus -> A, muidu otspunktide max
    float mx = (a <= T / 2 && b >= T / 2) ? A : fmaxf(ideal_at(a, A, T), ideal_at(b, A, T));
    // min üle [a,b]: unimodaalne -> otspunktide min; kui vahemik ulatub impulsist välja -> 0
    float mn = fminf(ideal_at(a, A, T), ideal_at(b, A, T));
    if (a < 0 || b > T) mn = 0;
    hi = mx + band * A;
    lo = mn - band * A;
}

float shock_ideal(float t, float A, float T) { return ideal_at(t, A, T); }
void shock_band(float t, float A, float T, float band, float &lo, float &hi) { band_at(t, A, T, band, lo, hi); }

void shock_analyze(const float *a, int n, float rate, float trig_index, ShotResult &r)
{
    const Settings &s = g_set;
    const float A = s.peak_g, T = s.td_ms;
    const float dtms = 1000.0f / rate;
    r.a_nom = A;
    r.td_nom = T;
    r.peak_min = A * (1 - s.tol_peak / 100);
    r.peak_max = A * (1 + s.tol_peak / 100);
    r.td_min = T * (1 - s.tol_td / 100);
    r.td_max = T * (1 + s.tol_td / 100);
    r.dv_nom = 2.0f * A * G0 * (T / 1000.0f) / (float)M_PI;
    r.dv_min = r.dv_nom * (1 - s.tol_dv / 100);
    r.dv_max = r.dv_nom * (1 + s.tol_dv / 100);
    r.rate = rate;
    r.dt_ms = dtms;
    r.reason.clear();

    // tipp käivituspunkti ümbruses
    int tn = (int)(T / dtms);
    int i0 = (int)trig_index - tn / 2, i1 = (int)trig_index + tn + tn / 2;
    if (i0 < 1) i0 = 1;
    if (i1 > n - 2) i1 = n - 2;
    int im = i0;
    for (int i = i0; i <= i1; i++)
        if (a[i] > a[im]) im = i;
    float pk = a[im];
    r.peak_raw = pk;
    float y0 = a[im - 1], y1 = a[im], y2 = a[im + 1];
    float den = y0 - 2 * y1 + y2;
    float tpk = (float)im;
    if (den < -1e-6f) {
        float p = 0.5f * (y0 - y2) / den;
        if (p > -1 && p < 1) {
            pk = y1 - 0.25f * (y0 - y2) * p;
            tpk = im + p;
        }
    }
    r.peak = pk;

    // 10 % ületuspunktid (lineaarne interpolatsioon)
    float th = 0.1f * pk;
    float t1 = 0, t2 = (float)(n - 1);
    for (int i = im; i > 0; i--)
        if (a[i - 1] < th) {
            t1 = (i - 1) + (th - a[i - 1]) / (a[i] - a[i - 1]);
            break;
        }
    for (int i = im; i < n - 1; i++)
        if (a[i + 1] < th) {
            t2 = i + (a[i] - th) / (a[i] - a[i + 1]);
            break;
        }
    float td_samples = (t2 - t1) / K10;
    r.td = td_samples * dtms;
    float ts = t1 - (0.100167421f / (float)M_PI) * td_samples;  // impulsi algus (valimites)
    r.pulse_samples = (int)lroundf(td_samples);

    // ΔV = ∫a dt üle [ts, ts+TD] (trapets, otstes interpolatsioon)
    {
        float te = ts + td_samples;
        double acc = 0;
        auto val = [&](float x) {
            if (x <= 0) return a[0];
            if (x >= n - 1) return a[n - 1];
            int k = (int)x;
            float f = x - k;
            return a[k] * (1 - f) + a[k + 1] * f;
        };
        float x = ts;
        while (x < te) {
            float nx = floorf(x) + 1;
            if (nx > te) nx = te;
            acc += 0.5 * (val(x) + val(nx)) * (nx - x);
            x = nx;
        }
        r.dv = (float)(acc * (dtms / 1000.0) * G0);
    }

    // kuju: nominaalne impulss joondatud mõõdetud impulsi keskele; aken 0,4·TD enne ... 1,0·TD pärast
    float tc = (ts + td_samples / 2) * dtms;  // ms
    float nom0 = tc - T / 2;                  // nominaalse impulsi algus (ms, valimi 0 suhtes)
    float w0 = nom0 - 0.4f * T, w1 = nom0 + 2.0f * T;
    int k0 = (int)ceilf(w0 / dtms), k1 = (int)floorf(w1 / dtms);
    if (k0 < 0) k0 = 0;
    if (k1 > n - 1) k1 = n - 1;
    r.wave.clear();
    r.band_lo.clear();
    r.band_hi.clear();
    r.ideal.clear();
    r.shape_viol = 0;
    r.shape_worst = 0;
    r.t0_ms = k0 * dtms - nom0;
    for (int k = k0; k <= k1; k++) {
        float t = k * dtms - nom0;
        float lo, hi;
        band_at(t, A, T, s.band / 100, lo, hi);
        r.wave.push_back(a[k]);
        r.band_lo.push_back(lo);
        r.band_hi.push_back(hi);
        r.ideal.push_back(ideal_at(t, A, T));
        float out = a[k] > hi ? a[k] - hi : a[k] < lo ? lo - a[k] : 0;
        if (out > 0) {
            r.shape_viol++;
            if (out > r.shape_worst) r.shape_worst = out;
        }
    }
    r.shape_ok = r.shape_viol == 0;

    r.pass_peak = r.peak >= r.peak_min && r.peak <= r.peak_max;
    r.pass_td = r.td >= r.td_min && r.td <= r.td_max;
    r.pass_dv = r.dv >= r.dv_min && r.dv <= r.dv_max;
    r.pass = r.pass_peak && r.pass_td && (r.pass_dv || !s.dv_check) && (r.shape_ok || !s.shape_check) && !r.saturated;
    if (r.saturated) r.reason += "andur küllastunud (>200 g); ";
    if (!r.pass_peak) r.reason += r.peak < r.peak_min ? "tipp liiga madal; " : "tipp liiga kõrge; ";
    if (!r.pass_td) r.reason += r.td < r.td_min ? "kestus liiga lühike; " : "kestus liiga pikk; ";
    if (!r.pass_dv && s.dv_check) r.reason += "ΔV väljas; ";
    if (!r.shape_ok && s.shape_check) r.reason += "kuju koridorist väljas; ";
    if (r.reason.size() > 2) r.reason.resize(r.reason.size() - 2);
}

static void shock_task(void *)
{
    uint32_t idx = adxl_count();
    float base[3] = {0, 0, 0};
    bool base_ok = false;
    uint32_t trig_at = 0;
    int trig_axis = 0;
    int64_t holdoff_until = 0;
    float live_peak = 0;
    int64_t live_t = 0;
    AccSample chunk[256];
    const size_t cap_max = 4096;
    AccSample *cap = (AccSample *)heap_caps_malloc(cap_max * sizeof(AccSample), MALLOC_CAP_SPIRAM);
    float *wave = (float *)heap_caps_malloc(cap_max * sizeof(float), MALLOC_CAP_SPIRAM);

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(5));
        const Settings &s = g_set;
        float rate = adxl_measured_rate();
        uint32_t cnt = adxl_count();
        if (cnt - idx > adxl_ring_size() / 2) idx = cnt - 256;  // jäime maha
        int pre_n = (int)((1.0f * s.td_ms + 10) * rate / 1000);
        int post_n = (int)((2.5f * s.td_ms + 10) * rate / 1000);
        if (pre_n + post_n > (int)cap_max) post_n = cap_max - pre_n;
        float trig = fmaxf(s.trig_pct / 100.0f * s.peak_g, 1.5f);

        // uued valimid: eelpinge (aeglane keskmine), reaalajas näit, käivitus
        while (idx < cnt) {
            size_t m = adxl_copy(idx, chunk, cnt - idx < 256 ? cnt - idx : 256);
            if (m == 0) {
                idx = cnt;
                break;
            }
            for (size_t i = 0; i < m; i++) {
                float v[3] = {(float)chunk[i].x, (float)chunk[i].y, (float)chunk[i].z};
                if (!base_ok) {
                    for (int k = 0; k < 3; k++) base[k] = v[k];
                    base_ok = true;
                }
                float mx = 0;
                int mk = 0;
                for (int k = 0; k < 3; k++) {
                    float d = fabsf((v[k] - base[k]) * ADXL_LSB_G * s.cal[k]);
                    if (d > mx) {
                        mx = d;
                        mk = k;
                    }
                }
                if (mx > live_peak) live_peak = mx;
                bool sel_ok = s.axis == AXIS_AUTO || mk == s.axis - 1;
                if (s_state == SH_ARMED && esp_timer_get_time() > holdoff_until && sel_ok && mx >= trig) {
                    s_state = SH_CAPTURING;
                    trig_at = idx + i;
                    trig_axis = mk;
                } else if (s_state != SH_CAPTURING && mx < trig * 0.5f) {
                    // eelpinge järgib ainult rahulikku signaali (~0,3 s ajakonstant)
                    for (int k = 0; k < 3; k++) base[k] += (v[k] - base[k]) * (3.0f / rate);
                }
            }
            idx += m;
        }
        int64_t now = esp_timer_get_time();
        if (now - live_t > 150000) {
            s_live = live_peak;
            live_peak = 0;
            live_t = now;
        }

        if (s_state == SH_IDLE && s_enabled) s_state = SH_ARMED;
        if (s_state == SH_DONE && s_enabled && now > holdoff_until) s_state = SH_ARMED;

        if (s_state == SH_CAPTURING && adxl_count() >= trig_at + post_n) {
            uint32_t from = trig_at - pre_n;
            int n = pre_n + post_n;
            size_t got = adxl_copy(from, cap, n);
            if ((int)got != n) {
                ESP_LOGW(TAG, "puhver üle kirjutatud");
                s_state = s_enabled ? SH_ARMED : SH_IDLE;
                continue;
            }
            int ax = s.axis == AXIS_AUTO ? trig_axis : s.axis - 1;
            // polaarsus: suurima |kõrvalekalde| märk
            float best = 0;
            bool sat = false;
            for (int i = 0; i < n; i++) {
                int16_t raw = ax == 0 ? cap[i].x : ax == 1 ? cap[i].y : cap[i].z;
                if (raw >= 4090 || raw <= -4090) sat = true;
                float d = (raw - base[ax]) * ADXL_LSB_G * s.cal[ax];
                wave[i] = d;
                if (fabsf(d) > fabsf(best)) best = d;
            }
            int pol = best < 0 ? -1 : 1;
            for (int i = 0; i < n; i++) wave[i] *= pol;
            ShotResult *r = new ShotResult();
            r->axis = ax;
            r->polarity = pol;
            r->saturated = sat;
            shock_analyze(wave, n, rate, (float)pre_n, *r);
            ESP_LOGI(TAG, "löök: telg %c%c tipp %.1f g, TD %.2f ms, dV %.3f m/s, kuju %s -> %s", pol > 0 ? '+' : '-',
                     'X' + ax, r->peak, r->td, r->dv, r->shape_ok ? "OK" : "väljas", r->pass ? "LÄBITUD" : "EBAÕNNESTUS");
            s_state = SH_DONE;
            holdoff_until = esp_timer_get_time() + 500000 + (int64_t)(s.td_ms * 3000);
            if (s_cb) s_cb(*r);
            delete r;
        }
    }
}

void shock_init(shock_result_cb_t cb)
{
    s_cb = cb;
    xTaskCreatePinnedToCore(shock_task, "shock", 8192, nullptr, 5, nullptr, 0);
}
