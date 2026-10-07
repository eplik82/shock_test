// Väike sündmuste logi (RAM-is, viimased ~60 rida) portaali diagnostikaks: /dev/log
#pragma once
#include <string>
void netlog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
std::string netlog_dump(void);
