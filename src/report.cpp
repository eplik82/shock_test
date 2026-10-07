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
        if (why) *why = "operaatori kontrollid tegemata";
        return false;
    }
    bool ok = true;
    std::string w;
    if (si.mass_before > 0) {
        float loss = (si.mass_before - si.mass_after) / si.mass_before * 100;
        if (loss > un_mass_loss_limit(si.mass_before)) { ok = false; w += "massikadu; "; }
    }
    if (si.ocv_before > 0 && si.ocv_after < 0.9f * si.ocv_before) { ok = false; w += "pinge < 90 %; "; }
    if (!(si.no_leak && si.no_vent && si.no_disasm && si.no_rupture && si.no_fire)) { ok = false; w += "visuaalne kontroll; "; }
    if (why) *why = w;
    return ok;
}

const char *report_verdict(const SeriesInfo &si, bool *pass)
{
    bool complete = si.shots >= si.shots_per_dir * 6;
    bool ok = si.passed == si.shots && un_checks_pass(si, nullptr);
    if (pass) *pass = ok && complete;
    if (!ok && si.shots > 0) return "EBAÕNNESTUS";
    if (!complete) return "POOLELI";
    return ok ? "LÄBITUD" : "EBAÕNNESTUS";
}

static std::string f2s(float v, int dec, bool comma = false)
{
    char b[32];
    snprintf(b, sizeof(b), "%.*f", dec, v);
    std::string s = b;
    if (comma)
        for (auto &c : s)
            if (c == '.') c = ',';
    return s;
}

// kümnendkoma arvudes (punkt kahe numbri vahel)
static std::string dc(const std::string &in)
{
    std::string o = in;
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
    snprintf(b, sizeof(b), "kell seadistamata (+%lus)", (unsigned long)r.uptime);
    return b;
}

