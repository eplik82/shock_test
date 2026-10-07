// Paanikakäsitleja mähis: salvestab erindi andmed RTC-mällu enne, kui käsitleja ise võib kinni jääda
#include "crashinfo.h"

#include <stdio.h>
#include <string.h>

#include "esp_attr.h"
#include "esp_debug_helpers.h"
#include "esp_private/panic_internal.h"
#include "xtensa_context.h"
#include "hardreset.h"
#include "hal/wdt_hal.h"
#include "esp_private/rtc_clk.h"
#include "soc/rtc.h"
#include "esp_rom_sys.h"

#define CRASH_MAGIC 0xC0FFEE01

struct CrashInfo {
    uint32_t magic;
    int core;
    uint32_t pc, exccause, excvaddr;
    uint32_t bt[12];
    int nbt;
    char reason[48];
};

RTC_NOINIT_ATTR static CrashInfo g_crash;

extern "C" void __real_esp_panic_handler(panic_info_t *info);

extern "C" void IRAM_ATTR __wrap_esp_panic_handler(panic_info_t *info)
{
    g_crash.magic = CRASH_MAGIC;
    g_crash.core = info->core;
    const XtExcFrame *f = (const XtExcFrame *)info->frame;
    g_crash.pc = f ? f->pc : (uint32_t)info->addr;
    g_crash.exccause = f ? f->exccause : 0;
    g_crash.excvaddr = f ? f->excvaddr : 0;
    g_crash.reason[0] = 0;
    if (info->reason) {
        int i = 0;
        for (; i < (int)sizeof(g_crash.reason) - 1 && info->reason[i]; i++) g_crash.reason[i] = info->reason[i];
        g_crash.reason[i] = 0;
    }
    g_crash.nbt = 0;
    if (f) {
        esp_backtrace_frame_t fr = {};
        fr.pc = f->pc;
        fr.sp = f->a1;
        fr.next_pc = f->a0;
        fr.exc_frame = f;
        g_crash.bt[g_crash.nbt++] = fr.pc;
        while (g_crash.nbt < 12 && fr.next_pc && esp_backtrace_get_next_frame(&fr)) g_crash.bt[g_crash.nbt++] = fr.pc;
    }
    // Paanika järel tavaline (soe) taaskäivitus jättis PSRAM-i koodi rikutuks -> uus krahh, tsükkel.
    // Seepärast: RTC watchdog "lähtesta kõik" 3 s pärast (jõuab enne veateate UART-i välja trükkida).
    wdt_hal_context_t rtc = {};
    wdt_hal_init(&rtc, WDT_RWDT, 0, false);
    wdt_hal_write_protect_disable(&rtc);
    wdt_hal_config_stage(&rtc, WDT_STAGE0, (uint32_t)(3000ULL * rtc_clk_slow_freq_get_hz() / 1000ULL),
                         WDT_STAGE_ACTION_RESET_RTC);
    wdt_hal_enable(&rtc);
    wdt_hal_write_protect_enable(&rtc);
    esp_rom_printf("\n*** PAANIKA: %s PC=0x%08x (RTC-lähtestus 3 s pärast)\n", g_crash.reason, (unsigned)g_crash.pc);
    gpio0_force_high();
    while (true) {}
}

std::string crashinfo_text(void)
{
    if (g_crash.magic != CRASH_MAGIC) return "";
    char s[400];
    int n = snprintf(s, sizeof(s), "tuum %d, põhjus '%s', PC 0x%08lx, EXCCAUSE %lu, EXCVADDR 0x%08lx\nbacktrace:",
                     g_crash.core, g_crash.reason, (unsigned long)g_crash.pc, (unsigned long)g_crash.exccause,
                     (unsigned long)g_crash.excvaddr);
    for (int i = 0; i < g_crash.nbt && i < 12 && n < (int)sizeof(s) - 12; i++)
        n += snprintf(s + n, sizeof(s) - n, " 0x%08lx", (unsigned long)((g_crash.bt[i] & 0x3fffffff) | 0x40000000));
    return s;
}
