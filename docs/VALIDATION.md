# Validation and current limits

## Accepted physical-device behavior

- Current M5 iPad update: the player confirmed the top-left FPS counter hides
  on tap and reappears from the same corner. Before launch, the installed build
  and all protected save, cloud, preference, environment and pipeline files
  matched the fresh backup. Device-motion samples reached guest six-axis reads
  without sensor errors, and the current runtime acknowledged iCloud sync.
  Those runtime observations do not certify physical aim/motion in every
  orientation or a new cross-device save round trip.
- M5 iPad Pro 13-inch: 4:3 world/HUD proportions, gameplay, sound and controls.
- iPhone 17 Pro Max: 239:110 gameplay/HUD and sound/controls; generated
  presentation observed around 120 displayed FPS in a gameplay sample.
- DualSense: live motion consumed by the game; authored left/right rumble;
  backflips accepted by the player; five successive held-trigger long jumps
  all produced the intended recoil, including rolling follow-ups.
- Wall-jump and side-flip updates were installed along with frame generation;
  the player reported the update working well. This is not certification of
  every haptic event, level, cutscene, controller or long session.
- The original iPad save was preserved byte-for-byte across the cloud update.
  Its complete committed digest was imported and verified on the iPhone before
  guest boot. The phone also uploaded later game saves.

## Known limits

- Shared top-left FPS counter: real game-free Catalyst UIKit fixture passes81
  checks for hidden/reveal hit testing, both landscape safe areas, compact L3
  clearance, normal ZL/L3 centers, saved visibility, accessibility, FG labeling
  and stale/reset sampling. Explicit corner taps take priority over expanded
  shoulder hit rims; the target never expands beyond80×22pt. Physical-device
  hide/reveal was accepted on the M5 iPad; delivery to the phone remains pending.
  Gamepad/settings/gyro/controller/haptic policies
  are unchanged from19.

- Optional device-local Core Haptics is a separate sink for the original native
  bands and verified takeoff composition. It never changes controller gains,
  textures or trigger curves. Native-band On gain stays 0.20 with a 0.18 intensity cap.
  Build18's stronger continuous action cues failed physical feel as too strong
  and buzzing. Build19 emits a single lower-strength transient (0.10–0.20,
  sharpness0.90–1) per confirmed in-game event, with no sustained cue envelope.
  Same-tick cues select the strongest event; duplicates, stale events and bursts
  within50ms are consumed without delayed replay. Native intensity0.055 or more
  suppresses extra taps, and stronger native output immediately preempts a tap.
  Quiet native texture pauses for at most30ms around a tap, so two players do
  not sum on the actuator. Controller gains and native capture mapping are unchanged.
  The local sink requires fresh game frames (150 ms), a native value newer than
  activation, foreground/permit state and a supported actuator. Effects have a
  finite 100 ms hardware lease for native continuous output; transients end
  themselves and release their player within30ms. Off, background, controller selection, stale
  heartbeat, native stops and engine failures release output; old engine callbacks
  cannot restart a new engine. Settings/reset recovery require fresh input.
  The coordinator's physical hardware probe reports iPad unsupported and iPhone
  supported. The player reports native Bullet Bill capture felt, while normal
  jumps/landings, long jumps/backflips, cap throws, coins and ground pounds were
  missing/too faint. The earlier global no-feel report is superseded by that
  narrower observation. Phone-specific event transients avoid multiplying
  already-small controller cues by another .20/.30; native bands/strong capture
  gain are unchanged. No touch/controller button edge creates an event.
  Seven extra first-step callbacks are guarded independently
  by exact main build, RTTI, singleton/vtable ownership and expected code bytes.
  Their separate bounded queue cannot evict or enter controller cues. Standard
  3D/2D coins and HackCap ThrowStart are covered; special variants remain untested.
  Build17 full controls and the three-switch sheet were physically accepted.
  Build18 changes activation to tap; its haptic texture/strength failed.
  Build19 actual sink/tick tests pass30 checks, including input-only silence,
  one-shot playback, native preemption and lifecycle cleanup; policy2600 checks
  and sanitizers pass. New transient feel and tap delivery still require physical follow-up. No
  audio or generic vibration fallback is used.

