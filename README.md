# smo-ios

A touch-first native iOS port of Super Mario Odyssey

Built off of [NicholasDominici/super-mario-odyssey-ios](https://github.com/NicholasDominici/super-mario-odyssey-ios)

###### *Disclaimer: This repo does not include any of the required game files or prod.keys needed for a build. Dump them from your own Switch.*

## Features

- **On-screen controls.** Custom control system designed to allow full gameplay without a physical controller
- **About half the download.** The entire base game was optimized for mobile devices, resulting in a **3.46 GB full install** instead of 6.32 GB. [Details](docs/VALIDATION.md)
- **Mod support.** *(Coming Soon)* Custom kingdoms, clothes, and more, all easily installed with the click of a button (or two)

## Display and performance

Each build is customized to whatever display you have using `--preset`. GUI and HUD elements are adapted to fit nicely inside whatever aspect ratio your device uses.

The internal raster remains **1600 × 900**. Full-screen aspect does not mean
native display-pixel rendering. On the tested ProMotion devices the FPS overlay
can show around 120 with `FG`; roughly half those frames are generated. This
doesn't double physics speed or mean the GPU renders 120 original game frames.
Interpolation can introduce visual artifacts and latency and falls back when
unsupported. Use `--frame-generation off` to turn it off, or `--show-fps` to see
the counter. See [validation and limitations](docs/VALIDATION.md).

## Saves and updates

Progress lives in `Documents/save`, visible through the app's folder in Files
or Finder file sharing. **Back it up before deleting or replacing the app.**
Keep the same bundle ID and signing team when updating; deleting the app removes
its local container. The build/install tools don't delete an installed app or
launch it automatically. Save and fully close it before installing an update.

iCloud is **off by default**. Enable it only after following
[the iCloud setup guide](docs/ICLOUD.md), using the same container and environment
on both devices. Saves upload in the background after completed game commits;
there is no permanent sync button. Cloud imports happen before the game boots.
Use one device at a time and close it before switching.

## Reporting bugs

[Open an issue](https://github.com/u4pak/smo-ios/issues)
with your device, OS version, port commit, game version, display preset and what
you were doing. The app's `log.txt` and diagnostics are in its Files folder.
Remove account/device details before posting; don't attach game files, keys,
saves or provisioning profiles.

Do not create an issue asking about any of these features:
- Balloon World
- amiibo
- VR Mode

## Build and Install

### Requirements:
- **Xcode 27** or newer
- iPhone / iPad on **iOS 26** or newer
- **SMO game files**, either NSP or XCI
- Switch **prod.keys** file
- **Apple Developer Account**, a free one will do

### Install
1. **Prepare your Mac.** Install and open Xcode, accept its license, and sign in
   under **Xcode → Settings → Apple Accounts**. Select that Xcode under
   **Settings → Locations → Command Line Tools**. Install
   [Homebrew](https://brew.sh/) if you don't already have it, then run:

   ```sh
   brew install python xcodegen spirv-cross zstd ffmpeg
   brew install --cask dotnet-sdk
   ```

   The tools require **.NET 10**. If you install a newer SDK, also install the
   [.NET 10 SDK](https://dotnet.microsoft.com/en-us/download/dotnet/10.0) so the
   pinned shader translator's .NET 10 runtime is available.

2. **Download the source and prepare Python.** Open Terminal and run:

   ```sh
   git clone https://github.com/u4pak/smo-ios.git
   cd smo-ios
   python3 -m venv .venv
   source .venv/bin/activate
   python -m pip install -r requirements.txt
   ```

3. **Put your game files together.** Create a folder outside the source repo,
   such as `~/Games/Odyssey`. Put your base-game **NSP or XCI** and update **1.4.1 NSP**
   there. Keep your `prod.keys` elsewhere, such as `~/Games/Keys/prod.keys`.
   Only one game's files should be in the folder. Don't rename a different
   update to 1.4.1; its actual build will be checked.

4. **Build your private IPA.** Replace the example paths below. Choose a bundle
   ID you control and keep it for future updates. For the tested 4:3 iPad:

   ```sh
   python tools/odyssey.py \
     --nsp-dir "$HOME/Games/Odyssey" \
     --keys "$HOME/Games/Keys/prod.keys" \
     --preset ipad \
     --bundle-id com.yourname.odyssey \
     --output "$HOME/Games/Odyssey-ipad-unsigned.ipa"
   ```

   For the tested iPhone 18 Pro layout, change `--preset ipad` to
   `--preset iPhone-18-pro`. Use a different output filename for each preparation. The first build takes
   longer because it prepares shaders. The last line prints the IPA's path.

5. **Connect and prepare the device.** Connect it by USB, unlock it and tap
   **Trust**. Enable **Settings → Privacy & Security → Developer Mode**, then
   restart if prompted. Run `xcrun devicectl list devices` in Terminal and copy
   your device's identifier. Find your Team ID in your Apple Developer account's
   Membership details. [Signing help](docs/INSTALL.md) covers common errors.

6. **Sign and install.** Replace `YOUR_TEAM_ID`, `YOUR_DEVICE_ID` and the IPA path:

   ```sh
   python tools/sign_install.py \
     --ipa "$HOME/Games/Odyssey-ipad-unsigned.ipa" \
     --team YOUR_TEAM_ID \
     --device YOUR_DEVICE_ID \
     --install
   ```

   Xcode obtains a development profile for that device, the helper checks its
   memory entitlement, and then signs and installs the game. If Xcode reports a
   missing capability, follow [the profile setup steps](docs/INSTALL.md#profile-setup)
   and rerun using `--profile`. A signed IPA is also saved next to the unsigned one.
   Merely tapping an IPA in Files doesn't install it; development builds run only
   on devices allowed by their profile, and must be renewed when signing expires.

7. **Open Super Mario Odyssey and play.** Choose Start or
   Continue, and test movement, sound and a few jumps. When updating later, make
   an in-game save, fully close the app, back up its `save` folder, and repeat
   steps 4–6 with the same bundle ID and signing team. Optional iCloud setup is
   in [docs/ICLOUD.md](docs/ICLOUD.md).

## Contributing

The supported inputs are the base game (**NSP or XCI**) plus **update 1.4.1 (`v393216`)**,
title ID `0100000000010000`. [Input details and extracted-folder support](docs/INPUTS.md)
cover the same files used for the tested builds.

The player-facing entry point is `tools/odyssey.py`. It uses the shared runtime
in `dm/`, the exact-build profile in `dm/games/0100000000010000/`, and structural
HUD preparation in `tools/prepare_odyssey_*.py`. All extracted files, rewritten
game modules, shader caches, signing profiles and IPAs stay outside the checkout.

Run checks without any game files:

```sh
python3 tools/check.py
```

For changes to binary observers, shaders or rendering, see
[AGENTS.md](AGENTS.md), [CLAUDE.md](CLAUDE.md) and the
[runtime development guide](docs/development.md). A source test passing is not
proof of gameplay performance or how a physical controller feels.

## Credits

- [NicholasDominici/super-mario-odyssey-ios](https://github.com/NicholasDominici/super-mario-odyssey-ios):
  Odyssey profile and display preparation,
  the iPad and Duo views, controller motion and haptics, iCloud saves and the
  build, signing and install tools.
- [seedds/nxnative](https://github.com/seedds/nxnative): native runtime, offline
  code rewriting, graphics translation, build tooling, and the original game profile.
- [ly05010419/MacNativeGames](https://github.com/ly05010419/MacNativeGames): work
  used by nxnative; its license and notices are retained.
- [fayaz12g/smo-aar](https://github.com/fayaz12g/smo-aar/tree/e8fd670d1faeab3d8c4904177928bcfd14afb42c):
  the aspect-ratio approach, reimplemented here with structural archive checks.
- [Ryujinx and the other dependencies](dm/third_party/README.md): shader
  translation, codecs, headers and utilities, under their respective licenses.

Port source is MIT; see [LICENSE](LICENSE) and [source provenance](docs/PROVENANCE.md).
This is an unofficial fan project. Super Mario Odyssey and its game content
belong to Nintendo. The game and its assets are not covered by this source license.
