# Upstream relationship — AL4AL/Throne

## Remotes

- **origin:** `https://github.com/AL4AL/Throne.git` (this fork)
- **upstream:** `https://github.com/throneproj/Throne.git`

```bash
git fetch upstream
git merge upstream/dev   # or rebase onto upstream/dev on feature branches
```

## Intent

AL4AL/Throne is the **desktop product successor** to archived [AL4AL/nekoray](https://github.com/AL4AL/nekoray). We stand on Throne’s architecture and absorb open lessons from Nekoray / Hiddify / Mahsa — we are not a hostile soft-fork forever.

## What we will try to upstream

| Change | Why upstream |
|---|---|
| Accept share-link `pinSHA256` as alias for `pcs` on Xray + sing-box TLS import | Nekoray / panel interop; small, behavior-compatible |

Open a PR against `throneproj/Throne` when the diff is isolated and tests/docs are clear. Until merged, keep the commit in this fork.

## What stays fork-only (for now)

- AL4AL branding / README successor notices / `docs/GOLDEN.md` roadmap
- Release tags `al4al-*` and any updater endpoint pointing at AL4AL releases
- Larger absorb work (Hiddify UX ports, Mahsa open bypass) until proven and discussed

## What we will not copy

- Closed MahsaNG auth / config encryption / server APIs
- Replacing Throne’s Qt stack with Flutter

## Courtesy

- Credit throneproj in README
- Do not force-push shared history others rely on
- Prefer cherry-picks that apply cleanly onto upstream `dev`
