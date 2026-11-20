# Building for Linux aarch64 (AYN Thor / Armada OS)

Native arm64 port of the Linux runtime so the lifted C compiles and runs on Snapdragon handhelds
(Adreno / Mesa Turnip), not under x86_64 translation.

This follows the same approach as [TotK recomp #492](https://github.com/valentderah/totk-pc-recomp/issues/492)
(FabianZettl), adapted to Astral Chain (`AC_*` knobs).

## What changed

- **Build**: `tools/build_game.py` uses `-march=armv8.2-a` on aarch64; `tools/build_xlate.py` publishes `linux-arm64`.
- **Platform**: aarch64 signal context, fibers, FPCR sync, `yield` pause; async preemption stays off (no `kern_ap_stub` yet).
- **Turnip / Armada**: `VK_EXT_external_memory_host` is optional (forces shadow storage). When `userfaultfd` is missing
  (`CONFIG_USERFAULTFD` off on Armada), a software write-watch (`AC_SOFT_WATCH`, default on) tracks dirty pages.
- **AppImage**: `tools/make_release_linux.py` selects aarch64 LLVM/Python/appimagetool pins when the host is arm64.

## Build on the Thor

Use a Fedora 43+ aarch64 container (or native Armada) with clang 21+, .NET 10 SDK, SDL3, opus, libvpx, zstd, Vulkan headers,
`glslc` / `spirv-tools`, and the matching `-devel` packages (`SDL3-devel`, `opus-devel`, `libvpx-devel`). On Armada:

```sh
rpm-ostree install glslc spirv-tools SDL3-devel opus-devel libvpx-devel
# reboot after layering, then continue
```

```sh
uv venv --python 3.12 .venv
uv pip install --python .venv/bin/python -r tools/requirements.txt
# config.local.toml with base NSP/XCI, 1.0.1 update, prod.keys
.venv/bin/python tools/extract.py
.venv/bin/python tools/build_xlate.py
.venv/bin/python recomp/gen.py --exefs build/extracted/v101/exefs --out generated/v101 \
    --patches recomp/patches-v101.txt --version v101
.venv/bin/python tools/build_game.py --version v101
```

Pin the ICD to Turnip (lavapipe can SIGILL on SVE):

```sh
export VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/freedreno_icd.aarch64.json
```

## AppImage

On an aarch64 host:

```sh
.venv/bin/python tools/make_release_linux.py \
    --out build/release/Astral-Chain-Setup-linux \
    --appimage build/release/Astral-Chain-Setup-aarch64.AppImage
```

The Setup AppImage contains the installer and toolchain only (no game files). It builds `astralchain` on the device
from your own dump, same as the x86_64 release.

## Expected performance (reference)

TotK on Thor (Adreno 740, Turnip) landed around 30 fps at 1280×720 with FSR after soft write-watch; Astral Chain is
lighter than TotK, but Turnip and full texture uploads still dominate. Prefer Performance power profile and default
render settings until Turnip quirks with non-default `render_scale` / `shadow_res` are checked.
