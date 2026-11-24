#pragma once

#include "RnStandoff.h"

#include <cstdint>

// Alle Saetze der Mod (Deutsch). Der Seed waehlt eine Variante, damit nicht jede Wache dasselbe sagt.
namespace rn::lines {
    const char* FirstWarning(uint64_t p_Seed);
    const char* LastWarning(uint64_t p_Seed);
    const char* OpenFire(uint64_t p_Seed);
    const char* Calmed(uint64_t p_Seed);         // Frist vorbei, aber Wache war beruhigt -> neue Warnung

    const char* PleaLabel(Plea p_Plea);           // Text im Menue
    const char* PleaSpoken(Plea p_Plea, uint64_t p_Seed); // was 47 sagt
    const char* Reply(Plea p_Plea, PleaResult p_Result, uint64_t p_Seed); // Antwort der Wache

    const char* Disarmed(uint64_t p_Seed);        // Waffe aus der Hand geschossen
    const char* Moan(float p_Progress, uint64_t p_Seed); // Verwundeter, 0 = frisch, 1 = kurz vor dem Tod
}
