<#
Installs (or refreshes) Steam Frame support in a legacy SteamVR 1.20.4. Safe to run again: it
re-copies the built shim DLL and re-checks everything else.

  .\install.ps1                 build if needed, install, register
  .\install.ps1 -UpdateDriver   also re-copy Valve's driver and the Frame resources from the current SteamVR
  .\install.ps1 -Uninstall      unregister the drivers and restore the whitelist (folders are left alone)

Layout under <Steam>\steamapps\common:
  SteamVR        the current SteamVR that Steam manages; source of driver_vrlink.dll and the Frame resources
  SteamVR_1204   the legacy runtime (see README.md for how to get it); change with -LegacyRuntime
  vrlink_shim    created here: the shim, Valve's driver renamed to driver_vrlink_real.dll, Frame resources
#>
param(
    [string]$SteamDir,
    [string]$LegacyRuntime = 'SteamVR_1204',
    [switch]$UpdateDriver,
    [switch]$Uninstall
)
$ErrorActionPreference = 'Stop'
$repo = $PSScriptRoot

if (-not $SteamDir) {
    $SteamDir = (Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue).SteamPath
    if (-not $SteamDir) { throw 'Steam not found in the registry; pass -SteamDir.' }
}
$SteamDir = $SteamDir -replace '/', '\'
$common = Join-Path $SteamDir 'steamapps\common'
$new = Join-Path $common 'SteamVR'
$old = Join-Path $common $LegacyRuntime
$shimRoot = Join-Path $common 'vrlink_shim'
$vrpathreg = Join-Path $new 'bin\win64\vrpathreg.exe'
$whitelist = Join-Path $old 'resources\safe_mode_driver_whitelist.json'
$drivers = 'vrlink', 'frame_hmd', 'frame_controller'
$whitelistNames = 'vrlink', 'frame_hmd', 'frame_controller'
$ruleName = 'SteamVR 1.20.4 (vrserver)'
$legacyVersion = '1634602223'   # bin\version.txt of SteamVR 1.20.4

if (Get-Process vrserver, vrmonitor -ErrorAction SilentlyContinue) { throw 'SteamVR is running. Close it first.' }
if (-not (Test-Path $vrpathreg)) { throw "Current SteamVR not found at $new" }

if ($Uninstall) {
    foreach ($d in $drivers) { & $vrpathreg removedriver (Join-Path $shimRoot $d) | Out-Null }
    & $vrpathreg setruntime $new | Out-Null
    if (Test-Path "$whitelist.orig") { Copy-Item "$whitelist.orig" $whitelist -Force; 'whitelist restored' }
    'drivers unregistered, active runtime set to the current SteamVR'
    "left in place: $shimRoot, $old, and the firewall rule `"$ruleName`" (remove it from an admin terminal)"
    return
}

if (-not (Test-Path (Join-Path $old 'bin\win64\vrserver.exe'))) { throw "Legacy runtime not found at $old (see README.md)" }
$version = Get-Content (Join-Path $old 'bin\version.txt') -TotalCount 1
"legacy runtime : $old (version.txt $version)"
"current SteamVR: $new (version.txt $(Get-Content (Join-Path $new 'bin\version.txt') -TotalCount 1))"
if ($version -ne $legacyVersion) { Write-Warning "The legacy runtime is not 1.20.4 (expected version.txt $legacyVersion). Only 1.20.4 is supported." }

# 1. the shim itself
$built = Join-Path $repo 'build\bin\driver_vrlink.dll'
$newestSource = Get-ChildItem (Join-Path $repo 'src') -Recurse -File | Sort-Object LastWriteTime | Select-Object -Last 1
if (-not (Test-Path $built) -or (Get-Item $built).LastWriteTime -lt $newestSource.LastWriteTime) {
    'building the shim...'
    cmd /c "`"$(Join-Path $repo 'build.bat')`"" | Select-String ' error |BUILD OK' | ForEach-Object { $_.Line }
    if ($LASTEXITCODE -ne 0) { throw 'build failed' }
}

# 2. Valve's driver and the Frame resources, copied from the current SteamVR
$binDir = Join-Path $shimRoot 'vrlink\bin\win64'
$real = Join-Path $binDir 'driver_vrlink_real.dll'
if ($UpdateDriver -or -not (Test-Path $real)) {
    foreach ($d in $drivers) {
        $from = Join-Path $new "drivers\$d"
        if (-not (Test-Path $from)) { throw "The current SteamVR has no drivers\$d (too old for the Frame?)" }
        # /XF keeps Valve's DLL from landing on top of the shim, which uses the same file name
        robocopy $from (Join-Path $shimRoot $d) /E /XF driver_vrlink.dll /NFL /NDL /NJH /NJS /NP | Out-Null
    }
    New-Item -ItemType Directory -Force $binDir | Out-Null
    Copy-Item (Join-Path $new 'drivers\vrlink\bin\win64\driver_vrlink.dll') $real -Force
    "copied Valve's driver ($((Get-Item $real).LastWriteTime.ToString('yyyy-MM-dd'))) and the Frame resources"
    'note: the shim was written against the driver from SteamVR 2.18.4; a newer driver may need new adapters'
}
else { "Valve's driver kept as is ($((Get-Item $real).LastWriteTime.ToString('yyyy-MM-dd'))); -UpdateDriver replaces it" }
Copy-Item $built (Join-Path $binDir 'driver_vrlink.dll') -Force
'shim DLL installed'

# 3. registration as external drivers (the current SteamVR skips them: its built-in ones win)
$registered = (Get-Content (Join-Path $env:LOCALAPPDATA 'openvr\openvrpaths.vrpath') -Raw | ConvertFrom-Json).external_drivers
foreach ($d in $drivers) {
    $p = Join-Path $shimRoot $d
    if ($registered -notcontains $p) { & $vrpathreg adddriver $p | Out-Null; "registered $d" }
}

# 4. after any crash the legacy server loads only whitelisted add-on drivers
if (-not (Test-Path "$whitelist.orig")) { Copy-Item $whitelist "$whitelist.orig" }
$wl = Get-Content $whitelist -Raw | ConvertFrom-Json
$missing = $whitelistNames | Where-Object { $wl.drivers -notcontains $_ }
if ($missing) {
    $wl.drivers = @($wl.drivers) + @($missing)
    [IO.File]::WriteAllText($whitelist, ($wl | ConvertTo-Json -Compress), (New-Object Text.UTF8Encoding($false)))
    "whitelisted in the legacy runtime: $($missing -join ', ')"
}

# 5. firewall: Steam's rule only covers <Steam>\steamapps\common\SteamVR\bin\win64\vrserver.exe
$oldServer = Join-Path $old 'bin\win64\vrserver.exe'
$hasRule = @(Get-NetFirewallApplicationFilter -All -ErrorAction SilentlyContinue | Where-Object { $_.Program -eq $oldServer }).Count -gt 0
if (-not $hasRule) {
    ''
    'FIREWALL: the headset cannot reach the legacy server until you run this once in an ADMIN terminal:'
    "  netsh advfirewall firewall add rule name=`"$ruleName`" dir=in action=allow program=`"$oldServer`" enable=yes"
}
else { 'firewall rule for the legacy server: present' }
''
"done. Start $(Join-Path $old 'bin\win64\vrstartup.exe'), then connect from the Frame."