// ---------------- CSV ----------------
bool report_csv(uint32_t id, std::string &o)
{
    SeriesInfo si;
    std::vector<ShotRec> shots;
    if (!store_load(id, si, shots)) return false;
    bool pass;
    const char *verdict = report_verdict(si, &pass);
    o = "\xEF\xBB\xBF";  // UTF-8 BOM (Excel)
    char b[256];
    auto kv = [&](const char *k, const std::string &v) { o += std::string(k) + ";" + v + "\r\n"; };
    kv("Raport", "Half-Sine Shock Pulse");
    kv("Seade", std::string(FW_NAME " v" FW_VERSION));
    kv("Seeria", std::to_string(si.id));
    kv("Katseobjekt", si.object);
    kv("Seerianumber", si.serial);
    kv("Operaator", si.oper);
    kv("Algus", si.start_epoch ? clock_fmt(si.start_epoch) : "kell seadistamata");
    kv("Standard", preset_standard(si.preset));
    kv("Eelseadistus", preset_name(si.preset));
    kv("A nominaal (g)", f2s(si.a_nom, 1, true));
    kv("TD nominaal (ms)", f2s(si.td_nom, 2, true));
    kv("Tipu tolerants (%)", f2s(si.tol_peak, 1, true));
    kv("TD tolerants (%)", f2s(si.tol_td, 1, true));
    kv("dV tolerants (%)", f2s(si.tol_dv, 1, true) + (si.dv_check ? "" : " (informatiivne)"));
    kv("Kuju koridor (% A)", f2s(si.band, 1, true) + (si.shape_check ? "" : " (informatiivne)"));
    kv("Valimisagedus (Hz)", f2s(si.rate, 0, true));
    kv("Koondtulemus", verdict);
    snprintf(b, sizeof(b), "%u / %u", si.passed, si.shots);
    kv("Läbitud lööke", b);
    if (si.un_checks) {
        kv("Mass enne (g)", f2s(si.mass_before, 3, true));
        kv("Mass pärast (g)", f2s(si.mass_after, 3, true));
        kv("OCV enne (V)", f2s(si.ocv_before, 3, true));
        kv("OCV pärast (V)", f2s(si.ocv_after, 3, true));
        kv("Leke/gaas/lagunemine/purunemine/tuli puudub",
           (si.no_leak && si.no_vent && si.no_disasm && si.no_rupture && si.no_fire) ? "jah" : "ei");
    }
    o += "\r\nNr;Suund;Löök;Aeg;Telg;Tipp (g);Tipp valim (g);TD (ms);dV (m/s);Kuju;Koridorist väljas (g);"
         "Küllastus;Valimeid impulsil;Tulemus;Põhjus\r\n";
    for (auto &r : shots) {
        snprintf(b, sizeof(b), "%u;%s;%u;%s;%c%c;", r.index + 1, DIR_NAMES[r.dir], r.num, shot_time(r).c_str(),
                 r.polarity > 0 ? '+' : '-', 'X' + r.axis);
        o += b;
        o += f2s(r.peak, 2, true) + ";" + f2s(r.peak_raw, 2, true) + ";" + f2s(r.td, 3, true) + ";" +
             f2s(r.dv, 4, true) + ";" + (r.shape_ok ? "OK" : "väljas") + ";" + f2s(r.shape_worst, 2, true) + ";" +
             (r.saturated ? "jah" : "ei") + ";" + std::to_string(r.pulse_samples) + ";" +
             (r.pass ? "LÄBITUD" : "EBAÕNNESTUS") + ";" + r.reason + "\r\n";
    }
    // toorandmed (pikk formaat)
    o += "\r\nToorandmed\r\nNr;Suund;Aeg nominaalse impulsi algusest (ms);Kiirendus (g)\r\n";
    std::vector<float> w;
    for (auto &r : shots) {
        float t0, dt;
        if (!store_load_wave(id, r.index, &t0, &dt, w)) continue;
        for (size_t k = 0; k < w.size(); k++) {
            snprintf(b, sizeof(b), "%u;%s;", r.index + 1, DIR_NAMES[r.dir]);
            o += b;
            o += f2s(t0 + k * dt, 3, true) + ";" + f2s(w[k], 3, true) + "\r\n";
        }
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
    snprintf(b, sizeof(b), "Löök %u: %s %u/%u", r.index + 1, DIR_NAMES[r.dir], r.num, si.shots_per_dir);
    p.fill_color(0, 0, 0);
    p.text(x, y - 4, 9, true, b);
    if (r.pass) p.fill_color(0.1f, 0.55f, 0.2f);
    else p.fill_color(0.8f, 0.1f, 0.1f);
    p.text_right(x + w, y - 4, 9, true, r.pass ? "LÄBITUD" : "EBAÕNNESTUS");
    p.fill_color(0.2f, 0.2f, 0.2f);
    snprintf(b, sizeof(b), "tipp %.1f g   TD %.2f ms   ΔV %.3f m/s", r.peak, r.td, r.dv);
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
    for (int k = 0; k <= 120; k++) {
        float t = tmin + (tmax - tmin) * k / 120;
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
    p.text(L, y, 18, true, "Löögitesti raport");
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
    row(L, "Seeria", std::to_string(si.id)); y += 13;
    row(L, "Katseobjekt", si.object[0] ? si.object : "—"); y += 13;
    row(L, "Seerianumber", si.serial[0] ? si.serial : "—"); y += 13;
    row(L, "Operaator", si.oper[0] ? si.oper : "—"); y += 13;
    row(L, "Algus", si.start_epoch ? clock_fmt(si.start_epoch) : "kell seadistamata"); y += 13;
    row(L, "Seade", FW_NAME " v" FW_VERSION ", ADXL375"); y += 13;
    float y1 = y;
    y = y0;
    float C = 310;
    row(C, "Standard", preset_standard(si.preset)); y += 13;
    row(C, "Eelseadistus", preset_name(si.preset)); y += 13;
    if (preset_is_un(si.preset) && (si.preset == PRESET_UN_SMALL_BATT || si.preset == PRESET_UN_LARGE_BATT)) {
        row(C, "Aku mass", f2s(si.mass_kg, 3, true) + " kg"); y += 13;
    }
    snprintf(b, sizeof(b), "%.1f g  (%.1f … %.1f g)", si.a_nom, si.a_nom * (1 - si.tol_peak / 100),
             si.a_nom * (1 + si.tol_peak / 100));
    row(C, "Tipp A", dc(b)); y += 13;
    snprintf(b, sizeof(b), "%.2f ms  (±%.0f %%)", si.td_nom, si.tol_td);
    row(C, "Kestus TD", dc(b)); y += 13;
    float dvn = 2 * si.a_nom * 9.80665f * si.td_nom / 1000 / (float)M_PI;
    snprintf(b, sizeof(b), "%.3f m/s  (±%.0f %%)%s", dvn, si.tol_dv, si.dv_check ? "" : " info");
    row(C, "ΔV", dc(b)); y += 13;
    snprintf(b, sizeof(b), "±%.0f %% A, ±10 %% TD%s", si.band, si.shape_check ? "" : " (informatiivne)");
    row(C, "Kuju koridor", b); y += 13;
    snprintf(b, sizeof(b), "%s, %.0f Hz (ODR %u)", AXIS_NAMES[si.axis % 4], si.rate, si.odr);
    row(C, "Telg, sagedus", b); y += 13;
    y = fmaxf(y, y1) + 8;

    // koondtulemus
    bool pass;
    const char *verdict = report_verdict(si, &pass);
    bool pending = strcmp(verdict, "POOLELI") == 0;
    if (pending) p.fill_color(0.95f, 0.85f, 0.5f);
    else if (pass) p.fill_color(0.75f, 0.92f, 0.78f);
    else p.fill_color(0.97f, 0.75f, 0.75f);
    p.stroke_color(0.3f, 0.3f, 0.3f);
    p.rect(L, y, R - L, 34, true);
    p.fill_color(0, 0, 0);
    p.text(L + 10, y + 22, 15, true, std::string("KOONDTULEMUS: ") + verdict);
    snprintf(b, sizeof(b), "läbitud %u / %u lööki (plaan %u = 6 suunda × %u)", si.passed, si.shots,
             si.shots_per_dir * 6, si.shots_per_dir);
    p.text_right(R - 10, y + 21, 9, false, b);
    y += 50;

    // UN38.3 kontrollid
    if (si.un_checks) {
        p.text(L, y, 11, true, "UN38.3 operaatori kontrollid");
        y += 14;
        std::string why;
        bool ok = un_checks_pass(si, &why);
        if (!si.checks_done) {
            p.text(L, y, 9, false, "Kontrollid on tegemata.");
            y += 13;
        } else {
            float loss = si.mass_before > 0 ? (si.mass_before - si.mass_after) / si.mass_before * 100 : 0;
            snprintf(b, sizeof(b), "Mass enne %.3f g, pärast %.3f g, kadu %.3f %% (lubatud ≤ %.1f %%)", si.mass_before,
                     si.mass_after, loss, un_mass_loss_limit(si.mass_before));
            p.text(L, y, 9, false, dc(b)); y += 13;
            float ocv = si.ocv_before > 0 ? si.ocv_after / si.ocv_before * 100 : 0;
            snprintf(b, sizeof(b), "Avatud ahela pinge enne %.3f V, pärast %.3f V (%.1f %%, nõue ≥ 90 %%)", si.ocv_before,
                     si.ocv_after, ocv);
            p.text(L, y, 9, false, dc(b)); y += 13;
            snprintf(b, sizeof(b), "Leke: %s · Gaas: %s · Lagunemine: %s · Purunemine: %s · Tuli: %s",
                     si.no_leak ? "ei" : "JAH", si.no_vent ? "ei" : "JAH", si.no_disasm ? "ei" : "JAH",
                     si.no_rupture ? "ei" : "JAH", si.no_fire ? "ei" : "JAH");
            p.text(L, y, 9, false, b); y += 13;
        }
        p.text(L, y, 9, true, ok ? "Kontrollid: LÄBITUD" : ("Kontrollid: EBAÕNNESTUS — " + why).c_str());
        y += 20;
    }

    // tabel
    const float cx[] = {L, L + 26, L + 58, L + 132, L + 162, L + 204, L + 248, L + 296, L + 334};
    const char *hd[] = {"Nr", "Suund", "Aeg", "Telg", "Tipp g", "TD ms", "ΔV m/s", "Kuju", "Tulemus / põhjus"};
    auto header = [&]() {
        p.fill_color(0.9f, 0.9f, 0.9f);
        p.rect(L, y - 10, R - L, 14, true, false);
        p.fill_color(0, 0, 0);
        for (int i = 0; i < 9; i++) p.text(cx[i] + 2, y, 8, true, hd[i]);
        y += 14;
    };
    p.text(L, y, 11, true, "Löögid");
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
        p.text(cx[7] + 2, y, 8, false, r.shape_ok ? "OK" : "väljas");
        if (r.pass) p.fill_color(0.1f, 0.55f, 0.2f);
        else p.fill_color(0.8f, 0.1f, 0.1f);
        std::string res = r.pass ? "LÄBITUD" : std::string("EBAÕNN.: ") + r.reason;
        p.text(cx[8] + 2, y, 8, true, fit(res, 8, true, R - cx[8] - 4));
        p.stroke_color(0.85f, 0.85f, 0.85f);
        p.line_width(0.3f);
        p.line(L, y + 4, R, y + 4);
        y += 13;
    }
    if (shots.empty()) {
        p.fill_color(0, 0, 0);
        p.text(L, y, 9, false, "Lööke pole.");
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
           "Hindamine: tipp, TD (10 % punktidest, poolsiinuse baaskestus), ΔV (integraal üle impulsi) ja kuju koridor");
    y += 10;
    p.text(L, y, 7.5f, false,
           "±0,2·A / ±0,1·TD jälgimisaknas 2,4·TD (MIL-STD-810H 516.8 joonis 516.8-5). Mõõtesüsteem: ADXL375 MEMS,");
    y += 10;
    p.text(L, y, 7.5f, false,
           "valimisagedus < 10·f_max (Annex A p 1.1) — kontroll- ja häälestusmõõtmine, mitte kvalifitseerimismõõtmine.");

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
            p.text(L, 40, 11, true, "Impulsid (sinine = mõõdetud, hall = nominaal, punane = tolerantsikoridor)");
        }
        float gx = L + (k % 2) * (gw + 20), gy = 62 + (k / 2) * (gh + 12);
        float t0, dt;
        if (!store_load_wave(id, sel[i]->index, &t0, &dt, wv)) wv.clear(), t0 = 0, dt = 1;
        graph(p, gx, gy, gw, gh, si, *sel[i], wv, t0, dt);
    }
    snprintf(b, sizeof(b), "%s v%s · seeria %lu%s%s", FW_NAME, FW_VERSION, (unsigned long)si.id,
             clock_valid() ? " · koostatud " : "", clock_valid() ? clock_fmt(clock_epoch()).c_str() : "");
    out = p.finish(b);
    return true;
}
