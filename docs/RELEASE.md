# Release bar — AL4AL/Throne

## Product narrative (v0.1)

**AL4AL Throne 0.1 — Nekoray users can move here.**

One sentence for release notes / README:

> Desktop Throne fork that replaces archived AL4AL/nekoray: same XHTTP cert-pin link aliases (`pcs` / `pinSHA256`), Throne’s modern protocol set, Windows / macOS / Linux.

Out of the 0.1 promise: Mahsa-class fragment theater, Hiddify UI rewrite, mobile, rebrand away from Throne.

## Release channel

| Branch | Role |
|---|---|
| `upstream` remote → `throneproj/Throne` | Track often (`git fetch upstream`) |
| `dev` | Default branch; AL4AL work lands here; merge/rebase upstream regularly |
| Tags `al4al-0.x.y` | Our cut releases (do not overwrite upstream tag names) |

**Policy:** prefer staying close to upstream `dev`. Fork-only commits should be small, documented in `docs/GOLDEN.md`, and candidates for upstreaming when polite (see `docs/UPSTREAM.md`).

## Local Linux build bar (verified on AL4AL workspace)

```
cmake -GNinja -DCMAKE_BUILD_TYPE=RelWithDebInfo -S . -B build
curl -fLso build/srslist.h https://raw.githubusercontent.com/throneproj/routeprofiles/rule-set/srslist.h
ninja -C build
# ThroneCore: from CI or upstream linux-amd64 zip → build/ThroneCore
```

Push gates (`.github/workflows/ci-push.yml`): EngineBoundary + P0 + synthetic P1.

If that workflow file is not on GitHub yet, the local copy lives at `.github/workflows/ci-push.yml`. Pushing it requires a token with the `workflow` scope:

```bash
gh auth refresh -h github.com -s repo,workflow
git add .github/workflows/ci-push.yml && git commit -m "ci: add AL4AL push gates" && git push
```

## Ship checklist (must be green)

1. **Lint:** `cmake -DROOT=<repo> -P cmake/EngineBoundary.cmake`
2. **P0 gate:** `bash script/ci/p0_xhttp_pin_check.sh`
3. **P1 gate (optional fixture):** `python3 script/ci/p1_sub_nul_gapcheck.py <sub.txt>` — must report Throne-safe or document a real gap before shipping “sub import” claims
4. **CI matrix:** upstream-style `.github/workflows/build.yml` via `workflow_dispatch` with tag `al4al-0.1.0` (or newer) when ready to publish artifacts
5. **Manual Linux smoke:** GUI or headless start with distinct CWD; import links from `docs/fixtures/xhttp_pin_links.txt`; confirm `pinSHA256`-only link stores a pin

## First public release steps

```bash
# after checklist
git tag -a al4al-0.1.0 -m "AL4AL Throne 0.1 — Nekoray successor (pinSHA256 alias)"
git push origin al4al-0.1.0
gh workflow run build.yml -R AL4AL/Throne -f tag=al4al-0.1.0 -f publish=r
```

Adjust `publish` to match workflow inputs (`r` / `p` / empty).

## Local Ubuntu 24.04 `.deb` (AL4AL)

```bash
# UI already built under build/; ThroneCore present beside it
# Deb epoch follows the prior local 1.0.0+al4al.dev-1 package (0.1.x would apt-downgrade).
INPUT_VERSION=1.0.1+al4al.dev-1 cmake -GNinja -DCMAKE_BUILD_TYPE=RelWithDebInfo -S . -B build
ninja -C build
bash script/pack_ubuntu24_amd64.sh 1.0.1+al4al.dev-1
# → deployment/throne_1.0.1+al4al.dev-1_amd64.deb
```

Simulate install before overwriting `/opt`: `apt-get -s install ./deployment/throne_*_amd64.deb`.

## Non-goals until after 0.1

- Matching every Throne upstream release asset name for in-app updater without review
- Claiming “best of Hiddify+Mahsa” in marketing copy
- PPTP egress (fork Linux feature; see `docs/GOLDEN.md`) is **not** part of the 0.1 Nekoray-successor promise
