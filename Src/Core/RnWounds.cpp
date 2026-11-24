#include "RnWounds.h"

#include <algorithm>
#include <cmath>

namespace rn {
    namespace {
        int BurstsFor(BodyZone p_Zone, const Settings& p_Settings) {
            if (!p_Settings.m_Blood) {
                return 0;
            }

            const int s_Amount = std::clamp(p_Settings.m_BloodAmount, 1, 3);

            switch (p_Zone) {
                case BodyZone::Head: return s_Amount + 1;
                case BodyZone::Arm:
                case BodyZone::Leg: return std::max(1, s_Amount - 1);
                default: return s_Amount;
            }
        }

        // Phasen des Todeskampfs (p = Fortschritt 0..1 ab dem Ende der Sterbe-Animation).
        constexpr float c_ShockEnd = 0.15f;
        constexpr double c_MinShockSeconds = 2.0;
        constexpr float c_AgonyStart = 0.8f;

        // Impuls je Staerke-Einheit und Knochen. Im Spiel gemessen (Paris, Koerper am Boden, Bildschirmfotos):
        // Becken 40 hebt die Huefte deutlich sichtbar an, 20 kaum, 10 gar nicht. Kopf ab etwa 12 sichtbar.
        // Haende sind leicht und bewegen sich schon bei 2-8 deutlich.
        struct BoneResponse {
            float m_Scale;
            float m_Up;   // Anteil nach oben
            float m_Side; // Anteil zur Seite (beim Becken klein, sonst rutscht der Koerper ueber den Boden)
        };

        constexpr BoneResponse c_Pelvis { 7.f, 1.f, 0.3f };
        constexpr BoneResponse c_Head { 3.f, 0.6f, 0.7f };
        constexpr BoneResponse c_Hand { 1.5f, 0.7f, 0.7f };
    }

    HitDecision DecideHit(BodyZone p_Zone, HitKind p_Kind, bool p_InAgony, const Settings& p_Settings, Rng& p_Rng) {
        HitDecision s_Decision;

        if (p_Kind != HitKind::Melee) {
            s_Decision.m_BloodBursts = BurstsFor(p_Kind == HitKind::Stab ? BodyZone::Torso : p_Zone, p_Settings);
        }

        if (!p_Settings.m_Enabled || !p_Settings.m_Wounds) {
            return s_Decision;
        }

        // Faustschlaege bleiben, wie sie sind.
        if (p_Kind == HitKind::Melee) {
            return s_Decision;
        }

        // Liegt schon im Todeskampf: jeder weitere Treffer beendet ihn (Gnadenschuss). Den Schaden macht das Spiel.
        if (p_InAgony) {
            s_Decision.m_FinishSoon = true;
            return s_Decision;
        }

        const double s_Seconds = std::min(p_Rng.Uniform(p_Settings.m_BleedMinSeconds, p_Settings.m_BleedMaxSeconds), c_MaxAgonySeconds);

        // Messer: ob der Stich toetet, entscheidet das Spiel (Messerangriffe sind dort eigene Animationen).
        // Stirbt der NPC daran, folgt der Todeskampf.
        if (p_Kind == HitKind::Stab) {
            if (p_Settings.m_StabWounds) {
                s_Decision.m_Agony = true;
                s_Decision.m_AgonySeconds = s_Seconds;
            }

            return s_Decision;
        }

        switch (p_Zone) {
            case BodyZone::Head:
                // Kopfschuss: sofort tot, wie im Spiel.
                return s_Decision;

            case BodyZone::Leg:
                s_Decision.m_Vanilla = false;

                if (p_Settings.m_LegWoundsLethal) {
                    s_Decision.m_Fatal = true;
                    s_Decision.m_Agony = true;
                    s_Decision.m_AgonySeconds = std::min(s_Seconds * std::max(1.f, p_Settings.m_LegBleedFactor), c_MaxAgonySeconds);
                }
                else {
                    s_Decision.m_DamageFactor = 0.3f;
                }

                return s_Decision;

            case BodyZone::Arm:
                s_Decision.m_Vanilla = false;
                s_Decision.m_DamageFactor = 0.25f;
                s_Decision.m_TryDisarm = p_Settings.m_Disarm && p_Rng.Int(0, 99) < p_Settings.m_DisarmChance;
                return s_Decision;

            case BodyZone::Torso:
            case BodyZone::Unknown:
            default:
                s_Decision.m_Vanilla = false;
                s_Decision.m_Fatal = true;
                s_Decision.m_Agony = true;
                s_Decision.m_AgonySeconds = s_Seconds;
                return s_Decision;
        }
    }

