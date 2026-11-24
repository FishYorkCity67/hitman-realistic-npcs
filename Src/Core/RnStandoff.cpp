#include "RnStandoff.h"

#include <algorithm>

namespace rn {
    // ---------------------------------------------------------------------------------------------------------------
    // Verhandeln
    // ---------------------------------------------------------------------------------------------------------------

    float PleaChance(Plea p_Plea, const PleaContext& p_Context) {
        const int s_Extra = std::min(std::max(0, p_Context.m_GuardsInvolved - 1), 3);
        const int s_Attempts = std::min(std::max(0, p_Context.m_Attempts), 5);
        const int s_Kills = std::min(std::max(0, p_Context.m_ArmedKills), 5);
        float s_Chance = 0.f;

        switch (p_Plea) {
            case Plea::Surrender:
                // Mit Waffe in der Hand glaubt dir niemand, dass du aufgibst.
                if (p_Context.m_PlayerArmed) {
                    return 0.f;
                }

                s_Chance = 0.92f - 0.04f * s_Attempts - (p_Context.m_Elite ? 0.07f : 0.f);
                return std::clamp(s_Chance, 0.5f, 0.95f);

            case Plea::Talk:
                s_Chance = 0.5f
                    + (p_Context.m_PlayerDisguised ? 0.15f : -0.05f)
                    - (p_Context.m_PlayerArmed ? 0.3f : 0.f)
                    - 0.15f * s_Attempts
                    - 0.08f * s_Extra
                    - (p_Context.m_Elite ? 0.15f : 0.f)
                    - 0.04f * s_Kills;
                return std::clamp(s_Chance, 0.05f, 0.85f);

            case Plea::Bribe:
                s_Chance = 0.45f
                    - 0.1f * s_Attempts
                    - 0.06f * s_Extra
                    - (p_Context.m_Elite ? 0.25f : 0.f)
                    - (p_Context.m_PlayerArmed ? 0.1f : 0.f);
                return std::clamp(s_Chance, 0.05f, 0.8f);

            case Plea::Threaten:
                // Ohne Waffe ist eine Drohung ein Bluff.
                if (!p_Context.m_PlayerArmed) {
                    return 0.05f;
                }

                s_Chance = 0.2f + 0.12f * s_Kills - 0.12f * s_Extra - (p_Context.m_Elite ? 0.2f : 0.f) - 0.1f * s_Attempts;
                return std::clamp(s_Chance, 0.05f, 0.85f);

            default:
                return 0.f;
        }
    }

    PleaResult ResolvePlea(Plea p_Plea, const PleaContext& p_Context, Rng& p_Rng) {
        if (p_Plea == Plea::Surrender && p_Context.m_PlayerArmed) {
            return PleaResult::HolsterFirst;
        }

        const bool s_Success = p_Rng.Chance(PleaChance(p_Plea, p_Context));

        if (s_Success) {
            return p_Plea == Plea::Threaten ? PleaResult::GuardSurrenders : PleaResult::StandDown;
        }

        return p_Plea == Plea::Threaten ? PleaResult::OpenFire : PleaResult::LastWarning;
    }

    const char* ChanceHint(float p_Chance) {
        if (p_Chance >= 0.7f) {
            return "gute Chance";
        }

        if (p_Chance >= 0.45f) {
            return "könnte klappen";
        }

        if (p_Chance >= 0.25f) {
            return "riskant";
        }

        return "kaum Chance";
    }

    // ---------------------------------------------------------------------------------------------------------------
    // Konfrontationen
    // ---------------------------------------------------------------------------------------------------------------

    void StandoffTracker::Configure(const StandoffConfig& p_Config) {
        std::scoped_lock s_Lock(m_Mutex);
        m_Config = p_Config;
        m_Config.m_MaxSeconds = std::max(m_Config.m_MaxSeconds, m_Config.m_WarnSeconds);
    }

    StandoffConfig StandoffTracker::Config() const {
        std::scoped_lock s_Lock(m_Mutex);
        return m_Config;
    }

    void StandoffTracker::Reset() {
        std::scoped_lock s_Lock(m_Mutex);
        m_Entries.clear();
        m_LastAggression = -1e9;
    }

