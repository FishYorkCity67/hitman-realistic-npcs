// Konfrontationen im Spiel: Warnungen, Fristen, Verhandeln und die Zeitlupe im Verhandlungsmenue (Spiel-Thread).

#include "RealisticNPCs.h"

#include "RnLines.h"

#include <Glacier/ZActor.h>
#include <Glacier/ZGameTime.h>

#include <algorithm>
#include <format>

namespace {
    const char* PleaName(rn::Plea p_Plea) {
        switch (p_Plea) {
            case rn::Plea::Surrender: return "Ergeben";
            case rn::Plea::Talk: return "Reden";
            case rn::Plea::Bribe: return "Bestechen";
            case rn::Plea::Threaten: return "Drohen";
            default: return "?";
        }
    }

    const char* ResultName(rn::PleaResult p_Result) {
        switch (p_Result) {
            case rn::PleaResult::StandDown: return "Wache gibt nach";
            case rn::PleaResult::LastWarning: return "letzte Warnung";
            case rn::PleaResult::OpenFire: return "Feuer";
            case rn::PleaResult::GuardSurrenders: return "Wache ergibt sich";
            case rn::PleaResult::HolsterFirst: return "erst Waffe weg";
            default: return "?";
        }
    }
}

void RealisticNPCs::UpdateStandoffs(const rn::Settings& p_Settings, double p_Now) {
    // --- Neue Konfrontationen: erste Warnung -----------------------------------------------------------------------
    std::vector<uint64_t> s_New;

    {
        std::scoped_lock s_Lock(m_EventMutex);
        s_New.swap(m_NewStandoffs);
    }

    for (const uint64_t s_Key : s_New) {
        const NpcInfo* s_Npc = m_World.Find(s_Key);

        if (!s_Npc) {
            continue;
        }

        ++m_TotalStandoffs;
        Say(s_Npc->m_Name, rn::lines::FirstWarning(s_Key ^ static_cast<uint64_t>(p_Now * 3.0)), false, 4.5f);
        Log(std::format("{} zielt auf 47 – Warnung statt Schuss ({:.0f} m).", s_Npc->m_Name, s_Npc->m_Distance));
    }

    // --- 47 haelt keine Waffe in der Hand: die Wachen geben ihm mehr Zeit ---------------------------------------
    if (!m_World.m_PlayerArmed) {
        m_Standoffs.OnCompliance(p_Now);
    }

    // Waehrend 47 im Menue spricht, schiesst niemand.
    if (m_MenuOpen) {
        m_Standoffs.Hold(p_Now, 4.f);
    }

    // --- Abgelaufene Fristen: Feuer ---------------------------------------------------------------------------------
    for (const uint64_t s_Key : m_Standoffs.TakeExpired(p_Now)) {
        const NpcInfo* s_Npc = m_World.Find(s_Key);

        if (s_Npc && s_Npc->m_Alive && !s_Npc->m_Pacified) {
            Say(s_Npc->m_Name, rn::lines::OpenFire(s_Key ^ static_cast<uint64_t>(p_Now)), false, 3.f);
            Log(std::format("Frist von {} abgelaufen – er schießt.", s_Npc->m_Name));
        }
    }

    // --- Verhandlungsergebnis aus dem Menue -----------------------------------------------------------------------
    const int s_Plea = m_PendingPlea.exchange(-1);

    if (s_Plea >= 0 && s_Plea < static_cast<int>(rn::Plea::Count)) {
        ResolvePlea(static_cast<rn::Plea>(s_Plea), p_Settings, p_Now);
    }

    // --- Laufende Konfrontationen fuer die Anzeige zusammenstellen ------------------------------------------------
    std::vector<StandoffView> s_Views;

    for (const uint64_t s_Key : m_Standoffs.Warnings(p_Now)) {
        const NpcInfo* s_Npc = m_World.Find(s_Key);

        // Tote, Bewusstlose oder Verschwundene warnen nicht mehr.
        if (!s_Npc || !s_Npc->m_Alive || s_Npc->m_Pacified) {
            m_Standoffs.Forget(s_Key);
            continue;
        }

        StandoffView s_View;
        s_View.m_Key = s_Key;
        s_View.m_Name = s_Npc->m_Name;
        s_View.m_SecondsLeft = static_cast<float>(m_Standoffs.SecondsLeft(s_Key, p_Now));
        s_View.m_Distance = s_Npc->m_Distance;
        s_Views.push_back(std::move(s_View));
    }

    std::sort(s_Views.begin(), s_Views.end(), [](const StandoffView& a, const StandoffView& b) {
        return a.m_Distance < b.m_Distance;
    });

    // Kurz vor Ablauf der Frist: einmal "Letzte Warnung!" (nur der Naechste, sonst rufen alle durcheinander).
    std::unordered_set<uint64_t> s_StillWarning;

    for (const StandoffView& s_View : s_Views) {
        s_StillWarning.insert(s_View.m_Key);
    }

    std::erase_if(m_LastWarned, [&](uint64_t p_Key) { return !s_StillWarning.contains(p_Key); });

    if (!s_Views.empty() && !m_MenuOpen) {
        const StandoffView& s_First = s_Views.front();

        if (s_First.m_SecondsLeft < 2.5f && m_LastWarned.insert(s_First.m_Key).second) {
            Say(s_First.m_Name, rn::lines::LastWarning(s_First.m_Key ^ static_cast<uint64_t>(p_Now * 5.0)), false, 2.5f);
        }
    }

    // Verhandelt wird mit dem Naechsten.
    NegotiationView s_Negotiation;

    if (!s_Views.empty()) {
        const StandoffView& s_First = s_Views.front();
        m_Addressee = s_First.m_Key;

        if (const NpcInfo* s_Npc = m_World.Find(s_First.m_Key)) {
            const rn::PleaContext s_Context = MakePleaContext(*s_Npc, static_cast<int>(s_Views.size()));
            s_Negotiation.m_Valid = true;
            s_Negotiation.m_Key = s_First.m_Key;
            s_Negotiation.m_Name = s_First.m_Name;
            s_Negotiation.m_SecondsLeft = s_First.m_SecondsLeft;
            s_Negotiation.m_Guards = static_cast<int>(s_Views.size());
            s_Negotiation.m_Attempts = s_Context.m_Attempts;
            s_Negotiation.m_PlayerArmed = s_Context.m_PlayerArmed;

            for (int i = 0; i < static_cast<int>(rn::Plea::Count); ++i) {
                s_Negotiation.m_Chances[i] = rn::PleaChance(static_cast<rn::Plea>(i), s_Context);
            }
        }
    }
    else {
        m_Addressee = 0;
    }

    {
        std::scoped_lock s_Lock(m_UiMutex);
        m_StandoffViews = std::move(s_Views);
        m_Negotiation = s_Negotiation;
    }

    if (!s_Negotiation.m_Valid && m_MenuOpen) {
        m_RequestCloseMenu = true;
    }
}

