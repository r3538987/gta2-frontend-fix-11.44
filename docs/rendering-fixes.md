# GTA2 v11.44 menu rendering

The reference images show natural background colors, white/red frontend text,
white/gray pause text, and a dark green translucent pause panel. The development
copy instead showed posterized backgrounds, black font textures, and a black
square around the cursor.

## Causes and changes

- The Plugin SDK's `TextureSearchCallback` can accept an 8-bit palettized texture
  when a non-palettized format is requested. Its GDI bitmap upload also loses
  alpha. `source/FrontendSprite.h` uploads BGRA pixels directly into an explicit
  ARGB8888 managed texture. It retains source pixels for video-device resets,
  respects surface pitch, and pads texture edges when device caps require it.
- Custom drawing previously changed device state without restoring the state
  cached by GTA2's GBH renderer, and used `VERTEXBLEND` for alpha blending.
  Custom sprites and primitives now explicitly set `ALPHABLENDENABLE`, color
  and alpha operations, and blend factors, then restore the previous texture and
  device state. The pause panel keeps its original destination-color blending;
  the audio icons keep additive blending.
- Startup font palettes were converted before video initialization finished.
  Reading the running process confirmed that each converted palette contained
  only zeroes, although the renderer's conversion masks were valid by then.
  The native frontend texture load at `0x456BDD` is deferred until the first
  background draw. Calling `gbh_SetColourDepth` earlier alone did not fix it.
- The pause-menu `fstyle.sty` texture manager was also loaded too early, and its
  physical palette IDs shared the level's palette table. It now loads on its
  first use after video initialization and uses IDs starting at `0x3C00`.
  Registration and PALX lookups use the same offset. The renderer has 16384
  palette slots; the reserved range holds up to 1024 frontend palettes.

The background TGA and frontend style files in the v9.6 and v11.44 development
copies had identical hashes. No recoloring of those assets is needed.

## Building

Build the Win32 `ReleaseII` configuration with Visual Studio Build Tools and
`PLUGIN_SDK_DIR` pointing to the ported SDK. The tested toolchain was VS 2022,
MSVC 14.44. The output is `output/asi/FrontendFixII.asi`.

The SDK is a separate dependency, outside this repository. Its GTA2 address
handling must preserve the original absolute code/data addresses:

```cpp
// shared/DynAddress.cpp, GTA2 only
plugin::GetGlobalAddress(address): return address;
plugin::GetExternalAddress(module, shift, address):
    return reinterpret_cast<uintptr_t>(GetModuleHandleA(module)) - shift + address;
```

v11.44 has an image base of `0x3F0000`, while the original hook addresses remain
unchanged. Applying the main executable's base delta to those addresses, or to
the external renderer DLL, shifts them `0x10000` too low. Rebuild `Plugin_II.lib`
after changing SDK address handling. These dependency fixes were already present
in the local SDK used for validation.

To build without the project's automatic copy into the game folder:

```powershell
msbuild project_files/gta2-frontend-fix.vcxproj /t:Build /p:Configuration=ReleaseII /p:Platform=Win32 /p:PLUGIN_SDK_DIR=C:/path/to/plugin-sdk /p:PostBuildEventUseInBuild=false
```

## Validation

Compared runtime screenshots against `samples` in the v11.44 development copy.
Verified fresh startup, the options page, level loading, gameplay, the pause
menu and quit confirmation, results after quitting, and pause rendering after
a second level load, return to the main menu, and normal exit. Backgrounds,
fonts, sliders, panel blending, and cursor transparency rendered correctly in
these checks.

The ReleaseII build passes with existing C4309/C4018 warnings. During testing,
one navigation sequence from options crashed with exception `0xC0000409`; the
Windows event named `GTA2.WidescreenFix.asi` as the faulting module. That crash
is unresolved and its root cause has not been established. Resolution changes,
the optional HD frontend, and a fresh v9.6 runtime have not been validated.
