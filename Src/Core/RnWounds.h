#pragma once

#include "RnMath.h"
#include "RnSettings.h"

#include <cstdint>
#include <vector>

// Verwundungen und Todeskampf – reine Logik ohne Spiel-Abhaengigkeiten.
//
// Wichtig: Lebende NPCs lassen sich in HITMAN nicht sauber per Physik umwerfen (die Animation bricht weg, der NPC
// steht dann in der T-Pose). Darum stirbt ein toedlich getroffener NPC ueber das Spiel selbst – mit dessen
// Sterbe-Animation und Ragdoll. Der Todeskampf (Zucken, Kraempfe, Zittern, Stoehnen, Blut) laeuft danach am Boden.
namespace rn {
    // Ragdoll-Knochen: Becken (aus den Beispiel-Mods des SDK, an NPCs erprobt), Kopf und Haende.
    constexpr uint32_t c_BonePelvis = 1;
    constexpr uint32_t c_BoneHead = 11;
    constexpr uint32_t c_BoneLeftHand = 128;
    constexpr uint32_t c_BoneRightHand = 157;

    // So lange laeuft die Sterbe-Animation des Spiels, bevor der Todeskampf am Boden beginnt.
    constexpr double c_AgonySettleSeconds = 1.8;
    // Laenger dauert kein Todeskampf.
    constexpr double c_MaxAgonySeconds = 150.0;

    enum class HitKind { Shot = 0, Stab, Melee };

    struct HitDecision {
        bool m_Vanilla = true;        // den Schaden rechnet das Spiel allein (Kopfschuss, Messer, Faust)
        float m_DamageFactor = 1.f;   // Anteil des Spielschadens (nur wenn !m_Vanilla und !m_Fatal)
        bool m_Fatal = false;         // stirbt sicher an diesem Treffer – ueber das Spiel, mit dessen Sterbe-Animation
        bool m_Agony = false;         // ist er danach tot: Todeskampf am Boden
        double m_AgonySeconds = -1.0; // Dauer des Todeskampfs
        bool m_TryDisarm = false;     // Waffe faellt aus der Hand
        bool m_FinishSoon = false;    // liegt schon im Todeskampf: stark verkuerzen (Gnadenschuss)
        int m_BloodBursts = 0;        // Blutspritzer beim Treffer
    };

    // p_InAgony: der Getroffene liegt schon im Todeskampf.
    HitDecision DecideHit(BodyZone p_Zone, HitKind p_Kind, bool p_InAgony, const Settings& p_Settings, Rng& p_Rng);

    enum class AgonyActionType { Twitch = 0, Moan, Blood, Die };

    struct AgonyAction {
        AgonyActionType m_Type = AgonyActionType::Twitch;
        uint32_t m_Bone = c_BonePelvis;
        Vec3 m_Impulse;       // Twitch: fertiger Impuls (Welt-Achsen, z = oben)
        float m_Strength = 1.f;
    };

    struct AgonyConfig {
        bool m_Twitching = true;
        float m_Strength = 1.f;
        bool m_Limbs = false;
        bool m_Lines = true;
        bool m_Blood = true;
        int m_BloodAmount = 2;
    };

    AgonyConfig MakeAgonyConfig(const Settings& p_Settings);

    // Todeskampf eines NPCs, der gerade (ueber das Spiel) gestorben ist:
    //   Sterbe-Animation ausklingen lassen -> Schock (heftige Kraempfe) -> Kampf (vereinzeltes Zucken, Stoehnen,
    //   Kraempfe) -> Agonie (Zittern) -> letztes Aufbaeumen -> Ruhe.
    class Agony {
    public:
        void Begin(double p_Now, double p_Seconds, Rng& p_Rng);

        // Weiterer Treffer: vorbei in Kuerze (mit letzten Kraempfen).
        void FinishSoon(double p_Now, Rng& p_Rng);

        bool Active() const { return m_Active && !m_Done; }
        bool Done() const { return m_Done; }
        bool Finishing() const { return m_Finishing; }
        double Start() const { return m_Start; }
        double DeathTime() const { return m_Death; }

        // Fortschritt 0..1 ab dem Ende der Sterbe-Animation.
        float Progress(double p_Now) const;

        // Liefert alle jetzt faelligen Aktionen. "Die" kommt genau einmal, danach nichts mehr.
        void Update(double p_Now, const AgonyConfig& p_Config, Rng& p_Rng, std::vector<AgonyAction>& p_Out);

    private:
        enum class TwitchStyle { Any, Body, Tremble }; // Body: nur Becken/Kopf; Tremble: vor allem Haende/Kopf
        void EmitTwitch(float p_Strength, const AgonyConfig& p_Config, Rng& p_Rng, std::vector<AgonyAction>& p_Out, TwitchStyle p_Style);

        bool m_Active = false;
        bool m_Done = false;
        bool m_Finishing = false;
        double m_Start = 0.0;
        double m_Settled = 0.0;
        double m_Death = 0.0;
        double m_NextTwitch = 0.0;
        double m_NextSpasm = 0.0;
        double m_NextSpasmPulse = 0.0;
        int m_SpasmPulsesLeft = 0;
        double m_NextMoan = 0.0;
        double m_NextBlood = 0.0;
        uint32_t m_LastBone = c_BoneHead;
    };
}
