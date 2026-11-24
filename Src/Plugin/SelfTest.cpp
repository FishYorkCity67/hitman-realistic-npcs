// Selbsttest fuer die Entwicklung: [debug] self_test = true in mods\realisticnpcs.ini.
// Probiert im Level nacheinander einen Todeskampf (Sterben ueber das Spiel, danach Zucken am Koerper), Blut-Effekte
// und das Entwaffnen aus und schreibt jedes Ergebnis ins Protokoll. Danach schaltet er sich selbst wieder ab.

#include "RealisticNPCs.h"

#include "Safe.h"

#include <Glacier/ZActor.h>
#include <Glacier/ZCameraEntity.h>
#include <Glacier/ZHM5BaseCharacter.h>
#include <Glacier/ZItem.h>
#include <Glacier/ZSpatialEntity.h>

#include <Windows.h>

#include <cmath>
#include <format>
#include <limits>

namespace {
    // Nur fuer die Entwicklung: Umgebungsvariable RN_CALIBRATE=1 -> statt des Selbsttests Stoesse verschiedener
    // Staerke an einem Koerper direkt vor 47 (zum Abstimmen des Zuckens mit Bildschirmfotos).
    bool CalibrationRequested() {
        char s_Value[8] = {};
        return GetEnvironmentVariableA("RN_CALIBRATE", s_Value, sizeof(s_Value)) > 0 && s_Value[0] == '1';
    }
}

void RealisticNPCs::RunImpulseCalibration(const rn::Settings& p_Settings, double p_Now, const std::vector<const NpcInfo*>& p_Candidates) {
    struct Probe {
        uint32_t m_Bone;
        float m_Up;
        const char* m_Name;
    };

    static constexpr Probe c_Probes[] = {
        { rn::c_BonePelvis, 5.f, "Becken 5" }, { rn::c_BonePelvis, 10.f, "Becken 10" }, { rn::c_BonePelvis, 20.f, "Becken 20" },
        { rn::c_BonePelvis, 40.f, "Becken 40" }, { rn::c_BoneHead, 3.f, "Kopf 3" }, { rn::c_BoneHead, 6.f, "Kopf 6" },
        { rn::c_BoneHead, 12.f, "Kopf 12" }, { rn::c_BoneLeftHand, 2.f, "Hand 2" }, { rn::c_BoneLeftHand, 4.f, "Hand 4" },
        { rn::c_BoneLeftHand, 8.f, "Hand 8" },
    };

    constexpr int c_First = 100;
    const int s_Probe = m_SelfTestStep - (c_First + 2);

    if (m_SelfTestStep < c_First) {
        m_SelfTestStep = c_First;
    }

    if (m_SelfTestStep == c_First) {
        // Je ein NPC links und rechts der Bildmitte (8-22 m): 47 verdeckt die Mitte, einer ist sicher zu sehen.
        float s_Fx = m_World.m_CameraForward.x;
        float s_Fy = m_World.m_CameraForward.y;
        const float s_FLen = std::max(0.01f, std::sqrt(s_Fx * s_Fx + s_Fy * s_Fy));
        s_Fx /= s_FLen;
        s_Fy /= s_FLen;
        float s_Rx = s_Fy;
        float s_Ry = -s_Fx;

        if (auto* s_Camera = Functions::GetCurrentCamera->Call()) {
            const SMatrix s_M = s_Camera->GetObjectToWorldMatrix();
            const float s_RLen = std::sqrt(s_M.XAxis.x * s_M.XAxis.x + s_M.XAxis.y * s_M.XAxis.y);

            if (s_RLen > 0.01f) {
                s_Rx = s_M.XAxis.x / s_RLen;
                s_Ry = s_M.XAxis.y / s_RLen;
            }
        }

        const NpcInfo* s_Side[2] = {};
        float s_SideScore[2] = { 1e9f, 1e9f };

        for (const NpcInfo* s_Candidate : p_Candidates) {
            const float s_Vx = s_Candidate->m_Pos.x - m_World.m_CameraPos.x;
            const float s_Vy = s_Candidate->m_Pos.y - m_World.m_CameraPos.y;
            const float s_Forward = s_Vx * s_Fx + s_Vy * s_Fy;
            const float s_Lateral = s_Vx * s_Rx + s_Vy * s_Ry;

            if (s_Forward < 8.f || s_Forward > 22.f) {
                continue;
            }

            const float s_Tan = s_Lateral / s_Forward;
            const int s_Index = s_Tan >= 0.f ? 0 : 1;
            const float s_Score = std::abs(std::abs(s_Tan) - 0.3f);

            if (std::abs(s_Tan) >= 0.12f && std::abs(s_Tan) <= 0.55f && s_Score < s_SideScore[s_Index]) {
                s_SideScore[s_Index] = s_Score;
                s_Side[s_Index] = s_Candidate;
            }
        }

        // In Paris steht zu Beginn eine NPC frei sichtbar am Brunnen (im ersten Selbsttest gut zu sehen): die bevorzugen.
        for (const NpcInfo* s_Candidate : p_Candidates) {
            if (s_Candidate->m_Name.find("Lindsey") != std::string::npos) {
                s_Side[0] = s_Candidate;
                s_Side[1] = nullptr;
                break;
            }
        }

        if (!s_Side[0] && !s_Side[1]) {
            Log("Kalibrierung: kein NPC im Bild – abgebrochen.");
            m_SelfTestStep = -1;
            return;
        }

        m_SelfTestDying = s_Side[0] ? s_Side[0]->m_Key : 0;
        m_SelfTestStab = s_Side[1] ? s_Side[1]->m_Key : 0;
        Log(std::format("Kalibrierung: Start – Kamera-X-Seite: {} ({:.1f} m), Gegenseite: {} ({:.1f} m).",
            s_Side[0] ? s_Side[0]->m_Name : "-", s_Side[0] ? s_Side[0]->m_Distance : 0.f,
            s_Side[1] ? s_Side[1]->m_Name : "-", s_Side[1] ? s_Side[1]->m_Distance : 0.f));
        m_SelfTestStep = c_First + 1;
        m_SelfTestNext = p_Now + 1.0;
        return;
    }

    const NpcInfo* s_Targets[2] = { m_World.Find(m_SelfTestDying), m_World.Find(m_SelfTestStab) };

    if (m_SelfTestStep == c_First + 1) {
        for (const NpcInfo* s_Npc : s_Targets) {
            if (s_Npc) {
                KillActor(s_Npc->m_Actor, true);
            }
        }

        Log("Kalibrierung: getötet, Stöße folgen alle 1,5 s ab +4 s.");
        m_SelfTestStep = c_First + 2;
        m_SelfTestNext = p_Now + 4.0;
        return;
    }

    if (s_Probe >= 0 && s_Probe < static_cast<int>(std::size(c_Probes))) {
        const Probe& s_P = c_Probes[s_Probe];

        for (const NpcInfo* s_Npc : s_Targets) {
            if (s_Npc && s_Npc->m_Actor) {
                ApplyImpulse(s_Npc->m_Actor, { 0.f, 0.f, s_P.m_Up }, s_P.m_Bone);
            }
        }

        Log(std::format("Kalibrierung: Stoß {} (Spielzeit {:.2f}).", s_P.m_Name, p_Now));
        ++m_SelfTestStep;
        m_SelfTestNext = p_Now + 1.5;
        return;
    }

    Log("Kalibrierung fertig.");
    m_SelfTestStep = -1;
    rn::Settings s_Off = p_Settings;
    s_Off.m_SelfTest = false;
    CommitSettings(s_Off);
}

