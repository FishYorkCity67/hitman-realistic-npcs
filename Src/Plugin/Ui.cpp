// Oberflaeche (Render-Thread): Warn-Anzeige, Untertitel, Verhandlungsmenue, Einstellungen und Diagnose.

#include "RealisticNPCs.h"

#include "RnLines.h"

#include <IconsMaterialDesign.h>

#include <Windows.h>

#include <algorithm>
#include <filesystem>
#include <format>

namespace {
    const ImVec4 c_Red { 0.95f, 0.32f, 0.28f, 1.f };
    const ImVec4 c_Orange { 1.f, 0.66f, 0.25f, 1.f };
    const ImVec4 c_Gold { 0.98f, 0.84f, 0.45f, 1.f };
    const ImVec4 c_Blue { 0.55f, 0.8f, 1.f, 1.f };
    const ImVec4 c_Grey { 0.72f, 0.72f, 0.72f, 1.f };
    const ImVec4 c_Green { 0.45f, 0.9f, 0.5f, 1.f };

    constexpr ImGuiWindowFlags c_OverlayFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;

    ImVec4 ChanceColor(float p_Chance) {
        if (p_Chance >= 0.7f) {
            return c_Green;
        }

        if (p_Chance >= 0.45f) {
            return c_Gold;
        }

        if (p_Chance >= 0.25f) {
            return c_Orange;
        }

        return c_Red;
    }

    void Help(const char* p_Text) {
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");

        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.f);
            ImGui::TextUnformatted(p_Text);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Einstieg
// ---------------------------------------------------------------------------------------------------------------------

void RealisticNPCs::OnDrawMenu() {
    if (ImGui::Button(ICON_MD_PERSON " REALISTIC NPCS")) {
        m_ShowSettings = !m_ShowSettings;
    }
}

void RealisticNPCs::OnDrawUI(bool p_HasFocus) {
    const rn::Settings s_Settings = CurrentSettings();

    if (m_RequestCloseMenu.exchange(false)) {
        CloseMenu();
    }

    HandleInput(s_Settings, p_HasFocus);

    if (s_Settings.m_Enabled) {
        DrawHud(s_Settings);

        if (s_Settings.m_ShowSubtitles) {
            DrawSubtitles();
        }

        if (m_MenuOpen) {
            DrawNegotiationMenu(s_Settings);
        }

        if (s_Settings.m_DebugOverlay) {
            DrawDebugOverlay();
        }
    }
    else if (m_MenuOpen) {
        CloseMenu();
    }

    if (m_ShowSettings) {
        DrawSettingsWindow();
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Eingaben
// ---------------------------------------------------------------------------------------------------------------------

void RealisticNPCs::HandleInput(const rn::Settings& p_Settings, bool p_HasFocus) {
    // Gehoert das Vordergrundfenster zu HITMAN? (Prozess-ID statt Fenster-Handle: robust bei Vollbild.)
    DWORD s_ForegroundProcess = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &s_ForegroundProcess);
    const bool s_Foreground = s_ForegroundProcess == GetCurrentProcessId();

    const bool s_Down = s_Foreground && (GetAsyncKeyState(p_Settings.m_NegotiateKey) & 0x8000) != 0;
    const bool s_Pressed = s_Down && !m_KeyWasDown;
    m_KeyWasDown = s_Down;

    if (!s_Pressed || !p_Settings.m_Enabled || m_MenuOpen || p_HasFocus) {
        return;
    }

    // Strg/Alt + Taste gehoert anderen Mods (z. B. Strg+T im Cheat-Menue).
    if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) || (GetAsyncKeyState(VK_MENU) & 0x8000)) {
        return;
    }

    bool s_CanNegotiate = false;

    {
        std::scoped_lock s_Lock(m_UiMutex);
        s_CanNegotiate = m_Negotiation.m_Valid;
    }

    if (s_CanNegotiate) {
        OpenMenu();
    }
}

void RealisticNPCs::OpenMenu() {
    if (m_MenuOpen) {
        return;
    }

    m_MenuOpen = true;
    m_MenuOpenedAt = ImGui::GetTime();

    // Sonst gilt die Taste, mit der das Menue geoeffnet wurde, darin gleich als gedrueckt.
    ImGui::GetIO().ClearEventsQueue();
    ImGui::GetIO().ClearInputKeys();
    SDK()->RequestUIFocus();
}

void RealisticNPCs::CloseMenu() {
    if (!m_MenuOpen) {
        return;
    }

    m_MenuOpen = false;
    ImGui::GetIO().ClearInputKeys();
    SDK()->ReleaseUIFocus();
}

