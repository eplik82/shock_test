#pragma once
#include "lvgl.h"

// Tiny TTF DejaVu (eesti tähed, ± ≤ Δ π √); LVGL ikoonid tulevad Montserrati varufondist
const lv_font_t *font_reg(int px);
const lv_font_t *font_bold(int px);
