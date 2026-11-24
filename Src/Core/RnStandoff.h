#pragma once

#include "RnMath.h"

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

// "Erst warnen, dann schiessen" und Verhandeln – reine Logik ohne Spiel-Abhaengigkeiten.
// Alle Zeiten sind Spielsekunden. Der Schuss-Hook des Spiels kann aus einem anderen Thread kommen, daher threadsicher.
namespace rn {
    enum class StandoffState { None = 0, Warning, Calm, Hostile };

    struct StandoffConfig {
        bool m_Enabled = true;
        float m_WarnSeconds = 8.f;
        float m_ComplySeconds = 6.f;
        float m_MaxSeconds = 45.f;
        float m_StandDownSeconds = 150.f;
        float m_AggressionMemorySeconds = 30.f;
        float m_ForgetSeconds = 25.f; // so lange ohne Schussversuch = Begegnung vorbei
    };

    struct ShotDecision {
        bool m_Block = false;
        bool m_NewStandoff = false; // erster abgefangener Schuss dieser Begegnung -> Warnung anzeigen
    };

    // Was 47 im Verhandlungsmenue sagen kann.
    enum class Plea { Surrender = 0, Talk, Bribe, Threaten, Count };

    struct PleaContext {
        bool m_PlayerArmed = false;     // haelt sichtbar eine Waffe in der Hand
        bool m_PlayerDisguised = false; // traegt eine Verkleidung statt des Anzugs
        int m_ArmedKills = 0;           // in dieser Mission getoetete Bewaffnete (Ruf)
        int m_GuardsInvolved = 1;       // wie viele Bewaffnete gerade auf 47 zielen
        int m_Attempts = 0;             // bisherige Verhandlungsversuche bei diesem NPC
        bool m_Elite = false;           // Elitewache / Leibwaechter
    };

    enum class PleaResult { StandDown = 0, LastWarning, OpenFire, GuardSurrenders, HolsterFirst };

    // Erfolgschance 0..1 (fuer die Anzeige im Menue und fuer ResolvePlea).
    float PleaChance(Plea p_Plea, const PleaContext& p_Context);
    PleaResult ResolvePlea(Plea p_Plea, const PleaContext& p_Context, Rng& p_Rng);
    const char* ChanceHint(float p_Chance);

    class StandoffTracker {
    public:
        void Configure(const StandoffConfig& p_Config);
        StandoffConfig Config() const;
        void Reset();

        // 47 hat geschossen oder jemanden angegriffen: ab jetzt wird zurueckgeschossen.
        void OnPlayerAggression(double p_Now);
        bool AggressionRecent(double p_Now) const;

        // Wird bei jedem Schussversuch eines NPCs auf 47 gefragt.
        ShotDecision OnShotAttempt(uint64_t p_Npc, double p_Now);

        // 47 haelt keine Waffe in der Hand: laufende Warnungen behalten mindestens ComplySeconds (bis MaxSeconds).
        void OnCompliance(double p_Now);

        // Verhandlungsmenue offen: Fristen laufen nicht unter p_Seconds (bis MaxSeconds).
        void Hold(double p_Now, float p_Seconds);

        void Apply(uint64_t p_Npc, PleaResult p_Result, double p_Now);
        void CountAttempt(uint64_t p_Npc);
        int Attempts(uint64_t p_Npc) const;

        StandoffState State(uint64_t p_Npc, double p_Now) const;
        double SecondsLeft(uint64_t p_Npc, double p_Now) const;

        // NPCs, deren Frist gerade laeuft.
        std::vector<uint64_t> Warnings(double p_Now) const;

        // NPCs, deren Frist gerade abgelaufen ist (jeder nur einmal pro Begegnung).
        std::vector<uint64_t> TakeExpired(double p_Now);

        // NPC ist tot oder verschwunden.
        void Forget(uint64_t p_Npc);

    private:
        struct Entry {
            StandoffState m_State = StandoffState::None;
            double m_Started = 0.0;
            double m_Deadline = 0.0;
            double m_CalmUntil = 0.0;
            double m_LastAttempt = -1e9;
            int m_Attempts = 0;
            bool m_ExpiredFromWarning = false;
            bool m_ExpiredReported = false;
        };

        // Zeitabhaengige Uebergaenge: Frist abgelaufen, Ruhe vorbei, Begegnung vergessen.
        void Normalize(Entry& p_Entry, double p_Now) const;
        Entry Effective(const Entry& p_Entry, double p_Now) const;
        bool AggressionRecentLocked(double p_Now) const;
        void ExtendLocked(Entry& p_Entry, double p_Now, float p_Seconds) const;

        mutable std::mutex m_Mutex;
        StandoffConfig m_Config;
        std::unordered_map<uint64_t, Entry> m_Entries;
        double m_LastAggression = -1e9;
    };
}