// ---------------------------------------------------------------------------------------------------------------------
// Untertitel
// ---------------------------------------------------------------------------------------------------------------------

void RealisticNPCs::Say(const std::string& p_Speaker, const std::string& p_Text, bool p_FromPlayer, float p_Seconds) {
    if (p_Text.empty()) {
        return;
    }

    const double s_Now = RealNow();
    std::scoped_lock s_Lock(m_UiMutex);

    Subtitle s_Subtitle;
    s_Subtitle.m_Speaker = p_Speaker;
    s_Subtitle.m_Text = p_Text;
    s_Subtitle.m_FromPlayer = p_FromPlayer;
    s_Subtitle.m_From = s_Now;

    // Antwort auf 47: kurz warten, als ob der andere erst nachdenkt.
    if (!p_FromPlayer && !m_Subtitles.empty() && m_Subtitles.back().m_FromPlayer && m_Subtitles.back().m_Until > s_Now) {
        s_Subtitle.m_From = std::max(s_Now, m_Subtitles.back().m_From + 1.3);
    }

    s_Subtitle.m_Until = s_Subtitle.m_From + p_Seconds;
    m_Subtitles.push_back(std::move(s_Subtitle));

    while (m_Subtitles.size() > 8) {
        m_Subtitles.pop_front();
    }
}

