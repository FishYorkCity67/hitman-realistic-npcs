#pragma once

#include <Windows.h>

namespace rn {
    // Fuehrt p_Fn aus und faengt Zugriffsfehler der Engine ab (SEH), statt das Spiel abstuerzen zu lassen.
    // Wichtig: in dieser Funktion selbst duerfen keine Objekte mit Destruktor leben (sonst C2712) – das Lambda
    // ist eine eigene Funktion, dort ist alles erlaubt.
    template <typename Fn>
    bool SafeCall(Fn&& p_Fn) {
        __try {
            p_Fn();
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }
}
