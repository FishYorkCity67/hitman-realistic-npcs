#pragma once

#include <functional>
#include <string>

namespace rn {
    // Alle Einstellungen der Mod. Die Standardwerte sind so gewaehlt, dass alles ohne Zutun laeuft.
    struct Settings {
        bool m_Enabled = true;
        bool m_ShowSubtitles = true;
        bool m_ShowHints = true;
        bool m_DebugOverlay = false;
        bool m_SelfTest = false;                // nur fuer die Entwicklung: testet Effekte automatisch im Level

        // --- Konfrontation: erst warnen, dann schiessen -----------------------------------------------------------
        bool m_WarnBeforeFiring = true;
        float m_WarnSeconds = 8.f;              // Frist nach der ersten Warnung
        float m_ComplySeconds = 6.f;            // ohne Waffe in der Hand bleibt mindestens so viel Zeit
        float m_MaxStandoffSeconds = 45.f;      // laenger wartet keine Wache
        float m_StandDownSeconds = 150.f;       // so lange halten sich Wachen nach erfolgreicher Verhandlung zurueck
        float m_AggressionMemorySeconds = 30.f; // nach einem eigenen Angriff wird nicht mehr gewarnt
        float m_GroupRadius = 20.f;             // Verhandlungsergebnis gilt fuer alle Wachen in diesem Umkreis
        int m_NegotiateKey = 'T';
        float m_MenuTimeScale = 0.25f;          // Zeitlupe, solange das Verhandlungsmenue offen ist

        // --- Verwundungen ---------------------------------------------------------------------------------------
        bool m_Wounds = true;                   // Rumpf-/Beintreffer: zusammenbrechen, danach Todeskampf am Boden
        float m_BleedMinSeconds = 20.f;         // Dauer des Todeskampfs (vom Treffer an)
        float m_BleedMaxSeconds = 45.f;
        bool m_LegWoundsLethal = true;          // Beinschuss: bricht zusammen, laengerer Todeskampf
        float m_LegBleedFactor = 2.5f;
        bool m_StabWounds = true;               // nach toedlichen Messerstichen ebenfalls Todeskampf
        bool m_Disarm = true;                   // Armtreffer schlaegt die Waffe aus der Hand
        int m_DisarmChance = 70;                // Prozent
        bool m_Twitching = true;                // Zucken und Krampfen waehrend des Sterbens
        float m_TwitchStrength = 1.f;
        bool m_LimbTwitching = true;            // auch die Haende/Arme zucken
        bool m_WoundedLines = true;             // Sterbende stoehnen/flehen (Untertitel)

        // --- Blut -----------------------------------------------------------------------------------------------
        bool m_Blood = true;
        int m_BloodAmount = 2;                  // 1 = wenig, 2 = normal, 3 = viel
    };

    // Schnittstelle zum Speichern: die Mod haengt hier die INI-Funktionen des SDK an, die Tests eine Map.
    struct SettingsStore {
        std::function<std::string(const std::string& p_Section, const std::string& p_Key, const std::string& p_Default)> m_Get;
        std::function<void(const std::string& p_Section, const std::string& p_Key, const std::string& p_Value)> m_Set;
    };

    void LoadSettings(Settings& p_Settings, const SettingsStore& p_Store);
    void SaveSettings(const Settings& p_Settings, const SettingsStore& p_Store);

    // Anzeigename einer Taste (virtueller Tastencode), z. B. "T", "F8".
    std::string KeyName(int p_VirtualKey);
}
