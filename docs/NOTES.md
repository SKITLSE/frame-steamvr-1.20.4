# Technical notes

What was learned while getting the Frame to run on SteamVR 1.20.4. Addresses belong to these
exact builds:

- `vrserver.exe` from SteamVR 2.18.4 (`version.txt` 1792185270), image base `0x140000000`;
- `driver_vrlink.dll` from the same SteamVR, image base `0x180000000`;
- the legacy server: SteamVR 1.20.4 (`version.txt` 1634602223).

Function names like `sub_...` are IDA's. In other builds the addresses differ; search for the
nearby strings instead.

## 1. Why it does not work out of the box

The 2026 driver asks the server for interface versions 1.20.4 does not have:

| Interface | Driver wants | 1.20.4 has |
|---|---|---|
| `IVRPaths` | 002 | 001 |
| `IVRDriverInput` | 005 | 003 |
| `IVRBlockQueue` | 005 | 004 |
| `IVRDisplayComponent` (implemented by the driver) | 003 | 002 |
| `IVRDriverDirectModeComponent` (implemented by the driver) | 009 | 007 |
| `IVRServerInternal_XXX` and about 25 other private interfaces | present | absent |

The first crash without the shim is at `driver_vrlink+0x9fce73`: the driver reads a boolean
through `IVRPaths` and the interface pointer is null. The driver almost never checks an
interface pointer before using it.

To list what a driver or a game asks for, search the binary for strings matching
`IVR\w+_\d{3}`.

## 2. How the shim is built

`driver_vrlink.dll` in the driver folder is the shim; the real driver sits next to it as
`driver_vrlink_real.dll`. The shim exports `HmdDriverFactory`, wraps the provider the real
factory returns (`driver/provider.cpp`) and, in `Init`, replaces the `IVRDriverContext`
(`driver/driver_context.cpp`).

### Interface proxies (`core/proxy.cpp`)

`ShimContext::GetGenericInterface` never returns null. For a version the legacy server lacks it
returns a proxy:

- slots that exist in an older version are forwarded by a small machine-code thunk (save the
  argument registers, log the call, `jmp` into the old method with `this` swapped);
- slots beyond the old version, and interfaces that do not exist at all, are stubs: they log
  the first four arguments (peeking at memory when one is a pointer) and return 0.

The old interface's method count comes from scanning its vtable up to the first entry that is
not a code pointer. For `IVRPaths_001` (4) and `IVRDriverInput_003` (7) the counts are fixed,
because there the scan ran on into the next table.

A stub that returns 0 breaks on methods returning a struct through a hidden pointer: the caller
dereferences the result at once. Those have to be implemented (slots 53 and 77 below). The
signature in a crash dump is a fault on `mov ..., [rax]` right after `call [reg+offset]`; the
offset divided by 8 is the slot.

### `IVRServerInternal_XXX` (`adapters/server_internal.cpp`)

Private interface of the current server. `CVRServer`'s primary vtable in 2.18.4 is at
`0x140354740`.

| Slot | What it does in the current server | In the shim |
|---|---|---|
| 1, 2 | by their arguments, post an event (id, device, data) | stub |
| 31, 32 | profiler scope markers, first argument is a function name; hundreds of calls per second | stub |
| 52 | `bool SetFeatureMask(uint32 which < 4, const uint64*)`, returns "value changed" | implemented |
| 53 | `uint64 GetFeatureMask(uint32 which)` through a hidden pointer; 4, 5, 6 are intersections | implemented |
| 56 | copies a 168-byte struct into shared state | stub |
| 57 | unknown; called when the dashboard is opened from the headset | stub |
| 61 | copies 80 bytes and posts event 825, once per second | stub |
| 72 | sets a flag and posts event 830 | stub |
| 75 | "client alive" timestamp | stub |
| 77 | `float[4] GetPerEyeScale(int eye)` through a hidden pointer; `{1,1,1,1}` when the compositor has published nothing | implemented |

Masks 0 and 1 are set by something other than the driver in a current SteamVR. In the shim they
stay zero and the driver treats optional features as unavailable.

### Device components and versions (`driver/device.cpp`, `driver/provider.cpp`)

