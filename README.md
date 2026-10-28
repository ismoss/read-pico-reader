# mossread · 小纸 Pico (RDP-G01-W)

> **This is a downstream fork, not an official kiikoread release.** Maintained by **ismoss**, based on upstream [kiikoread](https://github.com/wegooo-cell/read-pico-reader) **0.3.3-rc100**, which in turn is built on [MindReset's demo firmware](https://github.com/MindReset/read_pico_firmware). It is not affiliated with, endorsed by, or supported by MindReset or the kiikoread maintainers. Upstream release notes, sites, stores, support channels and issue trackers do not cover this fork.

> **License note.** Upstream code through rc93 is Apache-2.0; upstream original material from rc94 and explicitly marked carries a [personal-study license](licenses/kiikoread-Personal-Study-1.0.txt) (no commercial use). New files in this fork are Apache-2.0, but the combined firmware still contains upstream noncommercial material — personal use only, no selling or commercial distribution. See [LICENSE](LICENSE).

**Current mod build:** `0.3.3-rc100-mod55` · **Feed:** [update.json](https://ismoss.github.io/read-pico-reader/update.json) · **Full mod README:** [English](https://github.com/ismoss/read-pico-reader/blob/mod/glass-dock-lockoff/README.md) · [简体中文](https://github.com/ismoss/read-pico-reader/blob/mod/glass-dock-lockoff/README.zh-CN.md)

**The default `main` branch mirrors upstream source.** The mod itself lives on **`mod/glass-dock-lockoff`**; the summaries below describe that branch, not the code in `main`.

## What this fork is

| Position | Meaning |
| :--- | :--- |
| Not a rewrite | Same kiikoread UI, book formats, partition table, configuration backups and NVS keys |
| Not official | No endorsement; self-hosted update feed; report problems here, not upstream |
| Not a one-off hack | CI host regressions + board-timing build, per-release changelog and roadmap |

It keeps kiikoread's foundation and fixes the parts that make you frown: reading in landscape, networking that behaves like a phone, visible feedback for every wait, and lock/refresh paths that settle cleanly.

## What the mod changes

| Area | Highlights |
| :--- | :--- |
| Reading | In-book landscape (body, TOC, fifteen secondary panels), first-turn prefetch, font picker no longer reverts to the built-in face |
| Lock screen | Ticket / wallpaper / collage / transparent cover adapt to landscape; separate portrait and landscape wallpaper slots; GC16 settle; PIN grace window |
| Shelf | Chunked scan keeps the UI responsive; persisted index paints the full shelf instantly on cold boot and deep wake |
| Network | Real automatic network switching (the old path was dead code), auto-connect switch, real link display, router-following DNS + online list |
| Updates | Self-hosted OTA feed, live download rate, redirects, 90 s slow-link tolerance, non-blocking cancel, no auto-check on entry |
| Transfer | Leaving stops the session but reconnects WiFi; “N files transferred” counter; portrait/landscape wallpaper upload; the preview decodes once and the cache is keyed on mtime |
| Remote monitor | LAN screen mirror + touch injection; off every boot; one-time RAM token; optional four-digit PIN; 1.5 s touch expiry; ten-minute idle auto-off |
| Branding | Visible system name **mossread**; cold-boot splash draws the wordmark over a star chart with built-in primitives (no flashed bitmap, no card access) |
| Engineering | CI host regressions, board-timing build, post-deploy feed verification, changelog + roadmap per release |

### Selected root causes fixed

| Symptom | Root cause | Fix |
| :--- | :--- | :--- |
| Office device keeps retrying the home hotspot | Active credential wiped before the picker ran; the picker returned immediately (dead code) | Pick on reconnection paths; switch only when the active network is absent |
| Monitor page stuck on “waiting” with a broken image | BMP `biPlanes`/`biBitCount` are 16-bit fields but only the low bytes were written | Zero the whole 54-byte header before writing fields; golden-header host test |
| Font change flashes back to Source Han Sans | Restoring the body face after drawing the picker could fail silently | Drawing never changes the body face; every pick persisted |
| Passcode dots stay on the lock wallpaper | Two layers: the cached lock art on SD could hold a frame saved while the pad was up, so the dots came back from the cache; the lock presenter also used GL16 and ignored the settle request | Invalidate the art cache (magic bump) and delete it on a successful unlock; return to the lock face with a full GC16 and settle before deep sleep |
| Wallpaper preview shows white for a long time | Every frame re-decoded the large image | Decode once into PSRAM; key the cache on mtime |

## Known limitations

| Topic | State |
| :--- | :--- |
| On-device acceptance | Every release from mod27 on is verified by build + CI + feed checks only; a single device walk-through is still pending |
| OTA throughput | The feed lives on GitHub Pages, which can stall on some routes; the update screen shows the live rate, and the TF-card path is the reliable fallback |
| Deep sleep | After the idle timeout the host powers off and the panel keeps the lock face; waking is a cold boot, so the splash appears first |

## Branches

| Branch | Role |
| :--- | :--- |
| `main` | Upstream source mirror + this landing page; **no mod code** |
| `mod/glass-dock-lockoff` | Mod source, CI and OTA feed publishing |
| `pr/*` | Upstream-worthy fixes prepared as separate pull requests |

## Install and roll back

The mod publishes **application images only**; it has no full web-flash bundle.

1. **From kiikoread rc72+:** download the binary from [update.json](https://ismoss.github.io/read-pico-reader/update.json), rename it to `Pico-update.bin`, put it in the TF-card root, then use **Settings → Upgrade & restore → System update → TF-card update**.
2. **From the official demo or another system:** first install stock kiikoread rc72+ with the [official web flasher](https://kiikoread.com/#flash-action), then do step 1.
3. **Roll back:** a full official web flash. A mod device only checks the self-hosted feed and will never offer an official update, so USB is required.

A TF-card app update keeps internal settings and reading records and leaves the TF card untouched. Save a configuration backup first; it contains the WiFi password in plaintext. A full web flash may clear internal data depending on the “erase device” option.

## Verification status

| Item | How | Status |
| :--- | :--- | :--- |
| Build | GitHub Actions, ESP-IDF v6.1, board timing | Passing; zero warnings since mod30 |
| Host regressions | Display/reader/lock/shelf/EPUB/settings/OTA TLS/DNS/monitor suites | Passing on every push |
| Published feed | Post-deploy version, size, SHA-256 and live-fetch check | Passing on every dispatch |
| On-device walk-through | Manual acceptance of mod1–mod55 | **In progress** |

Green CI is not a hardware acceptance for every item; the changelog marks which fixes have hardware evidence.

## Read more

| Purpose | Link |
| :--- | :--- |
| Full mod README (English) | [mod branch README.md](https://github.com/ismoss/read-pico-reader/blob/mod/glass-dock-lockoff/README.md) |
| Full mod README (中文) | [mod branch README.zh-CN.md](https://github.com/ismoss/read-pico-reader/blob/mod/glass-dock-lockoff/README.zh-CN.md) |
| Mod changelog | [docs/CHANGELOG.md](https://github.com/ismoss/read-pico-reader/blob/mod/glass-dock-lockoff/docs/CHANGELOG.md) |
| Mod roadmap | [docs/ROADMAP.md](https://github.com/ismoss/read-pico-reader/blob/mod/glass-dock-lockoff/docs/ROADMAP.md) |
| Update feed | <https://ismoss.github.io/read-pico-reader/update.json> |
| DNS online list | <https://ismoss.github.io/read-pico-reader/dns.json> |
| Upstream kiikoread | <https://github.com/wegooo-cell/read-pico-reader> |
| Official device docs | <https://dot.mindreset.tech/docs/read_0> |
| Official web flasher | <https://kiikoread.com/#flash-action> |

## License and credit

- [MindReset](https://dot.mindreset.tech/docs/read_0): hardware, demo firmware and documentation.
- [Kiiko厂长 / wegooo-cell](https://github.com/wegooo-cell/read-pico-reader) and the kiikoread contributors: the foundation of this fork.
- Upstream third-party components: Lucide (ISC), epdiy (LGPL-3.0-or-later), pypinyin (MIT), Noto (SIL OFL 1.1). See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Do not pass a mod build off as official, strip copyright/license notices, or sell it. Upstream issues are for upstream; mod issues belong in this repository.
