#include "report.h"

#include <math.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>

#include "clock.h"
#include "pdf.h"
#include "settings.h"
#include "shock.h"
#include "version.h"

static const char *AXIS_NAMES[4] = {"Auto", "X", "Y", "Z"};

float un_mass_loss_limit(float m)
{
    if (m < 1.0f) return 0.5f;
    if (m <= 75.0f) return 0.2f;
    return 0.1f;
}

static bool un_checks_pass(const SeriesInfo &si, std::string *why)
{
    if (!si.un_checks) return true;
    if (!si.checks_done) {
        if (why) *why = TR("operaatori kontrollid tegemata", "operator checks not done");
        return false;
    }
    bool ok = true;
    std::string w;
    if (si.mass_before > 0) {
        float loss = (si.mass_before - si.mass_after) / si.mass_before * 100;
        if (loss > un_mass_loss_limit(si.mass_before)) { ok = false; w += TR("massikadu; ", "mass loss; "); }
    }
    if (si.ocv_before > 0 && si.ocv_after < 0.9f * si.ocv_before) { ok = false; w += TR("pinge < 90 %; ", "voltage < 90 %; "); }
    if (!(si.no_leak && si.no_vent && si.no_disasm && si.no_rupture && si.no_fire)) { ok = false; w += TR("visuaalne kontroll; ", "visual check; "); }
    if (why) *why = w;
    return ok;
}

const char *report_verdict(const SeriesInfo &si, bool *pass)
{
    bool complete = si.shots >= si.shots_per_dir * 6;
    bool ok = si.passed == si.shots && un_checks_pass(si, nullptr);
    if (pass) *pass = ok && complete;
    if (!ok && si.shots > 0) return TR("EBAÕNNESTUS", "FAIL");
    if (!complete) return TR("POOLELI", "IN PROGRESS");
    return ok ? TR("LÄBITUD", "PASS") : TR("EBAÕNNESTUS", "FAIL");
}

static std::string f2s(float v, int dec, bool comma = false)
{
    char b[32];
    snprintf(b, sizeof(b), "%.*f", dec, v);
    std::string s = b;
    if (comma && !g_set.lang)
        for (auto &c : s)
            if (c == '.') c = ',';
    return s;
}

// kümnendkoma arvudes (punkt kahe numbri vahel)
static std::string dc(const std::string &in)
{
    std::string o = in;
    if (g_set.lang) return o;
    for (size_t i = 1; i + 1 < o.size(); i++)
        if (o[i] == '.' && isdigit((unsigned char)o[i - 1]) && isdigit((unsigned char)o[i + 1])) o[i] = ',';
    return o;
}

// lühenda tekst laiusele w (pt)
static std::string fit(const std::string &t, float size, bool bold, float w)
{
    if (Pdf::text_width(t, size, bold) <= w) return t;
    std::string o = t;
    while (!o.empty() && Pdf::text_width(o + "…", size, bold) > w) {
        o.pop_back();
        while (!o.empty() && ((unsigned char)o.back() & 0xC0) == 0x80) o.pop_back();  // UTF-8 jätk
        if (!o.empty() && ((unsigned char)o.back() & 0xC0) == 0xC0) o.pop_back();
    }
    return o + "…";
}

static std::string shot_time(const ShotRec &r)
{
    if (r.epoch) return clock_fmt(r.epoch);
    char b[40];
    snprintf(b, sizeof(b), TR("kell seadistamata (+%lus)", "clock not set (+%lus)"), (unsigned long)r.uptime);
    return b;
}

// ---------------- CSV ----------------
// eesti: ';' ja kümnendkoma (Excel ET); inglise: ',' ja punkt. Tekstiväljad jutumärkides.
static std::string q(const std::string &v)
{
    std::string o = "\"";
    for (char c : v) {
        if (c == '"') o += '"';
        o += c;
    }
    return o + "\"";
}

