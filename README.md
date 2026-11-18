# Steam Frame support for SteamVR 1.20.4 (only)

A SteamVR driver shim that lets a Steam Frame stream from SteamVR **1.20.4**, a runtime from
October 2021 that knows nothing about the headset.

Valve's Frame streaming driver (`driver_vrlink.dll`) ships only with current SteamVR and expects
a current `vrserver`. This shim is installed as the `vrlink` driver of the old runtime, loads
Valve's driver, and stands between the two: it supplies the interface versions the old server
lacks, converts the ones that changed, and does the few jobs the old server never had.

Nothing in either SteamVR is patched. The only file changed in the old runtime is its safe-mode
driver whitelist.

> Unofficial and not affiliated with Valve. The repository contains no Valve binaries: the
> install script copies the driver from your own SteamVR installation.

## Status

Tested with SteamVR 1.20.4 (`bin\version.txt` = `1634602223`) and the Frame driver from SteamVR
2.18.4 (`1792185270`), on Windows 11 with an NVIDIA GPU.

| Works | Does not work |
|---|---|
| Wireless connection, video, audio, microphone | Eye tracking (stubbed out) |
| Play area and floor level taken from the headset | Recentering on the headset (ignored by default, see `recenter`) |
| Index controllers and base stations through Space Calibrator | Any SteamVR version other than 1.20.4 |
| Half-Life: Alyx (`previous_release` branch) | |

Frame controllers connect and the old runtime picks up Valve's bindings for them, but they have
not been tried in a game.

Other old SteamVR versions will not work as they are: interface versions and the private server
interface differ between builds, and the shim is written against exactly this pair.

## Requirements

- Windows, Steam, a Steam Frame that already works with your current SteamVR.
- Visual Studio with the "Desktop development with C++" workload, and CMake 3.20 or newer.

## 1. Get SteamVR 1.20.4 from Steam's depots

The old runtime lives in its own folder next to the regular one, so Steam can keep updating its
own SteamVR:

```
<Steam>\steamapps\common\SteamVR        current SteamVR, managed by Steam
<Steam>\steamapps\common\SteamVR_1204   SteamVR 1.20.4, put there by you
```

SteamVR's public beta branches (`beta`, `previous`) do not go back that far, so the files come
from the depots.

**Depots and manifests.** SteamVR is app `250820`. SteamVR 1.20.4 is these manifests:

| Depot | Manifest | Contains | Needed |
|---|---|---|---|
| `250821` | `7050037522605506278` | Windows binaries: `bin\` and the tools | yes |
| `250824` | `63682709097972403` | Content: `resources\`, `drivers\`, `content\` | yes |
| `250827` | `6625829212583320526` | `drivers\htc` | recommended (small) |
| `250830` | `3294970847572698078` | SteamVR Home: `tools\steamvr_environments` | no |
| `250831` | `3771695043387758798` | SteamVR Home, Windows binaries | no |
| `250833` | `4169803046413314207` | SteamVR Home, Windows binaries | no |

The three SteamVR Home depots are several GB and optional. Without them the old SteamVR only
logs that it could not start SteamVR Home.

**Download with the Steam console.** Open it with `steam://open/console` (in a browser or
Win+R) and run one command per depot:

```
download_depot 250820 250821 7050037522605506278
download_depot 250820 250824 63682709097972403
download_depot 250820 250827 6625829212583320526
```

Optional, SteamVR Home:

```
download_depot 250820 250830 3294970847572698078
download_depot 250820 250831 3771695043387758798
download_depot 250820 250833 4169803046413314207
```

Steam reports where each depot was saved, normally
`<Steam>\steamapps\content\app_250820\depot_<id>`.

**Or download with DepotDownloader.** If the console answers "Manifest not available", use
[DepotDownloader](https://github.com/SteamRE/DepotDownloader). With `-dir` all depots land in
one folder and the assemble step below is not needed:

```
DepotDownloader -app 250820 -depot 250821 -manifest 7050037522605506278 -username <you> -dir SteamVR_1204
DepotDownloader -app 250820 -depot 250824 -manifest 63682709097972403 -username <you> -dir SteamVR_1204
DepotDownloader -app 250820 -depot 250827 -manifest 6625829212583320526 -username <you> -dir SteamVR_1204
```

Optional, SteamVR Home:

```
DepotDownloader -app 250820 -depot 250830 -manifest 3294970847572698078 -username <you> -dir SteamVR_1204
DepotDownloader -app 250820 -depot 250831 -manifest 3771695043387758798 -username <you> -dir SteamVR_1204
DepotDownloader -app 250820 -depot 250833 -manifest 4169803046413314207 -username <you> -dir SteamVR_1204
```

**Assemble.** Create `<Steam>\steamapps\common\SteamVR_1204` and copy the *contents* of every
`depot_*` folder into it, merging them. The result must look like this:

```
SteamVR_1204\bin\win64\vrserver.exe
SteamVR_1204\bin\version.txt
SteamVR_1204\drivers\lighthouse\...
SteamVR_1204\resources\...
```

**Verify.** `SteamVR_1204\bin\version.txt` must start with `1634602223` (the build stamp,
2021-10-19 00:10 UTC), and the file version of `bin\win64\vrserver.exe` must be 1.20.4. The
install script checks the first and warns if it differs.

Do not put the old files into Steam's own `SteamVR` folder. Steam would overwrite them on the
next update, and the current SteamVR is where the Frame driver is taken from.

## 2. Build

```
build.bat
```

This runs CMake and produces `build\bin\driver_vrlink.dll`.

## 3. Install

Close SteamVR, then:

```
powershell -ExecutionPolicy Bypass -File install.ps1
```

The script is safe to run again. It:

1. builds the shim if the DLL is missing or older than the sources;
2. creates `<Steam>\steamapps\common\vrlink_shim` with the shim, Valve's driver (copied from the
   current SteamVR and renamed to `driver_vrlink_real.dll`) and the Frame resource drivers
   `frame_hmd` and `frame_controller`;
