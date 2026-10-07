// Testraport: PDF ja CSV
#pragma once
#include <stdint.h>
#include <string>

#include "store.h"

bool report_pdf(uint32_t id, std::string &out);
bool report_csv(uint32_t id, std::string &out);
// koondtulemus tekstina: "LÄBITUD", "EBAÕNNESTUS", "POOLELI"
const char *report_verdict(const SeriesInfo &si, bool *pass);
// UN38.3 massikao lubatud piir % (mass g-des)
float un_mass_loss_limit(float mass_g);