bool report_csv(uint32_t id, std::string &o)
{
    SeriesInfo si;
    std::vector<ShotRec> shots;
    if (!store_load(id, si, shots)) return false;
    bool pass;
    const char *verdict = report_verdict(si, &pass);
    const std::string S = g_set.lang ? "," : ";";
    o = "\xEF\xBB\xBF";  // UTF-8 BOM (Excel)
    char b[256];
    auto kv = [&](const char *k, const std::string &v) { o += q(k) + S + q(v) + "\r\n"; };
    kv(TR("Raport", "Report"), "Half-Sine Shock Pulse");
    kv(TR("Seade", "Device"), std::string(FW_NAME " v" FW_VERSION));
    kv(TR("Seeria", "Series"), std::to_string(si.id));
    kv(TR("Katseobjekt", "Test item"), si.object);
    kv(TR("Seerianumber", "Serial number"), si.serial);
    kv(TR("Operaator", "Operator"), si.oper);
    kv(TR("Algus", "Start"), si.start_epoch ? clock_fmt(si.start_epoch) : TR("kell seadistamata", "clock not set"));
    kv("Standard", preset_standard(si.preset));
    kv(TR("Eelseadistus", "Preset"), preset_name(si.preset));
    kv(TR("A nominaal (g)", "A nominal (g)"), f2s(si.a_nom, 1, true));
    kv(TR("TD nominaal (ms)", "TD nominal (ms)"), f2s(si.td_nom, 2, true));
    kv(TR("Tipu tolerants (%)", "Peak tolerance (%)"), f2s(si.tol_peak, 1, true));
    kv(TR("TD tolerants (%)", "TD tolerance (%)"), f2s(si.tol_td, 1, true));
    kv(TR("dV tolerants (%)", "dV tolerance (%)"), f2s(si.tol_dv, 1, true) + (si.dv_check ? "" : TR(" (informatiivne)", " (informative)")));
    kv(TR("Kuju koridor (% A)", "Shape band (% A)"), f2s(si.band, 1, true) + (si.shape_check ? "" : TR(" (informatiivne)", " (informative)")));
    kv(TR("Valimisagedus (Hz)", "Sample rate (Hz)"), f2s(si.rate, 0, true));
    kv(TR("Koondtulemus", "Overall result"), verdict);
    snprintf(b, sizeof(b), "%u / %u", si.passed, si.shots);
    kv(TR("Läbitud lööke", "Shocks passed"), b);
    if (si.un_checks) {
        kv(TR("Mass enne (g)", "Mass before (g)"), f2s(si.mass_before, 3, true));
        kv(TR("Mass pärast (g)", "Mass after (g)"), f2s(si.mass_after, 3, true));
        kv(TR("OCV enne (V)", "OCV before (V)"), f2s(si.ocv_before, 3, true));
        kv(TR("OCV pärast (V)", "OCV after (V)"), f2s(si.ocv_after, 3, true));
        kv(TR("Leke/gaas/lagunemine/purunemine/tuli puudub", "No leakage/venting/disassembly/rupture/fire"),
           (si.no_leak && si.no_vent && si.no_disasm && si.no_rupture && si.no_fire) ? TR("jah", "yes") : TR("ei", "no"));
    }
    const char *hdr[] = {"Nr", TR("Suund", "Direction"), TR("Löök", "Shock"), TR("Aeg", "Time"), TR("Telg", "Axis"),
                         TR("Tipp (g)", "Peak (g)"), TR("Tipp valim (g)", "Peak sample (g)"), "TD (ms)", "dV (m/s)",
                         TR("Kuju", "Shape"), TR("Koridorist väljas (g)", "Outside band (g)"), TR("Küllastus", "Saturated"),
                         TR("Valimeid impulsil", "Samples in pulse"), TR("Tulemus", "Result"), TR("Põhjus", "Reason")};
    o += "\r\n";
    for (int i = 0; i < 15; i++) o += (i ? S : "") + q(hdr[i]);
    o += "\r\n";
    for (auto &r : shots) {
        snprintf(b, sizeof(b), "%c%c", r.polarity > 0 ? '+' : '-', 'X' + r.axis);
        o += std::to_string(r.index + 1) + S + q(DIR_NAMES[r.dir]) + S + std::to_string(r.num) + S + q(shot_time(r)) + S + q(b) + S;
        o += f2s(r.peak, 2, true) + S + f2s(r.peak_raw, 2, true) + S + f2s(r.td, 3, true) + S + f2s(r.dv, 4, true) + S +
             q(r.shape_ok ? "OK" : TR("väljas", "outside")) + S + f2s(r.shape_worst, 2, true) + S +
             q(r.saturated ? TR("jah", "yes") : TR("ei", "no")) + S + std::to_string(r.pulse_samples) + S +
             q(r.pass ? TR("LÄBITUD", "PASS") : TR("EBAÕNNESTUS", "FAIL")) + S + q(r.reason) + "\r\n";
    }
    // toorandmed (pikk formaat)
    o += "\r\n" + q(TR("Toorandmed", "Raw data")) + "\r\nNr" + S + q(TR("Suund", "Direction")) + S +
         q(TR("Aeg nominaalse impulsi algusest (ms)", "Time from nominal pulse start (ms)")) + S +
         q(TR("Kiirendus (g)", "Acceleration (g)")) + "\r\n";
    std::vector<float> w;
    for (auto &r : shots) {
        float t0, dt;
        if (!store_load_wave(id, r.index, &t0, &dt, w)) continue;
        std::string pre = std::to_string(r.index + 1) + S + q(DIR_NAMES[r.dir]) + S;
        for (size_t k = 0; k < w.size(); k++) o += pre + f2s(t0 + k * dt, 3, true) + S + f2s(w[k], 3, true) + "\r\n";
    }
    return true;
}

