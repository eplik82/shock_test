// Kell: plaadil pole varupatareiga RTC-d -> aeg tuleb veebiportaalist (telefoni brauser)
#pragma once
#include <stdint.h>
#include <string>

void clock_init(void);
bool clock_valid(void);
// epoch_ms = UTC millisekundid, tz_min = kohaliku aja nihe UTC-st minutites (nt +180 suveajal)
void clock_set(int64_t epoch_ms, int tz_min);
int64_t clock_epoch(void);       // UTC sekundid (0 kui seadistamata)
int clock_tz_min(void);
// kohalik aeg tekstina "2026-10-07 21:30:05" (või "—" kui epoch==0)
std::string clock_fmt(int64_t epoch);
// käivituse juhuslik ID (seadistamata kellaga valimite tagantjärele dateerimiseks)
uint32_t clock_boot_id(void);
uint32_t clock_uptime_s(void);
