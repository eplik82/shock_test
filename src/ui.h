// Puuteekraani kasutajaliides: testi vaade ja seadistuse vaade (kutsuda LVGL luku all, v.a ui_on_shot)
#pragma once
#include "shock.h"

void ui_init(void);
void ui_refresh(void);
// püsivara uuenduse edenemine: pct 0..100, -1 = viga
void ui_ota_progress(int pct, const char *text);
void ui_message(const char *title, const char *text);
void ui_message_close(void);
// löögi tulemus (kutsutakse löögi lõimest, lukustab ise)
void ui_on_shot(const ShotResult &r);
// käivituslogo (enne ui_init): näidatakse kuni ui_splash_done(), vähemalt min_ms
void ui_splash_show(void);
void ui_splash_done(int min_ms);
