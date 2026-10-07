#include "ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

#include "adxl375.h"
#include "board.h"
#include "clock.h"
#include "fonts.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hardreset.h"
#include "lvgl_port.h"
#include "net.h"
#include "report.h"
#include "settings.h"
#include "store.h"
#include "version.h"

// ---------------------------------------------------------------------------
// Värvid
#define C_BG lv_color_hex(0x12161c)
#define C_CARD lv_color_hex(0x1c222b)
#define C_LINE lv_color_hex(0x2c3440)
#define C_FG lv_color_hex(0xe8eaf0)
#define C_MUT lv_color_hex(0x9aa1ad)
#define C_OK lv_color_hex(0x16a34a)
#define C_BAD lv_color_hex(0xdc2626)
#define C_WARN lv_color_hex(0xf59e0b)
#define C_ACC lv_color_hex(0x2563eb)
#define C_WAVE lv_color_hex(0x60a5fa)

static std::string fmtf(float v, int dec)
{
    char b[32];
    snprintf(b, sizeof(b), "%.*f", dec, v);
    if (!g_set.lang)
        for (char *p = b; *p; p++)
            if (*p == '.') *p = ',';
    return b;
}

static float parsef(const char *s)
{
    char b[32];
    strlcpy(b, s, sizeof(b));
    for (char *p = b; *p; p++)
        if (*p == ',') *p = '.';
    return strtof(b, nullptr);
}

// ---------------------------------------------------------------------------
// Klaviatuur (eesti tähed)
#define KBF LV_KEYBOARD_CTRL_BUTTON_FLAGS
static const char *const KB_LOWER[] = {
    "1#", "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "ü", "õ", LV_SYMBOL_BACKSPACE, "\n",
    "ABC", "a", "s", "d", "f", "g", "h", "j", "k", "l", "ö", "ä", "-", "\n",
    "_", "š", "z", "ž", "x", "c", "v", "b", "n", "m", ".", ",", "/", "\n",
    LV_SYMBOL_KEYBOARD, " ", LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""};
static const char *const KB_UPPER[] = {
    "1#", "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P", "Ü", "Õ", LV_SYMBOL_BACKSPACE, "\n",
    "abc", "A", "S", "D", "F", "G", "H", "J", "K", "L", "Ö", "Ä", "-", "\n",
    "_", "Š", "Z", "Ž", "X", "C", "V", "B", "N", "M", ".", ",", "@", "\n",
    LV_SYMBOL_KEYBOARD, " ", LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, LV_SYMBOL_OK, ""};
#define C(x) ((lv_buttonmatrix_ctrl_t)(x))
static const lv_buttonmatrix_ctrl_t KB_CTRL[] = {
    C(KBF | 5), C(4), C(4), C(4), C(4), C(4), C(4), C(4), C(4), C(4), C(4), C(4), C(4), C(LV_BUTTONMATRIX_CTRL_CHECKED | 6),
    C(KBF | 6), C(4), C(4), C(4), C(4), C(4), C(4), C(4), C(4), C(4), C(4), C(4), C(4),
    C(4), C(4), C(4), C(4), C(4), C(4), C(4), C(4), C(4), C(4), C(4), C(4), C(4),
    C(KBF | 3), C(12), C(KBF | 2), C(KBF | 2), C(KBF | 4)};
#undef C

static lv_obj_t *s_kb;
static lv_obj_t *s_kb_owner;  // konteiner, mille kõrgust klaviatuur vähendab
static int32_t s_kb_owner_h;

static void kb_hide(void)
{
    if (!s_kb) return;
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(s_kb, nullptr);
    if (s_kb_owner) {
        lv_obj_set_height(s_kb_owner, s_kb_owner_h);
        s_kb_owner = nullptr;
    }
}

static void kb_event(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_READY || c == LV_EVENT_CANCEL) kb_hide();
}

static void ta_event(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target_obj(e);
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_FOCUSED || c == LV_EVENT_CLICKED) {
        bool numeric = (bool)(intptr_t)lv_event_get_user_data(e);
        lv_keyboard_set_textarea(s_kb, ta);
        lv_keyboard_set_mode(s_kb, numeric ? LV_KEYBOARD_MODE_NUMBER : LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_obj_remove_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_kb);
        // konteiner, mis kerib: vähenda kõrgust, et väli jääks nähtavale
        lv_obj_t *p = lv_obj_get_parent(ta);
        while (p && !lv_obj_has_flag(p, LV_OBJ_FLAG_SCROLLABLE)) p = lv_obj_get_parent(p);
        if (p && p != s_kb_owner && lv_obj_get_y(p) + lv_obj_get_height(p) > LCD_H - 230) {
            if (s_kb_owner) lv_obj_set_height(s_kb_owner, s_kb_owner_h);
            s_kb_owner = p;
            s_kb_owner_h = lv_obj_get_height(p);
            int32_t top = lv_obj_get_y(p);
            lv_obj_set_height(p, LCD_H - 230 - top);
        }
        lv_obj_update_layout(lv_obj_get_screen(ta));
        lv_obj_scroll_to_view_recursive(ta, LV_ANIM_OFF);
    } else if (c == LV_EVENT_DEFOCUSED) {
        if (lv_keyboard_get_textarea(s_kb) == ta) kb_hide();
    }
}

// ---------------------------------------------------------------------------
// Abifunktsioonid
static lv_obj_t *label(lv_obj_t *par, const lv_font_t *f, lv_color_t col, const char *txt)
{
    lv_obj_t *l = lv_label_create(par);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, col, 0);
    lv_label_set_text(l, txt);
    return l;
}

static lv_obj_t *button(lv_obj_t *par, const char *txt, lv_color_t bg, int w, int h, lv_event_cb_t cb, void *ud = nullptr)
{
    lv_obj_t *b = lv_button_create(par);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_t *l = label(b, font_bold(18), lv_color_white(), txt);
    lv_obj_center(l);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    return b;
}

static void button_text(lv_obj_t *b, const char *txt) { lv_label_set_text(lv_obj_get_child(b, 0), txt); }