- The gear consumes a fixed 44-point hit region before gameplay routing. Opening
  activates on normal tap release with at most 12 points of motion; a resting
  gameplay thumb is allowed and presentation neutralizes all owned input.
  Cancellation, second gear fingers, rotation, disconnect/background and stale generations
  cannot open it later. Expanded +/- hit regions keep at least a 5-point gap.
  The UIKit adapter inspects real coalesced touch samples as well as the latest
  endpoint, so a rapid out-and-back excursion over 12 points stays cancelled.
  The portable canonical contract tests geometry/gesture boundaries; preview
  renders do not substitute for actual UIKit/device touch testing.

- The Gyro On/Off switch uses the existing Auto/Off source router and the
  existing guest six-axis ABI. Device transforms, source arbitration, original
  sample-age rejection, history reset and touch-only operation have local tests.
  Real iPad/iPhone aiming, shake actions, orientation and controller regression
  still require physical testing. No device update is implied by a build.

- Three thunder-effect geometry shaders remain unsupported in the original
  tested shader set. Full-game visual correctness isn't certified.
- Internal rendering is 1600×900. Camera/display aspect is separate from raster
  resolution. Presets are chosen at build time, not detected on the device.
- iPad model presets and `iphone-duo` select the same live 4:3 build as `ipad`
  and `duo`. iPhone model presets other than 239:110 reuse the guarded phone
  camera and HUD rules at that model's exact aspect, without the live path. Those
  aspects pass synthetic unit checks only; their gameplay, HUD placement and
  transitions haven't been checked on a device.
- Every preparation compensates the Moe-Eye glasses filter (`FilterGlasses`)
  on the axis its root narrows: phones widen it to the full panel width (it
  previously stopped short of both sides), 4:3 sets its 1000-unit surfaces to
  1000/.75 so it keeps the authored framing, and the live adapter holds that
  framing at any supported aspect. The 4:3 and live forms are untested on a device.
  Phone preparations also widen the shared footer bars (`FooterParts`) to both
  edges. Every preparation now corrects the map footer (`MapFooter`), which was
  left at 16:9 and stretched its button guides; 4:3 pins it to the bottom edge
  and the live adapter treats it as a bottom-edge group. 4:3 builds now change
  210 archives and phone builds 211. These were measured against the original
  layouts. The player reported the 437:201 phone build with these fixes working
  on an iPhone 18 Pro; 4:3 and live iPad/Duo builds of them are untested on a device.
- The regional coin counter (`CounterCollectCoin`) is its own layout beside
  `CounterCoin`. It now follows the same top-left edge anchor in every
  preparation and in the live adapter; before, the gap between the two counters
  widened on phones (and grew vertically on 4:3). Measured against the original
  layouts. The player reported it working in an iPhone 18 Pro (437:201) build; 4:3 and live builds are untested on a device.
- Balloon World is unsupported (it needs Nintendo's online service). Every
  preparation routes Luigi's standby event flow (`EventData/BalloonGame.szs`,
  `NpcStandby.byml`) so each talk shows one A-advanced message and ends, and no
  flow entry can reach `StartConnect`. The message replaces the otherwise
  2P-only `TimeBalloon_Separate` text in all 14 languages' `SystemMessage.szs`,
  in English, keeping Luigi's voice tag. Synthetic tests and a read-back of the
  prepared files cover it. The player reported it working in an iPhone 18 Pro (437:201) build; 4:3 and live builds are untested on a device.
