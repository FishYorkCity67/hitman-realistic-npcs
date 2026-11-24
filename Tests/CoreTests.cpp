// Tests fuer den Kern von Realistic NPCs (laufen ohne Spiel).
// Bauen: cmake --build _build/x64-Release --target RealisticNPCsTests
// Starten: _build\x64-Release\Mods\RealisticNPCs\RealisticNPCsTests.exe

#include "RnLines.h"
#include "RnMath.h"
#include "RnSettings.h"
#include "RnStandoff.h"
#include "RnWounds.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {
    int g_Failures = 0;
    int g_Checks = 0;

#define CHECK(cond) \
    do { \
        ++g_Checks; \
        if (!(cond)) { \
            ++g_Failures; \
            std::printf("  FEHLER %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

    void TestHitZones() {
        std::printf("Trefferzonen\n");
        const rn::Vec3 s_Feet { 10.f, 0.f, 0.f };
        const rn::Vec3 s_Origin { 0.f, 0.f, 1.6f };

        // Waagerecht auf Kopfhoehe -> Kopf.
        CHECK(rn::EstimateHitZone(s_Origin, { 1.f, 0.f, 0.01f }, s_Feet) == rn::BodyZone::Head);
        // Leicht nach unten auf die Brust -> Rumpf.
        CHECK(rn::EstimateHitZone(s_Origin, { 1.f, 0.f, -0.03f }, s_Feet) == rn::BodyZone::Torso);
        // Stark nach unten -> Bein.
        CHECK(rn::EstimateHitZone(s_Origin, { 1.f, 0.f, -0.12f }, s_Feet) == rn::BodyZone::Leg);
        // Seitlich auf Brusthoehe vorbei an der Achse -> Arm.
        CHECK(rn::EstimateHitZone(s_Origin, { 1.f, 0.03f, -0.03f }, s_Feet) == rn::BodyZone::Arm);
        // Weit daneben oder nach hinten -> unbekannt.
        CHECK(rn::EstimateHitZone(s_Origin, { 1.f, 0.3f, 0.f }, s_Feet) == rn::BodyZone::Unknown);
        CHECK(rn::EstimateHitZone(s_Origin, { -1.f, 0.f, 0.f }, s_Feet) == rn::BodyZone::Unknown);
        CHECK(rn::EstimateHitZone(s_Origin, { 0.f, 0.f, 1.f }, s_Feet) == rn::BodyZone::Unknown);

        // Abstand zum Zielstrahl (Gnadenschuss auf einen Koerper am Boden).
        const rn::Vec3 s_Body { 10.f, 0.f, 0.3f };
        CHECK(rn::DistanceToRay(s_Origin, { 10.f, 0.f, -1.3f }, s_Body, 80.f) < 0.01f);  // genau drauf (Richtung unnormiert)
        CHECK(std::abs(rn::DistanceToRay(s_Origin, { 1.f, 0.f, -0.13f }, { 10.f, 1.f, 0.3f }, 80.f) - 1.f) < 0.05f);
        CHECK(rn::DistanceToRay(s_Origin, { -1.f, 0.f, 0.f }, s_Body, 80.f) > 1e8f);     // hinter der Kamera
        CHECK(rn::DistanceToRay(s_Origin, { 1.f, 0.f, -0.13f }, s_Body, 5.f) > 1e8f);    // zu weit weg
        CHECK(rn::DistanceToRay(s_Origin, { 0.f, 0.f, 0.f }, s_Body, 80.f) > 1e8f);      // keine Richtung
    }

    void TestRng() {
        std::printf("Zufall\n");
        rn::Rng a(42), b(42);

        for (int i = 0; i < 100; ++i) {
            CHECK(a.Next() == b.Next());
        }

        rn::Rng r(7);

        for (int i = 0; i < 1000; ++i) {
            const double u = r.Uniform(2.0, 5.0);
            CHECK(u >= 2.0 && u < 5.0);
            const int n = r.Int(3, 6);
            CHECK(n >= 3 && n <= 6);
        }

        CHECK(r.Uniform(4.0, 4.0) == 4.0);
    }

    void Setup(rn::StandoffTracker& s_Tracker) {
        rn::StandoffConfig s_Config;
        s_Config.m_WarnSeconds = 8.f;
        s_Config.m_ComplySeconds = 6.f;
        s_Config.m_MaxSeconds = 30.f;
        s_Config.m_StandDownSeconds = 100.f;
        s_Config.m_AggressionMemorySeconds = 20.f;
        s_Config.m_ForgetSeconds = 25.f;
        s_Tracker.Configure(s_Config);
    }

    void TestStandoffBasics() {
        std::printf("Konfrontation: Warnung und Frist\n");
        rn::StandoffTracker t;
        Setup(t);

        // Erster Schussversuch: blockiert, neue Konfrontation.
        auto d = t.OnShotAttempt(1, 100.0);
        CHECK(d.m_Block && d.m_NewStandoff);
        CHECK(t.State(1, 100.0) == rn::StandoffState::Warning);
        CHECK(std::abs(t.SecondsLeft(1, 100.0) - 8.0) < 1e-6);

        // Weitere Versuche innerhalb der Frist: blockiert, aber keine neue Warnung.
        d = t.OnShotAttempt(1, 104.0);
        CHECK(d.m_Block && !d.m_NewStandoff);
        CHECK(t.Warnings(104.0).size() == 1);

        // Noch nicht abgelaufen.
        CHECK(t.TakeExpired(107.0).empty());

        // Frist vorbei: genau einmal als abgelaufen gemeldet, Schuesse gehen durch.
        const auto s_Expired = t.TakeExpired(108.5);
        CHECK(s_Expired.size() == 1 && s_Expired[0] == 1);
        CHECK(t.TakeExpired(109.0).empty());
        d = t.OnShotAttempt(1, 109.0);
        CHECK(!d.m_Block && !d.m_NewStandoff);
        CHECK(t.State(1, 109.0) == rn::StandoffState::Hostile);

        // Lange Ruhe: Begegnung vergessen, beim naechsten Mal wieder eine Warnung.
        CHECK(t.State(1, 140.0) == rn::StandoffState::None);
        d = t.OnShotAttempt(1, 140.0);
        CHECK(d.m_Block && d.m_NewStandoff);
    }

    void TestStandoffCompliance() {
        std::printf("Konfrontation: Waffe weg = mehr Zeit, aber nicht ewig\n");
        rn::StandoffTracker t;
        Setup(t);
        t.OnShotAttempt(1, 0.0);

        // Nach 7 s, 1 s Rest: ohne Waffe in der Hand wieder mindestens 6 s.
        t.OnCompliance(7.0);
        CHECK(std::abs(t.SecondsLeft(1, 7.0) - 6.0) < 1e-6);

        // Dauerhaft brav: haelt bis zur Hoechstdauer (30 s), dann ist Schluss.
        for (double s_Now = 7.0; s_Now < 29.0; s_Now += 0.5) {
            t.OnCompliance(s_Now);
        }

        CHECK(t.State(1, 29.0) == rn::StandoffState::Warning);
        CHECK(t.SecondsLeft(1, 29.0) <= 1.0 + 1e-6);
        t.OnCompliance(29.5);
        CHECK(t.State(1, 30.5) == rn::StandoffState::Hostile);

        // Menue offen: Hold verhaelt sich genauso.
        rn::StandoffTracker h;
        Setup(h);
        h.OnShotAttempt(2, 0.0);
        h.Hold(6.0, 4.f);
        CHECK(std::abs(h.SecondsLeft(2, 6.0) - 4.0) < 1e-6 || h.SecondsLeft(2, 6.0) >= 2.0);
    }

    void TestStandoffAggression() {
        std::printf("Konfrontation: Angriff von 47\n");
        rn::StandoffTracker t;
        Setup(t);
        t.OnShotAttempt(1, 0.0);
        t.OnPlayerAggression(2.0);

        // Wer gewarnt hat, schiesst jetzt.
        auto d = t.OnShotAttempt(1, 2.1);
        CHECK(!d.m_Block);

        // Neue NPCs warnen nach einem Angriff nicht mehr ...
        d = t.OnShotAttempt(2, 5.0);
        CHECK(!d.m_Block && !d.m_NewStandoff);

        // ... erst nach Ablauf des Gedaechtnisses (20 s) wieder – und nach dem Vergessen der Begegnung.
        d = t.OnShotAttempt(3, 23.0);
        CHECK(d.m_Block && d.m_NewStandoff);

        // Gewarnte Wachen, die noch keinen Schuss abgegeben haben, melden keine "abgelaufene Frist".
        CHECK(t.TakeExpired(24.0).empty());
    }

    void TestStandoffOutcomes() {
        std::printf("Konfrontation: Ergebnisse der Verhandlung\n");
        rn::StandoffTracker t;
        Setup(t);
        t.OnShotAttempt(1, 0.0);

        // Erfolgreich verhandelt: blockiert bis StandDownSeconds, danach neue Warnung.
        t.Apply(1, rn::PleaResult::StandDown, 3.0);
        CHECK(t.State(1, 3.0) == rn::StandoffState::Calm);
        CHECK(t.Warnings(3.0).empty());
        auto d = t.OnShotAttempt(1, 50.0);
        CHECK(d.m_Block && !d.m_NewStandoff);
        CHECK(t.State(1, 104.0) == rn::StandoffState::None);
        d = t.OnShotAttempt(1, 104.0);
        CHECK(d.m_Block && d.m_NewStandoff);

        // Letzte Warnung: nur noch 3 s.
        t.Apply(1, rn::PleaResult::LastWarning, 105.0);
        CHECK(std::abs(t.SecondsLeft(1, 105.0) - 3.0) < 1e-6);

        // Feuer: sofort Schluss.
        t.Apply(1, rn::PleaResult::OpenFire, 106.0);
        d = t.OnShotAttempt(1, 106.1);
        CHECK(!d.m_Block);
        CHECK(t.TakeExpired(106.2).empty()); // kein doppeltes "Feuer!"

        // Ergeben: schiesst dauerhaft nicht mehr.
        rn::StandoffTracker g;
        Setup(g);
        g.OnShotAttempt(5, 0.0);
        g.Apply(5, rn::PleaResult::GuardSurrenders, 1.0);
        d = g.OnShotAttempt(5, 5000.0);
        CHECK(d.m_Block && !d.m_NewStandoff);

        // Versuche zaehlen.
        g.CountAttempt(5);
        g.CountAttempt(5);
        CHECK(g.Attempts(5) == 2);
        CHECK(g.Attempts(99) == 0);

        // Vergessen (tot).
        g.Forget(5);
        CHECK(g.State(5, 5001.0) == rn::StandoffState::None);
    }

    void TestPleas() {
        std::printf("Verhandeln: Chancen\n");
        rn::PleaContext c;

        // Ergeben mit Waffe in der Hand geht nicht.
        c.m_PlayerArmed = true;
        CHECK(rn::PleaChance(rn::Plea::Surrender, c) == 0.f);
        rn::Rng r(1);
        CHECK(rn::ResolvePlea(rn::Plea::Surrender, c, r) == rn::PleaResult::HolsterFirst);

        // Ohne Waffe meistens erfolgreich.
        c.m_PlayerArmed = false;
        CHECK(rn::PleaChance(rn::Plea::Surrender, c) > 0.85f);

        // Drohen ohne Waffe ist ein Bluff.
        CHECK(rn::PleaChance(rn::Plea::Threaten, c) <= 0.05f + 1e-6f);

        // Ruf hilft beim Drohen, mehr Wachen und Elite machen es schwerer.
        rn::PleaContext s_Armed;
        s_Armed.m_PlayerArmed = true;
        const float s_Base = rn::PleaChance(rn::Plea::Threaten, s_Armed);
        s_Armed.m_ArmedKills = 4;
        const float s_Feared = rn::PleaChance(rn::Plea::Threaten, s_Armed);
        CHECK(s_Feared > s_Base);
        s_Armed.m_GuardsInvolved = 4;
        CHECK(rn::PleaChance(rn::Plea::Threaten, s_Armed) < s_Feared);
        s_Armed.m_Elite = true;
        CHECK(rn::PleaChance(rn::Plea::Threaten, s_Armed) >= 0.05f);

        // Reden: Verkleidung hilft, Waffe und Wiederholung schaden.
        rn::PleaContext t;
        const float s_Plain = rn::PleaChance(rn::Plea::Talk, t);
        t.m_PlayerDisguised = true;
        CHECK(rn::PleaChance(rn::Plea::Talk, t) > s_Plain);
        t.m_Attempts = 2;
        CHECK(rn::PleaChance(rn::Plea::Talk, t) < s_Plain + 0.2f);
        t.m_PlayerArmed = true;
        t.m_Attempts = 5;
        CHECK(rn::PleaChance(rn::Plea::Talk, t) >= 0.05f);

        // Alle Chancen im gueltigen Bereich.
        for (int p = 0; p < static_cast<int>(rn::Plea::Count); ++p) {
            for (int k = 0; k < 8; ++k) {
                rn::PleaContext x;
                x.m_PlayerArmed = (k & 1) != 0;
                x.m_PlayerDisguised = (k & 2) != 0;
                x.m_Elite = (k & 4) != 0;
                x.m_ArmedKills = k;
                x.m_GuardsInvolved = k;
                x.m_Attempts = k;
                const float s_Chance = rn::PleaChance(static_cast<rn::Plea>(p), x);
                CHECK(s_Chance >= 0.f && s_Chance <= 1.f);
            }
        }

        // Ergebnisse passen zur Art der Bitte.
        rn::Rng q(99);
        rn::PleaContext y;
        y.m_PlayerArmed = true;
        y.m_ArmedKills = 5;

        for (int i = 0; i < 200; ++i) {
            const auto s_Result = rn::ResolvePlea(rn::Plea::Threaten, y, q);
            CHECK(s_Result == rn::PleaResult::GuardSurrenders || s_Result == rn::PleaResult::OpenFire);
            const auto s_Talk = rn::ResolvePlea(rn::Plea::Talk, y, q);
            CHECK(s_Talk == rn::PleaResult::StandDown || s_Talk == rn::PleaResult::LastWarning);
        }

        CHECK(std::string(rn::ChanceHint(0.9f)) == "gute Chance");
        CHECK(std::string(rn::ChanceHint(0.1f)) == "kaum Chance");
    }

    void TestDecideHit() {
        std::printf("Treffer-Entscheidung\n");
        rn::Settings s;
        rn::Rng r(3);

        // Kopfschuss: wie im Spiel, kein Todeskampf.
        auto d = rn::DecideHit(rn::BodyZone::Head, rn::HitKind::Shot, false, s, r);
        CHECK(d.m_Vanilla && !d.m_Fatal && !d.m_Agony && d.m_BloodBursts > 0);

        // Rumpf: stirbt sicher (ueber das Spiel), danach Todeskampf im eingestellten Rahmen.
        d = rn::DecideHit(rn::BodyZone::Torso, rn::HitKind::Shot, false, s, r);
        CHECK(!d.m_Vanilla && d.m_Fatal && d.m_Agony);
        CHECK(d.m_AgonySeconds >= s.m_BleedMinSeconds && d.m_AgonySeconds <= s.m_BleedMaxSeconds);

        // Unbekannte Zone wird wie Rumpf behandelt.
        d = rn::DecideHit(rn::BodyZone::Unknown, rn::HitKind::Shot, false, s, r);
        CHECK(d.m_Fatal && d.m_Agony);

        // Bein: laengerer Todeskampf (oder nur Teilschaden, wenn abgeschaltet). Nie mehr als das Maximum.
        d = rn::DecideHit(rn::BodyZone::Leg, rn::HitKind::Shot, false, s, r);
        CHECK(d.m_Fatal && d.m_Agony && d.m_AgonySeconds >= s.m_BleedMinSeconds * s.m_LegBleedFactor - 1e-6);
        CHECK(d.m_AgonySeconds <= rn::c_MaxAgonySeconds);
        s.m_LegWoundsLethal = false;
        d = rn::DecideHit(rn::BodyZone::Leg, rn::HitKind::Shot, false, s, r);
        CHECK(!d.m_Vanilla && !d.m_Fatal && !d.m_Agony && d.m_DamageFactor > 0.f && d.m_DamageFactor < 1.f);
        s.m_LegWoundsLethal = true;
        s.m_LegBleedFactor = 10.f;
        s.m_BleedMinSeconds = 100.f;
        s.m_BleedMaxSeconds = 140.f;
        d = rn::DecideHit(rn::BodyZone::Leg, rn::HitKind::Shot, false, s, r);
        CHECK(d.m_AgonySeconds <= rn::c_MaxAgonySeconds);
        s = rn::Settings {};

        // Arm: Teilschaden, Entwaffnen nach Chance, kein Todeskampf.
        s.m_DisarmChance = 100;
        d = rn::DecideHit(rn::BodyZone::Arm, rn::HitKind::Shot, false, s, r);
        CHECK(!d.m_Vanilla && d.m_TryDisarm && !d.m_Fatal && !d.m_Agony && d.m_DamageFactor > 0.f);
        s.m_DisarmChance = 0;
        d = rn::DecideHit(rn::BodyZone::Arm, rn::HitKind::Shot, false, s, r);
        CHECK(!d.m_TryDisarm);
        s.m_Disarm = false;
        s.m_DisarmChance = 100;
        d = rn::DecideHit(rn::BodyZone::Arm, rn::HitKind::Shot, false, s, r);
        CHECK(!d.m_TryDisarm);

        // Messer: das Spiel entscheidet, Todeskampf nur falls toedlich (und eingeschaltet). Faust: wie im Spiel.
        d = rn::DecideHit(rn::BodyZone::Leg, rn::HitKind::Stab, false, s, r);
        CHECK(d.m_Vanilla && !d.m_Fatal && d.m_Agony && d.m_AgonySeconds <= s.m_BleedMaxSeconds);
        s.m_StabWounds = false;
        d = rn::DecideHit(rn::BodyZone::Torso, rn::HitKind::Stab, false, s, r);
        CHECK(d.m_Vanilla && !d.m_Agony);
        s.m_StabWounds = true;
        d = rn::DecideHit(rn::BodyZone::Torso, rn::HitKind::Melee, false, s, r);
        CHECK(d.m_Vanilla && !d.m_Agony && d.m_BloodBursts == 0);

        // Liegt schon im Todeskampf: Gnadenschuss, den Treffer selbst macht das Spiel.
        d = rn::DecideHit(rn::BodyZone::Head, rn::HitKind::Shot, true, s, r);
        CHECK(d.m_Vanilla && d.m_FinishSoon && !d.m_Agony && !d.m_Fatal);

        // Abgeschaltet: alles wie im Spiel (Blut trotzdem).
        s.m_Wounds = false;
        d = rn::DecideHit(rn::BodyZone::Torso, rn::HitKind::Shot, false, s, r);
        CHECK(d.m_Vanilla && !d.m_Fatal && !d.m_Agony && d.m_BloodBursts > 0);
        s.m_Blood = false;
        d = rn::DecideHit(rn::BodyZone::Torso, rn::HitKind::Shot, false, s, r);
        CHECK(d.m_BloodBursts == 0);
    }

    void TestAgony() {
        std::printf("Todeskampf\n");
        rn::Settings s;
        CHECK(s.m_LimbTwitching); // Standard: Arme zucken mit
        s.m_LimbTwitching = false;
        const rn::AgonyConfig c = rn::MakeAgonyConfig(s);
        rn::Rng r(11);
        rn::Agony a;
        a.Begin(100.0, 30.0, r);
        CHECK(a.Active() && !a.Done() && !a.Finishing());
        CHECK(std::abs(a.DeathTime() - 130.0) < 1e-9);
        CHECK(a.Progress(100.0) == 0.f && a.Progress(101.0) == 0.f); // waehrend der Sterbe-Animation

        int s_Twitches = 0, s_Moans = 0, s_Blood = 0, s_Dies = 0;
        int s_TwitchesDuringDeathAnim = 0, s_MiddleTwitches = 0, s_LateTwitches = 0;
        double s_FirstBlood = -1.0;
        float s_EarlyMax = 0.f, s_LateMax = 0.f, s_PelvisMax = 0.f;
        std::vector<rn::AgonyAction> s_Out;

        for (double t = 100.0; t <= 140.0; t += 1.0 / 60.0) {
            s_Out.clear();
            a.Update(t, c, r, s_Out);

            for (const auto& s_Action : s_Out) {
                switch (s_Action.m_Type) {
                    case rn::AgonyActionType::Twitch: {
                        ++s_Twitches;
                        const float m = std::sqrt(s_Action.m_Impulse.x * s_Action.m_Impulse.x + s_Action.m_Impulse.y * s_Action.m_Impulse.y +
                            s_Action.m_Impulse.z * s_Action.m_Impulse.z);
                        CHECK(s_Action.m_Bone == rn::c_BonePelvis || s_Action.m_Bone == rn::c_BoneHead); // Arme hier abgeschaltet
                        CHECK(m > 0.f && m < 60.f);
                        // Becken: im Spiel erst ab etwa 20 sichtbar – Kraempfe und Schock muessen das erreichen.
                        CHECK(s_Action.m_Bone != rn::c_BonePelvis || std::abs(s_Action.m_Impulse.z) >= 0.6f * s_Action.m_Strength - 1e-4f);
                        s_PelvisMax = s_Action.m_Bone == rn::c_BonePelvis ? std::max(s_PelvisMax, m) : s_PelvisMax;
                        s_TwitchesDuringDeathAnim += t < 100.0 + rn::c_AgonySettleSeconds ? 1 : 0;

                        if (t < 104.5) {
                            s_EarlyMax = std::max(s_EarlyMax, m);
                        }
                        else if (t > 125.0 && t < 129.9) {
                            s_LateMax = std::max(s_LateMax, m);
                            ++s_LateTwitches;
                        }
                        else if (t > 108.0 && t < 122.0) {
                            ++s_MiddleTwitches;
                        }

                        break;
                    }
                    case rn::AgonyActionType::Moan: ++s_Moans; break;
                    case rn::AgonyActionType::Blood:
                        ++s_Blood;

                        if (s_FirstBlood < 0.0) {
                            s_FirstBlood = t;
                        }

                        break;
                    case rn::AgonyActionType::Die:
                        ++s_Dies;
                        CHECK(t >= 130.0 && t < 130.1);
                        break;
                }
            }
        }

        CHECK(s_Dies == 1);
        CHECK(a.Done() && !a.Active());
        CHECK(s_Twitches > 30);
        CHECK(s_TwitchesDuringDeathAnim == 0);    // die Sterbe-Animation des Spiels bleibt ungestoert
        CHECK(s_MiddleTwitches > 0 && s_MiddleTwitches < 40); // dazwischen nur vereinzelt
        CHECK(s_Moans >= 2);
        CHECK(s_Blood >= 3);
        CHECK(s_FirstBlood >= 101.0 && s_FirstBlood < 101.7);  // Blutlache, sobald der Koerper liegt
        CHECK(s_EarlyMax > 0.f && s_LateMax > 0.f);
        CHECK(s_PelvisMax >= 25.f);                                  // sichtbare Kraempfe (im Spiel gemessen)
        CHECK(s_LateTwitches / 4.9 > 2.0 * s_MiddleTwitches / 14.0); // am Ende schnelles Zittern

        // Gnadenschuss: in wenigen Sekunden vorbei, danach keine Saetze mehr.
        rn::Agony b;
        b.Begin(0.0, 60.0, r);
        b.FinishSoon(10.0, r);
        CHECK(b.Finishing());
        CHECK(b.DeathTime() <= 12.5 && b.DeathTime() >= 11.0);
        bool s_Died = false;

        for (double t = 10.0; t <= 13.0; t += 1.0 / 60.0) {
            s_Out.clear();
            b.Update(t, c, r, s_Out);

            for (const auto& s_Action : s_Out) {
                s_Died |= s_Action.m_Type == rn::AgonyActionType::Die;
                CHECK(s_Action.m_Type != rn::AgonyActionType::Moan);
            }
        }

        CHECK(s_Died && b.Done());

        // Gnadenschuss noch waehrend der Sterbe-Animation: zaehlt erst ab deren Ende.
        rn::Agony e;
        e.Begin(0.0, 60.0, r);
        e.FinishSoon(0.5, r);
        CHECK(e.DeathTime() >= rn::c_AgonySettleSeconds + 1.0 - 1e-9 && e.DeathTime() <= rn::c_AgonySettleSeconds + 2.5 + 1e-9);

        // Sehr kurze oder sehr lange Vorgaben werden begrenzt.
        rn::Agony k;
        k.Begin(0.0, 0.5, r);
        CHECK(k.DeathTime() >= rn::c_AgonySettleSeconds + 3.0 - 1e-9);
        k.Begin(0.0, 1000.0, r);
        CHECK(k.DeathTime() <= rn::c_MaxAgonySeconds + 1e-9);

        // Ohne Zucken/Saetze/Blut: nur das Ende.
        rn::AgonyConfig s_Quiet;
        s_Quiet.m_Twitching = false;
        s_Quiet.m_Lines = false;
        s_Quiet.m_Blood = false;
        rn::Agony q;
        q.Begin(0.0, 10.0, r);
        int s_Other = 0, s_QuietDies = 0;

        for (double t = 0.0; t <= 12.0; t += 0.05) {
            s_Out.clear();
            q.Update(t, s_Quiet, r, s_Out);

            for (const auto& s_Action : s_Out) {
                if (s_Action.m_Type == rn::AgonyActionType::Die) {
                    ++s_QuietDies;
                }
                else {
                    ++s_Other;
                }
            }
        }

        CHECK(s_Other == 0 && s_QuietDies == 1);

        // Arme zucken nur, wenn eingeschaltet.
        rn::AgonyConfig s_Limbs = c;
        s_Limbs.m_Limbs = true;
        rn::Agony l;
        l.Begin(0.0, 20.0, r);
        bool s_SawHand = false;

        for (double t = 0.0; t <= 21.0; t += 1.0 / 60.0) {
            s_Out.clear();
            l.Update(t, s_Limbs, r, s_Out);

            for (const auto& s_Action : s_Out) {
                s_SawHand |= s_Action.m_Type == rn::AgonyActionType::Twitch &&
                    (s_Action.m_Bone == rn::c_BoneLeftHand || s_Action.m_Bone == rn::c_BoneRightHand);
            }
        }

        CHECK(s_SawHand);
    }

    void TestSettings() {
        std::printf("Einstellungen\n");
        std::map<std::string, std::string> s_Ini;
        rn::SettingsStore s_Store;
        s_Store.m_Get = [&](const std::string& a, const std::string& b, const std::string& d) {
            const auto it = s_Ini.find(a + "." + b);
            return it == s_Ini.end() ? d : it->second;
        };
        s_Store.m_Set = [&](const std::string& a, const std::string& b, const std::string& v) { s_Ini[a + "." + b] = v; };

        rn::Settings s_Out;
        s_Out.m_WarnSeconds = 12.f;
        s_Out.m_Blood = false;
        s_Out.m_DisarmChance = 33;
        s_Out.m_NegotiateKey = 'V';
        rn::SaveSettings(s_Out, s_Store);

        rn::Settings s_In;
        rn::LoadSettings(s_In, s_Store);
        CHECK(std::abs(s_In.m_WarnSeconds - 12.f) < 1e-3f);
        CHECK(!s_In.m_Blood);
        CHECK(s_In.m_DisarmChance == 33);
        CHECK(s_In.m_NegotiateKey == 'V');

        // Unsinnige Werte werden begrenzt, Kaputtes ignoriert.
        s_Ini["standoff.warn_seconds"] = "9999";
        s_Ini["wounds.disarm_chance"] = "-5";
        s_Ini["blood.enabled"] = "vielleicht";
        s_Ini["wounds.bleed_min"] = "100";
        s_Ini["wounds.bleed_max"] = "50";
        rn::Settings s_Clamped;
        rn::LoadSettings(s_Clamped, s_Store);
        CHECK(s_Clamped.m_WarnSeconds <= 60.f);
        CHECK(s_Clamped.m_DisarmChance == 0);
        CHECK(s_Clamped.m_Blood); // "vielleicht" ist kein gueltiger Wert -> Standardwert (an) bleibt
        CHECK(s_Clamped.m_BleedMaxSeconds >= s_Clamped.m_BleedMinSeconds);

        CHECK(rn::KeyName('T') == "T");
        CHECK(rn::KeyName(0x77) == "F8");
    }

    void TestLines() {
        std::printf("Sätze\n");

        for (uint64_t s_Seed = 0; s_Seed < 50; ++s_Seed) {
            CHECK(std::string(rn::lines::FirstWarning(s_Seed)).size() > 3);
            CHECK(std::string(rn::lines::Moan(0.1f, s_Seed)).size() > 0);
            CHECK(std::string(rn::lines::Moan(0.9f, s_Seed)).size() > 0);

            for (int p = 0; p < static_cast<int>(rn::Plea::Count); ++p) {
                const auto s_Plea = static_cast<rn::Plea>(p);
                CHECK(std::string(rn::lines::PleaLabel(s_Plea)).size() > 3);
                CHECK(std::string(rn::lines::PleaSpoken(s_Plea, s_Seed)).size() > 3);

                for (int r = 0; r <= static_cast<int>(rn::PleaResult::HolsterFirst); ++r) {
                    CHECK(std::string(rn::lines::Reply(s_Plea, static_cast<rn::PleaResult>(r), s_Seed)).size() > 3);
                }
            }
        }
    }
}

int main() {
    TestHitZones();
    TestRng();
    TestStandoffBasics();
    TestStandoffCompliance();
    TestStandoffAggression();
    TestStandoffOutcomes();
    TestPleas();
    TestDecideHit();
    TestAgony();
    TestSettings();
    TestLines();

    std::printf("\n%d Prüfungen, %d Fehler\n", g_Checks, g_Failures);
    return g_Failures == 0 ? 0 : 1;
}
