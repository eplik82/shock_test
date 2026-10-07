#include "netlog.h"

#include <stdarg.h>
#include <stdio.h>

#include <mutex>

#include "esp_timer.h"
#include "esp_rom_sys.h"

static std::mutex s_mtx;
static std::string s_lines[60];
static int s_head;

void netlog(const char *fmt, ...)
{
    char b[160];
    int n = snprintf(b, sizeof(b), "%7.1f ", esp_timer_get_time() / 1e6);
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b + n, sizeof(b) - n, fmt, ap);
    va_end(ap);
    esp_rom_printf("[net] %s\n", b);  // ka seeriaporti (USB kaudu jälgimiseks)
    std::lock_guard<std::mutex> g(s_mtx);
    s_lines[s_head] = b;
    s_head = (s_head + 1) % 60;
}

std::string netlog_dump(void)
{
    std::lock_guard<std::mutex> g(s_mtx);
    std::string o;
    for (int i = 0; i < 60; i++) {
        const std::string &l = s_lines[(s_head + i) % 60];
        if (!l.empty()) o += l + "\n";
    }
    return o;
}
