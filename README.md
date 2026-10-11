# Amnesia: The Bunker FPS Unlock

> [!WARNING]
> This patch has been pretty much entirely been generated using AI; 
> I won't and will never claim to have enough knowledge or expertise to do this 
> kind of reverse-engineering by myself; I've tested a couple levels (up to the generator room) and so far spotted 
> no major issues (on my machine ofc, if you have issues feel free to open an Issue).
> If you're interested and want more (human generated) info, I have a blog post [here](https://slop-blog.enkhayzomachines.net/posts/amnesia-the-bunker-fps-unlock) :)

An ASI plugin that lets the GOG release of *Amnesia: The Bunker* run above the 120 FPS its Framerate option stops at.

Current release: **v1.0.0**

## How it works

The Bunker runs on a fixed-step engine (HPL3): game logic and physics advance a set number of times per second, and one frame is drawn per update. That number is an ordinary setting. The Framerate menu writes 15, 30, 60 or 120 to `Screen.RefreshRate` and `Engine.GameplayUpdateRate`, and the engine passes the value to its logic timer, which runs on a microsecond clock and has no upper limit of its own. The 120 cap exists only in the menu.

The plugin patches three call sites in the game's settings code, in memory only:

- The two reads of `Screen.RefreshRate` and `Engine.GameplayUpdateRate` return the rate from `BunkerHighFPS.ini` in place of the value in the settings file.
- The write of `Engine.GameplayUpdateRate` saves the original file value back, so the plugin's rate never ends up in your settings.

It also corrects physics damping. The Newton physics library removes a fixed fraction of a body's velocity on every step, regardless of step length, so more steps per second means more drag per second. Measured with the game's own `newton.dll`, a body with the common damping value of 0.1 reaches 17.4 m/s after two seconds of free fall at 60 steps per second, but only 10.4 m/s at 360. The plugin intercepts the five Newton imports that create bodies and set or read damping, and converts each value so the velocity lost per second matches the game at 60 updates per second.

This is not interpolation: the game really simulates at the rate you set.

## Compatibility

- *Amnesia: The Bunker*, GOG release, Windows x64 (GOG build `57301760292462330`)
- `AmnesiaTheBunker.exe` SHA-256: `377664BA3E6FC84D4F9B04A75A1534868467106E7135FA951167E2C223C2DBD5`

The plugin does not check the hash. It checks the code at each patch site byte-for-byte before writing anything; on an executable where that fails (another storefront, another version) it changes nothing, the game runs unmodified, and the reason is in `BunkerHighFPS.log`. Only the GOG build above has been tested.

## Install

Download a release archive and follow its `README.txt`. In short:

1. Copy `BunkerHighFPS.asi` and `BunkerHighFPS.ini` beside `AmnesiaTheBunker.exe`. If the folder has no 64-bit ASI loader, use the archive that bundles Ultimate ASI Loader and copy its `version.dll` too.
2. Set `iUpdatesPerSec` in `BunkerHighFPS.ini` to the frame rate you want, normally your display's refresh rate. The default is 240.
3. Start the game normally.

## Settings

| Setting | Default | Meaning |
| --- | --- | --- |
| `bEnabled` | `1` | `0` leaves the game unmodified. |
| `iUpdatesPerSec` | `240` | Game updates, and frames, per second. 30 to 1000. |
| `bFixPhysicsDamping` | `1` | Keeps the physics slowdown per second as it is at 60 updates per second. |

The Framerate option in the game's menu has no effect while the plugin is active. If `LimitFPS` is switched off in the game's settings file, the game draws as many frames as it can instead of one per update; cap the frame rate at `iUpdatesPerSec` with VSync or an external limiter.

## Known limits

- Light testing only: a short session at 360 updates per second, from the start of the game to the generator, physics puzzles included, with no problems seen.
- The game ships with 60 and 120, so its code adapts to the rate, but parts of it may still be tuned for those two and behave differently at a higher one. Please open an issue for anything that differs from the unmodified game.
- Physics damping follows the game's 60 setting. At the game's own 120 setting objects are slowed slightly more than that.
- Your PC has to keep up with the rate you set. If it cannot, the game can run slower than real time.
- Custom stories and mods have not been tried.

## Build

Install Visual Studio or the Visual Studio Build Tools with the **Desktop development with C++** workload, then run in PowerShell:

```powershell
.\build.ps1
```

This builds `build\BunkerHighFPS.asi`. No game files are needed to compile. To also copy the plugin and its `.ini` into a game folder:

```powershell
.\build.ps1 -Install -GameDir 'C:\Path\To\Amnesia The Bunker'
```

## Packaging

```powershell
.\package-release.ps1
```

This writes the plugin-only and loader-bundled archives to `dist\`. The version comes from `PLUGIN_VERSION_STR` in `src\BunkerHighFPS.cpp`.

The loader is not in this repository. Before packaging, place Ultimate ASI Loader x64 v9.7.4 (`version.dll`) and its `LICENSE.txt` in `research\tools\ultimate-asi-loader-v9.7.4\`. The script refuses any other file: it pins the SHA-256 of the build the fix was tested with.

## Third-party

The loader-bundled archive includes [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by ThirteenAG, under the MIT License. The plugin itself has no third-party dependencies.

## License

MIT. See [LICENSE](LICENSE).
