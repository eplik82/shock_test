// WiFi pääsupunkt + captive portaal (DNS + HTTP), valikuline klientühendus (arendus/OTA)
#pragma once
#include <string>

void net_init(void);
std::string net_ap_ssid(void);
std::string net_sta_ip(void);  // tühi kui pole ühendatud
int net_ap_clients(void);
bool net_ota_active(void);
// rakenda muudetud WiFi seaded (AP parool / klientvõrk)
void net_apply_settings(void);
