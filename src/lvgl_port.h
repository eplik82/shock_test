#pragma once
#include "lvgl.h"

void lvport_init(void);
bool lvport_lock(int timeout_ms = -1);
void lvport_unlock(void);

// Seeriakonsooli testimiseks: süstitud puudutused / lohistused
void lvport_inject_tap(int x, int y);
void lvport_inject_drag(int x1, int y1, int x2, int y2, int steps);
// Saadab praeguse ekraanipildi seriaalporti (RLE + base64, read "@S...")
void lvport_screenshot(void);
// ekraanipilt RLE-kujul (u16 kordus, u16 piksel); vabasta free()-ga
uint8_t *lvport_screenshot_rle(size_t *len);
// praegu kuvatav kaadripuhver (RGB565, LCD_W x LCD_H) — ainult lugemiseks
const uint16_t *lvport_front_buffer(void);

// kas kasutaja puudutas/keris viimase ~1,5 s jooksul (siis ei tasu PSRAM-i koormata)
bool lvport_user_active(void);

struct LvGuard {
    LvGuard() { lvport_lock(); }
    ~LvGuard() { lvport_unlock(); }
};