    AgonyConfig MakeAgonyConfig(const Settings& p_Settings) {
        AgonyConfig s_Config;
        s_Config.m_Twitching = p_Settings.m_Twitching;
        s_Config.m_Strength = p_Settings.m_TwitchStrength;
        s_Config.m_Limbs = p_Settings.m_LimbTwitching;
        s_Config.m_Lines = p_Settings.m_WoundedLines;
        s_Config.m_Blood = p_Settings.m_Blood;
        s_Config.m_BloodAmount = std::clamp(p_Settings.m_BloodAmount, 1, 3);
        return s_Config;
    }

    void Agony::Begin(double p_Now, double p_Seconds, Rng& p_Rng) {
        m_Active = true;
        m_Done = false;
        m_Finishing = false;
        m_Start = p_Now;
        m_Settled = p_Now + c_AgonySettleSeconds;
        m_Death = p_Now + std::clamp(p_Seconds, c_AgonySettleSeconds + 3.0, c_MaxAgonySeconds);

        // Waehrend der Sterbe-Animation passiert nichts, danach sofort der Schock.
        m_NextTwitch = m_Settled + p_Rng.Uniform(0.0, 0.3);
        m_NextSpasm = m_Settled + p_Rng.Uniform(0.2, 0.8);
        m_NextSpasmPulse = 0.0;
        m_SpasmPulsesLeft = 0;
        m_NextMoan = m_Settled + p_Rng.Uniform(0.8, 2.5);

        // Die Blutlache bildet sich, sobald der Koerper am Boden liegt.
        m_NextBlood = p_Now + p_Rng.Uniform(1.0, 1.6);
    }

    void Agony::FinishSoon(double p_Now, Rng& p_Rng) {
        if (!m_Active || m_Done) {
            return;
        }

        const double s_From = std::max(p_Now, m_Settled);
        m_Finishing = true;
        m_Death = std::min(m_Death, s_From + p_Rng.Uniform(1.0, 2.5));
        m_NextSpasm = s_From;
        m_NextMoan = m_Death + 1.0; // keine Saetze mehr
    }

    float Agony::Progress(double p_Now) const {
        if (!m_Active || m_Death <= m_Settled) {
            return 0.f;
        }

        return static_cast<float>(std::clamp((p_Now - m_Settled) / (m_Death - m_Settled), 0.0, 1.0));
    }

    void Agony::EmitTwitch(float p_Strength, const AgonyConfig& p_Config, Rng& p_Rng, std::vector<AgonyAction>& p_Out, TwitchStyle p_Style) {
        AgonyAction s_Action;
        s_Action.m_Type = AgonyActionType::Twitch;

        uint32_t s_Bone = c_BonePelvis;

        if (p_Style == TwitchStyle::Tremble) {
            // Zittern: vor allem Haende und Kopf (leicht, schon kleine Stoesse sind zu sehen).
            const double s_Roll = p_Rng.Uniform(0.0, 1.0);

            if (p_Config.m_Limbs && s_Roll < 0.5) {
                s_Bone = p_Rng.Chance(0.5) ? c_BoneLeftHand : c_BoneRightHand;
            }
            else if (s_Roll < 0.8) {
                s_Bone = c_BoneHead;
            }
        }
        else if (p_Config.m_Limbs && p_Style == TwitchStyle::Any && p_Rng.Chance(0.35)) {
            s_Bone = p_Rng.Chance(0.5) ? c_BoneLeftHand : c_BoneRightHand;
        }
        else if (m_LastBone == c_BonePelvis) {
            // Becken (Koerper baeumt sich auf) und Kopf abwechseln.
            s_Bone = c_BoneHead;
        }

        m_LastBone = s_Bone;

        const BoneResponse& s_Response = s_Bone == c_BonePelvis ? c_Pelvis : (s_Bone == c_BoneHead ? c_Head : c_Hand);
        const float s_Magnitude = p_Strength * p_Config.m_Strength * s_Response.m_Scale;
        const float s_X = s_Response.m_Side * p_Rng.UniformF(-1.f, 1.f);
        const float s_Y = s_Response.m_Side * p_Rng.UniformF(-1.f, 1.f);
        const float s_Z = s_Response.m_Up * p_Rng.UniformF(0.6f, 1.f);

        s_Action.m_Bone = s_Bone;
        s_Action.m_Impulse = { s_X * s_Magnitude, s_Y * s_Magnitude, s_Z * s_Magnitude };
        s_Action.m_Strength = s_Magnitude;
        p_Out.push_back(s_Action);
    }

