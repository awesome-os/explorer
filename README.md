# Awesome-OS Explorer (`awesome-os/explorer`)

> **A sovereign, GPU-rendered native Wayland file manager (C++23 + direct Wayland + OpenGL ES — zero Qt, zero GTK) hard-forked from MIT Luciole for Awesome-OS (`awesome-os/desktop`, `awesome-os/t2linux-baseline`) and Unified Holo `v11` CAS (`awesome-os/holo-v11`).**

## Why Awesome-OS Explorer?

Awesome-OS Explorer builds on the clean, zero-toolkit C++23 / direct Wayland / OpenGL ES foundation of [Luciole](https://github.com/noctalia-dev/luciole) (MIT) as an independent hard-fork tailored for **Awesome-OS** ([`awesome-os/awesome-os`](https://github.com/awesome-os/awesome-os), [`awesome-os/desktop`](https://github.com/awesome-os/desktop), [`awesome-os/t2linux-baseline`](https://github.com/awesome-os/t2linux-baseline)) and the **Unified Holo `v11` CAS** ([`awesome-os/holo-v11`](https://github.com/awesome-os/holo-v11)):

- **Direct GPU Rendering:** OpenGL ES directly over Wayland — zero Qt, zero GTK, zero Electron overhead.
- **Full Wayland Desktop Feature Set:** Grid, list, and Miller column views, tabs, split view, thumbnails/previews (images, video, text, PDF), archives (browse/create/extract + encrypted), SFTP/SMB/WebDAV, Undo/Redo, and `xdg-desktop-portal` Open/Save file picker.
- **Single-Process Multi-Window:** Every window shares one process with background thumbnail decoding and folder listing.
- **Holo `v11` CAS & Sectium Host Device Bridge (`src/b8g_ghost_bridge.h`):** `B8G_ABI_MAGIC_V3` (`0x33564742`) hooks aligning with `HOLO_CAS_DIR` / `~/.b8g/cas/v11` and the Sectium Host Device (`\\.\pipe\sectium-dev` / `/tmp/sectium-dev.sock`) boundary.

## Build & Test (Linux / Awesome-OS Desktop / Arch Container)

```bash
just build          # Debug build (build-debug/luciole)
just test debug     # Run full C++23 test suite
just build release  # LTO release build (build-release/luciole)
```

## Releases

GitHub Actions builds, tests, and publishes `explorer-linux-x64.tar.zst` + `SHA256SUMS.txt` on every `v*` tag (and manual `workflow_dispatch`).

## License & Attribution

MIT License — see [`LICENSE`](./LICENSE). Hard-forked from `noctalia-dev/luciole` (MIT).

## Quick Install & Run (Holo `v11` CAS — Windows / Linux / macOS)

```sh
# Install / hydrate from Holo v11 CAS via holo-pm (0-byte hardlink into ./bin/)
holo-pm install repository://github.com/awesome-os/explorer@main

# Or project & run directly via holo-project (0-byte hardlink)
holo-project "project://cpp/explorer?from=repository://github.com/awesome-os/explorer@main"
```

## Ecosystem Alignment (5 Canonical Monorepos)

- [`awesome-os/awesome-os`](https://github.com/awesome-os/awesome-os) · [`awesome-os/chromium-baseline`](https://github.com/awesome-os/chromium-baseline) · [`awesome-os/agent-platform`](https://github.com/awesome-os/agent-platform) · [`awesome-os/t2linux-baseline`](https://github.com/awesome-os/t2linux-baseline) · [`awesome-os/devtools`](https://github.com/awesome-os/devtools)
- [`awesome-os/holo-v11`](https://github.com/awesome-os/holo-v11) (Global `v11` Registry) · [`awesome-os/maintenance-workflows`](https://github.com/awesome-os/maintenance-workflows) (`holo-ci@v1`)