static lv_obj_t *panel(lv_obj_t *par, int x, int y, int w, int h)
{
    lv_obj_t *p = lv_obj_create(par);
    lv_obj_set_pos(p, x, y);
    lv_obj_set_size(p, w, h);
    lv_obj_set_style_bg_color(p, C_CARD, 0);
    lv_obj_set_style_border_color(p, C_LINE, 0);
    lv_obj_set_style_border_width(p, 1, 0);
    lv_obj_set_style_radius(p, 10, 0);
    lv_obj_set_style_pad_all(p, 10, 0);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

static lv_obj_t *textarea(lv_obj_t *par, int w, bool numeric, int maxlen)
{
    lv_obj_t *ta = lv_textarea_create(par);
    lv_textarea_set_one_line(ta, true);
    lv_obj_set_width(ta, w);
    lv_textarea_set_max_length(ta, maxlen);
    lv_obj_set_style_text_font(ta, font_reg(20), 0);
    if (numeric) lv_textarea_set_accepted_chars(ta, "0123456789,.-");
    lv_obj_add_event_cb(ta, ta_event, LV_EVENT_ALL, (void *)(intptr_t)numeric);
    return ta;
}

// modaalne aken ülemisel kihil
static lv_obj_t *modal(int w, int h, int y)
{
    lv_obj_t *bg = lv_obj_create(lv_layer_top());
    lv_obj_set_size(bg, LCD_W, LCD_H);
    lv_obj_set_style_bg_color(bg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_60, 0);
    lv_obj_set_style_border_width(bg, 0, 0);
    lv_obj_set_style_radius(bg, 0, 0);
    lv_obj_remove_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *p = panel(bg, (LCD_W - w) / 2, y, w, h);
    lv_obj_add_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    return p;
}

static void modal_close(lv_obj_t *p)
{
    kb_hide();
    lv_obj_delete_async(lv_obj_get_parent(p));
}

// ---------------------------------------------------------------------------
// Testi vaade
static lv_obj_t *s_scr_test, *s_scr_set;
static lv_obj_t *s_preset_l, *s_wifi_l, *s_banner, *s_res_box, *s_res_l, *s_peak_l, *s_lim_l, *s_bar, *s_mk_min,
    *s_mk_max, *s_td_l, *s_dv_l, *s_shape_l, *s_reason_l, *s_chart, *s_chart_y, *s_series_l, *s_live_l, *s_btn_arm,
    *s_btn_undo, *s_btn_end, *s_btn_new, *s_sat_l;
static lv_chart_series_t *s_ser_hi, *s_ser_lo, *s_ser_id, *s_ser_w;
static bool s_have_result;
static ShotResult s_last;
static ShotRec s_last_rec;

static void open_settings(lv_event_t *);
static void new_series_dialog(void);
static void end_series_dialog(void);

static void update_limits(void)
{
    const Settings &s = store_has_open() ? g_set : g_set;
    float A = s.peak_g;
    std::string t = "min " + fmtf(A * (1 - s.tol_peak / 100), 1) + " g     max " + fmtf(A * (1 + s.tol_peak / 100), 1) + " g";
    lv_label_set_text(s_lim_l, t.c_str());
    if (A * (1 + s.tol_peak / 100) > ADXL_RANGE_G) {
        lv_label_set_text(s_sat_l, TR("Ülemine piir ületab anduri mõõtepiiri 200 g", "Upper limit exceeds sensor range 200 g"));
        lv_obj_remove_flag(s_sat_l, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_sat_l, LV_OBJ_FLAG_HIDDEN);
    }
    // riba: 0 .. 1,4·A ; markerid min/max kohal
    float mx = A * 1.4f;
    int bw = lv_obj_get_width(s_bar);
    lv_bar_set_range(s_bar, 0, (int32_t)(mx * 10));
    lv_obj_set_x(s_mk_min, lv_obj_get_x(s_bar) + (int)(bw * (1 - s.tol_peak / 100) / 1.4f) - 1);
    lv_obj_set_x(s_mk_max, lv_obj_get_x(s_bar) + (int)(bw * (1 + s.tol_peak / 100) / 1.4f) - 1);
}

static void show_state_box(void)
{
    if (s_have_result) return;
    ShockState st = shock_state();
    const char *t;
    lv_color_t c;
    if (!store_has_open()) {
        t = TR("ALUSTA SEERIAT", "START A SERIES");
        c = C_LINE;
    } else if (st == SH_ARMED || st == SH_CAPTURING) {
        t = TR("OOTAB LÖÖKI", "WAITING FOR SHOCK");
        c = C_ACC;
    } else {
        t = TR("PEATATUD", "PAUSED");
        c = C_LINE;
    }
    lv_label_set_text(s_res_l, t);
    lv_obj_set_style_bg_color(s_res_box, c, 0);
}

static void show_result(void)
{
    const ShotResult &r = s_last;
    lv_label_set_text(s_res_l, r.pass ? TR("LÄBITUD", "PASS") : TR("EBAÕNNESTUS", "FAIL"));
    lv_obj_set_style_bg_color(s_res_box, r.pass ? C_OK : C_BAD, 0);
    std::string pk = fmtf(r.peak, 1) + " g";
    lv_label_set_text(s_peak_l, pk.c_str());
    lv_obj_set_style_text_color(s_peak_l, r.pass_peak ? C_OK : C_BAD, 0);
    lv_bar_set_value(s_bar, (int32_t)(fminf(r.peak, r.a_nom * 1.4f) * 10), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_bar, r.pass_peak ? C_OK : C_BAD, LV_PART_INDICATOR);
    std::string t = "TD   " + fmtf(r.td, 2) + " ms   (" + fmtf(r.td_min, 2) + " … " + fmtf(r.td_max, 2) + ")";
    lv_label_set_text(s_td_l, t.c_str());
    lv_obj_set_style_text_color(s_td_l, r.pass_td ? C_FG : C_BAD, 0);
    t = "ΔV   " + fmtf(r.dv, 3) + " m/s   (" + fmtf(r.dv_min, 3) + " … " + fmtf(r.dv_max, 3) + ")";
    lv_label_set_text(s_dv_l, t.c_str());
    lv_obj_set_style_text_color(s_dv_l, r.pass_dv ? C_FG : (g_set.dv_check ? C_BAD : C_WARN), 0);
    char b[96];
    snprintf(b, sizeof(b), TR("Kuju  %s  ·  %c%c  ·  %d p", "Shape  %s  ·  %c%c  ·  %d pts"), r.shape_ok ? TR("koridoris", "in band") : TR("väljas", "outside"),
             r.polarity > 0 ? '+' : '-', 'X' + r.axis, r.pulse_samples);
    lv_label_set_text(s_shape_l, b);
    lv_obj_set_style_text_color(s_shape_l, r.shape_ok ? C_FG : (g_set.shape_check ? C_BAD : C_WARN), 0);
    lv_label_set_text(s_reason_l, r.reason.c_str());

    // graafik
    int n = (int)r.wave.size();
    if (n < 2) return;
    float gmax = r.a_nom * (1 + g_set.band / 100) * 1.15f, gmin = -r.a_nom * 0.3f;
    for (float v : r.wave) {
        gmax = fmaxf(gmax, v);
        gmin = fminf(gmin, v);
    }
    lv_chart_set_point_count(s_chart, n);
    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, (int32_t)(gmin * 10), (int32_t)(gmax * 10));
    for (int i = 0; i < n; i++) {
        lv_chart_set_value_by_id(s_chart, s_ser_w, i, (int32_t)(r.wave[i] * 10));
        lv_chart_set_value_by_id(s_chart, s_ser_hi, i, (int32_t)(r.band_hi[i] * 10));
        lv_chart_set_value_by_id(s_chart, s_ser_lo, i, (int32_t)(r.band_lo[i] * 10));
        lv_chart_set_value_by_id(s_chart, s_ser_id, i, (int32_t)(r.ideal[i] * 10));
    }
    lv_chart_refresh(s_chart);
    snprintf(b, sizeof(b), "%s g\n\n\n\n\n\n\n%s g", fmtf(gmax, 0).c_str(), fmtf(gmin, 0).c_str());
    lv_label_set_text(s_chart_y, b);
}