    void StandoffTracker::OnPlayerAggression(double p_Now) {
        std::scoped_lock s_Lock(m_Mutex);
        m_LastAggression = p_Now;

        // Wer gerade wartet oder sich beruhigt hat, schiesst jetzt zurueck.
        for (auto& [s_Npc, s_Entry] : m_Entries) {
            Normalize(s_Entry, p_Now);

            if (s_Entry.m_State == StandoffState::Warning || s_Entry.m_State == StandoffState::Calm) {
                s_Entry.m_State = StandoffState::Hostile;
                s_Entry.m_LastAttempt = p_Now;
                s_Entry.m_ExpiredReported = true;
            }
        }
    }

    bool StandoffTracker::AggressionRecent(double p_Now) const {
        std::scoped_lock s_Lock(m_Mutex);
        return AggressionRecentLocked(p_Now);
    }

    bool StandoffTracker::AggressionRecentLocked(double p_Now) const {
        return p_Now - m_LastAggression < m_Config.m_AggressionMemorySeconds;
    }

    void StandoffTracker::Normalize(Entry& p_Entry, double p_Now) const {
        if (p_Entry.m_State == StandoffState::Warning && p_Now >= p_Entry.m_Deadline) {
            p_Entry.m_State = StandoffState::Hostile;
            p_Entry.m_ExpiredFromWarning = true;
            p_Entry.m_LastAttempt = std::max(p_Entry.m_LastAttempt, p_Entry.m_Deadline);
        }

        if (p_Entry.m_State == StandoffState::Calm && p_Now >= p_Entry.m_CalmUntil) {
            p_Entry = Entry {};
        }

        if (p_Entry.m_State == StandoffState::Hostile && p_Now - p_Entry.m_LastAttempt > m_Config.m_ForgetSeconds) {
            p_Entry = Entry {};
        }
    }

    StandoffTracker::Entry StandoffTracker::Effective(const Entry& p_Entry, double p_Now) const {
        Entry s_Copy = p_Entry;
        Normalize(s_Copy, p_Now);
        return s_Copy;
    }

    ShotDecision StandoffTracker::OnShotAttempt(uint64_t p_Npc, double p_Now) {
        std::scoped_lock s_Lock(m_Mutex);

        if (!m_Config.m_Enabled) {
            return {};
        }

        Entry& s_Entry = m_Entries[p_Npc];
        Normalize(s_Entry, p_Now);
        s_Entry.m_LastAttempt = p_Now;

        // Nach einem eigenen Angriff gibt es keine Warnungen mehr.
        if (AggressionRecentLocked(p_Now) && s_Entry.m_State != StandoffState::Calm) {
            if (s_Entry.m_State != StandoffState::Hostile) {
                s_Entry.m_State = StandoffState::Hostile;
                s_Entry.m_ExpiredReported = true;
            }

            return {};
        }

        switch (s_Entry.m_State) {
            case StandoffState::None:
                s_Entry.m_State = StandoffState::Warning;
                s_Entry.m_Started = p_Now;
                s_Entry.m_Deadline = p_Now + m_Config.m_WarnSeconds;
                s_Entry.m_Attempts = 0;
                s_Entry.m_ExpiredFromWarning = false;
                s_Entry.m_ExpiredReported = false;
                return { true, true };

            case StandoffState::Warning:
            case StandoffState::Calm:
                return { true, false };

            default:
                return {};
        }
    }

    void StandoffTracker::ExtendLocked(Entry& p_Entry, double p_Now, float p_Seconds) const {
        if (p_Entry.m_State != StandoffState::Warning) {
            return;
        }

        const double s_Cap = p_Entry.m_Started + m_Config.m_MaxSeconds;
        p_Entry.m_Deadline = std::max(p_Entry.m_Deadline, std::min(p_Now + p_Seconds, s_Cap));
    }

    void StandoffTracker::OnCompliance(double p_Now) {
        std::scoped_lock s_Lock(m_Mutex);

        for (auto& [s_Npc, s_Entry] : m_Entries) {
            Normalize(s_Entry, p_Now);
            ExtendLocked(s_Entry, p_Now, m_Config.m_ComplySeconds);
        }
    }

