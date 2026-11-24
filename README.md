# Realistic NPCs für HITMAN World of Assassination

Eine Mod für das [ZHMModSDK](https://github.com/OrfeasZ/ZHMModSDK). Sie macht die NPCs glaubwürdiger: Wachen schießen nicht sofort, sondern warnen. Mit ihnen lässt sich verhandeln. Wer angeschossen wird, bricht zusammen und liegt noch eine Weile im Todeskampf.

## Was die Mod macht

### Erst warnen, dann schießen
- Will eine Wache auf 47 schießen, wird der erste Schuss abgefangen. Die Wache ruft eine Warnung („Keine Bewegung! Hände hoch!“), und eine **Frist** läuft (Standard 8 Sekunden). Oben im Bild steht, wer auf dich zielt und wie lange du noch hast.
- **Waffe wegstecken** gibt dir mehr Zeit. Solange du keine Waffe in der Hand hältst, läuft die Frist nicht unter 6 Sekunden, aber höchstens bis 45 Sekunden insgesamt.
- **Schießt 47 selbst** oder verletzt jemanden, ist die Schonfrist vorbei: Gewarnte Wachen feuern sofort. Für die nächsten 30 Sekunden warnt niemand mehr.
- Wer im Todeskampf liegt, schießt nie.

### Verhandeln – Taste **T**
Während einer Konfrontation öffnet **T** das Verhandlungsmenü. Solange es offen ist, läuft die Zeit in Zeitlupe.

| Taste | Option | Wann sie klappt |
|---|---|---|
| **1** | Ergeben – Hände hoch | Fast immer, aber **nur ohne Waffe in der Hand** |
| **2** | Reden – Missverständnis erklären | Besser in Verkleidung, schlechter mit Waffe in der Hand, gegen Elitewachen oder viele Wachen |
| **3** | Bestechen | Bei normalen Wachen oft, bei Elitewachen selten |
| **4** | Drohen | Nur mit Waffe in der Hand. Wird besser, je mehr Bewaffnete 47 schon erledigt hat (Ruf). Klappt es, lässt die Wache ihre Waffe fallen. Klappt es nicht, wird sofort geschossen. |
| **Esc** | Nichts sagen | – |

Neben jeder Option steht eine Einschätzung („gute Chance“ … „kaum Chance“). Jeder weitere Versuch macht die Wache misstrauischer. Gelingt die Verhandlung, gilt das auch für die Wachen im Umkreis von 20 m. Sie halten sich dann 150 Sekunden zurück.

### Treffer und Sterben
- **Kopfschuss:** sofort tot, wie im Spiel.
- **Oberkörper und unklare Treffer:** Der NPC bricht mit der **Sterbe-Animation des Spiels** zusammen. Danach liegt er im **Todeskampf**:
  - Erst ein **Schock** mit heftigen Krämpfen.
  - Dann vereinzeltes **Zucken**. Er **stöhnt und fleht** (Untertitel), dazwischen kommen immer wieder Krämpfe.
  - Am Ende kommt das **Zittern der Agonie** und ein letztes Aufbäumen. Danach ist Ruhe.
  - Das Ganze dauert 20–45 Sekunden. Gezuckt wird an Becken, Kopf und Händen.
- **Beinschuss:** wie Oberkörper, nur dauert der Todeskampf länger (2,5-mal so lange, höchstens 150 Sekunden). Lässt sich abschalten, dann verletzt ein Beinschuss nur.
- **Messer:** Ob ein Stich tötet, entscheidet das Spiel. Tötet er, folgt derselbe Todeskampf.
- **Armtreffer:** schlägt mit 70 % Chance die Waffe aus der Hand.
- **Noch ein Schuss auf den Körper** ist ein Gnadenschuss: Der Todeskampf ist nach 1–2,5 Sekunden vorbei.
- Wird der Körper weggezogen oder versteckt, endet der Todeskampf.

Technisch ist der NPC schon beim Zusammenbrechen tot. Lebende NPCs lassen sich in HITMAN nicht sauber umwerfen: Die Animationen brechen weg, und der NPC steht dann in der T-Pose. Das war der Fehler in Version 1.0.0. Darum stirbt er über das Spiel selbst, und Zucken, Stöhnen und Blut kommen danach am Körper.

### Blut
- Die Mod erzeugt die **Blut-Effekte des Spiels selbst**: Spritzer an der Trefferstelle, Kopfschuss-Effekt und Spritzer am Boden.
- Unter jedem Sterbenden bildet sich eine **Blutlache**.
- Das klappt nur in Levels, in denen das Spiel diese Effekte geladen hat. Ob es klappt, zeigt der Reiter *Diagnose*.

## Bedienung

| Taste | Aktion |
|---|---|
| `^` (bzw. `~`) | SDK-Menü öffnen, dort **REALISTIC NPCS** anklicken (Einstellungen) |
| `T` | Verhandeln (nur während eine Wache auf dich zielt; Taste einstellbar) |
| `1`–`4`, `Esc` | Auswahl im Verhandlungsmenü |

Alle Werte lassen sich im Einstellungsfenster ändern. Sie werden in `HITMAN 3\Retail\mods\realisticnpcs.ini` gespeichert und lassen sich dort auch von Hand bearbeiten.

## Grenzen (ehrlich gesagt)
- **Die Wachen bleiben misstrauisch:** Die Mod hält nur ihre Schüsse zurück. Die eigentliche Spiel-KI kann sie nicht „beruhigen“. Nach einer erfolgreichen Verhandlung können sie dir also noch mit gezogener Waffe folgen, sie schießen aber nicht.
- **Trefferzone:** Wo du triffst, wird aus deiner Zielrichtung geschätzt, weil das Spiel es nicht verrät. Das passt fast immer, bei sehr schrägen Winkeln nicht ganz.
- **Untertitel statt Sprachausgabe:** Die Sätze der Mod erscheinen als Untertitel. Die Original-Stimmen des Spiels laufen weiter.
- **Online:** Mods können Online-Funktionen stören. Am sichersten spielst du offline oder mit [Peacock](https://thepeacockproject.org/). Elusive Targets und Bestenlisten besser ohne Mods.

## Was schon im Spiel getestet ist (18.11.2026, Version 1.1.0, Spielversion 3.290.1, SDK 4.2.1)
Mehrere automatische Selbsttests liefen in Paris (48 NPCs). Die Bildschirmfotos dazu habe ich mir angesehen:
- Ein NPC stirbt mit der Sterbe-Animation des Spiels und fällt um. Danach liegt er im Todeskampf: Er zuckt sichtbar, die Beine schlagen aus, die Hände bewegen sich, und unter ihm bildet sich eine Blutlache. Gegen Ende wird es ruhiger. **Keine T-Pose mehr.**
- Wer einen Treffer überlebt, bekommt keinen Todeskampf.
- Blut-Effekte, Entwaffnen und Stöße an Becken, Kopf und Händen laufen ohne Fehler. Das Spiel lässt sich normal beenden.
- Die Stoßstärken sind im Spiel gemessen. Damit die Hüfte am Boden sichtbar zuckt, braucht das Becken einen Stoß von etwa 40. Hände bewegen sich schon bei 2–8.

**Noch nicht getestet** ist alles, wofür man selbst spielen muss: echte Schüsse von 47 (Trefferzonen), Wachen, die auf 47 zielen (Warnung und Frist), das Verhandlungsmenü und die Anzeige im Bild.

### Selbsttest selbst laufen lassen
Am einfachsten mit `Tools\run-selftest.ps1` (vorher HITMAN schließen, die Mod bauen und einmal `Tools\build-grab.cmd` ausführen). Das Skript:
- spielt die DLL ein und startet das Spiel direkt in Paris,
- macht während des Todeskampfs Bildschirmfotos (in `%TEMP%\RealisticNPCs-shots`),
- räumt danach alles wieder auf.

Mit `-Calibrate` misst es stattdessen Stöße verschiedener Stärke an einem Körper.

Von Hand geht es auch:
1. In `mods\realisticnpcs.ini` unter `[debug]` den Wert `self_test = true` setzen.
2. Optional in `mods.ini` unter `[sdk]` die Zeile `auto_load_scene = assembly:/_PRO/Scenes/Missions/Paris/_scene_paris.entity` ergänzen. Dann startet das Spiel direkt in Paris.
3. Das Spiel starten. Etwa 30 Sekunden nach dem Laden des Levels stehen die Ergebnisse in `RealisticNPCs.log`. Danach schaltet sich der Test von selbst ab.
4. Die Zeile `auto_load_scene` wieder entfernen.

## Fehlersuche
- Protokoll: `HITMAN 3\Retail\mods\RealisticNPCs.log`. Dort steht jede Warnung, jede Verhandlung, jeder Treffer und jeder Tod.
- Der Reiter **Diagnose** zählt abgefangene Schüsse, Treffer, Verwundete und Blut-Effekte. Steht dort „0 blockiert“, obwohl Wachen geschossen haben, hat der Schuss-Hook nicht gegriffen.
- Löst ein Effekt einen Fehler aus, schaltet die Mod nur diesen Teil für die laufende Sitzung ab (Blut, Arm-Zucken oder Zucken) und vermerkt das im Protokoll. Sie stürzt dabei nicht ab.

## Bauen
Im SDK-Baum `D:\HitmanModding\ZHMModSDK`, Branch `realistic-npcs`. Die Mod braucht **SDK 4.2.1**, weil erst diese Version die Spielversion 3.290.1 unterstützt.

In einer Konsole mit MSVC-Umgebung (`vcvars64.bat`). Der Rust-Teil des SDK braucht **Nightly-Rust**, daher `RUSTUP_TOOLCHAIN=nightly`:

```
set RUSTUP_TOOLCHAIN=nightly
cmake --build _build/x64-Release --target RealisticNPCs RealisticNPCsTests
_build\x64-Release\Mods\RealisticNPCs\RealisticNPCsTests.exe
```

Die DLL liegt dann in `_build\x64-Release\Mods\RealisticNPCs\RealisticNPCs.dll` und kommt nach `HITMAN 3\Retail\mods\`. In `mods.ini` muss ein Abschnitt `[realisticnpcs]` stehen.

Aufbau:
- `Src/Core`: die reine Logik ohne Spiel. Dazu gehören Konfrontation und Verhandeln (`RnStandoff`), Treffer und Todeskampf (`RnWounds`), Sätze (`RnLines`) und Einstellungen. Die Tests in `Tests/CoreTests.cpp` decken diesen Teil ab.
- `Src/Plugin`: die Anbindung ans Spiel, also Hooks, Weltabbild, Todeskampf und Impulse, Blut-Effekte und Oberfläche.
- `Tools`: Testlauf im Spiel mit Bildschirmfotos (`run-selftest.ps1`, `grab.cpp`).

Wichtig beim Weiterbauen: Lebende NPCs nie per `ActivateRagdoll` umwerfen, das endet in der T-Pose. Lebende NPCs per `SetObjectToWorldMatrixFromEditor` zu versetzen, hat im Test ebenfalls nicht geklappt.

## Lizenz

GPL-3.0 – siehe [LICENSE](LICENSE). Die Mod baut auf dem [ZHMModSDK](https://github.com/OrfeasZ/ZHMModSDK) auf, das ebenfalls unter der GPL-3.0 steht.

Copyright (C) 2026 FishYorkCity67. Inoffizielle Fan-Mod, nicht mit IO Interactive verbunden.