static void clear_result(void)
{
    s_have_result = false;
    lv_label_set_text(s_peak_l, "— g");
    lv_obj_set_style_text_color(s_peak_l, C_FG, 0);
    lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
    lv_label_set_text(s_td_l, "TD   —");
    lv_label_set_text(s_dv_l, "ΔV   —");
    lv_label_set_text(s_shape_l, TR("Kuju  —", "Shape  —"));
    lv_obj_set_style_text_color(s_td_l, C_FG, 0);
    lv_obj_set_style_text_color(s_dv_l, C_FG, 0);
    lv_obj_set_style_text_color(s_shape_l, C_FG, 0);
    lv_label_set_text(s_reason_l, "");
    lv_chart_set_point_count(s_chart, 2);
    lv_chart_set_all_value(s_chart, s_ser_w, LV_CHART_POINT_NONE);
    lv_chart_set_all_value(s_chart, s_ser_hi, LV_CHART_POINT_NONE);
    lv_chart_set_all_value(s_chart, s_ser_lo, LV_CHART_POINT_NONE);
    lv_chart_set_all_value(s_chart, s_ser_id, LV_CHART_POINT_NONE);
    lv_label_set_text(s_chart_y, "");
    show_state_box();
}

static void update_series(void)
{
    char b[200];
    if (!store_has_open()) {
        snprintf(b, sizeof(b), TR("Avatud seeriat pole. Seaded: %s, %s g / %s ms", "No open series. Settings: %s, %s g / %s ms"), preset_name(g_set.preset),
                 fmtf(g_set.peak_g, 1).c_str(), fmtf(g_set.td_ms, 2).c_str());
        lv_obj_add_state(s_btn_undo, LV_STATE_DISABLED);
        lv_obj_add_state(s_btn_end, LV_STATE_DISABLED);
    } else {
        const SeriesInfo &si = store_open_series();
        int dir, num;
        store_next_pos(&dir, &num);
        if (store_series_complete())
            snprintf(b, sizeof(b), TR("Seeria #%lu %s · kõik %u lööki tehtud · läbitud %u", "Series #%lu %s · all %u shocks done · passed %u"), (unsigned long)si.id,
                     si.object, si.shots, si.passed);
        else
            snprintf(b, sizeof(b), TR("Seeria #%lu %s · järgmine: %s %d/%u · %u/%u lööki · läbitud %u", "Series #%lu %s · next: %s %d/%u · %u/%u shocks · passed %u"),
                     (unsigned long)si.id, si.object, DIR_NAMES[dir], num, si.shots_per_dir, si.shots,
                     si.shots_per_dir * 6, si.passed);
        if (si.shots) lv_obj_remove_state(s_btn_undo, LV_STATE_DISABLED);
        else lv_obj_add_state(s_btn_undo, LV_STATE_DISABLED);
        lv_obj_remove_state(s_btn_end, LV_STATE_DISABLED);
    }
    lv_label_set_text(s_series_l, b);
    bool armed = shock_state() != SH_IDLE;
    button_text(s_btn_arm, armed ? TR(LV_SYMBOL_PAUSE "  Peata", LV_SYMBOL_PAUSE "  Pause") : TR(LV_SYMBOL_PLAY "  Valmis", LV_SYMBOL_PLAY "  Ready"));
    lv_obj_set_style_bg_color(s_btn_arm, armed ? C_WARN : C_OK, 0);
    lv_label_set_text(s_preset_l, preset_name(g_set.preset));
}

static void on_arm(lv_event_t *)
{
    if (!store_has_open()) {
        new_series_dialog();
        return;
    }
    if (store_series_complete()) {
        end_series_dialog();
        return;
    }
    bool on = shock_state() == SH_IDLE;
    shock_arm(on);
    if (on) {
        s_have_result = false;
        show_state_box();
    }
    update_series();
}

static void on_undo(lv_event_t *)
{
    if (store_undo_last()) {
        clear_result();
        update_series();
    }
}

static void on_end(lv_event_t *) { end_series_dialog(); }
static void on_new(lv_event_t *) { new_series_dialog(); }

