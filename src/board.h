// Waveshare ESP32-S3-Touch-LCD-4.3B: 800x480 RGB565 paneel, GT911 puude, CH422G IO-laiendi
#pragma once
#include "esp_lcd_panel_ops.h"
#include "driver/i2c_master.h"

#define LCD_W 800
#define LCD_H 480

// 1 = topeltpuhver (vahetus vsync-il), 0 = üks kaadripuhver
#ifndef LCD_DOUBLE_FB
#define LCD_DOUBLE_FB 0
#endif

typedef bool (*board_vsync_cb_t)(void *ctx);

void board_init(board_vsync_cb_t on_vsync, void *ctx);
esp_lcd_panel_handle_t board_panel(void);
void board_get_framebuffers(void **fb0, void **fb1);
// true kui ekraani puudutatakse; x,y ekraani koordinaadid
bool board_touch_read(int *x, int *y);
void board_backlight(bool on);
// peatab ekraani ja selle DMA (enne taaskäivitust)
void board_shutdown_display(void);

// Jagatud I2C siin (GT911, CH422G, ADXL375)
i2c_master_bus_handle_t board_i2c_bus(void);
// käivituse I2C diagnostika (liinide tasemed, vastanud seadmed)
const char *board_i2c_diag(void);