The legacy server asks a device for exactly the component versions the driver listed in
`GetInterfaceVersions` and silently ignores versions it does not know. So:

- the provider's `GetInterfaceVersions` is patched: `IVRDisplayComponent_003` becomes `_002`,
  `IVRDriverDirectModeComponent_009` becomes `_007`;
- each device's `GetComponent` is hooked by swapping the device's vtable for a copy. The copy
  carries the whole table: the driver's class appends its own virtual methods after the six
  interface slots and calls them through the object. A six-slot copy crashed the server at
  `call [rax+40h]`;
- `IVRDisplayComponent_002` is answered by the `_003` object: the newer version only appended a
  method;
- `IVRDriverDirectModeComponent_007` is answered by an adapter over `_009`
  (`adapters/direct_mode.cpp`). The differences: `SubmitLayerPerEye_t` gained
  `float flHmdPosePredictionTimeInSecondsFromNow` (144 to 152 bytes) and `PostPresent()` gained
  a `const Throttling_t*` argument.

The legacy server has no value for the prediction time, so it is the `predict_ms` constant.

### Handing the connection parameters to Steam (`adapters/paths.cpp`, `adapters/steam_handoff.cpp`)

Without this the headset does not connect at all, and Steam's `remote_connections.txt` shows
`CRemoteClientJobStartStream timed out k_ERemoteClientWaitVRConnectionReady`.

- On start the driver writes a string to the path `/steam/vr_connection_ready` (root
  `0x600000000`) through `IVRPaths`.
- The current server reads it in `CVRServer::RunSteamFrame` (`sub_1400B2E20`) and gives it to
  Steam through the private interface `SteamSteamVRPrivate002`, second method. The wrapper for
  version 001 (`sub_140200C80`) falls back to opening
  `steam://vr_connection_ready/<value>` when the interface is missing.
- Steam relays the value to the headset (message `CVRConnectionReady`, field `connect_params`)
  and the headset connects to the driver over UDP port 10400.

1.20.4 has none of this. The shim keeps the path store itself (and answers `true` for
`/steam/is_running`), and on a write to `/steam/vr_connection_ready` opens the same URL with
`ShellExecute`. The value is never logged and is wiped after sending.

The headset polls Steam every 10 to 20 seconds, so it connects a little after SteamVR starts,
not immediately.

Starting `steam.exe -- <url>` directly is implemented as a fallback. It matches the registered
`steam://` handler command but was never confirmed with a headset connecting.

### Play area and floor (`adapters/chaperone.cpp`, `adapters/properties.cpp`)

- The headset sends its own `chaperone_info` in `.vrchap` format. The driver stores the JSON in
  property 2095 (`Prop_DriverProvidedChaperoneJson_String`) and then writes 2004
  (`Prop_CurrentUniverseId_Uint64`). See `UpdateChaperone`, `sub_1809F1670` in the driver.
- 1.20.4 only knows 2048 (`Prop_DriverProvidedChaperonePath_String`, the mechanism the Oculus
  driver used): a path to a file with the same JSON.
- The shim intercepts the write of 2095, saves the JSON as `frame_chaperone.vrchap` next to the
  DLL and sets 2048 before the driver writes 2004.

The JSON has two origins. `standing` moves when the user recenters on the headset;
`setup_standing2` is fixed to the room and the bounds are relative to it. The current server
(`sub_14018D5D0`) keeps both; the legacy server has one. Passed through unchanged, the bounds
end up displaced by the difference (0.5 m and 49 degrees in practice). By default the shim puts
`setup_standing2` in place of `standing`.

An origin means `standing = R(yaw) * raw + translation`. The sign of the height term was checked
against data (raw head height 0.2 to 0.3 m, `translation.y` 1.18 m). The direction of the
rotation was not, which is why `recenter=1`, which moves the bounds into the recentered frame
instead, is experimental.

When the headset is taken off it sends a fallback universe `424242` with a 1 x 1 m square.

### Safe mode