3. registers those three folders as external OpenVR drivers. The current SteamVR ignores them
   because its built-in drivers with the same names win;
4. adds them to `resources\safe_mode_driver_whitelist.json` of the old runtime (the original is
   kept as `.orig`). Without this the old server refuses add-on drivers after any crash. Other
   add-on drivers you rely on, such as `01spacecalibrator`, have to be added to that list by hand;
5. checks the firewall and prints the command from the next step if the rule is missing.

Options: `-LegacyRuntime <folder name>` if the old runtime is not called `SteamVR_1204`,
`-UpdateDriver` to re-copy Valve's driver after a SteamVR update, `-Uninstall`.

## 4. Allow the old server through the firewall

Steam's firewall rule covers only `SteamVR\bin\win64\vrserver.exe`. The old server runs from a
different path, and without its own rule the headset's packets never reach it. Once, in a
terminal **run as administrator**:

```
netsh advfirewall firewall add rule name="SteamVR 1.20.4 (vrserver)" dir=in action=allow program="C:\Program Files (x86)\Steam\steamapps\common\SteamVR_1204\bin\win64\vrserver.exe" enable=yes
```

Adjust the path if Steam is installed elsewhere; `install.ps1` prints the exact line.

## 5. Run

1. Start `<Steam>\steamapps\common\SteamVR_1204\bin\win64\vrstartup.exe`.
2. Connect from the Frame as usual. It links up within about 20 seconds of SteamVR running.
3. Start the game from Steam.

The order matters. If the headset connects while no SteamVR is running, Steam launches its own
current SteamVR instead, which is simply the normal way to play: the shim is not involved.

SteamVR registers itself as the active OpenVR runtime when it starts, so after an old-runtime
session the old one stays registered until the current SteamVR runs again. To switch back by
hand:

```
"<Steam>\steamapps\common\SteamVR\bin\win64\vrpathreg.exe" setruntime "<Steam>\steamapps\common\SteamVR"
```

### Games

A game has to be old enough for SteamVR 1.20.4. A build that asks for newer interface versions
fails with `VRInitError_Init_InterfaceNotFound (105)` on any headset. Half-Life: Alyx needs its
`previous_release` beta branch for this reason: the current build asks for `IVRSystem_026`,
`IVRCompositor_029`, `IVRInput_011` and `IVROverlay_028`, and 1.20.4 stops at 022, 027, 010 and
025.

## Settings

`vrlink_shim\vrlink\bin\win64\vrlink_shim.ini` is created on first start and read each time
SteamVR starts.

| Key | Default | Meaning |
|---|---|---|
| `predict_ms` | `22` | How far ahead, in milliseconds, a frame's head pose is reported as predicted. Change in steps of 5 if the picture swims or lags when you turn your head |
| `recenter` | `0` | `1` lets recentering on the headset move the centre while the bounds stay in the room. Untested |
| `pose_log` | `0` | `1` writes the headset position to the log every two seconds |

The log is `vrlink_shim.log` in the same folder; past 4 MB it is rotated to
`vrlink_shim.previous.log`.

## Troubleshooting

| Symptom | Cause |
|---|---|
| Headset never connects; `vrserver.txt` has no `Found primary link` line | Firewall rule for the old server is missing (step 4) |
| Headset never connects; Steam's `remote_connections.txt` says `timed out k_ERemoteClientWaitVRConnectionReady` | The shim is not loaded. Look for `Not loading driver vrlink` in `vrserver.txt` |
| `Not loading driver vrlink because it was blocked by a previous safe mode event` | The whitelist entry is missing; run `install.ps1` again |
| A current SteamVR starts instead of the old one | The headset connected before `vrstartup.exe` of the old runtime was started |
| Game shows `VRInitError_Init_InterfaceNotFound (105)` | The game build is too new for 1.20.4 |
| Bounds are shifted or rotated | Make sure `recenter=0` |

SteamVR's own logs are in `<Steam>\logs`. To close SteamVR from a script, end `vrmonitor.exe`
and let `vrserver.exe` exit by itself; the old server treats a killed `vrserver.exe` as a crash.

## Uninstall

```
powershell -ExecutionPolicy Bypass -File install.ps1 -Uninstall
netsh advfirewall firewall delete rule name="SteamVR 1.20.4 (vrserver)"
```

Then delete `vrlink_shim` and, if you no longer want it, `SteamVR_1204`.

## How it works

```
legacy vrserver  <->  shim (driver_vrlink.dll)  <->  Valve's driver (driver_vrlink_real.dll)  <->  headset
```

| Folder | Role |
|---|---|
| `src/driver` | What the old server sees: the provider, the driver context, the devices |
| `src/adapters` | One module per thing the two sides disagree on: frame hand-off, play area, the path store, the Steam hand-off, the private server interface |
| `src/core` | Logging, settings, the generic interface proxy, ABI declarations |

Every interface the driver asks for goes through the driver context. Versions the old server has
are passed through. Missing ones get a proxy: slots that exist in an older version are forwarded
to it, slots that do not are stubs that log their arguments and return 0. The few calls that
need a real answer are implemented by hand.

[docs/NOTES.md](docs/NOTES.md) has the details: which interfaces differ and how, what the Steam
client expects, how the play area is translated, and how to debug it when a new driver or
firmware breaks something.