    void Agony::Update(double p_Now, const AgonyConfig& p_Config, Rng& p_Rng, std::vector<AgonyAction>& p_Out) {
        if (!m_Active || m_Done) {
            return;
        }

        // --- Ende -----------------------------------------------------------------------------------------------
        if (p_Now >= m_Death) {
            if (p_Config.m_Twitching) {
                // Ein letztes Aufbaeumen.
                EmitTwitch(5.f, p_Config, p_Rng, p_Out, TwitchStyle::Body);
                EmitTwitch(5.f, p_Config, p_Rng, p_Out, TwitchStyle::Body);
            }

            p_Out.push_back({ AgonyActionType::Die });
            m_Done = true;
            return;
        }

        // --- Blut -----------------------------------------------------------------------------------------------
        if (p_Config.m_Blood && p_Now >= m_NextBlood) {
            AgonyAction s_Blood;
            s_Blood.m_Type = AgonyActionType::Blood;
            s_Blood.m_Strength = 1.f - 0.5f * Progress(p_Now);
            p_Out.push_back(s_Blood);
            m_NextBlood = p_Now + p_Rng.Uniform(3.0, 6.0) / std::max(1, p_Config.m_BloodAmount);
        }

        // Die Sterbe-Animation des Spiels nicht stoeren.
        if (p_Now < m_Settled) {
            return;
        }

        const float s_P = Progress(p_Now);
        const bool s_Shock = s_P < c_ShockEnd || p_Now < m_Settled + c_MinShockSeconds;
        const bool s_Final = s_P >= c_AgonyStart;

        if (p_Config.m_Twitching) {
            // --- Kraempfe: kurze Serien schneller Stoesse ------------------------------------------------------
            if (m_SpasmPulsesLeft > 0) {
                if (p_Now >= m_NextSpasmPulse) {
                    const float s_Pulse = s_Final ? p_Rng.UniformF(3.f, 4.5f) : (s_Shock ? p_Rng.UniformF(4.f, 6.f) : p_Rng.UniformF(2.5f, 4.f));
                    EmitTwitch(s_Pulse, p_Config, p_Rng, p_Out, TwitchStyle::Any);
                    --m_SpasmPulsesLeft;
                    m_NextSpasmPulse = p_Now + p_Rng.Uniform(0.07, 0.13);
                }
            }
            else if (p_Now >= m_NextSpasm) {
                m_SpasmPulsesLeft = s_Final ? p_Rng.Int(6, 10) : (s_Shock ? p_Rng.Int(5, 8) : p_Rng.Int(3, 6));
                m_NextSpasmPulse = p_Now;
                m_NextSpasm = p_Now + (s_Shock ? p_Rng.Uniform(2.5, 4.5) : (s_Final ? p_Rng.Uniform(2.0, 3.5) : p_Rng.Uniform(6.0, 12.0)));
            }

            // --- Einzelnes Zucken, gegen Ende Zittern -----------------------------------------------------------
            if (p_Now >= m_NextTwitch) {
                if (s_Shock) {
                    EmitTwitch(p_Rng.UniformF(3.f, 5.f), p_Config, p_Rng, p_Out, TwitchStyle::Any);
                    m_NextTwitch = p_Now + p_Rng.Uniform(0.25, 0.6);
                }
                else if (s_Final) {
                    EmitTwitch(p_Rng.UniformF(1.5f, 2.5f), p_Config, p_Rng, p_Out, TwitchStyle::Tremble);
                    m_NextTwitch = p_Now + p_Rng.Uniform(0.12, 0.3);
                }
                else {
                    EmitTwitch(p_Rng.UniformF(1.2f, 2.5f), p_Config, p_Rng, p_Out, TwitchStyle::Any);
                    m_NextTwitch = p_Now + p_Rng.Uniform(1.2, 3.5);
                }
            }
        }

        // --- Stoehnen / Flehen --------------------------------------------------------------------------------
        if (p_Config.m_Lines && !m_Finishing && p_Now >= m_NextMoan) {
            AgonyAction s_Moan;
            s_Moan.m_Type = AgonyActionType::Moan;
            s_Moan.m_Strength = s_P;
            p_Out.push_back(s_Moan);
            m_NextMoan = p_Now + (s_P < 0.7f ? p_Rng.Uniform(6.0, 11.0) : p_Rng.Uniform(9.0, 14.0));
        }
    }
}
