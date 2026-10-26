# How we release

**Story for 0.1:** Nekoray users can move to this Throne fork — `pcs` / `pinSHA256` cert pins work; full Throne protocol set on Windows / macOS / Linux.

## Branches and tags

| | |
|---|---|
| `upstream` → `throneproj/Throne` | Merge often |
| `dev` | Default; all AL4AL work |
| Tags `al4al-0.x.y` | Our releases (never reuse upstream tag names) |

## Local Linux check

```bash
cmake -GNinja -DCMAKE_BUILD_TYPE=RelWithDebInfo -S . -B build
curl -fLso build/srslist.h https://raw.githubusercontent.com/throneproj/routeprofiles/rule-set/srslist.h
ninja -C build
# Put ThroneCore next to build/Throne (CI artifact or upstream linux-amd64 zip)

cmake -DROOT=$PWD -P cmake/EngineBoundary.cmake
bash script/ci/p0_xhttp_pin_check.sh
bash script/ci/p1_sub_nul_check.sh
```

Ubuntu 24 system-Qt `.deb`: `script/pack_ubuntu24_amd64.sh` reads `packaging/ubuntu24_version.txt`, or takes `[version]` / `UBUNTU_DEB_VERSION`. Current local package line: **`1.0.5+al4al.dev-1`** (PPTP / L2TP-IPsec egress with PSK, dependency check/fix).

## Ship

1. Gates above green (also run on every push via `ci-push.yml`)
2. Manual full matrix when you want binaries:
   ```bash
   gh workflow run build.yml -R AL4AL/Throne -f tag=al4al-0.1.0 -f publish=r
   ```
   (`publish`: `r` = release, `p` = prerelease, empty = build only)

Pushing or editing workflow YAML needs `gh auth refresh -h github.com -s repo,workflow`.
