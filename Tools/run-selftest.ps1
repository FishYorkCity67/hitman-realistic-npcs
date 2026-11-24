# Testlauf im Spiel ohne Mitspielen: spielt die frisch gebaute RealisticNPCs.dll ein, startet HITMAN direkt in Paris
# mit Selbsttest, macht waehrend des Todeskampfs Bildschirmfotos (grab.exe) und raeumt danach wieder auf
# (auto_load_scene raus, self_test aus, Spiel zu).
#
#   .\run-selftest.ps1                 Selbsttest (Todeskampf, Blut, Entwaffnen)
#   .\run-selftest.ps1 -Calibrate      Stoss-Messung: Stoesse verschiedener Staerke an einem Koerper (RN_CALIBRATE=1)
#
# Vorher: HITMAN schliessen, Mod bauen, grab.exe mit build-grab.cmd bauen.
# Die Bilder landen in %TEMP%\RealisticNPCs-shots (times.txt: Millisekunden seit Beginn je Bild).
param(
    [switch]$Calibrate,
    [int]$Count = 72,
    [int]$Interval = 250,
    [string]$Game = 'D:\SteamLibrary\steamapps\common\HITMAN 3'
)
$ErrorActionPreference = 'Stop'

$Retail  = Join-Path $Game 'Retail'
$Mods    = Join-Path $Retail 'mods'
$Log     = Join-Path $Mods 'RealisticNPCs.log'
$Ini     = Join-Path $Mods 'realisticnpcs.ini'
$ModsIni = Join-Path $Retail 'mods.ini'
$NewDll  = Join-Path $PSScriptRoot '..\..\..\_build\x64-Release\Mods\RealisticNPCs\RealisticNPCs.dll'
$Grab    = Join-Path $PSScriptRoot 'grab.exe'
$Shots   = Join-Path $env:TEMP 'RealisticNPCs-shots'
$Scene   = 'auto_load_scene = assembly:/_PRO/Scenes/Missions/Paris/_scene_paris.entity'
$Trigger = if ($Calibrate) { 'Kalibrierung: Start' } else { 'Selbsttest Todeskampf:' }
$Done    = if ($Calibrate) { 'Kalibrierung fertig|abgebrochen' } else { 'Selbsttest fertig' }

if (Get-Process HITMAN3 -ErrorAction SilentlyContinue) { throw 'HITMAN laeuft noch – bitte zuerst schliessen.' }
if (-not (Test-Path $Grab)) { throw 'grab.exe fehlt – zuerst build-grab.cmd ausfuehren.' }

Copy-Item $NewDll (Join-Path $Mods 'RealisticNPCs.dll') -Force
'DLL eingespielt.'

$iniText = [IO.File]::ReadAllText($Ini)
[IO.File]::WriteAllText($Ini, ($iniText -replace 'self_test = false', 'self_test = true'))
$modsText = [IO.File]::ReadAllText($ModsIni)
if ($modsText -notmatch 'auto_load_scene') {
    [IO.File]::WriteAllText($ModsIni, ($modsText -replace '\[sdk\]\r?\n', "[sdk]`r`n$Scene`r`n"))
}

$logStart = (Get-Item $Log).Length
if (Test-Path $Shots) { Remove-Item -Recurse -Force $Shots }
New-Item -ItemType Directory -Force $Shots | Out-Null

function Get-NewLog {
    $fs = [IO.File]::Open($Log, 'Open', 'Read', 'ReadWrite')
    try {
        $fs.Seek($logStart, 'Begin') | Out-Null
        return (New-Object IO.StreamReader($fs, [Text.Encoding]::UTF8)).ReadToEnd()
    } finally { $fs.Dispose() }
}

try {
    $env:SteamAppId = '1659040'
    $env:RN_CALIBRATE = if ($Calibrate) { '1' } else { '0' }
    $proc = Start-Process -FilePath (Join-Path $Retail 'HITMAN3.exe') -WorkingDirectory $Game -PassThru
    "Spiel gestartet (PID $($proc.Id))."

    $deadline = (Get-Date).AddMinutes(6)
    $grabProc = $null

    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
        if ($proc.HasExited) { 'Das Spiel hat sich selbst beendet!'; break }
        $text = Get-NewLog
        if (-not $grabProc -and $text -match $Trigger) {
            $grabProc = Start-Process -FilePath $Grab -ArgumentList "`"$Shots`" $Count $Interval full" -PassThru -NoNewWindow
        }
        if ($text -match $Done) { break }
    }

    if ($grabProc) { $grabProc.WaitForExit(60000) | Out-Null }
    Start-Sleep -Seconds 2
}
finally {
    $p = Get-Process HITMAN3 -ErrorAction SilentlyContinue
    if ($p) {
        $p.CloseMainWindow() | Out-Null
        if (-not $p.WaitForExit(20000)) { Stop-Process -Id $p.Id -Force; 'Spiel hart beendet.' } else { 'Spiel normal beendet.' }
    }

    $modsText = [IO.File]::ReadAllText($ModsIni)
    [IO.File]::WriteAllText($ModsIni, ($modsText -replace "(?m)^auto_load_scene = .*\r?\n", ''))
    $iniText = [IO.File]::ReadAllText($Ini)
    if ($iniText -match 'self_test = true') {
        [IO.File]::WriteAllText($Ini, ($iniText -replace 'self_test = true', 'self_test = false'))
    }
}

'--- Neue Zeilen im Protokoll ---'
Get-NewLog
"--- $((Get-ChildItem $Shots -Filter *.png | Measure-Object).Count) Bilder in $Shots ---"