// ---------------- PDF ----------------
static void graph(Pdf &p, float x, float y, float w, float h, const SeriesInfo &si, const ShotRec &r,
                  const std::vector<float> &wv, float t0, float dt)
{
    const float A = si.a_nom, T = si.td_nom;
    float tmin = -0.4f * T, tmax = 2.0f * T;
    float gmax = fmaxf(A * (1 + si.band / 100) * 1.1f, r.peak * 1.1f);
    float gmin = -A * (si.band / 100) * 2.0f;
    for (float v : wv) {
        if (v > gmax) gmax = v;
        if (v < gmin) gmin = v;
    }
    char b[96];
    // pealkiri
    snprintf(b, sizeof(b), TR("Löök %u: %s %u/%u", "Shock %u: %s %u/%u"), r.index + 1, DIR_NAMES[r.dir], r.num, si.shots_per_dir);
    p.fill_color(0, 0, 0);
    p.text(x, y - 4, 9, true, b);
    if (r.pass) p.fill_color(0.1f, 0.55f, 0.2f);
    else p.fill_color(0.8f, 0.1f, 0.1f);
    p.text_right(x + w, y - 4, 9, true, r.pass ? TR("LÄBITUD", "PASS") : TR("EBAÕNNESTUS", "FAIL"));
    p.fill_color(0.2f, 0.2f, 0.2f);
    snprintf(b, sizeof(b), TR("tipp %.1f g   TD %.2f ms   ΔV %.3f m/s", "peak %.1f g   TD %.2f ms   ΔV %.3f m/s"), r.peak, r.td, r.dv);
    p.text(x, y + 8, 7.5f, false, dc(b));
    float gy = y + 14, gh = h - 30, gx = x + 26, gw = w - 30;
    auto X = [&](float t) { return gx + (t - tmin) / (tmax - tmin) * gw; };
    auto Y = [&](float g) { return gy + gh - (g - gmin) / (gmax - gmin) * gh; };
    // raam ja võrk
    p.line_width(0.3f);
    p.stroke_color(0.85f, 0.85f, 0.85f);
    float step = A > 100 ? 50 : A > 40 ? 20 : A > 15 ? 10 : 5;
    p.fill_color(0.35f, 0.35f, 0.35f);
    for (float g = ceilf(gmin / step) * step; g <= gmax; g += step) {
        p.line(gx, Y(g), gx + gw, Y(g));
        snprintf(b, sizeof(b), "%.0f", g);
        p.text_right(gx - 2, Y(g) + 2.5f, 6, false, b);
    }
    float ts = T >= 40 ? 20 : T >= 15 ? 10 : T >= 6 ? 5 : 2;
    for (float t = ceilf(tmin / ts) * ts; t <= tmax; t += ts) {
        p.line(X(t), gy, X(t), gy + gh);
        snprintf(b, sizeof(b), "%.0f", t + 0.0f);
        p.text_center(X(t), gy + gh + 8, 6, false, b);
    }
    p.text_right(gx + gw, gy + gh + 15, 6, false, "ms");
    p.text(gx - 24, gy + 6, 6, false, "g");
    p.stroke_color(0.5f, 0.5f, 0.5f);
    p.rect(gx, gy, gw, gh, false);
    p.save();
    p.clip_rect(gx, gy, gw, gh);
    // koridor
    std::vector<float> lo, hi, id;
    for (int k = 0; k <= 60; k++) {
        float t = tmin + (tmax - tmin) * k / 60;
        float l, u;
        shock_band(t, A, T, si.band / 100, l, u);
        lo.push_back(X(t)); lo.push_back(Y(l));
        hi.push_back(X(t)); hi.push_back(Y(u));
        id.push_back(X(t)); id.push_back(Y(shock_ideal(t, A, T)));
    }
    p.line_width(0.6f);
    p.stroke_color(0.85f, 0.2f, 0.2f);
    p.dash(true);
    p.polyline(lo);
    p.polyline(hi);
    p.dash(false);
    p.stroke_color(0.6f, 0.6f, 0.6f);
    p.polyline(id);
    // mõõdetud
    std::vector<float> m;
    for (size_t k = 0; k < wv.size(); k++) {
        m.push_back(X(t0 + k * dt));
        m.push_back(Y(wv[k]));
    }
    p.line_width(0.9f);
    p.stroke_color(0.1f, 0.3f, 0.85f);
    p.polyline(m);
    p.restore();
    p.line_width(0.5f);
}