void RealisticNPCs::DrawSubtitles() {
    const double s_Now = RealNow();
    std::vector<Subtitle> s_Visible;

    {
        std::scoped_lock s_Lock(m_UiMutex);

        while (!m_Subtitles.empty() && m_Subtitles.front().m_Until < s_Now) {
            m_Subtitles.pop_front();
        }

        for (const Subtitle& s_Subtitle : m_Subtitles) {
            if (s_Subtitle.m_From <= s_Now && s_Subtitle.m_Until > s_Now) {
                s_Visible.push_back(s_Subtitle);
            }
        }
    }

    if (s_Visible.empty()) {
        return;
    }

    // Hoechstens die letzten drei.
    if (s_Visible.size() > 3) {
        s_Visible.erase(s_Visible.begin(), s_Visible.end() - 3);
    }

    const ImGuiIO& s_Io = ImGui::GetIO();
    ImGui::SetNextWindowPos({ s_Io.DisplaySize.x * 0.5f, s_Io.DisplaySize.y * 0.86f }, ImGuiCond_Always, { 0.5f, 1.f });
    ImGui::SetNextWindowBgAlpha(0.55f);

    if (ImGui::Begin("##RnSubtitles", nullptr, c_OverlayFlags)) {
        ImGui::PushTextWrapPos(s_Io.DisplaySize.x * 0.45f);

        for (const Subtitle& s_Subtitle : s_Visible) {
            ImGui::TextColored(s_Subtitle.m_FromPlayer ? c_Blue : c_Gold, "%s:", s_Subtitle.m_Speaker.c_str());
            ImGui::SameLine();
            ImGui::TextWrapped("%s", s_Subtitle.m_Text.c_str());
        }

        ImGui::PopTextWrapPos();
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------------------------------------------------
// Warn-Anzeige
// ---------------------------------------------------------------------------------------------------------------------

void RealisticNPCs::DrawHud(const rn::Settings& p_Settings) {
    std::vector<StandoffView> s_Views;

    {
        std::scoped_lock s_Lock(m_UiMutex);
        s_Views = m_StandoffViews;
    }

    if (s_Views.empty() || m_MenuOpen) {
        return;
    }

    const StandoffView& s_First = s_Views.front();
    const ImGuiIO& s_Io = ImGui::GetIO();
    ImGui::SetNextWindowPos({ s_Io.DisplaySize.x * 0.5f, s_Io.DisplaySize.y * 0.1f }, ImGuiCond_Always, { 0.5f, 0.f });
    ImGui::SetNextWindowBgAlpha(0.6f);

    if (ImGui::Begin("##RnStandoff", nullptr, c_OverlayFlags)) {
        ImGui::PushFont(SDK()->GetImGuiBoldFont(), 0.f);

        if (s_Views.size() > 1) {
            ImGui::TextColored(c_Red, ICON_MD_WARNING " %s und %d weitere zielen auf dich", s_First.m_Name.c_str(), static_cast<int>(s_Views.size() - 1));
        }
        else {
            ImGui::TextColored(c_Red, ICON_MD_WARNING " %s zielt auf dich", s_First.m_Name.c_str());
        }

        ImGui::PopFont();

        const float s_Total = std::max(p_Settings.m_WarnSeconds, s_First.m_SecondsLeft);
        const std::string s_Overlay = std::format("{:.0f} s", s_First.m_SecondsLeft);
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, s_First.m_SecondsLeft < 3.f ? c_Red : c_Orange);
        ImGui::ProgressBar(s_Total > 0.f ? s_First.m_SecondsLeft / s_Total : 0.f, { 340.f, 0.f }, s_Overlay.c_str());
        ImGui::PopStyleColor();

        if (p_Settings.m_ShowHints) {
            ImGui::TextColored(c_Grey, "[%s] Verhandeln   ·   Waffe wegstecken = mehr Zeit", rn::KeyName(p_Settings.m_NegotiateKey).c_str());
        }
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------------------------------------------------
// Verhandlungsmenue
// ---------------------------------------------------------------------------------------------------------------------

void RealisticNPCs::DrawNegotiationMenu(const rn::Settings& p_Settings) {
    NegotiationView s_View;

    {
        std::scoped_lock s_Lock(m_UiMutex);
        s_View = m_Negotiation;
    }

    if (!s_View.m_Valid) {
        CloseMenu();
        return;
    }

    const ImGuiIO& s_Io = ImGui::GetIO();
    ImGui::SetNextWindowPos({ s_Io.DisplaySize.x * 0.5f, s_Io.DisplaySize.y * 0.6f }, ImGuiCond_Always, { 0.5f, 0.5f });
    ImGui::SetNextWindowSize({ 560.f, 0.f }, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.93f);

    constexpr ImGuiWindowFlags c_Flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize;

    int s_Choice = -1;
    bool s_Close = false;
    const bool s_InputReady = ImGui::GetTime() - m_MenuOpenedAt > 0.15;

    if (ImGui::Begin("##RnNegotiate", nullptr, c_Flags)) {
        ImGui::PushFont(SDK()->GetImGuiBoldFont(), 0.f);
        ImGui::TextColored(c_Gold, ICON_MD_FORUM " VERHANDELN");
        ImGui::PopFont();

        ImGui::TextWrapped("%s zielt auf dich – noch %.0f s.", s_View.m_Name.c_str(), s_View.m_SecondsLeft);

        if (s_View.m_Guards > 1) {
            ImGui::TextColored(c_Orange, "%d Bewaffnete zielen auf dich.", s_View.m_Guards);
        }

        if (s_View.m_PlayerArmed) {
            ImGui::TextColored(c_Orange, ICON_MD_INFO " Du hältst eine Waffe in der Hand.");
        }

        if (s_View.m_Attempts > 0) {
            ImGui::TextColored(c_Grey, "Er wird mit jedem Versuch misstrauischer (%d bisher).", s_View.m_Attempts);
        }

        ImGui::Separator();

        for (int i = 0; i < static_cast<int>(rn::Plea::Count); ++i) {
            const auto s_Plea = static_cast<rn::Plea>(i);
            const float s_Chance = s_View.m_Chances[i];
            const bool s_NeedsHolster = s_Plea == rn::Plea::Surrender && s_View.m_PlayerArmed;
            const std::string s_Label = std::format("[{}]  {}##plea{}", i + 1, rn::lines::PleaLabel(s_Plea), i);

            if (ImGui::Button(s_Label.c_str(), { 360.f, 0.f })) {
                s_Choice = i;
            }

            ImGui::SameLine();

            if (s_NeedsHolster) {
                ImGui::TextColored(c_Orange, "erst Waffe weg");
            }
            else {
                ImGui::TextColored(ChanceColor(s_Chance), "%s", rn::ChanceHint(s_Chance));
            }

            if (s_InputReady && (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_1 + i), false) ||
                                 ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_Keypad1 + i), false))) {
                s_Choice = i;
            }
        }

        ImGui::Separator();

        if (ImGui::Button("[Esc]  Nichts sagen", { 360.f, 0.f }) || (s_InputReady && ImGui::IsKeyPressed(ImGuiKey_Escape, false))) {
            s_Close = true;
        }

        if (p_Settings.m_ShowHints) {
            ImGui::TextColored(c_Grey, "Die Zeit läuft langsamer, solange du überlegst.");
        }
    }

    ImGui::End();

    if (s_Choice >= 0) {
        m_PendingPlea = s_Choice;
        CloseMenu();
    }
    else if (s_Close) {
        CloseMenu();
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Diagnose-Anzeige
// ---------------------------------------------------------------------------------------------------------------------

void RealisticNPCs::DrawDebugOverlay() {
    std::vector<StandoffView> s_Standoffs;
    std::vector<std::string> s_Dying;

    {
        std::scoped_lock s_Lock(m_UiMutex);
        s_Standoffs = m_StandoffViews;
        s_Dying = m_DyingViews;
    }

    ImGui::SetNextWindowPos({ 20.f, 120.f }, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.5f);

    if (ImGui::Begin("##RnDebug", nullptr, c_OverlayFlags)) {
        ImGui::TextColored(c_Gold, "Realistic NPCs – Diagnose");
        ImGui::Text("Schüsse: %d blockiert, %d erlaubt, %d unbekannt", m_BlockedShots.load(), m_AllowedShots.load(), m_UnknownShooters.load());
        ImGui::Text("Treffer: %d · Todeskämpfe: %d · zu Ende: %d", m_HitsSeen.load(), m_WoundsStarted.load(), m_DeathsCaused.load());
        ImGui::Text("Blut: %d erzeugt, %d fehlgeschlagen", m_BloodSpawned.load(), m_BloodFailed.load());

        for (const StandoffView& s_View : s_Standoffs) {
            ImGui::TextColored(c_Orange, "Warnung: %s (%.0f m, %.0f s)", s_View.m_Name.c_str(), s_View.m_Distance, s_View.m_SecondsLeft);
        }

        for (const std::string& s_Line : s_Dying) {
            ImGui::TextColored(c_Red, "Todeskampf: %s", s_Line.c_str());
        }
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------------------------------------------------
// Einstellungen
// ---------------------------------------------------------------------------------------------------------------------

void RealisticNPCs::DrawSettingsWindow() {
    ImGui::SetNextWindowSize({ 620.f, 560.f }, ImGuiCond_FirstUseEver);

    if (!ImGui::Begin(ICON_MD_PERSON " Realistic NPCs", &m_ShowSettings)) {
        ImGui::End();
        return;
    }

    rn::Settings s = CurrentSettings();
    bool c = false;

    c |= ImGui::Checkbox("Mod aktiv", &s.m_Enabled);
    ImGui::Separator();

    if (ImGui::BeginTabBar("##RnTabs")) {
        if (ImGui::BeginTabItem("Konfrontation")) {
            c |= ImGui::Checkbox("Wachen warnen, bevor sie schießen", &s.m_WarnBeforeFiring);
            Help("Statt sofort zu schießen, warnt eine Wache und gibt dir eine Frist. In der Zeit kannst du verhandeln.");
            c |= ImGui::SliderFloat("Frist nach der Warnung (s)", &s.m_WarnSeconds, 3.f, 30.f, "%.0f");
            c |= ImGui::SliderFloat("Mindestzeit ohne Waffe in der Hand (s)", &s.m_ComplySeconds, 0.f, 20.f, "%.0f");
            Help("Steckst du die Waffe weg, läuft die Frist nicht unter diesen Wert – bis zur Höchstdauer.");
            c |= ImGui::SliderFloat("Höchstdauer einer Konfrontation (s)", &s.m_MaxStandoffSeconds, 10.f, 120.f, "%.0f");
            c |= ImGui::SliderFloat("Wachen bleiben ruhig für (s)", &s.m_StandDownSeconds, 30.f, 600.f, "%.0f");
            c |= ImGui::SliderFloat("Nach eigenem Angriff keine Warnung für (s)", &s.m_AggressionMemorySeconds, 0.f, 120.f, "%.0f");
            c |= ImGui::SliderFloat("Ergebnis gilt für Wachen im Umkreis (m)", &s.m_GroupRadius, 0.f, 50.f, "%.0f");
            c |= ImGui::SliderFloat("Zeitlupe im Verhandlungsmenü", &s.m_MenuTimeScale, 0.05f, 1.f, "%.2f");

            int s_KeyIndex = std::clamp(s.m_NegotiateKey - 'A', 0, 25);
            const std::string s_Preview = rn::KeyName(s.m_NegotiateKey);

            if (ImGui::BeginCombo("Taste zum Verhandeln", s_Preview.c_str())) {
                for (int i = 0; i < 26; ++i) {
                    const char s_Letter[2] = { static_cast<char>('A' + i), 0 };

                    if (ImGui::Selectable(s_Letter, i == s_KeyIndex)) {
                        s.m_NegotiateKey = 'A' + i;
                        c = true;
                    }
                }

                ImGui::EndCombo();
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Verwundungen")) {
            c |= ImGui::Checkbox("Todeskampf nach Treffern", &s.m_Wounds);
            Help("Rumpf- und Beintreffer: Der NPC bricht mit der Sterbe-Animation des Spiels zusammen und liegt danach noch eine Weile "
                 "im Todeskampf (Zucken, Krämpfe, Zittern, Stöhnen, Blutlache). Kopfschüsse bleiben sofort tödlich.");
            c |= ImGui::SliderFloat("Todeskampf mindestens (s)", &s.m_BleedMinSeconds, 5.f, 150.f, "%.0f");
            c |= ImGui::SliderFloat("Todeskampf höchstens (s)", &s.m_BleedMaxSeconds, 5.f, 150.f, "%.0f");
            c |= ImGui::Checkbox("Beinschuss: bricht zusammen", &s.m_LegWoundsLethal);
            Help("Aus: Ein Beinschuss verletzt nur (weniger Schaden).");
            c |= ImGui::SliderFloat("Beinschuss: Todeskampf so viel länger", &s.m_LegBleedFactor, 1.f, 5.f, "%.1fx");
            c |= ImGui::Checkbox("Todeskampf auch nach tödlichen Messerstichen", &s.m_StabWounds);
            c |= ImGui::Checkbox("Armtreffer schlägt die Waffe aus der Hand", &s.m_Disarm);
            c |= ImGui::SliderInt("Chance dafür (%)", &s.m_DisarmChance, 0, 100);
            ImGui::Separator();
            c |= ImGui::Checkbox("Zucken und Krämpfe", &s.m_Twitching);
            c |= ImGui::SliderFloat("Stärke des Zuckens", &s.m_TwitchStrength, 0.f, 3.f, "%.1f");
            c |= ImGui::Checkbox("Auch die Arme zucken", &s.m_LimbTwitching);
            Help("Stöße auf die Hände. Schaltet sich bei einem Fehler von selbst ab.");
            c |= ImGui::Checkbox("Sterbende stöhnen und flehen (Untertitel)", &s.m_WoundedLines);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Blut")) {
            c |= ImGui::Checkbox("Zusätzliches Blut", &s.m_Blood);
            Help("Blutspritzer an der Trefferstelle und eine Blutlache unter Sterbenden – mit den Effekten des Spiels.");
            c |= ImGui::SliderInt("Menge (1 = wenig, 3 = viel)", &s.m_BloodAmount, 1, 3);
            ImGui::TextColored(c_Grey, "Funktioniert nur in Levels, in denen das Spiel die Effekte geladen hat.");
            ImGui::TextColored(c_Grey, "Gefundene Effekt-Vorlagen: %d", m_BloodTemplatesFound.load());
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Anzeige")) {
            c |= ImGui::Checkbox("Untertitel", &s.m_ShowSubtitles);
            c |= ImGui::Checkbox("Tipps anzeigen", &s.m_ShowHints);
            c |= ImGui::Checkbox("Diagnose-Anzeige", &s.m_DebugOverlay);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Diagnose")) {
            ImGui::Text("Abgefangene Schüsse: %d (erlaubt: %d, unbekannter Schütze: %d)", m_BlockedShots.load(), m_AllowedShots.load(), m_UnknownShooters.load());
            ImGui::Text("Konfrontationen: %d", m_TotalStandoffs.load());
            ImGui::Text("Treffer durch 47: %d", m_HitsSeen.load());
            ImGui::Text("Todeskämpfe: %d begonnen · %d zu Ende", m_WoundsStarted.load(), m_DeathsCaused.load());
            ImGui::Text("Getötete Bewaffnete (Ruf): %d", m_ArmedKills.load());
            ImGui::Text("Blut-Effekte: %d erzeugt, %d fehlgeschlagen", m_BloodSpawned.load(), m_BloodFailed.load());

            if (m_BloodBroken) {
                ImGui::TextColored(c_Red, "Blut wurde nach einem Fehler abgeschaltet (Neustart des Spiels setzt das zurück).");
            }

            if (m_ImpulsesBroken) {
                ImGui::TextColored(c_Red, "Zucken wurde nach einem Fehler abgeschaltet.");
            }
            else {
                if (m_HeadBoneBroken) {
                    ImGui::TextColored(c_Orange, "Kopf-Zucken wurde nach einem Fehler abgeschaltet (der Körper zuckt weiter).");
                }

                if (m_LimbBonesBroken) {
                    ImGui::TextColored(c_Orange, "Arm-Zucken wurde nach einem Fehler abgeschaltet.");
                }
            }

            ImGui::Separator();
            ImGui::TextColored(c_Grey, "Protokoll: HITMAN 3\\Retail\\mods\\RealisticNPCs.log");
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();

    if (c) {
        if (s.m_BleedMaxSeconds < s.m_BleedMinSeconds) {
            s.m_BleedMaxSeconds = s.m_BleedMinSeconds;
        }

        if (s.m_MaxStandoffSeconds < s.m_WarnSeconds) {
            s.m_MaxStandoffSeconds = s.m_WarnSeconds;
        }

        CommitSettings(s);
    }
}
