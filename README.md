# SWBF2 Import — 3ds Max importer for Star Wars Battlefront II (2017)

Imports characters, heroes, soldiers and vehicles from **Star Wars Battlefront II
(2017)** straight from your game installation into **3ds Max 2016–2027**:

- skeleton in its bind pose
- skinned meshes with bone weights (all LODs)
- materials and textures
- animations, loaded one at a time or all at once on the timeline, with a note
  track and named sequences

You don't need any extraction tools. The plugin reads the game files directly.

## Installation

1. Download **SWBF2Import-&lt;version&gt;-Setup.exe** (or the ZIP) from the
   [Releases](../../releases) page.
2. Close 3ds Max, then run the setup. You can install for all users
   (`%ProgramData%\Autodesk\ApplicationPlugins\SWBF2Import`) or only for yourself
   (`%AppData%\Autodesk\ApplicationPlugins\SWBF2Import`). The setup also installs
   the Microsoft Visual C++ runtime if it is missing.
3. Start 3ds Max. You now have the **EAfront Tool** menu.

**Using the ZIP instead:** extract everything and run `Install.bat`. To remove
it, run `Uninstall.bat`. If you used the setup, uninstall it via
*Windows Settings → Apps*.

**Windows SmartScreen:** the setup is not code-signed yet, so Windows may show
*"Windows protected your PC"*. Click *More info → Run anyway*. You can check
the download against `SHA256SUMS.txt` on the release page.

## Requirements

- Windows 10/11, 64-bit
- 3ds Max 2016–2027
- Star Wars Battlefront II (2017) installed. No game data is included; the
  plugin reads your own installation.
- Microsoft Visual C++ runtime 14.50 or newer. The setup installs it; otherwise
  get it here: [vc_redist.x64.exe](https://aka.ms/vc14/vc_redist.x64.exe)

## Usage

### Import a character or vehicle

Open **EAfront Tool → Import SWBF2**.

1. The plugin looks for the game in the usual EA app, Origin and Steam folders.
   If it isn't found, use *Browse…* and pick the folder that contains
   `Data\layout.toc`.
2. The first time, the plugin builds an index of the game files. This takes a
   moment and is then cached. It is rebuilt automatically after a game update.
3. Pick a model:
   - Use the tabs *Heroes / Light side / Dark side / Other / Vehicles / All*.
   - Search accepts several words, and all of them must match.
   - *1st person* also shows first-person models.
4. Double-click the model or press **Import**.

You can also use *File → Import*: select the game's `Data\layout.toc` to open
the same window.

### Load animations

Open **EAfront Tool → SWBF2 Animations**. On first use, reading the animation
banks takes a few seconds.

- The list shows the animations of the character in your scene, assigned the
  way the game does it. *Loadable only* hides clips that can't be decoded.
- **Load** (or double-click) puts one clip on the skeleton.
- **Load all to timeline** places all listed clips one after another. *Gap*
  sets the number of frames between clips.
- With **Note track** on, each clip also gets a start and end key, named after
  the clip, in a note track. The *Sequence* list then jumps to each clip.

### Settings

Characters are imported at a fixed size of 1 m = 39.37 units, which is real
size in an inch-based scene. To change it, edit
`%LOCALAPPDATA%\SWBF2Import\einstellungen.ini`:

```ini
[Import]
EinheitenJeMeter=100   ; e.g. 100 = centimetres, 0 = follow the scene's system unit
```

The index, clip cache and imported textures are also stored in
`%LOCALAPPDATA%\SWBF2Import`. The import log is written to
`Downloads\Import swbf2.log`.

## Known limitations

- 8 animation clips (VBR codec with a constant palette of more than 256 values,
  e.g. some General Grievous get-up clips) can't be decoded yet. They are skipped
  and a note is written to the log.
- Vehicle parts that the game doesn't attach to bones are imported as rigid
  meshes.
- Level geometry is not supported. First-person models are listed but have been
  tested less.

## Building from source

You need:

- Visual Studio 2022 or 2026 with C++ and CMake
- the 3ds Max SDK of every year you want to build, in
  `C:\Program Files\Autodesk\3ds Max <year> SDK`

`BUILD.bat` builds `SWBF2Import.dlu` for every SDK it finds, or pass a year:
`BUILD.bat 2025`. `installer\BAUE_RELEASE.bat` then creates the setup and the
ZIP; it needs [Inno Setup 6](https://jrsoftware.org/isinfo.php).

See [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md) for the source layout, how the
data path works and the rules for 3ds Max plugin code.

## License

The plugin is licensed under GPL-3.0 (see [LICENSE](LICENSE)). An
[additional permission](LICENSE-EXCEPTION.md) under GPL-3.0 §7 lets you link it
with the Autodesk 3ds Max SDK and distribute the resulting plugin.

Bundled third-party code in `vendor/` keeps its own license:

- lz4: BSD-2-Clause
- miniz: MIT
- zstd: BSD
- bcdec: MIT/Unlicense

## Disclaimer

This is an unofficial, free fan project. It is **not** made by, affiliated with,
endorsed or sponsored by **Lucasfilm Ltd.**, **The Walt Disney Company**,
Electronic Arts, DICE or Autodesk.

- Star Wars is a trademark of Lucasfilm Ltd. / Disney.
- Star Wars Battlefront II belongs to Electronic Arts.
- 3ds Max is a trademark of Autodesk.

All trademarks belong to their respective owners. No game data is included; the
plugin reads the files of your own installation.