After any crash the legacy server starts in safe mode and refuses add-on drivers ("blocked by a
previous safe mode event"). The `enableSafeMode` setting does not prevent it. Only
`resources\safe_mode_driver_whitelist.json` in the legacy runtime does, so the install script
adds `vrlink`, `frame_hmd` and `frame_controller` there. Add `01spacecalibrator` by hand if you
use Space Calibrator.

A killed `vrserver.exe` also counts as a crash. To close SteamVR from a script, end
`vrmonitor.exe`; the server then exits by itself and cleanly.

## 3. The Steam side

- Steam treats any `vrserver.exe` that initialised the Steam API as app 250820 as "SteamVR is
  running", whatever folder it started from. That is why the legacy runtime can live in its own
  folder and be started before the headset connects.
- If the headset connects and no SteamVR is running, Steam launches app 250820 from its own
  folder, which is the current one.
- Steam creates its Windows Firewall rule only for
  `steamapps\common\SteamVR\bin\win64\vrserver.exe`. A server in another folder gets no packets
  from the headset without its own rule. From outside it looks like "everything is running, the
  parameters were handed over, the headset does not connect"; the server log has no
  `Found primary link` line.
- When Steam starts, and after every VR session, it loads the drivers of the registered runtime
  into its own process (`SteamWatchdog` mode). The shim is loaded into `steam.exe` at that
  moment too and keeps `driver_vrlink_real.dll` open.
- SteamVR's depots, from the local app info: `250821` Windows binaries, `250824` content
  (`resources`, `drivers`, `content`), `250827` `drivers\htc`, `250830`/`250831`/`250833`
  SteamVR Home. Each regular SteamVR update changes `250821` and `250824`. The 1.20.4 manifest
  ids are in the README.

## 4. Half-Life: Alyx

The build of 4 November 2026 asks for `IVRSystem_026`, `IVRCompositor_029`, `IVRInput_011` and
`IVROverlay_028`. 1.20.4 stops at 022, 027, 010 and 025, so the game shows
`VRInitError_Init_InterfaceNotFound (105)` on any headset. The `previous_release` branch works.

The `frame_controller` resources include Valve's Alyx bindings for Frame controllers
(`default_bindings` in the input profile), and 1.20.4 understands that key.

## 5. Debugging

- The shim log is `vrlink_shim.log` next to the DLL. `iface` lines show what the driver asked
  for and what it got, `STUB` lines are stub calls with their arguments, `comp` lines are
  component requests, `dm` lines are frames, `chap` lines are the play area.
- SteamVR's and Steam's logs are in `<Steam>\logs`. The legacy server writes the driver's
  messages into `vrserver.txt` with a `vrlink:` prefix. Explorer's size and time for a log lag
  while the file is open; read the contents.
- Crash dumps are `<Steam>\dumps\crash_vrserver.exe_*.dmp`. For the crash site:
  `cdb -z <dump> -c ".ecxr; r; ub @rip L10; kn 20; q"`. There are no symbols, only module and
  offset.
- The headset reconnects by itself while it is awake and Steam Link is open on it, so most
  checks can be done without anyone wearing it: restart the legacy SteamVR and watch the logs.
- A development loop that worked: close SteamVR (end `vrmonitor.exe`), remove any
  `blocked_by_safe_mode` keys from `<Steam>\config\steamvr.vrsettings`, rebuild, copy the DLL,
  start `vrstartup.exe`, wait for `TrackedDeviceAdded serial='F...` in the shim log.

## 6. Open items

- A rare crash of the first server instance at startup (it exits because no headset is found
  and faults inside itself while reading properties). The dump shows the shim's `ShellExecute`
  thread running at that moment; a link was not proven. It has no consequence because the shim
  is whitelisted.
- `recenter=1` and tuning of `predict_ms` were never checked with a headset.
- Frame controllers were never checked in a game. The pose components (`CreatePoseComponent`,
  slots 7 and 8 of `IVRDriverInput_005`) and eye tracking (slots 9 and 10) are stubs.
- The shim is tied to the driver from SteamVR 2.18.4. A newer driver or headset firmware may ask
  for other interfaces or other slots of the private server interface: run
  `install.ps1 -UpdateDriver`, then read the `STUB` lines and the crash site.
