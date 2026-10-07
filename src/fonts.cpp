#include "fonts.h"

#include <mutex>

extern const uint8_t font_sans_start[] asm("_binary_DejaVuSans_ttf_start");
extern const uint8_t font_sans_end[] asm("_binary_DejaVuSans_ttf_end");
extern const uint8_t font_bold_start[] asm("_binary_DejaVuSans_Bold_ttf_start");
extern const uint8_t font_bold_end[] asm("_binary_DejaVuSans_Bold_ttf_end");

static std::mutex s_mtx;
static lv_font_t *s_fonts[2][64];

// Tiny TTF tagastab puuduva glüüfi korral "notdef" kasti; LVGL ikoonid (privaatala) suuname varufonti
static bool (*s_orig_glyph_dsc)(const lv_font_t *, lv_font_glyph_dsc_t *, uint32_t, uint32_t);

static bool glyph_dsc_cb(const lv_font_t *f, lv_font_glyph_dsc_t *d, uint32_t cp, uint32_t next)
{
    if (cp >= 0xF000 && cp <= 0xF8FF) return false;
    return s_orig_glyph_dsc(f, d, cp, next);
}

static const lv_font_t *get(int face, int px)
{
    if (px < 8) px = 8;
    if (px > 63) px = 63;
    std::lock_guard<std::mutex> g(s_mtx);
    lv_font_t *&f = s_fonts[face][px];
    if (!f) {
        const uint8_t *st = face ? font_bold_start : font_sans_start;
        const uint8_t *en = face ? font_bold_end : font_sans_end;
        f = lv_tiny_ttf_create_data_ex(st, en - st, px, LV_FONT_KERNING_NONE, 256);
        if (f) {
            s_orig_glyph_dsc = f->get_glyph_dsc;
            f->get_glyph_dsc = glyph_dsc_cb;
            f->fallback = px >= 26 ? &lv_font_montserrat_28 : px >= 22 ? &lv_font_montserrat_24
                        : px >= 18 ? &lv_font_montserrat_20 : &lv_font_montserrat_16;
        }
    }
    return f ? f : &lv_font_montserrat_16;
}

const lv_font_t *font_reg(int px) { return get(0, px); }
const lv_font_t *font_bold(int px) { return get(1, px); }
