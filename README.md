# Awesome-OS Explorer (`awesome-os/explorer`)

> **A sovereign, GPU-rendered native Wayland file manager (C++23 + direct Wayland + OpenGL ES — zero Qt, zero GTK) hard-forked from MIT Luciole for Awesome-OS, `t2desktop`, and `b8g-ghost` CAS.**

## Why Awesome-OS Explorer?

Awesome-OS Explorer builds on the clean, zero-toolkit C++23 / direct Wayland / OpenGL ES foundation of [Luciole](https://github.com/noctalia-dev/luciole) (MIT) as an independent hard-fork tailored for **Awesome-OS**, **`awesome-os/t2desktop`** (Hyprland/Wayland), and **`awesome-os/b8g-ghost @ cas-v1`**:

- **Direct GPU Rendering:** OpenGL ES directly over Wayland — zero Qt, zero GTK, zero Electron overhead.
- **Full Wayland Desktop Feature Set:** Grid, list, and Miller column views, tabs, split view, thumbnails/previews (images, video, text, PDF), archives (browse/create/extract + encrypted), SFTP/SMB/WebDAV, Undo/Redo, and `xdg-desktop-portal` Open/Save file picker.
- **Single-Process Multi-Window:** Every window shares one process with background thumbnail decoding and folder listing.
- **Awesome-OS / `b8g-ghost` Bridge (`src/b8g_ghost_bridge.h`):** `B8G_ABI_MAGIC_V3` (`0x33564742`) hooks for `B8G_GHOST_CACHE_DIR` (`~/.b8g/cas`) and Sectium Host Device (`\\.\pipe\sectium-dev` / `/tmp/sectium-dev.sock`) expansion.

## Build & Test (Linux / T2Desktop / Arch Container)

```bash
just build          # Debug build (build-debug/luciole)
just test debug     # Run full C++23 test suite
just build release  # LTO release build (build-release/luciole)
```

## Releases

GitHub Actions builds, tests, and publishes `explorer-linux-x64.tar.zst` + `SHA256SUMS.txt` on every `v*` tag (and manual `workflow_dispatch`).

## License & Attribution

MIT License — see [`LICENSE`](./LICENSE). Hard-forked from `noctalia-dev/luciole` (MIT).

## Quick Install & Run (Windows / Linux / macOS)

```sh
# Install / hydrate from v11 CAS via holo-pm
holo-pm install repository://github.com/awesome-os/explorer@main

# Or project & run directly via holo-project (0-byte hardlink)
holo-project "project://cpp/explorer?from=repository://github.com/awesome-os/explorer@main"
```