- Menu cursors (the cap beside the selected item) are written by the game as
  the item's on-screen position into a `ParCursor` pane under an identity
  `All` pane. The Calculate adapter converts that into the corrected root's
  units in every preparation and live state, so the cap sits beside the item
  instead of over it. Synthetic adapter tests cover phone, static 4:3 and live
  roots. The player reported it working in an iPhone 18 Pro (437:201) build; 4:3 and live builds are untested on a device.
- Phone preparations move `BootLoading`'s logo with `Menu`'s. The two layouts
  overlap on the title screen, so only one moved logo showed a second copy.
  The player reported it working in an iPhone 18 Pro (437:201) build.
- The title and pause menus drop Playing in VR and Action Guide. Ten guarded
  main cuts shorten both item tables to four and remove the 2P checks that
  addressed the VR slot; the `Menu` layout hides those two items and closes up
  the rest (hash-pinned source files). The pinned rewritten `main` hash changed
  accordingly, so an older `--rewritten` folder is rejected; omit the option to
  regenerate. Synthetic and cut-table tests cover it. The player reported it working in an iPhone 18 Pro (437:201) build; 4:3 and live
  builds are untested on a device.
- Touch camera: the right half is a swipe-to-look surface. Odyssey reads its
  camera only as a stick (0.3 dead zone, then a yaw buffer and a pitch chase that
  each release about 10% per frame), which made both stick-emulating versions feel
  delayed; a velocity version and then a turn-budget version were rejected on
  device. Swipes now turn the follow camera (`CameraPoserFollowLimit`) directly
  through three exact-build observation gates (`hle_odyssey_camera.c`): the
  poser's yaw step gains the queued turn on entry to its apply function and gets
  the game's own step back after it, so area limits still clamp it and the turn
  is applied once; the owned `CameraAngleVerticalCtrl` user nerve moves target and
  current pitch together inside the nerve's own limits. Each camera update takes
  65% of the queued turn (remainders under 0.01° whole). 0.3° per point at layout
  unit 1, times the stored **Swipe look sensitivity** (0.25× to 3×). When no
  follow camera has updated for 100 ms the swipe falls back to the turn budget the
  right stick drains, and queued turns are dropped. The pinned rewritten `main`
  hash changed for the new gates; omit `--rewritten` to regenerate. The adapter
  (`t_odyssey_camera.c`, synthetic objects) and the portable math
  (`t_touch_controls.c`) have their own checks, and the iOS sources are
  compile-checked against the SDK; feel, direction, sensitivity range and frame
  pacing are not yet seen on a device. The game's camera-speed and any invert
  options do not apply to the direct turn.