void RealisticNPCs::ResolvePlea(rn::Plea p_Plea, const rn::Settings& p_Settings, double p_Now) {
    const NpcInfo* s_Found = m_World.Find(m_Addressee);

    if (!s_Found || !s_Found->m_Alive) {
        return;
    }

    const NpcInfo s_Npc = *s_Found;
    const std::vector<uint64_t> s_Warnings = m_Standoffs.Warnings(p_Now);
    const rn::PleaContext s_Context = MakePleaContext(s_Npc, static_cast<int>(s_Warnings.size()));
    const rn::PleaResult s_Result = rn::ResolvePlea(p_Plea, s_Context, m_Rng);
    const uint64_t s_Seed = s_Npc.m_Key ^ static_cast<uint64_t>(p_Now * 17.0);

    m_Standoffs.CountAttempt(s_Npc.m_Key);

    Say("47", rn::lines::PleaSpoken(p_Plea, s_Seed), true, 4.f);
    Say(s_Npc.m_Name, rn::lines::Reply(p_Plea, s_Result, s_Seed >> 3), false, 5.f);

    Log(std::format("Verhandeln mit {}: {} ({:.0f} % Chance) -> {}.", s_Npc.m_Name, PleaName(p_Plea),
        rn::PleaChance(p_Plea, s_Context) * 100.f, ResultName(s_Result)));

    // Wer in der Naehe mitzielt, haelt sich an das Ergebnis.
    std::vector<uint64_t> s_Group;

    for (const uint64_t s_Key : s_Warnings) {
        if (s_Key == s_Npc.m_Key) {
            continue;
        }

        const NpcInfo* s_Other = m_World.Find(s_Key);

        if (s_Other && rn::Distance(s_Other->m_Pos, s_Npc.m_Pos) <= p_Settings.m_GroupRadius) {
            s_Group.push_back(s_Key);
        }
    }

    switch (s_Result) {
        case rn::PleaResult::GuardSurrenders:
            // Er legt die Waffe hin. Die anderen sind beeindruckt, wenn 47 schon einige Bewaffnete erledigt hat.
            DisarmActor(s_Npc.m_Actor, s_Npc.m_Key, p_Settings, p_Now, true);
            m_Standoffs.Apply(s_Npc.m_Key, rn::PleaResult::GuardSurrenders, p_Now);

            for (const uint64_t s_Key : s_Group) {
                m_Standoffs.Apply(s_Key, m_ArmedKills >= 3 ? rn::PleaResult::StandDown : rn::PleaResult::LastWarning, p_Now);
            }

            break;

        default:
            m_Standoffs.Apply(s_Npc.m_Key, s_Result, p_Now);

            if (p_Settings.m_GroupRadius > 0.f) {
                for (const uint64_t s_Key : s_Group) {
                    m_Standoffs.Apply(s_Key, s_Result, p_Now);
                }
            }

            break;
    }
}

void RealisticNPCs::UpdateTimeScale(const rn::Settings& p_Settings) {
    auto* s_TimeManager = Globals::GameTimeManager;

    if (!s_TimeManager) {
        return;
    }

    const bool s_Want = m_MenuOpen && p_Settings.m_Enabled && p_Settings.m_MenuTimeScale < 0.999f;

    if (s_Want && !m_TimeScaleApplied) {
        m_PreviousTimeScale = s_TimeManager->m_fGameTimeMultiplier > 0.01f ? s_TimeManager->m_fGameTimeMultiplier : 1.f;
        s_TimeManager->m_fGameTimeMultiplier = std::min(m_PreviousTimeScale, p_Settings.m_MenuTimeScale);
        m_TimeScaleApplied = true;
    }
    else if (!s_Want && m_TimeScaleApplied) {
        s_TimeManager->m_fGameTimeMultiplier = m_PreviousTimeScale;
        m_TimeScaleApplied = false;
    }
}
