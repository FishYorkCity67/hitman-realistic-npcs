#include "RnSettings.h"

#include <algorithm>
#include <charconv>
#include <cstdio>

namespace rn {
    namespace {
        std::string FormatFloat(float p_Value) {
            char s_Buffer[32];
            std::snprintf(s_Buffer, sizeof(s_Buffer), "%.4f", p_Value);
            return s_Buffer;
        }

        bool ParseFloat(const std::string& p_Text, float& p_Out) {
            try {
                size_t s_Used = 0;
                const float s_Value = std::stof(p_Text, &s_Used);

                if (s_Used == 0) {
                    return false;
                }

                p_Out = s_Value;
                return true;
            }
            catch (...) {
                return false;
            }
        }

        bool ParseInt(const std::string& p_Text, int& p_Out) {
            int s_Value = 0;
            const auto s_Result = std::from_chars(p_Text.data(), p_Text.data() + p_Text.size(), s_Value);

            if (s_Result.ec != std::errc()) {
                return false;
            }

            p_Out = s_Value;
            return true;
        }

        // Laedt oder speichert jedes Feld ueber dieselbe Liste (so koennen Laden und Speichern nicht auseinanderlaufen).
        struct Binder {
            const SettingsStore& m_Store;
            bool m_Saving;

            void Bool(const char* p_Section, const char* p_Key, bool& p_Value) const {
                if (m_Saving) {
                    m_Store.m_Set(p_Section, p_Key, p_Value ? "true" : "false");
                    return;
                }

                const std::string s_Text = m_Store.m_Get(p_Section, p_Key, p_Value ? "true" : "false");

                if (s_Text == "true" || s_Text == "1") {
                    p_Value = true;
                }
                else if (s_Text == "false" || s_Text == "0") {
                    p_Value = false;
                }
            }

            void Float(const char* p_Section, const char* p_Key, float& p_Value, float p_Min, float p_Max) const {
                if (m_Saving) {
                    m_Store.m_Set(p_Section, p_Key, FormatFloat(p_Value));
                    return;
                }

                float s_Value = p_Value;

                if (ParseFloat(m_Store.m_Get(p_Section, p_Key, FormatFloat(p_Value)), s_Value)) {
                    p_Value = std::clamp(s_Value, p_Min, p_Max);
                }
            }

            void Int(const char* p_Section, const char* p_Key, int& p_Value, int p_Min, int p_Max) const {
                if (m_Saving) {
                    m_Store.m_Set(p_Section, p_Key, std::to_string(p_Value));
                    return;
                }

                int s_Value = p_Value;

                if (ParseInt(m_Store.m_Get(p_Section, p_Key, std::to_string(p_Value)), s_Value)) {
                    p_Value = std::clamp(s_Value, p_Min, p_Max);
                }
            }
        };

        void Bind(Settings& s, const Binder& b) {
            b.Bool("general", "enabled", s.m_Enabled);
            b.Bool("general", "subtitles", s.m_ShowSubtitles);
            b.Bool("general", "hints", s.m_ShowHints);
            b.Bool("general", "debug_overlay", s.m_DebugOverlay);
            b.Bool("debug", "self_test", s.m_SelfTest);

            b.Bool("standoff", "warn_before_firing", s.m_WarnBeforeFiring);
            b.Float("standoff", "warn_seconds", s.m_WarnSeconds, 2.f, 60.f);
            b.Float("standoff", "comply_seconds", s.m_ComplySeconds, 0.f, 30.f);
            b.Float("standoff", "max_seconds", s.m_MaxStandoffSeconds, 5.f, 300.f);
            b.Float("standoff", "stand_down_seconds", s.m_StandDownSeconds, 10.f, 3600.f);
            b.Float("standoff", "aggression_memory", s.m_AggressionMemorySeconds, 0.f, 600.f);
            b.Float("standoff", "group_radius", s.m_GroupRadius, 0.f, 100.f);
            b.Int("standoff", "negotiate_key", s.m_NegotiateKey, 1, 254);
            b.Float("standoff", "menu_time_scale", s.m_MenuTimeScale, 0.05f, 1.f);

            b.Bool("wounds", "enabled", s.m_Wounds);
            b.Float("wounds", "bleed_min", s.m_BleedMinSeconds, 3.f, 150.f);
            b.Float("wounds", "bleed_max", s.m_BleedMaxSeconds, 3.f, 150.f);
            b.Bool("wounds", "leg_lethal", s.m_LegWoundsLethal);
            b.Float("wounds", "leg_bleed_factor", s.m_LegBleedFactor, 1.f, 10.f);
            b.Bool("wounds", "stab_wounds", s.m_StabWounds);
            b.Bool("wounds", "disarm", s.m_Disarm);
            b.Int("wounds", "disarm_chance", s.m_DisarmChance, 0, 100);
            b.Bool("wounds", "twitching", s.m_Twitching);
            b.Float("wounds", "twitch_strength", s.m_TwitchStrength, 0.f, 3.f);
            b.Bool("wounds", "limb_twitching", s.m_LimbTwitching);
            b.Bool("wounds", "wounded_lines", s.m_WoundedLines);

            b.Bool("blood", "enabled", s.m_Blood);
            b.Int("blood", "amount", s.m_BloodAmount, 1, 3);

            // Unsinnige Kombinationen geradeziehen.
            if (s.m_BleedMaxSeconds < s.m_BleedMinSeconds) {
                s.m_BleedMaxSeconds = s.m_BleedMinSeconds;
            }

            if (s.m_MaxStandoffSeconds < s.m_WarnSeconds) {
                s.m_MaxStandoffSeconds = s.m_WarnSeconds;
            }
        }
    }

    void LoadSettings(Settings& p_Settings, const SettingsStore& p_Store) {
        if (!p_Store.m_Get) {
            return;
        }

        Bind(p_Settings, Binder { p_Store, false });
    }

    void SaveSettings(const Settings& p_Settings, const SettingsStore& p_Store) {
        if (!p_Store.m_Set) {
            return;
        }

        Settings s_Copy = p_Settings;
        Bind(s_Copy, Binder { p_Store, true });
    }

    std::string KeyName(int p_VirtualKey) {
        if ((p_VirtualKey >= 'A' && p_VirtualKey <= 'Z') || (p_VirtualKey >= '0' && p_VirtualKey <= '9')) {
            return std::string(1, static_cast<char>(p_VirtualKey));
        }

        if (p_VirtualKey >= 0x70 && p_VirtualKey <= 0x87) {
            return "F" + std::to_string(p_VirtualKey - 0x70 + 1);
        }

        switch (p_VirtualKey) {
            case 0x05: return "Maus 4";
            case 0x06: return "Maus 5";
            case 0x09: return "Tab";
            case 0x14: return "Feststell";
            case 0x20: return "Leertaste";
            case 0x2D: return "Einfg";
            case 0x2E: return "Entf";
            case 0x24: return "Pos1";
            case 0x23: return "Ende";
            case 0x21: return "Bild auf";
            case 0x22: return "Bild ab";
            default: return "Taste " + std::to_string(p_VirtualKey);
        }
    }
}