- Tappable UI (`hle_odyssey_ui.c`): the HUD adapter's Calculate hook hands
  every primary layout to the observer after Calculate. `Menu`, `Option*`,
  `WindowConfirm*`, `TalkMessage` (its `Select` choices and the dialogue
  itself), `WorldSelect` (the `WorldNN`/`ParWorldNN` strip) and `Map` publish
  their visible rows as tap targets from pane global matrices (+0x70, 1280×720
  layout units on the picture), each with its layout root. Thirteen execute
  slots of the screens' input nerves (MenuSelectParts Select/SelectSecond,
  CommonSelectParts Select/SelectNoAppearAction, CommonVerticalList Active,
  WindowConfirm Wait, TalkMessage IconWait/TextAnim, StageSceneStateWorldMap
  Wait, StageSceneStateStageMap Wait/WaitAdsorb/IconSelectMove/Scroll) run a
  host step first; a tap is taken only by the object whose layout root (found
  through the game's own `getLayoutKeeper` path) owns the target. The step
  puts that object's cursor on the row with the game's functions and fields
  and then grants exactly what the screen asks next, through four new result
  gates (patch_nso.py `RETS`: the callback may answer for the function):
  `rs::isTriggerUiDecide` once, `KeyRepeatCtrl::isUp/isDown` once for the
  kingdom select's controller (so its own step turns the globe), and the UI
  left stick (s0/s1) while a map nerve runs, so map drags scroll, snap and
  select through the stick path. Map icon taps set the adsorb target and the
  Adsorb nerve (as the cursor's snap does); a tap on the selected icon grants
  decide. Config rows step their value with the list's own left/right. No
  controller input is simulated. Exact-build guards: nerve typeinfo names,
  slots and thunks, nerve objects, strings, all four gates, and FNV-1a hashes
  of the first 64 bytes of 29 functions whose fields or calls are mirrored
  (pinned from the rewritten `main`). The pinned rewritten `main` hash changed
  for the gates; omit `--rewritten` to regenerate. Screens: tappable screens
  hide the iOS controls (one-finger tap/drag pointer with a 10 pt slop);
  `ModeSelect`, `Shop*` and `MiniGameMenu*` keep them; the coin/life counters
  mean gameplay. Controller guides (`ParFooter` under `All` or the root) drop
  90 layout units and dialogue/window `ParHardKey` icons shrink to nothing
  while touch is in use, easing over 0.3 s; any touch hides them, a controller
  connecting or a button/tilted stick brings them back. The iOS `+`/`−` keys
  draw pause and map SF Symbols. Synthetic tests cover the observer, every
  screen step with recorded game calls, the gates, guards and slides
  (`t_odyssey_ui.c`), and the footer/icon edit (`t_odyssey_display_map.c`); the
  iOS sources are compile-checked. Field offsets come from the decompiled
  1.4.1 code and the community decompilation; none of this is seen on a
  device yet.
- Tappable UI, second round: CommonHorizontalList Active (shops, captures,
  souvenirs: rows across, item top + row), WindowConfirmData Wait and
  SceneStateSelectMode Wait join the hooked slots (sixteen); list targets are
  found under `ListArea` for `Option*`, `Shop`, `ShineList`, `BgmList`,
  `HackList` and `SouvenirList`; `MiniGameMenu` rows go to CommonSelectParts.
  Seven more result gates answer one intent once and then none: the Back button
  (`rs::isTriggerUiCancel`, the map close and the collection list close), the
  cutscene Skip label (`rs::isTriggerUiPause`, which the skip-demo state checks)
  and the collection tab bar (previous / next tab and the map's tab change).
  Info windows (`WindowInfo`, `ImportantMessage`) take a tap as decide; their
  button icons hide with the other guides. The map drag now moves the selector
  (a stick toward the drag) at a 0.3 gain, clamped to a full stick. The Back
  button shows only while touch is in use on a screen that has a way back
  (not the title menu, dialogue or cutscenes). Loading screens keep the
  controls hidden; any other unrecognized
  screen brings them back after 2.5 s once a menu or gameplay has been seen.
  43 code hashes and every hooked slot are guarded; the rewritten `main` pin
  changed again. Synthetic tests in `t_odyssey_ui.c` cover each addition; none
  of it is seen on a device yet.
- Tappable UI, third round (synthetic tests and compile checks only; nothing
  here is seen on a device yet):
  - Wardrobe: `StageSceneStateCloset` Select joins the hooked slots
    (seventeen). `ClosetBody` / `ClosetHead` rows (`ListArea`, `ParList00..10`)
    are targets. A tap on the inactive list answers the closet's own key
    repeat (+0x1d0) once to switch lists and is taken again next frame; a tap
    on an item puts the cursor one short and answers one left / right step; a
    tap on the selected item grants decide (wear). Four result gates
    (`0x4f7e90`, `0x4f7ec0`, `0x4f7ef0`, `0x4f7f20`) answer only for that
    controller. `MiniGameRaceResult` takes a tap as decide.
  - Cutscenes: an observation gate on the skip-demo `tryStartSkip` entry
    (`0x49bab0`, asked every cutscene frame) classifies a cutscene as a tap
    screen (controls hidden, no Back); a tap answers the pause check once,
    which shows the Skip guide; while the guide is up only the label skips.
  - Footer actions: Lists (map tab change gate), Reset / Random (a new gate on
    the Y trigger `0x51d890`) and map zoom (the UI right stick `0x51e830`,
    answered only for the map's four zoom calls by return address while a
    zoom button is held) appear as iOS buttons where the guide was.
  - Moped: an observation gate on the moped's jump check (`0x5132e0`,
    callers in Motorcycle code by return address) marks riding; iOS swaps the
    diamond for a pedal (Y) and an up-arrow (B). A result gate on the pad
    style handheld check (`0x5d3ec0`) answers true only for the message
    PadStyle draw (return address `0x6a47e8`) while touch is in use, so
    messages draw their handheld pad pictures. Prep points the moped
    tutorial's handheld variants at `LeftButtonL` / `LeftButtonR` (the SL /
    SR glyphs), redraws those two DeviceFont80 cells (BC4, Tegra block
    linear, block height from the embedded BNTX) as a pedal and an up arrow
    in every language, and copies the controller variants of the JoyCon
    names into the handheld ones. SL / SR appear only in the single-Joy-Con
    pad style, which the host never reports. The font archive re-encode
    reuses the original Yaz0 tokens (about 3 KB larger per language).
  - "Pro Controller" reads "Controller / Device" in the English pad-style
    names and glossary (USen, EUen).
  - Boot screen: the tooltip strap (`PicStrap`) and every key of its slide
    move out by the phone's extra half-width, so it starts off screen and
    ends at the true edge. The main-menu / loading `ParBG` now covers the
    panel uniformly (cropping top and bottom), and the capture shown before
    kingdom select (`WipeWorldSelectCapture` `PicCaptureUse`) fills the panel
    exactly instead of being expanded as a round wipe.
  - Movie flush now resets the AAC decoder, as a seek should (the kingdom
    preview stutter turned out to be the H.264 loop below).
  - 54 code hashes (61 after the fourth round) and every hooked slot are
    guarded; the rewritten `main` pin changed again.
- Tappable UI, fourth round (synthetic tests and compile checks only, except
  the movie diagnosis, which comes from the player's device log):
  - Kingdom previews loop by sending their first packets again with times
    restarting at 0, with no flush or end of stream (one decoder took 5,010
    packets of a ~740-packet preview). The H.264 reorder queue sorted by time
    alone kept the previous pass's last pictures at its back, so B-frames came
    out in decode order until a new decoder was built. A picture more than
    0.5 s before the latest now starts a new pass; old passes leave first.
  - Horizontal lists publish their own rows from their row actors (the
    observer no longer needs their layouts). A tap jumps the list one short of
    the item with the cursor in its slot and answers one right / left step
    (new gates on the UI repeat and trigger right / left checks); the owner
    scrolls, plays its sound and updates its text. The selected item is
    decide. The wardrobe publishes both lists itself and steps the same way;
    its earlier one-short step moved the cursor out of its slot.
  - Cutscenes: while the Skip guide is up, a tap anywhere skips. In English
    the guide's pad-pair picture becomes a + picture whose handheld variant
    (`LeftButtonPlus`) reads "Tap anywhere to".
  - Pad pictures follow live switches: the set-time pick (`0x6a4d70`, caller
    `0x6a4d88`) is answered like the draw-time one, and for 0.5 s after a
    switch every drawn text box with a tag processor gets its rebuild flag
    (`nn::ui2d::TextBox` +0x11c bit 2, checked by `TextBox::Calculate`).
  - The map drag scrolls the map again (the stick away from the drag).
  - Layout preparation: the boot Joy-Con picture fills the width (wider, its
    texture cropped evenly top and bottom); the band under it is widened; the
    mode-select background is scaled up uniformly to cover; the wardrobe's
    light bands and panels are widened and its coin counters keep their
    distance from the right edge.
  - The first sighting of each drawn layout is logged, for screens that still
    lack taps.
- Smaller IPA (unit tests, pack-time read-back and Mac boot comparisons; not
  yet installed on a device):
  - RomFS archives, music streams and sound archives go in as DMZ files
    (`dm/host/dmz.h`): 2 MiB chunks of zstd level 19 (decoder vendored in
    `dm/third_party/zstd`), read back by the host's file layer. An archive the
    game would Yaz0-decode is presented as a Yaz0 stream of literals over its
    decompressed bytes, behind its original header: sead's `SZSDecompressor`
    (streaming, `0x97d5c0`, and the parallel loader, `0x97df50`) sizes its
    buffers by the header's size and alignment, never by the file's.
  - Archive textures over 512 pixels lose their top mip levels in place
    (`dm/tools/texcap.py`): the smaller levels move to where level 0 was, the
    header gets the new size, level count, block height and image size, and the
    freed bytes are zeroed, so no container changes size. Only 2D single-layer
    textures with mips whose level pointers and image size match the computed
    Tegra layout exactly are changed; all 2,382 such textures over 512 pixels in
    the object and effect archives matched. UI and text archives are left
    alone; skies, Mario and the world-map globe keep 1024; the full-screen
    kingdom maps have no mips and keep their size. 2,247 textures are capped.
  - Movies are re-encoded (H.264 CRF 26, AAC 128 kb/s; same size, frame rate,
    2 s keyframes and B-frame depth) and kept only when they decode to the same
    number of frames: 237 MB → 43 MB.
  - RomFS files 5.74 GB → 3.23 GB (archives 4.16 → 2.12, streams 1.10 → 0.86,
    movies 0.24 → 0.04). The 66,471 iOS shader libraries and reflection files go
    in as one bundle, `DmIOS.app/msl/<folder>.dmz`.
  - Every packed file is read back through the host's reader and compared with
    its payload before it is used (a Yaz0 view is decoded as Yaz0). `t_dmz`
    (ASan/UBSan) checks views, sizes, listings, zstd and LZFSE chunks, damaged
    files and bundle lookups against hand-written bytes; `test_texcap` checks the
    layout against offsets read from the game and the in-place move on synthetic
    BNTX files; `test_dmz_tree` checks rules, reruns and refusal of a mirror onto
    its own source.
  - On the Mac (LZFSE round) the game booted to the first-launch screen from the
    packed RomFS and shader bundle exactly as from the originals: the same 103
    files in the same order, no red lines. With zstd, capped textures and the
    zstd shader bundle, a scripted run through mode select into the opening
    cutscene captured the same eight frames as the originals with no red lines;
    close-ups of Bowser and Peach render correctly, slightly softer.
  - Odyssey iPhone 18 Pro IPA: 6.32 GB / 71,530 entries → 3.46 GB / 5,059
    (3.5 GB installed). Install time, load times and play on a phone are
    untested.
- Controllers are picked up when they connect, become current, or finish
  attaching their gamepad profile (short re-checks after a connect), and input
  switches back to touch when they disconnect. **Touch controls with
  controller** (stored, on unless turned off) keeps the touch controls usable while a
  pad is connected; with it on, the drawings fade while the controller is used
  and return on the next touch. Compile-checked against the iOS SDK; not yet
  seen on a device.
- Map icons are positioned by the game at runtime in the map's own root units,
  which skip the map's prepared or live correction, so they drifted from their
  spots away from the cursor. The exact-build Calculate adapter now records the
  `Map` layout's root scale and applies it to `MapIcon`, `MapIconLine` and
  `MapPlayer` root positions (restoring the player marker's prepared scale ratio
  after the game resets it), for phone, static 4:3 and live builds. Phone builds
  install that adapter for the map and menu-cursor rules only; the live HUD
  stays 4:3-only.
  Synthetic adapter tests cover both profiles. The player reported it working
  in that iPhone 18 Pro build; 4:3 and live builds are untested on a device, and
  the HUD minimap's icons are a separate path that is unchanged.
- 120 FPS is optional frame interpolation, with the simulation at 60 Hz.
  Hardware support, latency, artifacts, heat and sustained performance vary.
- Cloud save capture failed intermittently on some phone commits. The included
  EINTR retry fix preserves all consistency guards and improves diagnostics;
  the actual failing device branch, a full live retest of that fix and the
  reverse phone-to-iPad handoff are still unverified.
- The release candidate also gates cloud publication on a durable committed-save
  journal, and persists account/ancestry state before acknowledging sync. Failed
  writes retain pending progress and retry without requiring another game save.
  The production coordinator passes 24 synthetic scenarios and 251 checks using
  real Foundation write failures, including recovery after server acknowledgement,
  newer commits, import and account changes. Ordinary and sanitizer runs cover
  local logic; a fresh two-device physical handoff remains a separate check.
- Xcode 27 is the tested build toolchain; deployment minimum is iOS 26. Other
  devices, low-memory phones, free-account signing and ad hoc signing are unverified.
- A clean source build/signing-helper check does not automatically install or
  replace the already tested apps. New packaged artifacts need their own live QA.

## Repository workflow verification

The portable workflow was exercised with Xcode 27.1 and the original local
cartridge XCI plus update 1.4.1 NSP, in an independent private work directory.
It rebuilt the extractor from source, extracted and integrity-checked the merged
game, rewrote all three modules, split 27,984 shaders, prepared the matching HUD,
compiled the app and packed a private IPA. The shader pass reused a verified
local cache: 27,981 were ready and the same three documented shaders failed.
This was a full input-to-IPA check, not a new cold compile of every shader.

Separate extracted-file builds produced both the iPad and iPhone variants. All
209 output archive hashes for each preset match the accepted local display
build, and the phone rewritten-module hash matches its accepted camera/haptic
module. The controller, cloud and interpolation runtime sources are unchanged
from the local port. Both variants were signed and strictly verified with an
existing development profile; the phone check included its CloudKit entitlements.
No device app was replaced during repository validation. Automatic creation of
a new developer team's profile is not covered by those signing checks.

A clean source clone passed the game-free suite: 24 Python tests, native
controller/observer/viewport checks, save-commit callback tests, 146 save-archive
checks, and 17 coordinator scenarios with 153 checks. GitHub Actions runs the
same game-free suite. Private inputs, caches, IPAs, signing receipts and logs
are not uploaded to GitHub.

## Game-free checks

```sh
python3 tools/check.py
```

The checks cover structural layout handling with synthetic archives, preset,
model-table and CloudKit configuration, exact-profile signing policy, controller transport and
effect policy, gameplay-observer guards, presentation geometry, save archive
integrity/atomic restore, and the production CloudKit coordinator with controlled
transport callbacks. No real cloud account or device is touched.

## Local input verification

With the virtual environment active, use the same personal files as the port:

```sh
python tools/odyssey.py --extracted-dir /path/to/merged-1.4.1 \
  --preset ipad --prepare-only --work /path/to/private-ipad-check
python tools/odyssey.py --extracted-dir /path/to/merged-1.4.1 \
  --preset iphone --prepare-only --work /path/to/private-iphone-check
```

Both commands validate all three module hashes and the rewritten camera/haptic
guards. They create and read back the corrected layout archives (210 for 4:3,
211 for phone presets), hash-check
the original sources after preparation, and write private receipts. They never
sign or install. Optional `--rewritten` reuses an already checked local rewrite.

Before any requested live update, save and close the app, take a stable backup,
preserve bundle/team identity, install, then compare every save file before
launch. Verify gameplay, round HUD elements, dialogue/loading transitions,
controller actions and foreground/background behavior. Test cloud handoffs one
direction at a time and distinguish acknowledged digests from merely queued work.
