# Golden desktop roadmap (AL4AL/Throne)

Successor to the archived [AL4AL/nekoray](https://github.com/AL4AL/nekoray). Goal: best **desktop** client (Windows / macOS / Linux) by absorbing open strengths from Nekoray, Hiddify, and Mahsa into this Throne fork.

Also see: [RELEASE.md](RELEASE.md) (0.1 bar) · [UPSTREAM.md](UPSTREAM.md)

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
| Huge subscription import | Byte-aware parsers | NUL sanitize + progress UX | — | Fast UI import | **P1: Throne already NUL-safe** |
| Kill switch / headless / scanner | Yes | No / limited | Partial | Scanner etc. | Keep Throne |
| Auto selector | Yes | Limited | Strong UX | Rotating configs | Defer until after 0.1 |
| TLS fragment / fake-host | Partial (`tls_fragment` in TLS) | No | — | Strong (open core) | Defer until after 0.1 |
| Packaging (APT/RPM/brew) | Strong | Deb lessons | Stores | APK | Keep Throne |

## Priority order

| Pri | Source | Work | Status |
|---|---|---|---|
| **P0** | Nekoray | XHTTP interop: Xray HTTP-version; `pinSHA256` as `pcs` alias | **Done** (code + `script/ci/p0_xhttp_pin_check.sh`) |
| **P1** | Nekoray | Large-sub NUL / hygiene | **Gap-checked: no port** — Throne `fromUtf8(size)` + Clash NUL sanitize; fixture `All_Configs_Sub.txt` has 1 NUL / ~7k lines |
| **P2** | Hiddify | Auto-select / subscription metadata polish | **Deferred** past 0.1 |
| **P3** | Mahsa | Extra open bypass | **Deferred** past 0.1 |
| **P4** | Throne | Track upstream; packaging | Ongoing |

## Fork features (past 0.1 bar, not upstream)

| Feature | Platforms | Notes |
|---|---|---|
| **PPTP egress** | Linux only | Preferences wizard + main-window toggle. NetworkManager PPTP (`never-default`) + host routes for the **selected profile’s server** via `ppp*`. Coexists with System Proxy by merging the PPTP gateway into GNOME `ignore-hosts` / KDE `NoProxy`. Mutually exclusive with Tun Mode. Needs `network-manager-pptp`, `pptp-linux`, `pkexec`. |
| **Remove duplicates on update (main pane)** | Desktop | Surfaced upstream’s `sub_options.remove_duplicates` on the Subscription edit dialog (kept in sync with Advanced). Same GroupUpdater post-update dedupe path. |

## Explicitly deferred (do not start before 0.1 ships)

- Mahsa “fragment theater” / rotating free-server platform / closed sources
- Hiddify Flutter rewrite or large UX redesign
- Android / iOS
- Product rebrand away from Throne naming
- Boiling-the-ocean protocol checklist without user evidence

## P0 audit notes (2026-10-21)

1. **HTTP version** — Throne XHTTP uses the Xray outbound path; runtime selection is Xray’s interop contract. Gate: `script/ci/p0_xhttp_pin_check.sh`.
2. **`pcs` / `pinSHA256`** — Fork accepts `pinSHA256` when `pcs` is absent (Xray + sing-box TLS link/JSON).
3. **Fixtures** — see `docs/fixtures/xhttp_pin_links.txt`.

## P1 gap-check notes (2026-10-21)

Ran `script/ci/p1_sub_nul_gapcheck.py` on a ~1.9 MB free-list with one embedded NUL mid-line. A Qt5 `QString(QByteArray)` style truncate would drop thousands of following lines. Throne’s parser is already size-aware; **no Nekoray sanitizer port for v0.1**.

## Syncing with upstream

```bash
git fetch upstream
git merge upstream/dev
```
