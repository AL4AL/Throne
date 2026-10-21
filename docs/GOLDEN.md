# Golden desktop roadmap (AL4AL/Throne)

Successor to the archived [AL4AL/nekoray](https://github.com/AL4AL/nekoray). Goal: best **desktop** client (Windows / macOS / Linux) by absorbing open strengths from Nekoray, Hiddify, and Mahsa into this Throne fork.

Upstream reference: [throneproj/Throne](https://github.com/throneproj/Throne).

## Feature matrix

| Capability | Throne (upstream) | Nekoray (archived) | Hiddify | Mahsa (open) | Golden target |
|---|---|---|---|---|---|
| Platforms | Win / macOS / Linux | Win / Linux | + mobile | Android | **Desktop only** (Throne) |
| Core | sing-box + Xray | Matsuri sing-box + XHTTP patch | hiddify-sing-box + Xray | Mahsa Xray | Keep Throne dual-core |
| Protocols (AnyTLS, MASQUE, …) | Broad | Narrower | Broad via core | Xray-centric | Keep / extend Throne |
| XHTTP transport | Via **Xray** path (full UI) | Via **sing-box** patch | Via core / Xray option | Via Xray | Xray path + Nekoray link aliases |
| HTTP version for XHTTP | Xray-native `decideHTTPVersion` | Patched to match Xray | Historically drifted | Xray | **Audit: OK via Xray** |
| Cert pin `pcs` | Yes (Xray + sing-box TLS) | `pcs` + `pinSHA256` | varies | Xray pins | **`pcs` + `pinSHA256` alias** |
| Huge subscription import | Byte-aware parsers | NUL sanitize + progress UX | — | Fast UI import | Audit / port gaps as found |
| Kill switch / headless / scanner | Yes | No / limited | Partial | Scanner etc. | Keep Throne |
| Auto selector | Yes | Limited | Strong UX | Rotating configs | Port Hiddify UX polish if behind |
| TLS fragment / fake-host | Partial (`tls_fragment` in TLS) | No | — | Strong (open core) | Port remaining open Mahsa bits |
| Packaging (APT/RPM/brew) | Strong | Deb lessons | Stores | APK | Keep Throne; fold Nekoray lessons |

## Priority order

| Pri | Source | Work | Status |
|---|---|---|---|
| **P0** | Nekoray | XHTTP interop: confirm Xray HTTP-version; accept `pinSHA256` as `pcs` alias on link import (sing-box + Xray TLS) | **Started** |
| **P1** | Nekoray | Large-sub import hygiene / tray progress if still weaker than Nekoray 4.1.x | Planned |
| **P2** | Hiddify | Auto-select / subscription metadata / parse edge cases | Planned |
| **P3** | Mahsa | Open bypass only (extra fragment/fake-host from published Xray-core)—no closed MahsaNG code | Planned |
| **P4** | Throne | Preserve protocols, kill switch, headless, scanner, packaging; track upstream | Ongoing |

## P0 audit notes (2026-10-21)

1. **HTTP version** — Throne builds XHTTP through the Xray outbound path (`xrayStreamSetting` / `xrayVless`). Runtime version selection is Xray’s, which is the interop contract Nekoray had to re-implement in its sing-box patch. No Throne runtime change required for the classic “empty ALPN ⇒ HTTP/2” rule.
2. **`pcs` / `pinSHA256`** — Nekoray’s `Link2Bean` accepted either query key. Throne only read `pcs` in `xrayTLS::ParseFromLink` and sing-box `TLS::ParseFromLink`. **Fixed in this fork:** `pinSHA256` falls back when `pcs` is absent (same hex → pin pipeline).
3. **Self-signed servers** — Keep using pins + insecure skip as Xray/sing-box already do when pins are set; do not drop pin support when polishing UI.

## Out of scope

- Android / iOS product work
- Closed MahsaNG auth / config encryption / server APIs
- Reviving AL4AL/nekoray as an active app (repo archived)

## Syncing with upstream

```bash
git fetch upstream
git merge upstream/dev   # or rebase, per branch policy
```