    void StandoffTracker::Hold(double p_Now, float p_Seconds) {
        std::scoped_lock s_Lock(m_Mutex);

        for (auto& [s_Npc, s_Entry] : m_Entries) {
            Normalize(s_Entry, p_Now);
            ExtendLocked(s_Entry, p_Now, p_Seconds);
        }
    }

    void StandoffTracker::Apply(uint64_t p_Npc, PleaResult p_Result, double p_Now) {
        std::scoped_lock s_Lock(m_Mutex);
        Entry& s_Entry = m_Entries[p_Npc];
        Normalize(s_Entry, p_Now);

        switch (p_Result) {
            case PleaResult::StandDown:
                s_Entry.m_State = StandoffState::Calm;
                s_Entry.m_CalmUntil = p_Now + m_Config.m_StandDownSeconds;
                break;

            case PleaResult::GuardSurrenders:
                // Wer seine Waffe abgegeben hat, schiesst in dieser Mission nicht mehr.
                s_Entry.m_State = StandoffState::Calm;
                s_Entry.m_CalmUntil = p_Now + 1e7;
                break;

            case PleaResult::LastWarning:
                if (s_Entry.m_State == StandoffState::Warning) {
                    s_Entry.m_Deadline = p_Now + 3.0;
                }

                break;

            case PleaResult::HolsterFirst:
                ExtendLocked(s_Entry, p_Now, 4.f);
                break;

            case PleaResult::OpenFire:
                s_Entry.m_State = StandoffState::Hostile;
                s_Entry.m_LastAttempt = p_Now;
                s_Entry.m_ExpiredReported = true;
                break;
        }
    }

    void StandoffTracker::CountAttempt(uint64_t p_Npc) {
        std::scoped_lock s_Lock(m_Mutex);
        ++m_Entries[p_Npc].m_Attempts;
    }

    int StandoffTracker::Attempts(uint64_t p_Npc) const {
        std::scoped_lock s_Lock(m_Mutex);
        const auto s_It = m_Entries.find(p_Npc);
        return s_It == m_Entries.end() ? 0 : s_It->second.m_Attempts;
    }

    StandoffState StandoffTracker::State(uint64_t p_Npc, double p_Now) const {
        std::scoped_lock s_Lock(m_Mutex);
        const auto s_It = m_Entries.find(p_Npc);
        return s_It == m_Entries.end() ? StandoffState::None : Effective(s_It->second, p_Now).m_State;
    }

    double StandoffTracker::SecondsLeft(uint64_t p_Npc, double p_Now) const {
        std::scoped_lock s_Lock(m_Mutex);
        const auto s_It = m_Entries.find(p_Npc);

        if (s_It == m_Entries.end()) {
            return 0.0;
        }

        const Entry s_Entry = Effective(s_It->second, p_Now);

        switch (s_Entry.m_State) {
            case StandoffState::Warning: return std::max(0.0, s_Entry.m_Deadline - p_Now);
            case StandoffState::Calm: return std::max(0.0, s_Entry.m_CalmUntil - p_Now);
            default: return 0.0;
        }
    }

    std::vector<uint64_t> StandoffTracker::Warnings(double p_Now) const {
        std::scoped_lock s_Lock(m_Mutex);
        std::vector<uint64_t> s_Result;

        for (const auto& [s_Npc, s_Entry] : m_Entries) {
            if (Effective(s_Entry, p_Now).m_State == StandoffState::Warning) {
                s_Result.push_back(s_Npc);
            }
        }

        return s_Result;
    }

    std::vector<uint64_t> StandoffTracker::TakeExpired(double p_Now) {
        std::scoped_lock s_Lock(m_Mutex);
        std::vector<uint64_t> s_Result;

        for (auto& [s_Npc, s_Entry] : m_Entries) {
            Normalize(s_Entry, p_Now);

            if (s_Entry.m_State == StandoffState::Hostile && s_Entry.m_ExpiredFromWarning && !s_Entry.m_ExpiredReported) {
                s_Entry.m_ExpiredReported = true;
                s_Result.push_back(s_Npc);
            }
        }

        return s_Result;
    }

    void StandoffTracker::Forget(uint64_t p_Npc) {
        std::scoped_lock s_Lock(m_Mutex);
        m_Entries.erase(p_Npc);
    }
}
