# game_starfield.dll for Mod Organizer 2.5.2 — vanilla plugins stay ticked

Starfield rewrites `Plugins.txt` (on loading a save or opening the Creations menu),
adding newer official plugins such as `SFBGS004.esm` and `ShatteredSpace.esm` without
the `*` that activates them. MO2 2.5.2 reads that file back when the game exits and
unticks them, and on the next launch the game doesn't load them.

This build of MO2's Starfield game plugin checks the first line of the profile's
`plugins.txt` whenever MO2 reads it. If the line is not MO2's own header (i.e. the
file was rewritten by Starfield), every plugin that physically exists in the game
installation's `Data` folder is ticked again, and MO2 writes the corrected file
straight away. Plugins from mods and in-game Creations (which live in
`Documents\My Games\Starfield\Data`) are left as they are, as are
`BlueprintShips-*` files.

The behaviour can be switched off in *Settings → Plugins → Starfield Support Plugin →
reenable_vanilla_plugins*. Unticking a vanilla plugin by hand in MO2 still works; it
is only re-ticked after the next time Starfield rewrites the file.

## Install

1. Close Mod Organizer.
2. Back up `<MO2 folder>\plugins\game_starfield.dll`.
3. Copy the new `game_starfield.dll` into `<MO2 folder>\plugins\`.
4. Start MO2. *Settings → Plugins → Starfield Support Plugin* should show version
   **1.1.1** and the new setting.

To undo, put the backed-up DLL back.

## Sources

| Path | Origin |
| --- | --- |
| `src/starfield` | `ModOrganizer2/modorganizer-game_starfield` @ `c3adebc` (2024-07-29, the 2.5.2 build) |
| `src/gamebryo`, `src/creation` | `ModOrganizer2/modorganizer-game_gamebryo` @ `0076e54` (2024-06-22) |
| `third_party/uibase-2.5.2` | headers of `ModOrganizer2/modorganizer-uibase` @ `v2.5.2` |
| `third_party/zlib`, `third_party/lz4` | zlib 1.3.1, lz4 1.10.0 (built statically) |
| `uibase.lib` | generated at configure time from the `uibase.dll` of your MO2 2.5.2 folder |

The first commit is the untouched upstream code; the second commit is the patch.
`patches/mo2-2.5.3-dev-game_bethesda.patch` is the same change for the current
`modorganizer-game_bethesda` master (MO2 2.5.3 dev builds), should you move to one.

## Build on your PC

Needs Visual Studio 2022 or 2026 (C++ workload), Qt 6.7.x for MSVC 64-bit, and your
MO2 2.5.2 folder. Python is optional but recommended (for Qt and the post-build check).

1. Qt 6.7.1 (only the qtbase part is needed):

       py -m pip install aqtinstall pefile
       py -m aqt install-qt windows desktop 6.7.1 win64_msvc2019_64 --archives qtbase -O C:\Qt

   If 6.7.1 is no longer offered, `py -m aqt list-qt windows desktop` lists the
   available 6.7.x releases; any 6.7.x works. Not 6.8 or later: MO2 2.5.2 refuses
   plugins built with a newer Qt minor version. Installed elsewhere? Set `QT_ROOT`.

2. Configure with `MO2_DIR` set to the MO2 folder (the one with `ModOrganizer.exe`).
   If MO2 handles your nxm:// links it is found automatically. Otherwise, in VS Code
   add to `settings.json`:

       "cmake.configureSettings": { "MO2_DIR": "C:/path/to/Mod Organizer 2" }

   or on the command line: `cmake -S . -B build -A x64 -DMO2_DIR="C:/path/to/MO2"`.
   Configure refuses to continue unless `ModOrganizer.exe` reports 2.5.2.

3. Build the **RelWithDebInfo** (or Release) configuration: in VS Code, *CMake:
   Select Variant*; on the command line, `cmake --build build --config RelWithDebInfo`.
   The DLL ends up in `build/RelWithDebInfo/game_starfield.dll`.

When `pefile` is installed, every build ends by running `ci/verify_imports.py`
against your MO2 folder: each symbol the new DLL imports from `uibase.dll`, the Qt
DLLs and any runtime DLL MO2 ships must exist in MO2's copies, and the Qt plugin
metadata must match MO2's own `game_starfield.dll`. The build fails otherwise, and
the report is written to `build/verify-report.txt`.

GitHub Actions (`.github/workflows/build.yml`) does the same on `windows-2022`
against the official MO2 2.5.2 release and uploads the DLL, PDB and report.

Mod Organizer 2 is GPL-3.0; this repository carries its code under the same terms.
