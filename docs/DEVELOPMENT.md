# Development notes

This page is for people who want to build or change the plugin. If you only
want to use it, see the [README](../README.md).

Identifiers and code comments are in German. The file map below explains what
each file does.

## Building

Requirements:

- Visual Studio 2022 or 2026 with the C++ workload and CMake
- the 3ds Max SDKs you want, installed in
  `C:\Program Files\Autodesk\3ds Max <year> SDK\maxsdk`
- for releases, [Inno Setup 6](https://jrsoftware.org/isinfo.php)

| Script | What it does |
|---|---|
| `BUILD.bat [year]` | Builds `SWBF2Import.dlu` for every installed SDK (or one year) into `output\<year>\` and installs the package. The log is `BUILD.log`. |
| `START.bat` | Full developer run: also builds `fbdump.exe` and cross-checks the readers against the Python reference tool *fbtools* (not part of this repo; those steps are skipped if it isn't found). |
| `INSTALLIERE.bat` | Copies `output\` into the package and installs it to `%ProgramData%` (or `%AppData%` without admin rights). |
| `installer\BAUE_RELEASE.bat [dir]` | Builds `dist\SWBF2Import-<version>-Setup.exe`, the ZIP and `SHA256SUMS.txt` from `output\` (or `dir\<year>\SWBF2Import.dlu`). Set `SWBF2_SIGNTOOL` to sign. |
| `ANIMLAB.bat`, `BERICHT.bat`, `FAHRZEUGE.bat` | Research tools built on `castool`: animation codec lab, a report on whether every character or vehicle gets animations, and the vehicle list. They need `index.fbidx` from `START.bat`. |

To build one year by hand:

```bat
cmake -S . -B build_2025 -G "Visual Studio 17 2022" -A x64 ^
      -DSWBF2_BUILD_PLUGIN=ON -DSWBF2_BUILD_TOOL=OFF ^
      -D3DSMAX_SDK_DIR="C:\Program Files\Autodesk\3ds Max 2025 SDK\maxsdk" -DMAX_VERSION=2025
cmake --build build_2025 --config Release
```

The code is C++17 so it also compiles against older SDK headers. It is linked
with `/MD` in every configuration. It needs the VC++ runtime 14.50 or newer.

### Releasing a new version

1. Raise the version in `src/swbf2import.h`, `src/swbf2import.rc`,
   `scripts/EAfrontImport.mcr` and `package/SWBF2Import/PackageContents.xml`.
   PackageContents.xml needs a new `ProductCode`. The `UpgradeCode` never
   changes, and the setup uses it as its AppId.
2. Run `BUILD.bat`, then `installer\BAUE_RELEASE.bat`.
3. Upload the Setup.exe, the ZIP and `SHA256SUMS.txt` to a GitHub release.

## How the data gets from the game into Max

The plugin reads the Frostbite files of the installed game directly. No
extraction step is needed.

```
layout.toc / cas.cat ──► catalog: SHA-1 → cas file, offset, size (+ patches)
        │
   bundles (DbObject) ──► index over all bundles (cached as index.fbidx)
        │
   EBX assets ─────────► skeleton (*_ske), MeshSet, materials, textures
        │
   MeshSet + chunks ───► meshes, bone indices and weights, LODs
        │
   figure model ───────► skeleton, skin, materials in 3ds Max
        │
   GD banks (anim) ────► clips (RAW, FRAME, DCT, VBR) → keys on the bones
```

Worth knowing:

- Encrypted files (`0x01CED100` / `0x03CED100` header) start at `0x22C`.
- Blocks can be raw, zlib, lz4, zstd or zstd with a dictionary. There is also a
  delta path for patches.
- `cas_01.cas` exists in almost every catalog, so every entry remembers which
  catalog it came from.
- Some MeshSets have an all-zero `chunkId`. Their buffers are inside the `.res`
  file, at the offset and size given by the first 8 bytes of `resMeta`.
- Bone indices with bit `0x8000` are procedural bones. They are resolved per
  mesh (see `LoeseProzeduraleBones` in `fbfigur.cpp`).
- Characters share `Characters/Rigs/Humanoids/Walrus_HumanMale` unless their
  folder has its own `*_ske` (see `fbauswahl::SkelettFuer`).
- Animations are assigned the way the game does it: from the hero or class
  specialization, through the animation state machine, down to the clip
  controllers (see `fbzuordnung`).

## Source map

| File | Purpose |
|---|---|
| `src/fbdatei.*` | File access with Unicode paths and 64-bit offsets |
| `src/fbcas.*` | Decryption, `cas.cat` catalog, block decompression |
| `src/fbdb.*` | DbObject, Frostbite's tree format (`layout.toc`, manifests) |
| `src/fbgame.*` | The game as a whole: paths, superbundles, patches |
| `src/fbbundle.*` | Reading one bundle (EBX, RES and chunk lists) |
| `src/fbindex.*` | Index over all bundles: name → SHA-1 → location |
| `src/fbebx.*` | EBX reader (objects with names) |
| `src/fbmeshset.*` | MeshSet reader: vertex declarations, sections, LODs |
| `src/fbteile.*` | Composite MeshSets, e.g. binding AT-AT parts to bones |
| `src/fbfigur.*` | Assembles one character: skeleton, meshes, bind pose |
| `src/fbauswahl.*` | The model list and skeleton lookup |
| `src/fbfahrzeug.*` | Vehicles: finding their MeshSets and skeletons |
| `src/fbmaterial.*`, `src/fbbeipack.*` | Which texture belongs to which mesh |
| `src/fbtextur.*` | Texture header, mip 0 decoding (BCn via bcdec), PNG output |
| `src/fbgesicht.*` | Face pose of a character |
| `src/fbgd.*`, `src/fbgdwerte.*` | GD banks (EA GenericData) holding the animations |
| `src/fbanim.*` | Animation clips: find, name and decode RAW, FRAME, DCT, VBR |
| `src/fbzuordnung.*` | Assigning animations to a character like the game |
| `src/fbdump.*` | `.fbmodel` / `.fbanim` dump format (read and write) |
| `src/swbf2import_max.cpp` | Everything that talks to the 3ds Max SDK: nodes, Skin, materials, keys |
| `src/swbf2import_fenster.*` | Model window (Win32, no SDK calls) |
| `src/swbf2import_animfenster.*` | Animation window (Win32, no SDK calls) |
| `src/swbf2import_ui.h` | Shared drawing that follows the Max theme colors |
| `src/swbf2import_ablage.h` | Where the plugin stores files (`SWBF2IMPORT_ABLAGE` overrides it) |
| `src/swbf2import_dll.cpp`, `swbf2import.def` | DLL entry points, `LibVersion` etc. |
| `scripts/` | MacroScripts and the "EAfront Tool" menu (old and new menu system) |
| `tools/castool.cpp` and friends | Command-line research and test tools on the same readers |
| `tools/sdkstub/` | A stub of the Max SDK headers, for compiling without an SDK |
| `vendor/` | lz4, miniz, zstd (decompression only) and bcdec, see `vendor/HERKUNFT.md` |

## Rules for plugin code

The rules come from the Autodesk SDK docs and from bugs we hit in earlier
plugins:

- **Heap:** classes shared with Max derive from `MaxHeapOperators`. Don't write
  your own `operator new`. Use the `/MD` runtime everywhere, because debug and
  release runtimes have separate heaps.
- **No exceptions** may leave the plugin. Catch everything at the SDK boundary.
- **Threads:** the windows do their work (reading the game, building the index,
  decoding) on one worker thread that never calls the SDK. Nodes, bones and
  keys are created on the main thread only.
- **Keys:** `Control::SetValue` only creates a key while animating. Wrap it in
  `SuspendAnimate` + `AnimateOn`. Don't conjugate quaternions for
  `CTRL_ABSOLUTE`, but keep their sign continuous. `ClearKeys` must walk into
  sub-controllers.
- **Decimal comma:** 3ds Max sets the C runtime to the user's locale, so
  `printf("%f")` writes `1,000000` on a German Windows. Anything written to a
  file uses a forced decimal point (`MaxLocaleHandler` or `std::to_chars`).
  Readers accept both.
- **Parsers read untrusted files:** check every size and offset against the
  buffer before reading. The build uses `/guard:cf` and MSVC STL hardening.
- **Menus:** 3ds Max 2025+ uses the new menu system (`cuiRegisterMenus`,
  `scripts/EAfrontMenu_2025_2027.ms`). 2016–2024 uses the old MenuMan
  (`EAfrontMenu_2016_2024.ms`).
- **Package:** `PackageContents.xml` must sit directly in
  `ApplicationPlugins\SWBF2Import\`, not one level deeper.

## Testing

- `tools/fenster_probe.cpp` shows the real model window without Max and
  without the game.
- `castool --clipcheck`, `--skeletttest --alle`, `--meshprobe` and
  `--teilbones` check animations, skeletons and meshes against the game
  from the command line.
- The plugin writes a detailed log to `Downloads\Import swbf2.log`
  (`SWBF2IMPORT_PROTOKOLL` overrides the path).
- Version 1.43.0 was tested in 3ds Max 2025. The test imported one
  representative of every hero, all 97 vehicles and pilots, and every soldier
  class of each era and side, then loaded all of their animations.
- Version 1.44.0 fixes DCT-compressed clips (about 4,000 clips, for example
  Darth Vader's backwards run): they jumped every 8 frames because the
  decoder used half the weight for DCT coefficients 1-7. Checked with
  `castool --clipcheck` over all clips (the new "Blockgrenze" value is the
  step at block boundaries divided by the step inside a block, 1 = smooth)
  and in 3ds Max 2027.