bool report_pdf(uint32_t id, std::string &out)
{
    SeriesInfo si;
    std::vector<ShotRec> shots;
    if (!store_load(id, si, shots)) return false;
    Pdf p;
    p.new_page();
    char b[200];
    float y = 50;
    const float L = 40, R = Pdf::W - 40;

    p.fill_color(0, 0, 0);
    p.text(L, y, 18, true, TR("Löögitesti raport", "Shock test report"));
    p.text_right(R, y, 10, false, "Half-Sine Shock Pulse");
    y += 8;
    p.line_width(1);
    p.stroke_color(0, 0, 0);
    p.line(L, y, R, y);
    y += 18;

    auto row = [&](float x, const char *k, const std::string &v) {
        p.fill_color(0.35f, 0.35f, 0.35f);
        p.text(x, y, 9, false, k);
        p.fill_color(0, 0, 0);
        float kw = x < 300 ? 80 : 95;
        p.text(x + kw, y, 9, true, fit(v, 9, true, (x < 300 ? 300 : R) - x - kw - 6));
    };
    float y0 = y;
    row(L, TR("Seeria", "Series"), std::to_string(si.id)); y += 13;
    row(L, TR("Katseobjekt", "Test item"), si.object[0] ? si.object : "—"); y += 13;
    row(L, TR("Seerianumber", "Serial number"), si.serial[0] ? si.serial : "—"); y += 13;
    row(L, TR("Operaator", "Operator"), si.oper[0] ? si.oper : "—"); y += 13;
    row(L, TR("Algus", "Start"), si.start_epoch ? clock_fmt(si.start_epoch) : TR("kell seadistamata", "clock not set")); y += 13;
    row(L, TR("Seade", "Device"), FW_NAME " v" FW_VERSION ", ADXL375"); y += 13;
    float y1 = y;
    y = y0;
    float C = 310;
    row(C, "Standard", preset_standard(si.preset)); y += 13;
    row(C, TR("Eelseadistus", "Preset"), preset_name(si.preset)); y += 13;
    if (preset_is_un(si.preset) && (si.preset == PRESET_UN_SMALL_BATT || si.preset == PRESET_UN_LARGE_BATT)) {
        row(C, TR("Aku mass", "Battery mass"), f2s(si.mass_kg, 3, true) + " kg"); y += 13;
    }
    snprintf(b, sizeof(b), "%.1f g  (%.1f … %.1f g)", si.a_nom, si.a_nom * (1 - si.tol_peak / 100),
             si.a_nom * (1 + si.tol_peak / 100));
    row(C, TR("Tipp A", "Peak A"), dc(b)); y += 13;
    snprintf(b, sizeof(b), "%.2f ms  (±%.0f %%)", si.td_nom, si.tol_td);
    row(C, TR("Kestus TD", "Duration TD"), dc(b)); y += 13;
    float dvn = 2 * si.a_nom * 9.80665f * si.td_nom / 1000 / (float)M_PI;
    snprintf(b, sizeof(b), "%.3f m/s  (±%.0f %%)%s", dvn, si.tol_dv, si.dv_check ? "" : " info");
    row(C, "ΔV", dc(b)); y += 13;
    snprintf(b, sizeof(b), "±%.0f %% A, ±10 %% TD%s", si.band, si.shape_check ? "" : TR(" (informatiivne)", " (informative)"));
    row(C, TR("Kuju koridor", "Shape band"), b); y += 13;
    snprintf(b, sizeof(b), "%s, %.0f Hz (ODR %u)", AXIS_NAMES[si.axis % 4], si.rate, si.odr);
    row(C, TR("Telg, sagedus", "Axis, rate"), b); y += 13;
    y = fmaxf(y, y1) + 8;

    // koondtulemus
    bool pass;
    const char *verdict = report_verdict(si, &pass);
    bool pending = !pass && si.passed == si.shots && si.shots < si.shots_per_dir * 6;
    if (pending) p.fill_color(0.95f, 0.85f, 0.5f);
    else if (pass) p.fill_color(0.75f, 0.92f, 0.78f);
    else p.fill_color(0.97f, 0.75f, 0.75f);
    p.stroke_color(0.3f, 0.3f, 0.3f);
    p.rect(L, y, R - L, 34, true);
    p.fill_color(0, 0, 0);
    p.text(L + 10, y + 22, 15, true, std::string(TR("KOONDTULEMUS: ", "OVERALL RESULT: ")) + verdict);
    snprintf(b, sizeof(b), TR("läbitud %u / %u lööki (plaan %u = 6 suunda × %u)", "passed %u / %u shocks (plan %u = 6 directions × %u)"), si.passed, si.shots,
             si.shots_per_dir * 6, si.shots_per_dir);
    p.text_right(R - 10, y + 21, 9, false, b);
    y += 50;

    // UN38.3 kontrollid
    if (si.un_checks) {
        p.text(L, y, 11, true, TR("UN38.3 operaatori kontrollid", "UN38.3 operator checks"));
        y += 14;
        std::string why;
        bool ok = un_checks_pass(si, &why);
        if (!si.checks_done) {
            p.text(L, y, 9, false, TR("Kontrollid on tegemata.", "Checks not done."));
            y += 13;
        } else {
            float loss = si.mass_before > 0 ? (si.mass_before - si.mass_after) / si.mass_before * 100 : 0;
            snprintf(b, sizeof(b), TR("Mass enne %.3f g, pärast %.3f g, kadu %.3f %% (lubatud ≤ %.1f %%)", "Mass before %.3f g, after %.3f g, loss %.3f %% (allowed ≤ %.1f %%)"), si.mass_before,
                     si.mass_after, loss, un_mass_loss_limit(si.mass_before));
            p.text(L, y, 9, false, dc(b)); y += 13;
            float ocv = si.ocv_before > 0 ? si.ocv_after / si.ocv_before * 100 : 0;
            snprintf(b, sizeof(b), TR("Avatud ahela pinge enne %.3f V, pärast %.3f V (%.1f %%, nõue ≥ 90 %%)", "Open-circuit voltage before %.3f V, after %.3f V (%.1f %%, required ≥ 90 %%)"), si.ocv_before,
                     si.ocv_after, ocv);
            p.text(L, y, 9, false, dc(b)); y += 13;
            snprintf(b, sizeof(b), TR("Leke: %s · Gaas: %s · Lagunemine: %s · Purunemine: %s · Tuli: %s", "Leakage: %s · Venting: %s · Disassembly: %s · Rupture: %s · Fire: %s"),
                     si.no_leak ? TR("ei", "no") : TR("JAH", "YES"), si.no_vent ? TR("ei", "no") : TR("JAH", "YES"),
                     si.no_disasm ? TR("ei", "no") : TR("JAH", "YES"), si.no_rupture ? TR("ei", "no") : TR("JAH", "YES"),
                     si.no_fire ? TR("ei", "no") : TR("JAH", "YES"));
            p.text(L, y, 9, false, b); y += 13;
        }
        p.text(L, y, 9, true, ok ? TR("Kontrollid: LÄBITUD", "Checks: PASS") : (std::string(TR("Kontrollid: EBAÕNNESTUS — ", "Checks: FAIL — ")) + why).c_str());
        y += 20;
    }

    // tabel
    const float cx[] = {L, L + 26, L + 58, L + 132, L + 162, L + 204, L + 248, L + 296, L + 334};
    const char *hd[] = {"Nr", TR("Suund", "Dir."), TR("Aeg", "Time"), TR("Telg", "Axis"), TR("Tipp g", "Peak g"), "TD ms", "ΔV m/s", TR("Kuju", "Shape"), TR("Tulemus / põhjus", "Result / reason")};
    auto header = [&]() {
        p.fill_color(0.9f, 0.9f, 0.9f);
        p.rect(L, y - 10, R - L, 14, true, false);
        p.fill_color(0, 0, 0);
        for (int i = 0; i < 9; i++) p.text(cx[i] + 2, y, 8, true, hd[i]);
        y += 14;
    };
    p.text(L, y, 11, true, TR("Löögid", "Shocks"));
    y += 16;
    header();
    for (auto &r : shots) {
        if (y > Pdf::H - 50) {
            p.new_page();
            y = 50;
            header();
        }
        p.fill_color(0, 0, 0);
        snprintf(b, sizeof(b), "%u", r.index + 1);
        p.text(cx[0] + 2, y, 8, false, b);
        snprintf(b, sizeof(b), "%s %u", DIR_NAMES[r.dir], r.num);
        p.text(cx[1] + 2, y, 8, false, b);
        std::string t = r.epoch ? clock_fmt(r.epoch).substr(5) : "—";
        p.text(cx[2] + 2, y, 8, false, t);
        snprintf(b, sizeof(b), "%c%c", r.polarity > 0 ? '+' : '-', 'X' + r.axis);
        p.text(cx[3] + 2, y, 8, false, b);
        p.fill_color(r.pass_peak ? 0 : 0.8f, 0, 0);
        p.text(cx[4] + 2, y, 8, false, f2s(r.peak, 1, true) + (r.saturated ? "!" : ""));
        p.fill_color(r.pass_td ? 0 : 0.8f, 0, 0);
        p.text(cx[5] + 2, y, 8, false, f2s(r.td, 2, true));
        p.fill_color(r.pass_dv || !si.dv_check ? 0 : 0.8f, 0, 0);
        p.text(cx[6] + 2, y, 8, false, f2s(r.dv, 3, true));
        p.fill_color(r.shape_ok || !si.shape_check ? 0 : 0.8f, 0, 0);
        p.text(cx[7] + 2, y, 8, false, r.shape_ok ? "OK" : TR("väljas", "outside"));
        if (r.pass) p.fill_color(0.1f, 0.55f, 0.2f);
        else p.fill_color(0.8f, 0.1f, 0.1f);
        std::string res = r.pass ? TR("LÄBITUD", "PASS") : std::string(TR("EBAÕNN.: ", "FAIL: ")) + r.reason;
        p.text(cx[8] + 2, y, 8, true, fit(res, 8, true, R - cx[8] - 4));
        p.stroke_color(0.85f, 0.85f, 0.85f);
        p.line_width(0.3f);
        p.line(L, y + 4, R, y + 4);
        y += 13;
    }
    if (shots.empty()) {
        p.fill_color(0, 0, 0);
        p.text(L, y, 9, false, TR("Lööke pole.", "No shocks."));
        y += 13;
    }
    // märkus
    y += 10;
    if (y > Pdf::H - 90) {
        p.new_page();
        y = 50;
    }
    p.fill_color(0.3f, 0.3f, 0.3f);
    p.text(L, y, 7.5f, false,
           TR("Hindamine: tipp, TD (10 % punktidest, poolsiinuse baaskestus), ΔV (integraal üle impulsi) ja kuju koridor", "Evaluation: peak, TD (from 10 % points, half-sine base duration), ΔV (integral over pulse) and shape band"));
    y += 10;
    p.text(L, y, 7.5f, false,
           TR("±0,2·A / ±0,1·TD jälgimisaknas 2,4·TD (MIL-STD-810H 516.8 joonis 516.8-5). Mõõtesüsteem: ADXL375 MEMS,", "±0.2·A / ±0.1·TD in a 2.4·TD monitoring window (MIL-STD-810H 516.8 figure 516.8-5). Measurement: ADXL375 MEMS,"));
    y += 10;
    p.text(L, y, 7.5f, false,
           TR("valimisagedus < 10·f_max (Annex A p 1.1) — kontroll- ja häälestusmõõtmine, mitte kvalifitseerimismõõtmine.", "sample rate < 10·f_max (Annex A 1.1) — verification/setup measurement, not a qualification measurement."));

    // graafikud: kõik kui ≤ 60, muidu ebaõnnestunud + iga suuna esimene (kuni 120)
    std::vector<const ShotRec *> sel;
    for (auto &r : shots)
        if (shots.size() <= 60 || !r.pass || r.num == 1) sel.push_back(&r);
    if (sel.size() > 120) sel.resize(120);
    std::vector<float> wv;
    const float gw = (R - L - 20) / 2, gh = 235;
    for (size_t i = 0; i < sel.size(); i++) {
        int k = i % 6;
        if (k == 0) {
            p.new_page();
            p.fill_color(0, 0, 0);
            p.text(L, 40, 11, true, TR("Impulsid (sinine = mõõdetud, hall = nominaal, punane = tolerantsikoridor)", "Pulses (blue = measured, grey = nominal, red = tolerance band)"));
        }
        float gx = L + (k % 2) * (gw + 20), gy = 62 + (k / 2) * (gh + 12);
        float t0, dt;
        if (!store_load_wave(id, sel[i]->index, &t0, &dt, wv)) wv.clear(), t0 = 0, dt = 1;
        graph(p, gx, gy, gw, gh, si, *sel[i], wv, t0, dt);
    }
    snprintf(b, sizeof(b), TR("%s v%s · seeria %lu%s%s", "%s v%s · series %lu%s%s"), FW_NAME, FW_VERSION, (unsigned long)si.id,
             clock_valid() ? TR(" · koostatud ", " · generated ") : "", clock_valid() ? clock_fmt(clock_epoch()).c_str() : "");
    out = p.finish(b);
    return true;
}