void RealisticNPCs::RunSelfTest(const rn::Settings& p_Settings, double p_Now) {
    if (!p_Settings.m_SelfTest || m_SelfTestStep < 0) {
        return;
    }

    std::vector<const NpcInfo*> s_Candidates;

    for (const NpcInfo& s_Npc : m_World.m_Npcs) {
        if (s_Npc.m_Alive && !s_Npc.m_Pacified && !m_Dying.contains(s_Npc.m_Key) && IsLiveActor(s_Npc.m_Actor)) {
            s_Candidates.push_back(&s_Npc);
        }
    }

    // Warten, bis das Level NPCs hat und sich eingeschwungen hat.
    if (m_SelfTestStart < 0.0) {
        if (s_Candidates.size() < 4) {
            return;
        }

        m_SelfTestStart = p_Now;
        m_SelfTestNext = p_Now + 10.0;
        Log(std::format("Selbsttest: Level bereit – {} NPCs in der Nähe, {} lebend. Start in 10 s.", m_World.m_Npcs.size(), s_Candidates.size()));
        return;
    }

    if (p_Now < m_SelfTestNext) {
        return;
    }

    if (CalibrationRequested()) {
        RunImpulseCalibration(p_Settings, p_Now, s_Candidates);
        return;
    }

    const auto s_Pick =[&](uint64_t p_Avoid1, uint64_t p_Avoid2, bool p_Armed) -> const NpcInfo* {
        for (const NpcInfo* s_Npc : s_Candidates) {
            if (s_Npc->m_Key == p_Avoid1 || s_Npc->m_Key == p_Avoid2) {
                continue;
            }

            if (p_Armed && !(s_Npc->m_Armed && s_Npc->m_Actor->m_pInventoryHandler && s_Npc->m_Actor->m_pInventoryHandler->m_rMainWeapon)) {
                continue;
            }

            return s_Npc;
        }

        return nullptr;
    };

    // Moeglichst jemand, den die Kamera gerade im Bild hat (fuer Bildschirmfotos beim Testen).
    const auto s_PickVisible = [&](uint64_t p_Avoid) -> const NpcInfo* {
        const NpcInfo* s_Best = nullptr;
        float s_BestOffset = std::numeric_limits<float>::max();

        for (const NpcInfo* s_Npc : s_Candidates) {
            if (s_Npc->m_Key == p_Avoid || s_Npc->m_Distance < 3.f || s_Npc->m_Distance > 25.f) {
                continue;
            }

            const rn::Vec3 s_Chest { s_Npc->m_Pos.x, s_Npc->m_Pos.y, s_Npc->m_Pos.z + 1.f };
            const float s_Offset = rn::DistanceToRay(m_World.m_CameraPos, m_World.m_CameraForward, s_Chest, 40.f) / std::max(1.f, s_Npc->m_Distance);

            if (s_Offset < s_BestOffset) {
                s_BestOffset = s_Offset;
                s_Best = s_Npc;
            }
        }

        return s_Best ? s_Best : s_Pick(p_Avoid, 0, false);
    };

    // Reihenfolge: erst der Todeskampf (die Bildschirmfotos sollen ihn ungestoert zeigen), dann Blut, zuletzt Entwaffnen
    // (danach laufen Wachen herum und stellen sich gern vor die Kamera).
    switch (m_SelfTestStep) {
        case 0: {
            // --- Todeskampf: Sterben ueber das Spiel, danach Zucken am Koerper ------------------------------------
            // In Paris steht zu Beginn eine NPC frei sichtbar am Brunnen – die eignet sich fuer Bildschirmfotos.
            const NpcInfo* s_Npc = nullptr;

            for (const NpcInfo* s_Candidate : s_Candidates) {
                if (s_Candidate->m_Name.find("Lindsey") != std::string::npos) {
                    s_Npc = s_Candidate;
                    break;
                }
            }

            s_Npc = s_Npc ? s_Npc : s_PickVisible(0);
            m_SelfTestDying = s_Npc ? s_Npc->m_Key : 0;

            if (s_Npc) {
                KillActor(s_Npc->m_Actor, true);
                StartAgony(*s_Npc, 16.0, p_Now);
                Log(std::format("Selbsttest Todeskampf: {} ({:.0f} m) stirbt mit der Sterbe-Animation des Spiels, danach 16 s Todeskampf.",
                    s_Npc->m_Name, s_Npc->m_Distance));
            }
            else {
                Log("Selbsttest Todeskampf: Test-NPC fehlt – übersprungen.");
            }

            m_SelfTestStep = 1;
            m_SelfTestNext = p_Now + 3.0;
            break;
        }

        case 1: {
            // --- Blut ---------------------------------------------------------------------------------------------
            if (const NpcInfo* s_Npc = s_Pick(m_SelfTestDying, 0, false)) {
                const rn::Vec3 s_Feet = s_Npc->m_Pos;
                const rn::Vec3 s_Chest { s_Feet.x, s_Feet.y, s_Feet.z + 1.25f };
                const rn::Vec3 s_Head { s_Feet.x, s_Feet.y, s_Feet.z + 1.65f };
                const bool s_Spray = SpawnBlood(BloodKind::Spray, s_Chest);
                const bool s_HeadOk = SpawnBlood(BloodKind::Headshot, s_Head);
                const bool s_Ground = SpawnBlood(BloodKind::Ground, s_Feet);
                const bool s_Pool = SpawnBlood(BloodKind::Pool, s_Feet);
                Log(std::format("Selbsttest Blut bei {}: Spritzer {}, Kopfschuss {}, Boden {}, Lache {} (Fehler insgesamt: {}).", s_Npc->m_Name,
                    s_Spray ? "ok" : "fehlt", s_HeadOk ? "ok" : "fehlt", s_Ground ? "ok" : "fehlt", s_Pool ? "ok" : "fehlt", m_BloodFailed.load()));
            }

            m_SelfTestStep = 2;
            m_SelfTestNext = p_Now + 2.0;
            break;
        }

        case 2: {
            // --- Laeuft der Todeskampf? Zusaetzlich Kopf und Haende am Koerper ausprobieren --------------------------
            const NpcInfo* s_Npc = m_World.Find(m_SelfTestDying);
            const auto s_It = m_Dying.find(m_SelfTestDying);
            const bool s_Dead = s_Npc && !s_Npc->m_Alive;
            const bool s_Ragdoll = s_Npc && s_Npc->m_Actor && s_Npc->m_Actor->m_pRagdollHandler;

            if (s_Dead && s_Ragdoll) {
                ApplyImpulse(s_Npc->m_Actor, { 0.f, 0.f, 8.f }, rn::c_BoneHead);
                ApplyImpulse(s_Npc->m_Actor, { 0.f, 0.f, 3.f }, rn::c_BoneLeftHand);
                ApplyImpulse(s_Npc->m_Actor, { 0.f, 0.f, 3.f }, rn::c_BoneRightHand);
            }

            Log(std::format("Selbsttest Todeskampf läuft: NPC {}, Todeskampf {}, Ragdoll {}, Kopf {}, Hände {}, Becken {}.",
                s_Npc ? (s_Dead ? "tot" : "lebt (FEHLER)") : "weg", s_It == m_Dying.end() ? "fehlt (FEHLER)" : (s_It->second.m_Started ? "läuft" : "wartet"),
                s_Ragdoll ? "ja" : "nein", m_HeadBoneBroken ? "FEHLER" : "ok", m_LimbBonesBroken ? "FEHLER" : "ok", m_ImpulsesBroken ? "FEHLER" : "ok"));

            // Gegenprobe: wer nicht stirbt, bekommt keinen Todeskampf.
            if (const NpcInfo* s_Other = s_Pick(m_SelfTestDying, 0, false)) {
                m_SelfTestStab = s_Other->m_Key;
                StartAgony(*s_Other, 20.0, p_Now);
            }

            m_SelfTestStep = 3;
            m_SelfTestNext = p_Now + 13.0;
            break;
        }

        case 3: {
            const bool s_AgonyOver = !m_Dying.contains(m_SelfTestDying);
            const bool s_NoAgonyForLiving = !m_Dying.contains(m_SelfTestStab);
            const NpcInfo* s_Living = m_World.Find(m_SelfTestStab);
            Log(std::format("Selbsttest Todeskampf-Ergebnis: vorbei {}, beendet bisher {}. Gegenprobe (lebt weiter, kein Todeskampf): {}.",
                s_AgonyOver ? "ja" : "nein (FEHLER)", m_DeathsCaused.load(),
                s_NoAgonyForLiving && s_Living && s_Living->m_Alive ? "ok" : "FEHLER"));

            m_SelfTestStep = 4;
            m_SelfTestNext = p_Now + 1.0;
            break;
        }

        case 4: {
            // --- Entwaffnen ---------------------------------------------------------------------------------------
            if (const NpcInfo* s_Npc = s_Pick(m_SelfTestStab, 0, true)) {
                DisarmActor(s_Npc->m_Actor, s_Npc->m_Key, p_Settings, p_Now, false);
                const bool s_Gone = !s_Npc->m_Actor->m_pInventoryHandler || !s_Npc->m_Actor->m_pInventoryHandler->m_rMainWeapon;
                Log(std::format("Selbsttest Entwaffnen bei {}: {}.", s_Npc->m_Name, s_Gone ? "Waffe fallen gelassen" : "Waffe noch in der Hand"));
            }
            else {
                Log("Selbsttest Entwaffnen: kein bewaffneter NPC in der Nähe – übersprungen.");
            }

            m_SelfTestStep = 5;
            m_SelfTestNext = p_Now + 2.0;
            break;
        }

        case 5: {
            Log(std::format("Selbsttest fertig. Blut: {} erzeugt / {} fehlgeschlagen / {} Vorlagen gefunden. Zucken: {}. Kopf: {}. Arme: {}.",
                m_BloodSpawned.load(), m_BloodFailed.load(), m_BloodTemplatesFound.load(), m_ImpulsesBroken ? "AUS (Fehler)" : "ok",
                m_HeadBoneBroken ? "AUS (Fehler)" : "ok", m_LimbBonesBroken ? "AUS (Fehler)" : "ok"));

            m_SelfTestStep = -1;
            rn::Settings s_Off = p_Settings;
            s_Off.m_SelfTest = false;
            CommitSettings(s_Off);
            break;
        }

        default:
            m_SelfTestStep = -1;
            break;
    }
}