static void build_test(void)
{
    s_scr_test = lv_obj_create(nullptr);
    lv_obj_t *sc = s_scr_test;
    lv_obj_set_style_bg_color(sc, C_BG, 0);
    lv_obj_remove_flag(sc, LV_OBJ_FLAG_SCROLLABLE);

    // ülariba
    lv_obj_t *t = label(sc, font_bold(24), C_FG, TR("Löögitest", "Shock test"));
    lv_obj_set_pos(t, 12, 10);
    s_preset_l = label(sc, font_reg(18), C_MUT, "");
    lv_obj_set_pos(s_preset_l, 150, 14);
    s_wifi_l = label(sc, font_reg(16), C_MUT, "");
    lv_obj_align(s_wifi_l, LV_ALIGN_TOP_RIGHT, -70, 16);
    lv_obj_t *bs = button(sc, LV_SYMBOL_SETTINGS, C_LINE, 52, 40, open_settings);
    lv_obj_align(bs, LV_ALIGN_TOP_RIGHT, -8, 5);

    // kella hoiatus
    s_banner = lv_obj_create(sc);
    lv_obj_set_pos(s_banner, 8, 50);
    lv_obj_set_size(s_banner, LCD_W - 16, 32);
    lv_obj_set_style_bg_color(s_banner, C_WARN, 0);
    lv_obj_set_style_border_width(s_banner, 0, 0);
    lv_obj_set_style_radius(s_banner, 6, 0);
    lv_obj_set_style_pad_all(s_banner, 4, 0);
    lv_obj_remove_flag(s_banner, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *bl = label(s_banner, font_bold(17), lv_color_black(), "");
    lv_obj_center(bl);

    // vasak paneel
    lv_obj_t *lp = panel(sc, 8, 88, 330, 316);
    s_res_box = lv_obj_create(lp);
    lv_obj_set_size(s_res_box, 308, 52);
    lv_obj_set_pos(s_res_box, 0, 0);
    lv_obj_set_style_radius(s_res_box, 8, 0);
    lv_obj_set_style_border_width(s_res_box, 0, 0);
    lv_obj_remove_flag(s_res_box, LV_OBJ_FLAG_SCROLLABLE);
    s_res_l = label(s_res_box, font_bold(26), lv_color_white(), "");
    lv_obj_center(s_res_l);
    s_peak_l = label(lp, font_bold(56), C_FG, "— g");
    lv_obj_set_pos(s_peak_l, 0, 58);
    s_lim_l = label(lp, font_reg(18), C_MUT, "");
    lv_obj_set_pos(s_lim_l, 0, 124);
    s_bar = lv_bar_create(lp);
    lv_obj_set_pos(s_bar, 0, 152);
    lv_obj_set_size(s_bar, 308, 14);
    lv_obj_set_style_bg_color(s_bar, C_LINE, 0);
    lv_obj_set_style_radius(s_bar, 3, 0);
    lv_obj_set_style_radius(s_bar, 3, LV_PART_INDICATOR);
    s_mk_min = lv_obj_create(lp);
    s_mk_max = lv_obj_create(lp);
    for (lv_obj_t *m : {s_mk_min, s_mk_max}) {
        lv_obj_set_size(m, 3, 24);
        lv_obj_set_y(m, 147);
        lv_obj_set_style_bg_color(m, C_FG, 0);
        lv_obj_set_style_border_width(m, 0, 0);
        lv_obj_set_style_radius(m, 0, 0);
    }
    s_sat_l = label(lp, font_reg(14), C_WARN, "");
    lv_obj_set_pos(s_sat_l, 0, 172);
    s_td_l = label(lp, font_reg(18), C_FG, "");
    lv_obj_set_pos(s_td_l, 0, 192);
    s_dv_l = label(lp, font_reg(18), C_FG, "");
    lv_obj_set_pos(s_dv_l, 0, 218);
    s_shape_l = label(lp, font_reg(16), C_FG, "");
    lv_obj_set_pos(s_shape_l, 0, 244);
    s_reason_l = label(lp, font_bold(16), C_BAD, "");
    lv_obj_set_pos(s_reason_l, 0, 268);
    lv_obj_set_width(s_reason_l, 308);
    lv_label_set_long_mode(s_reason_l, LV_LABEL_LONG_WRAP);

    // graafik
    lv_obj_t *rp = panel(sc, 346, 88, 446, 316);
    lv_obj_t *ct = label(rp, font_reg(14), C_MUT, TR("sinine = mõõdetud · hall = nominaal · punane = koridor", "blue = measured · grey = nominal · red = band"));
    lv_obj_set_pos(ct, 0, -4);
    s_chart = lv_chart_create(rp);
    lv_obj_set_pos(s_chart, 48, 16);
    lv_obj_set_size(s_chart, 376, 270);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_div_line_count(s_chart, 5, 5);
    lv_obj_set_style_bg_color(s_chart, C_BG, 0);
    lv_obj_set_style_border_color(s_chart, C_LINE, 0);
    lv_obj_set_style_line_color(s_chart, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_size(s_chart, 0, 0, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(s_chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_pad_all(s_chart, 0, 0);
    s_ser_hi = lv_chart_add_series(s_chart, C_BAD, LV_CHART_AXIS_PRIMARY_Y);
    s_ser_lo = lv_chart_add_series(s_chart, C_BAD, LV_CHART_AXIS_PRIMARY_Y);
    s_ser_id = lv_chart_add_series(s_chart, lv_color_hex(0x6b7280), LV_CHART_AXIS_PRIMARY_Y);
    s_ser_w = lv_chart_add_series(s_chart, C_WAVE, LV_CHART_AXIS_PRIMARY_Y);
    s_chart_y = label(rp, font_reg(14), C_MUT, "");
    lv_obj_set_pos(s_chart_y, 0, 12);
    lv_obj_set_style_text_line_space(s_chart_y, 12, 0);
    lv_obj_t *cx = label(rp, font_reg(14), C_MUT, TR("−0,4·TD                    impulss                         +2·TD", "−0.4·TD                    pulse                           +2·TD"));
    lv_obj_set_pos(cx, 60, 288);

    // alumine riba
    s_series_l = label(sc, font_reg(16), C_FG, "");
    lv_obj_set_pos(s_series_l, 12, 410);
    lv_obj_set_width(s_series_l, 560);
    lv_label_set_long_mode(s_series_l, LV_LABEL_LONG_DOT);
    s_live_l = label(sc, font_reg(16), C_MUT, "");
    lv_obj_align(s_live_l, LV_ALIGN_TOP_RIGHT, -12, 410);
    s_btn_new = button(sc, TR(LV_SYMBOL_PLUS "  Uus seeria", LV_SYMBOL_PLUS "  New series"), C_ACC, 190, 44, on_new);
    lv_obj_set_pos(s_btn_new, 8, 432);
    s_btn_arm = button(sc, "", C_OK, 190, 44, on_arm);
    lv_obj_set_pos(s_btn_arm, 206, 432);
    s_btn_undo = button(sc, TR(LV_SYMBOL_BACKSPACE "  Tühista viimane", LV_SYMBOL_BACKSPACE "  Undo last"), C_LINE, 220, 44, on_undo);
    lv_obj_set_pos(s_btn_undo, 404, 432);
    s_btn_end = button(sc, TR(LV_SYMBOL_OK "  Lõpeta", LV_SYMBOL_OK "  Finish"), C_LINE, 160, 44, on_end);
    lv_obj_set_pos(s_btn_end, 632, 432);
}

// ---------------------------------------------------------------------------
// Seadistuse vaade
static Settings s_edit;
struct NumField {
    lv_obj_t *ta;
    float *f;
    uint8_t *u8;
    int dec;
    float mn, mx;
};
static std::vector<NumField> s_fields;
static lv_obj_t *s_dd_lang;
static lv_obj_t *s_set_list, *s_dd_preset, *s_dd_amax, *s_dd_axis, *s_dd_odr, *s_sw_shape, *s_sw_dv, *s_sw_un,
    *s_ta_appass, *s_ta_ssid, *s_ta_spass, *s_set_info, *s_row_mass;

static void fields_to_ui(void)
{
    for (auto &f : s_fields) {
        float v = f.f ? *f.f : *f.u8;
        lv_textarea_set_text(f.ta, fmtf(v, f.dec).c_str());
    }
    lv_dropdown_set_selected(s_dd_lang, s_edit.lang);
    lv_dropdown_set_selected(s_dd_preset, s_edit.preset);
    lv_dropdown_set_selected(s_dd_amax, s_edit.amax == 166 ? 1 : 0);
    lv_dropdown_set_selected(s_dd_axis, s_edit.axis);
    lv_dropdown_set_selected(s_dd_odr, s_edit.odr >= 3200 ? 2 : s_edit.odr >= 1600 ? 1 : 0);
    lv_obj_set_state(s_sw_shape, LV_STATE_CHECKED, s_edit.shape_check);
    lv_obj_set_state(s_sw_dv, LV_STATE_CHECKED, s_edit.dv_check);
    lv_obj_set_state(s_sw_un, LV_STATE_CHECKED, s_edit.un_checks);
    lv_textarea_set_text(s_ta_appass, s_edit.ap_pass);
    lv_textarea_set_text(s_ta_ssid, s_edit.sta_ssid);
    lv_textarea_set_text(s_ta_spass, s_edit.sta_pass);
    bool mass = s_edit.preset == PRESET_UN_SMALL_BATT || s_edit.preset == PRESET_UN_LARGE_BATT;
    if (mass) lv_obj_remove_flag(s_row_mass, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_row_mass, LV_OBJ_FLAG_HIDDEN);
    float up = s_edit.peak_g * (1 + s_edit.tol_peak / 100);
    char b[200];
    snprintf(b, sizeof(b), TR("Piirid: %s … %s g, TD %s … %s ms%s", "Limits: %s … %s g, TD %s … %s ms%s"), fmtf(s_edit.peak_g * (1 - s_edit.tol_peak / 100), 1).c_str(),
             fmtf(up, 1).c_str(), fmtf(s_edit.td_ms * (1 - s_edit.tol_td / 100), 2).c_str(),
             fmtf(s_edit.td_ms * (1 + s_edit.tol_td / 100), 2).c_str(),
             up > ADXL_RANGE_G ? TR("\nHOIATUS: ülemine piir ületab anduri mõõtepiiri 200 g", "\nWARNING: upper limit exceeds sensor range 200 g") : "");
    lv_label_set_text(s_set_info, b);
}

static void ui_to_fields(void)
{
    for (auto &f : s_fields) {
        float v = parsef(lv_textarea_get_text(f.ta));
        if (!(v >= f.mn)) v = f.mn;
        if (v > f.mx) v = f.mx;
        if (f.f) *f.f = v;
        else *f.u8 = (uint8_t)lroundf(v);
    }
    s_edit.lang = lv_dropdown_get_selected(s_dd_lang) ? 1 : 0;
    s_edit.amax = lv_dropdown_get_selected(s_dd_amax) == 1 ? 166 : 200;
    s_edit.axis = lv_dropdown_get_selected(s_dd_axis);
    int o = lv_dropdown_get_selected(s_dd_odr);
    s_edit.odr = o == 2 ? 3200 : o == 1 ? 1600 : 800;
    s_edit.shape_check = lv_obj_has_state(s_sw_shape, LV_STATE_CHECKED);
    s_edit.dv_check = lv_obj_has_state(s_sw_dv, LV_STATE_CHECKED);
    s_edit.un_checks = lv_obj_has_state(s_sw_un, LV_STATE_CHECKED);
    strlcpy(s_edit.ap_pass, lv_textarea_get_text(s_ta_appass), sizeof(s_edit.ap_pass));
    strlcpy(s_edit.sta_ssid, lv_textarea_get_text(s_ta_ssid), sizeof(s_edit.sta_ssid));
    strlcpy(s_edit.sta_pass, lv_textarea_get_text(s_ta_spass), sizeof(s_edit.sta_pass));
    // A piirang
    float amax = s_edit.amax;
    if (s_edit.peak_g > amax) s_edit.peak_g = amax;
    if (s_edit.peak_g < settings_min_peak()) s_edit.peak_g = settings_min_peak();
}

static void on_field_changed(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    if (c == LV_EVENT_DEFOCUSED || c == LV_EVENT_READY || c == LV_EVENT_VALUE_CHANGED) {
        // tekstiväljad: väärtus loetakse lõpus; ülejäänud kohe
        lv_obj_t *t = lv_event_get_target_obj(e);
        if (lv_obj_check_type(t, &lv_textarea_class) && c == LV_EVENT_VALUE_CHANGED) return;
        lv_obj_t *prev_owner = s_kb_owner;
        ui_to_fields();
        // kasutaja muutis A/TD käsitsi -> eelseadistus "Kasutaja", v.a massi muutmine
        fields_to_ui();
        (void)prev_owner;
    }
}

static void on_preset(lv_event_t *)
{
    ui_to_fields();
    s_edit.preset = lv_dropdown_get_selected(s_dd_preset);
    settings_apply_preset(s_edit);
    fields_to_ui();
}

static void on_mass(lv_event_t *e)
{
    lv_event_code_t c = lv_event_get_code(e);
    if (c != LV_EVENT_DEFOCUSED && c != LV_EVENT_READY) return;
    ui_to_fields();
    settings_apply_preset(s_edit);
    fields_to_ui();
}

static lv_obj_t *row(const char *txt, lv_obj_t **row_out = nullptr)
{
    lv_obj_t *r = lv_obj_create(s_set_list);
    lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(r, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(r, 0, 0);
    lv_obj_set_style_pad_all(r, 2, 0);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *l = label(r, font_reg(18), C_FG, txt);
    lv_obj_set_width(l, 420);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    if (row_out) *row_out = r;
    return r;
}

static void header(const char *txt)
{
    lv_obj_t *l = label(s_set_list, font_bold(20), C_WAVE, txt);
    lv_obj_set_style_pad_top(l, 10, 0);
}

static void num_row(const char *txt, float *f, uint8_t *u8, int dec, float mn, float mx, lv_obj_t **row_out = nullptr,
                    lv_event_cb_t cb = on_field_changed)
{
    lv_obj_t *r = row(txt, row_out);
    lv_obj_t *ta = textarea(r, 180, true, 10);
    lv_obj_add_event_cb(ta, cb, LV_EVENT_ALL, nullptr);
    s_fields.push_back({ta, f, u8, dec, mn, mx});
}

static lv_obj_t *dd_row(const char *txt, const char *opts, lv_event_cb_t cb = on_field_changed, bool wide = false)
{
    lv_obj_t *r = row(txt);
    lv_obj_t *dd = lv_dropdown_create(r);
    lv_dropdown_set_options(dd, opts);
    lv_obj_set_width(dd, 300);
    if (wide) {
        // pikad valikud: rippmenüü omal real täislaiuses (muidu jääb loend ekraanist välja)
        lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW_WRAP);
        lv_obj_set_width(dd, LV_PCT(100));
        lv_obj_set_style_max_height(lv_dropdown_get_list(dd), 300, 0);
    }
    lv_obj_set_style_text_font(dd, font_reg(18), 0);
    lv_obj_set_style_text_font(lv_dropdown_get_list(dd), font_reg(20), 0);
    lv_obj_add_event_cb(dd, cb, LV_EVENT_VALUE_CHANGED, nullptr);
    return dd;
}

static lv_obj_t *sw_row(const char *txt)
{
    lv_obj_t *r = row(txt);
    lv_obj_t *sw = lv_switch_create(r);
    lv_obj_set_size(sw, 70, 36);
    lv_obj_add_event_cb(sw, on_field_changed, LV_EVENT_VALUE_CHANGED, nullptr);
    return sw;
}

static lv_obj_t *text_row(const char *txt, int maxlen)
{
    lv_obj_t *r = row(txt);
    return textarea(r, 300, false, maxlen);
}

static void on_set_save(lv_event_t *)
{
    ui_to_fields();
    bool odr_changed = s_edit.odr != g_set.odr || s_edit.lang != g_set.lang;  // keel: vaated ehitatakse uuesti
    bool net_changed = strcmp(s_edit.ap_pass, g_set.ap_pass) || strcmp(s_edit.sta_ssid, g_set.sta_ssid) ||
                       strcmp(s_edit.sta_pass, g_set.sta_pass);
    g_set = s_edit;
    settings_save();
    kb_hide();
    if (net_changed) net_apply_settings();
    if (odr_changed) {
        ui_message(TR("Seaded salvestatud", "Settings saved"), TR("Seade taaskäivitub ...", "Restarting ..."));
        lv_refr_now(nullptr);
        vTaskDelay(pdMS_TO_TICKS(1500));
        hard_restart();
    }
    lv_screen_load(s_scr_test);
    ui_refresh();
}

static void on_set_cancel(lv_event_t *)
{
    kb_hide();
    lv_screen_load(s_scr_test);
}

static void build_settings(void)
{
    s_scr_set = lv_obj_create(nullptr);
    lv_obj_t *sc = s_scr_set;
    lv_obj_set_style_bg_color(sc, C_BG, 0);
    lv_obj_remove_flag(sc, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *t = label(sc, font_bold(24), C_FG, TR("Seaded", "Settings"));
    lv_obj_set_pos(t, 12, 10);
    lv_obj_t *b1 = button(sc, TR(LV_SYMBOL_SAVE "  Salvesta", LV_SYMBOL_SAVE "  Save"), C_OK, 170, 40, on_set_save);
    lv_obj_align(b1, LV_ALIGN_TOP_RIGHT, -8, 5);
    lv_obj_t *b2 = button(sc, TR(LV_SYMBOL_CLOSE "  Loobu", LV_SYMBOL_CLOSE "  Cancel"), C_LINE, 140, 40, on_set_cancel);
    lv_obj_align(b2, LV_ALIGN_TOP_RIGHT, -186, 5);

    s_set_list = lv_obj_create(sc);
    lv_obj_set_pos(s_set_list, 8, 52);
    lv_obj_set_size(s_set_list, LCD_W - 16, LCD_H - 60);
    lv_obj_set_style_bg_color(s_set_list, C_CARD, 0);
    lv_obj_set_style_border_color(s_set_list, C_LINE, 0);
    lv_obj_set_style_pad_all(s_set_list, 12, 0);
    lv_obj_set_style_pad_row(s_set_list, 4, 0);
    lv_obj_set_flex_flow(s_set_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_set_list, LV_DIR_VER);

    s_set_info = label(s_set_list, font_reg(16), C_WARN, "");
    s_dd_lang = dd_row("Keel / Language", "Eesti\nEnglish");
    header("Test");
    std::string opts;
    for (int i = 0; i < PRESET_COUNT; i++) opts += std::string(i ? "\n" : "") + preset_name(i);
    s_dd_preset = dd_row(TR("Eelseadistus (standard)", "Preset (standard)"), opts.c_str(), on_preset, true);
    num_row(TR("Aku mass (kg) — UN38.3 valem", "Battery mass (kg) — UN38.3 formula"), &s_edit.mass_kg, nullptr, 3, 0.001f, 1000, &s_row_mass, on_mass);
    num_row(TR("Nominaalne tipp A (g)", "Nominal peak A (g)"), &s_edit.peak_g, nullptr, 1, 5, 200);
    num_row(TR("Impulsi kestus TD (ms)", "Pulse duration TD (ms)"), &s_edit.td_ms, nullptr, 2, 1, 200);
    s_dd_amax = dd_row(TR("A ülempiir", "A upper limit"), TR("200 g\n166 g (1,2·A mahub anduri piiri)", "200 g\n166 g (1.2·A fits sensor range)"));
    num_row(TR("Lööke suuna kohta (6 suunda)", "Shocks per direction (6 directions)"), nullptr, &s_edit.shots_per_dir, 0, 1, 200);
    s_dd_axis = dd_row(TR("Mõõtetelg", "Measuring axis"), TR("Automaatne\nX\nY\nZ", "Automatic\nX\nY\nZ"));
    header(TR("Tolerantsid (MIL-STD-810H 516.8)", "Tolerances (MIL-STD-810H 516.8)"));
    num_row(TR("Tipp ± (%)", "Peak ± (%)"), &s_edit.tol_peak, nullptr, 1, 1, 50);
    num_row(TR("Kestus TD ± (%)", "Duration TD ± (%)"), &s_edit.tol_td, nullptr, 1, 1, 50);
    num_row(TR("Kiiruse muutus ΔV ± (%)", "Velocity change ΔV ± (%)"), &s_edit.tol_dv, nullptr, 1, 1, 50);
    num_row(TR("Kuju koridor ± (% A)", "Shape band ± (% A)"), &s_edit.band, nullptr, 1, 1, 50);
    s_sw_shape = sw_row(TR("Kuju koridori kontroll mõjutab otsust", "Shape band check affects verdict"));
    s_sw_dv = sw_row(TR("ΔV kontroll mõjutab otsust", "ΔV check affects verdict"));
    s_sw_un = sw_row(TR("UN38.3 operaatori kontrollid (mass, pinge, visuaalne)", "UN38.3 operator checks (mass, voltage, visual)"));
    header(TR("Andur", "Sensor"));
    num_row(TR("Käivituslävi (% A)", "Trigger level (% A)"), &s_edit.trig_pct, nullptr, 0, 5, 90);
    num_row(TR("Ooteaeg pärast lööki (s)", "Hold-off after shock (s)"), &s_edit.holdoff_s, nullptr, 1, 0.3f, 30);
    s_dd_odr = dd_row(TR("Valimisagedus (ODR)", "Sample rate (ODR)"), "800 Hz\n1600 Hz\n3200 Hz");
    num_row(TR("Kalibreerimistegur X", "Calibration factor X"), &s_edit.cal[0], nullptr, 4, 0.5f, 2);
    num_row(TR("Kalibreerimistegur Y", "Calibration factor Y"), &s_edit.cal[1], nullptr, 4, 0.5f, 2);
    num_row(TR("Kalibreerimistegur Z", "Calibration factor Z"), &s_edit.cal[2], nullptr, 4, 0.5f, 2);
    header("WiFi");
    s_ta_appass = text_row(TR("Pääsupunkti parool (≥ 8 märki, tühi = avatud)", "Access point password (≥ 8 chars, empty = open)"), 32);
    // klientvõrk välja lülitatud: väljad peidetud (seaded jäävad alles)
    s_ta_ssid = textarea(s_set_list, 10, false, 32);
    s_ta_spass = textarea(s_set_list, 10, false, 64);
    lv_obj_add_flag(s_ta_ssid, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ta_spass, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *inf = label(s_set_list, font_reg(14), C_MUT, "");
    lv_label_set_text_fmt(inf, "%s v%s", FW_NAME, FW_VERSION);
}

static void open_settings(lv_event_t *)
{
    s_edit = g_set;
    fields_to_ui();
    lv_obj_scroll_to_y(s_set_list, 0, LV_ANIM_OFF);
    lv_screen_load(s_scr_set);
}

// ---------------------------------------------------------------------------
// Uue seeria dialoog
static lv_obj_t *s_dlg, *s_ta_obj, *s_ta_sn, *s_ta_op, *s_ta_m0, *s_ta_v0;

static lv_obj_t *dlg_field(lv_obj_t *p, const char *txt, int y, bool numeric, int maxlen)
{
    lv_obj_t *l = label(p, font_reg(18), C_FG, txt);
    lv_obj_set_pos(l, 0, y + 8);
    lv_obj_t *ta = textarea(p, 400, numeric, maxlen);
    lv_obj_set_pos(ta, 340, y);
    return ta;
}

static void on_new_start(lv_event_t *)
{
    store_new_series(lv_textarea_get_text(s_ta_obj), lv_textarea_get_text(s_ta_sn), lv_textarea_get_text(s_ta_op));
    if (s_ta_m0) {
        SeriesInfo &si = store_open_series();
        si.mass_before = parsef(lv_textarea_get_text(s_ta_m0));
        si.ocv_before = parsef(lv_textarea_get_text(s_ta_v0));
        store_save_open();
    }
    modal_close(s_dlg);
    s_dlg = nullptr;
    clear_result();
    shock_arm(true);
    s_have_result = false;
    show_state_box();
    update_series();
}

static void on_dlg_cancel(lv_event_t *)
{
    modal_close(s_dlg);
    s_dlg = nullptr;
}

static void new_series_dialog(void)
{
    if (s_dlg) return;
    bool un = g_set.un_checks;
    s_dlg = modal(780, un ? 250 : 200, 0);
    lv_obj_t *p = s_dlg;
    lv_obj_t *t = label(p, font_bold(22), C_FG, TR("Uus testiseeria", "New test series"));
    lv_obj_set_pos(t, 0, 0);
    s_ta_obj = dlg_field(p, TR("Katseobjekt", "Test item"), 34, false, 47);
    s_ta_sn = dlg_field(p, TR("Seerianumber", "Serial number"), 80, false, 31);
    s_ta_op = dlg_field(p, TR("Operaator", "Operator"), 126, false, 31);
    s_ta_m0 = s_ta_v0 = nullptr;
    int y = 172;
    if (un) {
        s_ta_m0 = dlg_field(p, TR("Mass enne (g)", "Mass before (g)"), y, true, 12);
        s_ta_v0 = dlg_field(p, TR("Pinge (OCV) enne (V)", "Voltage (OCV) before (V)"), y + 46, true, 12);
        y += 92;
    }
    lv_obj_t *b1 = button(p, TR(LV_SYMBOL_PLAY "  Alusta", LV_SYMBOL_PLAY "  Start"), C_OK, 180, 44, on_new_start);
    lv_obj_set_pos(b1, 560, y + 4);
    lv_obj_t *b2 = button(p, TR("Loobu", "Cancel"), C_LINE, 140, 44, on_dlg_cancel);
    lv_obj_set_pos(b2, 400, y + 4);
    char b[160];
    snprintf(b, sizeof(b), TR("%s\n%s g / %s ms · %u × 6 lööki", "%s\n%s g / %s ms · %u × 6 shocks"), preset_name(g_set.preset), fmtf(g_set.peak_g, 1).c_str(),
             fmtf(g_set.td_ms, 2).c_str(), g_set.shots_per_dir);
    lv_obj_t *inf = label(p, font_reg(16), C_MUT, b);
    lv_obj_set_pos(inf, 0, y + 4);
    lv_obj_set_height(p, y + 74);
}

// ---------------------------------------------------------------------------
// Seeria lõpetamine (UN38.3 kontrollid)
static lv_obj_t *s_ta_m1, *s_ta_v1, *s_cb[5];

static void finish_series(void)
{
    const SeriesInfo &si = store_open_series();
    bool pass;
    const char *v = report_verdict(si, &pass);
    char b[300];
    snprintf(b, sizeof(b), TR("Seeria #%lu: %s\nLäbitud %u / %u lööki.\n\nRaporti (PDF, CSV) saad alla laadida WiFi kaudu:\nvõrk „%s“, aadress http://4.3.2.1", "Series #%lu: %s\nPassed %u / %u shocks.\n\nDownload the report (PDF, CSV) over WiFi:\nnetwork \"%s\", address http://4.3.2.1"),
             (unsigned long)si.id, v, si.passed, si.shots, net_ap_ssid().c_str());
    store_close_series();
    shock_arm(false);
    clear_result();
    update_series();
    ui_message(TR("Seeria lõpetatud", "Series finished"), b);
}

static void on_end_save(lv_event_t *)
{
    SeriesInfo &si = store_open_series();
    if (s_ta_m1) {
        si.mass_after = parsef(lv_textarea_get_text(s_ta_m1));
        si.ocv_after = parsef(lv_textarea_get_text(s_ta_v1));
        si.no_leak = lv_obj_has_state(s_cb[0], LV_STATE_CHECKED);
        si.no_vent = lv_obj_has_state(s_cb[1], LV_STATE_CHECKED);
        si.no_disasm = lv_obj_has_state(s_cb[2], LV_STATE_CHECKED);
        si.no_rupture = lv_obj_has_state(s_cb[3], LV_STATE_CHECKED);
        si.no_fire = lv_obj_has_state(s_cb[4], LV_STATE_CHECKED);
        si.checks_done = true;
        store_save_open();
    }
    modal_close(s_dlg);
    s_dlg = nullptr;
    finish_series();
}

static void end_series_dialog(void)
{
    if (s_dlg || !store_has_open()) return;
    shock_arm(false);
    update_series();
    const SeriesInfo &si = store_open_series();
    bool un = si.un_checks;
    s_dlg = modal(780, un ? 250 : 190, un ? 0 : 120);
    lv_obj_t *p = s_dlg;
    char b[160];
    snprintf(b, sizeof(b), TR("Lõpeta seeria #%lu?  (%u / %u lööki tehtud)", "Finish series #%lu?  (%u / %u shocks done)"), (unsigned long)si.id, si.shots,
             si.shots_per_dir * 6);
    lv_obj_t *t = label(p, font_bold(22), C_FG, b);
    lv_obj_set_pos(t, 0, 0);
    int y = 40;
    s_ta_m1 = s_ta_v1 = nullptr;
    if (un) {
        s_ta_m1 = dlg_field(p, TR("Mass pärast (g)", "Mass after (g)"), y, true, 12);
        s_ta_v1 = dlg_field(p, TR("Pinge (OCV) pärast (V)", "Voltage (OCV) after (V)"), y + 46, true, 12);
        y += 96;
        const char *cb[5] = {TR("Leket pole", "No leakage"), TR("Gaasi eraldumist pole", "No venting"), TR("Ei lagunenud", "No disassembly"), TR("Ei purunenud", "No rupture"), TR("Ei süttinud/plahvatanud", "No fire/explosion")};
        for (int i = 0; i < 5; i++) {
            s_cb[i] = lv_checkbox_create(p);
            lv_checkbox_set_text(s_cb[i], cb[i]);
            lv_obj_set_style_text_font(s_cb[i], font_reg(18), 0);
            lv_obj_set_style_text_color(s_cb[i], C_FG, 0);
            lv_obj_set_pos(s_cb[i], (i % 3) * 250, y + (i / 3) * 36);
        }
        y += 76;
    } else {
        lv_obj_t *l = label(p, font_reg(18), C_MUT, TR("Seeria suletakse; raport jääb alles ja on WiFi portaalist allalaaditav.", "The series will be closed; the report stays available in the WiFi portal."));
        lv_obj_set_pos(l, 0, y);
        y += 40;
    }
    lv_obj_t *b1 = button(p, TR(LV_SYMBOL_OK "  Lõpeta", LV_SYMBOL_OK "  Finish"), C_OK, 180, 44, on_end_save);
    lv_obj_set_pos(b1, 560, y);
    lv_obj_t *b2 = button(p, TR("Tagasi", "Back"), C_LINE, 140, 44, on_dlg_cancel);
    lv_obj_set_pos(b2, 400, y);
    lv_obj_set_height(p, y + 70);
}

// ---------------------------------------------------------------------------
// Teated ja OTA
static lv_obj_t *s_msg;

static void on_msg_close(lv_event_t *)
{
    if (s_msg) modal_close(s_msg);
    s_msg = nullptr;
}

void ui_message_close(void) { on_msg_close(nullptr); }

void ui_message(const char *title, const char *text)
{
    if (s_msg) modal_close(s_msg);
    s_msg = modal(600, 280, 100);
    lv_obj_t *t = label(s_msg, font_bold(22), C_FG, title);
    lv_obj_set_pos(t, 0, 0);
    lv_obj_t *l = label(s_msg, font_reg(18), C_FG, text);
    lv_obj_set_pos(l, 0, 40);
    lv_obj_set_width(l, 570);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_t *b = button(s_msg, "OK", C_ACC, 120, 44, on_msg_close);
    lv_obj_align(b, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
}

static lv_obj_t *s_ota, *s_ota_bar, *s_ota_l;
static lv_timer_t *s_ota_timer;

static void ota_hide(lv_timer_t *)
{
    if (s_ota) lv_obj_add_flag(lv_obj_get_parent(s_ota), LV_OBJ_FLAG_HIDDEN);
    s_ota_timer = nullptr;
}

void ui_ota_progress(int pct, const char *text)
{
    if (!s_ota) {
        s_ota = modal(560, 150, 150);
        lv_obj_t *t = label(s_ota, font_bold(22), C_FG, TR("Püsivara uuendus", "Firmware update"));
        lv_obj_set_pos(t, 0, 0);
        s_ota_bar = lv_bar_create(s_ota);
        lv_obj_set_size(s_ota_bar, 530, 24);
        lv_obj_set_pos(s_ota_bar, 0, 44);
        s_ota_l = label(s_ota, font_reg(18), C_FG, "");
        lv_obj_set_pos(s_ota_l, 0, 82);
    }
    lv_obj_remove_flag(lv_obj_get_parent(s_ota), LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(lv_obj_get_parent(s_ota));
    if (pct >= 0) lv_bar_set_value(s_ota_bar, pct, LV_ANIM_OFF);
    lv_label_set_text(s_ota_l, text);
    lv_obj_set_style_text_color(s_ota_l, pct < 0 ? C_BAD : C_FG, 0);
    if (pct < 0 && !s_ota_timer) {
        s_ota_timer = lv_timer_create(ota_hide, 5000, nullptr);
        lv_timer_set_repeat_count(s_ota_timer, 1);
    }
    lv_refr_now(nullptr);
}

// ---------------------------------------------------------------------------
// Löögi tulemus (löögi lõimest)
void ui_on_shot(const ShotResult &r)
{
    if (!store_has_open()) return;
    ShotRec rec = store_add_shot(r);
    bool complete = store_series_complete();
    if (complete) shock_arm(false);
    LvGuard g;
    s_last = r;
    s_last_rec = rec;
    s_have_result = true;
    show_result();
    update_series();
    if (complete) end_series_dialog();
}

// ---------------------------------------------------------------------------
static void timer_cb(lv_timer_t *)
{
    static int tick;
    tick++;
    std::string live = TR("praegu ", "now ") + fmtf(shock_live_g(), 1) + " g";
    if (!adxl_present()) live += TR(" (SIMULATSIOON)", " (SIMULATION)");
    lv_label_set_text(s_live_l, live.c_str());
    if (!s_have_result) show_state_box();
    if (tick % 5 == 0) {
        char b[160];
        int cl = net_ap_clients();
        snprintf(b, sizeof(b), LV_SYMBOL_WIFI " %s%s", net_ap_ssid().c_str(), cl ? (cl == 1 ? TR(" · 1 ühendus", " · 1 client") : TR(" · ühendused", " · clients")) : "");
        lv_label_set_text(s_wifi_l, b);
        lv_obj_t *bl = lv_obj_get_child(s_banner, 0);
        if (clock_valid()) {
            // kell sünkroniseeritud: kuupäev ja kellaaeg hoiatuse asemel
            std::string t = clock_fmt(clock_epoch());  // "YYYY-MM-DD HH:MM:SS"
            if (g_set.lang) snprintf(b, sizeof(b), "%s", t.c_str());
            else snprintf(b, sizeof(b), "%s.%s.%s   %s", t.substr(8, 2).c_str(), t.substr(5, 2).c_str(),
                          t.substr(0, 4).c_str(), t.substr(11).c_str());
            lv_label_set_text(bl, b);
            lv_obj_set_style_bg_color(s_banner, C_CARD, 0);
            lv_obj_set_style_text_color(bl, C_FG, 0);
        } else {
            bool open = strlen(g_set.ap_pass) < 8;
            snprintf(b, sizeof(b),
                     TR("Kell seadistamata – WiFi „%s“, parool %s%s%s → ava portaal",
                        "Clock not set – WiFi \"%s\", password %s%s%s → open portal"),
                     net_ap_ssid().c_str(), open ? "" : TR("„", "\""), open ? TR("puudub", "none") : g_set.ap_pass, open ? "" : TR("“", "\""));
            lv_label_set_text(bl, b);
            lv_obj_set_style_bg_color(s_banner, C_WARN, 0);
            lv_obj_set_style_text_color(bl, lv_color_black(), 0);
        }
        lv_obj_remove_flag(s_banner, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_refresh(void)
{
    if (!s_scr_test) return;
    update_limits();
    update_series();
    if (!s_have_result) clear_result();
    timer_cb(nullptr);
}

void ui_init(void)
{
    LvGuard g;
    lv_display_t *d = lv_display_get_default();
    lv_theme_t *th = lv_theme_default_init(d, C_ACC, C_WARN, true, font_reg(18));
    lv_display_set_theme(d, th);
    esp_rom_printf("[ui] teema\n");
    build_test();
    esp_rom_printf("[ui] testi vaade\n");
    build_settings();
    esp_rom_printf("[ui] seaded\n");

    s_kb = lv_keyboard_create(lv_layer_top());
    lv_obj_set_size(s_kb, LCD_W, 230);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(s_kb, font_reg(20), 0);
    lv_keyboard_set_map(s_kb, LV_KEYBOARD_MODE_TEXT_LOWER, KB_LOWER, KB_CTRL);
    lv_keyboard_set_map(s_kb, LV_KEYBOARD_MODE_TEXT_UPPER, KB_UPPER, KB_CTRL);
    lv_obj_add_event_cb(s_kb, kb_event, LV_EVENT_ALL, nullptr);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);

    lv_screen_load(s_scr_test);
    lv_obj_update_layout(s_scr_test);
    clear_result();
    ui_refresh();
    lv_timer_create(timer_cb, 200, nullptr);
    esp_rom_printf("[ui] valmis\n");
}
