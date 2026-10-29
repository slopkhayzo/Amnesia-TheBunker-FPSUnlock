BunkerHighFPS @VERSION@ - high frame rate fix for Amnesia: The Bunker
==================================================================

What it does
------------
The game's Framerate option stops at 120. Amnesia: The Bunker runs its game
logic a fixed number of times a second and draws one frame per update, and
that number is a setting the engine accepts well above 120. This plugin makes
the engine use a rate of your choice (240 by default) in place of the
Framerate option.

It also corrects one side effect of a higher rate: the physics library slows
moving objects by a fixed share on every update, so at a higher rate objects
would fall and slide more slowly. The plugin keeps that slowdown per second as
it is at 60.

Unlike an interpolation fix, this one really does run the game logic and
physics faster, at the rate you set. The game itself ships with 60 and 120, so
its code adapts to the rate, but rates above 120 were never tested by its
developers. See "Known limits".


Supported game builds
---------------------
The 64-bit Amnesia: The Bunker from GOG (AmnesiaTheBunker.exe, GOG build
57301760292462330) and from Steam (AmnesiaTheBunker.exe and
AmnesiaTheBunker_NoSteam.exe). The log names the build it found.

Before changing anything the plugin checks the code it patches. On any other
executable (another version) the check fails, the plugin changes nothing, the
game runs unmodified and the reason is in the log.


Choose an installation
----------------------
Plugin-only archive:
  For a game folder that already has a 64-bit ASI loader. Copy
  BunkerHighFPS.asi and BunkerHighFPS.ini beside AmnesiaTheBunker.exe, or into
  the plugin folder your loader uses. Keep both files together.

Archive with Ultimate ASI Loader:
  For a game folder without an ASI loader. Copy version.dll,
  BunkerHighFPS.asi and BunkerHighFPS.ini beside AmnesiaTheBunker.exe.
  The bundled version.dll is Ultimate ASI Loader x64 v9.7.4 by ThirteenAG; its
  license is included.

  If the folder already has a version.dll, do not overwrite it without
  finding out what it is and backing it up. If it is already an ASI loader,
  copy only the .asi and the .ini.

For either installation:
1. Close the game before copying files.
2. Open BunkerHighFPS.ini and set iUpdatesPerSec to the frame rate you want,
   normally your display's refresh rate.
3. Start the game normally.


Settings (BunkerHighFPS.ini)
----------------------------
  bEnabled            1 = active, 0 = game unmodified.
  iUpdatesPerSec      Game updates, and frames, per second. 30 to 1000.
                      Default 240.
  bFixPhysicsDamping  1 = keep the physics slowdown per second as it is at
                      60 updates per second. Leave at 1.

The Framerate option in the game's menu has no effect while the plugin is
active. The plugin does not write its rate into the game's settings file.

If you have switched off LimitFPS in the game's settings file, the game draws
as many frames as it can instead of one per update. In that case cap the frame
rate at iUpdatesPerSec with VSync or an external limiter.


Known limits
------------
  - Light testing only: a short session at 360 updates per second, from the
    start of the game to the generator, physics puzzles included, with no
    problems seen. The rest of the game has not been played with it.
  - Parts of the game may still be tuned for 60 or 120 updates per second and
    behave differently at a higher rate. Please report anything that differs
    from the unmodified game.
  - Physics damping follows the game's 60 setting. At the game's own 120
    setting objects are slowed slightly more than that.
  - Your PC has to keep up with the rate you set. If it cannot, the game can
    run slower than real time; lower iUpdatesPerSec.
  - Custom stories and mods have not been tried.


Diagnostics
-----------
The plugin writes BunkerHighFPS.log beside the .asi each time the game starts.
A working start ends with a line like:

  engine: logic timer 240 updates per second ...

  - No log file: the .asi was not loaded. Check that an ASI loader is in the
    game folder and that the .asi is where the loader looks for it.
  - "... does not match" followed by "Plugin disabled; no changes made.":
    this is not the supported executable.
  - "physics damping fix disabled": the rate is applied but the physics
    correction is not.


Uninstallation
--------------
Close the game and remove BunkerHighFPS.asi, BunkerHighFPS.ini and
BunkerHighFPS.log.

Remove version.dll and ULTIMATE-ASI-LOADER-LICENSE.txt only if you installed
them for this fix and no other ASI plugin needs the loader.

The plugin changes nothing on disk except its own log. Saves, the game's
settings file and the game executable are not modified.

This software is provided without warranty. Back up your saves before
installing game modifications.

Misc
-------
For more game fixes, visit https://slop-blog.enkhayzomachines.net/fixes :)
