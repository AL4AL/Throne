# Headless Throne

`Throne --headless` runs Throne without a window: on a server, a router, a Raspberry Pi or in a
container. It is the same program as the desktop app, but it shows no window, tray icon or dialog.
You control it with [thronectl](https://github.com/throneproj/Throne-api) through the Throne API,
which is on from the first start.

Throne never sends itself to the background. A service manager starts it at boot, starts it again
after a failure and stops it: systemd on Linux, launchd on macOS, the Service Control Manager on
Windows, or Docker. This repository has a ready file for each:

| File | What it is |
|---|---|
| [`dist/linux/throne.service`](../dist/linux/throne.service) | systemd system unit (root). The deb and rpm packages install it as `/usr/lib/systemd/system/throne.service` |
| [`dist/linux/throne.env`](../dist/linux/throne.env) | Environment file for that unit, installed as `/etc/throne/throne.env`. Every variable in it is a commented-out example |
| [`dist/linux/caps.conf`](../dist/linux/caps.conf) | Optional drop-in: run the unit as user `throne` with capabilities instead of root |
| [`dist/linux/throne-user.service`](../dist/linux/throne-user.service) | systemd user unit, without root |
| [`dist/macos/moe.Throne.headless.plist`](../dist/macos/moe.Throne.headless.plist) | launchd LaunchDaemon |
| [`dist/windows/install-service.bat`](../dist/windows/install-service.bat), [`uninstall-service.bat`](../dist/windows/uninstall-service.bat) | Windows service |
| [`docker/Dockerfile`](../docker/Dockerfile), [`docker/compose.yaml`](../docker/compose.yaml) | Container image and a compose example |

The requests, status fields and error codes of the Throne API are in the
[Throne API specification](https://github.com/throneproj/Throne-api/blob/main/spec/api.md#headless-throne).

- [Quick start](#quick-start)
- [Install](#install): [deb and rpm](#debian-ubuntu-raspberry-pi-os-fedora-opensuse), [other Linux installs](#other-linux-installs),
  [privileges on Linux](#privileges-on-linux), [user unit](#systemd-user-unit), [Docker](#docker), [macOS](#macos),
  [Windows](#windows-service)
- [Command line](#command-line): [options](#options-and-variables), [data directory](#data-directory),
  [restart modes](#restart-modes), [exit codes](#exit-codes)
- [Connecting thronectl and setting Throne up](#connecting-thronectl-and-setting-throne-up)
- [Checking and monitoring](#checking-and-monitoring): [`--check`](#throne---check), [`--health`](#throne---health),
  [systemd](#systemd-readiness-status-and-watchdog), [logs](#logs)
- [Without a window](#without-a-window)
- [Lockout protection and recovery](#lockout-protection-and-recovery)
- [One Tun per computer](#one-tun-per-computer)
- [Device ID](#device-id)
- [Windows notes](#windows-notes)
- [Troubleshooting](#troubleshooting)
- [CI coverage](#ci-coverage)

## Quick start

On Debian 12 or Ubuntu 22.04 and later, amd64:

```sh
sudo apt install ./Throne-<version>-debian-amd64.deb
sudo systemctl enable --now throne
systemctl status throne
```

`systemctl status` shows what Throne is doing, such as `Status: "Idle"`. Throne keeps its data in
`/var/lib/throne` and its API key in `/var/lib/throne/config/api.key`, readable only by root. Copy
the key for yourself and use thronectl:

```sh
mkdir -p ~/.config/throne
sudo install -m 600 -o "$USER" /var/lib/throne/config/api.key ~/.config/throne/api-key
export THRONE_KEY_FILE=~/.config/throne/api-key

thronectl status
thronectl sub add 'https://sub.example.com/abc' --name Provider
thronectl profiles
thronectl connect 12
```

thronectl is not part of the Throne packages: download it from the
[Throne-api releases](https://github.com/throneproj/Throne-api/releases), or run
`go install github.com/throneproj/Throne-api/cmd/thronectl@latest`. The Docker image includes it.

## Install

### Debian, Ubuntu, Raspberry Pi OS, Fedora, openSUSE

The deb and rpm packages from [Releases](https://github.com/throneproj/Throne/releases) install
Throne to `/opt/Throne`, the systemd unit and `/etc/throne/throne.env`. Pick the package by the
system's C library (glibc):

| Package | Needs | Runs on, for example |
|---|---|---|
| `Throne-<version>-debian-amd64.deb`, `...-fedora-amd64.rpm` | glibc 2.35 | Debian 12, Ubuntu 22.04, Fedora, openSUSE Tumbleweed |
| `...-amd64-system-qt.deb`, `...-amd64-system-qt.rpm` | glibc 2.35 and the system's Qt 6 | The same, with Qt from the distribution |
| `Throne-<version>-debian-arm64-system-qt.deb` | glibc 2.35 and the system's Qt 6 | Raspberry Pi OS bookworm, Debian 12, Ubuntu 22.04 on arm64 |
| `Throne-<version>-debian-arm64.deb`, `...-fedora-arm64.rpm` | glibc 2.39 | Ubuntu 24.04, Debian 13, Raspberry Pi OS trixie |

The versions are those of the systems the packages are built on. Each package depends on the glibc
its files actually need (`libc6 (>= ...)`, `glibc >= ...`), so the package manager refuses it on an
older system instead of installing a Throne that cannot start.

On a Raspberry Pi with Raspberry Pi OS bookworm (glibc 2.36), use the arm64 system-qt package. The
packages without system-qt carry their own Qt, but they still depend on the system's graphics, font
and X11 libraries, because the one Throne program also contains the window app. The package manager
installs them; nothing needs a display.

```sh
sudo apt install ./Throne-<version>-debian-amd64.deb      # or: sudo dnf install ./Throne-<version>-fedora-amd64.rpm
sudo /opt/Throne/Throne --check                           # optional: can this host run it?
sudo systemctl enable --now throne
```

- The package never enables or starts the unit by itself. An upgrade restarts it when it runs, and
  removing the package stops and disables it. `/var/lib/throne` (profiles, settings, the API key) stays,
  even on `apt purge`; delete it yourself to start over.
- `systemctl start throne` returns once Throne has started (see [systemd](#systemd-readiness-status-and-watchdog)).
- `/etc/throne/throne.env` holds a commented example for each variable the unit can use. Uncomment a
  line and run `sudo systemctl restart throne`. The package keeps your edits on upgrades. Never put a
  key in it: `THRONE_API_KEY_FILE` names a key file instead.
- To change the command line, add a drop-in with `sudo systemctl edit throne`:

  ```ini
  [Service]
  ExecStart=
  ExecStart=/opt/Throne/Throne --headless --data-dir=%S/throne --debug
  ```

- Logs: `journalctl -u throne -f`.

The unit runs Throne as root, which [Privileges on Linux](#privileges-on-linux) compares with the
other ways.

### Other Linux installs

The zip from Releases and the [Linux CLI installer](../script/install_linux.py) put Throne in
`/opt/Throne` without a unit. Install the unit by hand:

```sh
curl -fsSLo throne.service https://raw.githubusercontent.com/throneproj/Throne/dev/dist/linux/throne.service
sudo install -m 644 throne.service /etc/systemd/system/throne.service
sudo systemctl daemon-reload
sudo systemctl enable --now throne
```

`/etc/throne/throne.env` is optional for this unit. Copy
[`dist/linux/throne.env`](../dist/linux/throne.env) there to have the examples.

The [AUR package](https://aur.archlinux.org/packages/throne) is maintained separately and installs
Throne to `/usr/lib/throne`. If it does not ship a unit, use the one above with a drop-in
(`sudo systemctl edit throne`):

```ini
[Service]
ExecStart=
ExecStart=/usr/lib/throne/Throne --headless --data-dir=%S/throne
```

### Privileges on Linux

Tun and the kill switch need privileges; profiles with an Extra Core need a user to run that core
as, since the core never runs it as root:

| How the unit runs | Tun | Kill switch | Extra Core profiles | For |
|---|---|---|---|---|
| As root (the unit as shipped) | yes | yes | no | Gateways and dedicated boxes |
| As `throne` with capabilities ([`caps.conf`](../dist/linux/caps.conf)) | yes | no: `unsupported` | yes | A proxy box that does not need the kill switch |
| As `throne` with a setuid-root `ThroneCore` | yes | yes | yes | Everything, without running Throne itself as root |
| As `throne` with nothing else | no | no | yes | Proxy only (the mixed inbound) |

The capabilities variant, as `caps.conf` describes:

```sh
sudo useradd --system --no-create-home --home-dir /var/lib/throne --shell /usr/sbin/nologin throne
sudo install -Dm644 caps.conf /etc/systemd/system/throne.service.d/caps.conf
sudo systemctl daemon-reload && sudo systemctl restart throne
```

systemd hands `/var/lib/throne` over to `throne` at the next start. `ThroneCore` must have no setuid
bit and no file capabilities here: running such a file drops the capabilities the unit grants.

The setuid variant uses a drop-in with only `User=throne` and `Group=throne`, and a core that runs as
root:

```sh
sudo chown root:root /opt/Throne/ThroneCore && sudo chmod u+s /opt/Throne/ThroneCore
```

A package upgrade replaces `ThroneCore` without the bit. On Debian,
`sudo dpkg-statoverride --update --add root root 4755 /opt/Throne/ThroneCore` keeps it across
upgrades. Do not add `NoNewPrivileges=` or a hardening option that implies it to the unit: it would
stop the setuid core.

`thronectl status` shows what this Throne can do in `capabilities.tun` and
`capabilities.kill_switch`. The core checks for itself whether it can open `/dev/net/tun` and has
CAP_NET_ADMIN, so a root container without `NET_ADMIN` reports `false` rather than failing later.

### systemd user unit

Without root, Throne runs as a user unit in your own session:

```sh
install -Dm644 throne-user.service ~/.config/systemd/user/throne.service
systemctl --user daemon-reload && systemctl --user enable --now throne
loginctl enable-linger "$USER"      # start at boot, without a login
```

- It uses the desktop app's data directory, `~/.config/Throne`: the same profiles and settings.
  Quit the desktop app first. While the unit runs, the app says that Throne is running in the
  background and exits; `systemctl --user stop throne` before you open it.
- Tun needs a setuid-root core. The desktop app sets it up the first time it asks for privileges,
  and `Throne --check` prints the commands. The kill switch needs it too.
- `Throne --health`, `--print-connection` and `--check` find it without options, as your user.

### Docker

The image `ghcr.io/throneproj/throne:<version>` (also `latest`) is built for linux/amd64 and
linux/arm64 from Ubuntu 24.04 and the Linux release: Throne in `/opt/Throne`, thronectl in
`/usr/local/bin`, and tini as init. It keeps its data in the volume `/var/lib/throne`, exits with
75 for a restart (`THRONE_RESTART_MODE=exit`) and runs `Throne --health` as its health check.
[`docker/compose.yaml`](../docker/compose.yaml) is a starting point. It uses the `latest` tag; put a
release tag there to update on your own schedule:

```sh
curl -fsSLO https://raw.githubusercontent.com/throneproj/Throne/dev/docker/compose.yaml
docker compose up -d
docker compose exec throne thronectl --key-file /var/lib/throne/config/api.key status
```

- Tun and the kill switch need `cap_add: [NET_ADMIN]` and the device `/dev/net/tun`. Without them
  the container still serves the mixed proxy inbound.
- The mixed inbound listens on `127.0.0.1` by default, which a published port cannot reach. Run
  `thronectl settings set inbound_address=0.0.0.0`, then publish port 2080. The change applies when
  the profile starts again.
- With `network_mode: host`, Tun and the kill switch apply to the whole host, and the
  [one Tun per computer](#one-tun-per-computer) rule counts the host's other Thrones.
- Keep `stop_grace_period: 45s`. Docker's default of 10 seconds can kill Throne before it has
  stopped its core.
- With `restart: unless-stopped`, Docker starts Throne again after every exit, even after
  `thronectl shutdown`. Stop it with `docker compose stop`.
- Docker only marks an unhealthy container; it does not restart it.
- Extra Core profiles do not run, since the container runs as root.
- In rootless Docker and other user-namespaced containers the kill switch fails with
  `unsupported` (see [Lockout protection](#lockout-protection-and-recovery)).
- Flags and variables: put the variables in `environment:`, or add flags with `command:`, which
  Docker appends after `--headless`. Change the data directory only with `THRONE_DATA_DIR`: the
  health check finds Throne through it. Adding `THRONE_KEY_FILE: /var/lib/throne/config/api.key` to
  `environment:` lets plain `docker compose exec throne thronectl status` work; Throne itself
  ignores that variable.
- The API listens on the container's loopback. To reach it from outside, set
  `THRONE_API_LISTEN: "*:9095"` with a `THRONE_API_ALLOW` list and publish the port. Admin requests,
  such as imports and settings, then also need `THRONE_API_ADMIN_LAN: "1"`. Behind Docker's NAT all
  clients share one address, so one client with an old key locks the others out for a minute.
- To pipe a file into thronectl, `docker compose exec -T throne thronectl ... import - < links.txt`.
- Logs: `docker compose logs -f throne`.

### macOS

A LaunchDaemon runs Throne as root at boot: Tun and the pf kill switch work, Extra Core profiles do
not. Move `Throne.app` to `/Applications` first, then:

```sh
sudo mkdir -p /Library/Logs/Throne
sudo install -m 644 -o root -g wheel moe.Throne.headless.plist /Library/LaunchDaemons/
sudo xattr -dr com.apple.quarantine /Applications/Throne.app      # unsigned builds only
sudo launchctl bootstrap system /Library/LaunchDaemons/moe.Throne.headless.plist
```

- Stop and unload it with `sudo launchctl bootout system/moe.Throne.headless`.
- Its data is in `/Library/Application Support/Throne`, its output in `/Library/Logs/Throne/throne.log`.
- The plist runs the binary directly, so launchd follows the process. Never start headless Throne
  with `open`, which hands it to LaunchServices and returns at once.
- launchd starts Throne again after a non-zero exit, 10 seconds later. It cannot leave out exit
  codes, so a configuration error (3, 64, 78) is retried every 10 seconds: read the log.
- `sudo /Applications/Throne.app/Contents/MacOS/Throne --health` asks it how it is doing.

### Windows service

`Throne.exe --headless --service` runs as a native Windows service, as LocalSystem, so Tun and the
WFP kill switch work. Install Throne for all users (in Program Files: a service must not run a
program that a user can replace), then run from an elevated Command Prompt:

```bat
install-service.bat
```

or `install-service.bat "D:\Apps\Throne\Throne.exe"` for another location. It creates the service
`Throne`, starts it after boot (delayed), and sets its recovery:

```bat
sc.exe failure Throne reset= 86400 actions= restart/3000/restart/10000/restart/60000
sc.exe failureflag Throne 1
```

- `failureflag 1` makes Windows apply the recovery actions when Throne stops with a non-zero exit
  code, not only after a crash. That is how a restart request (exit 75) and the
  [watchdog](#systemd-readiness-status-and-watchdog) exits come back. Each counts as a failure:
  within a day, the first comes back after 3 seconds, the second after 10, later ones after 60.
- `sc.exe stop Throne` and `thronectl shutdown` stop it for good (exit 0). At shutdown, Windows
  gives it up to 45 seconds.
- `sc.exe query Throne` after it stopped with an error shows `WIN32_EXIT_CODE : 1066` and Throne's
  exit code as `SERVICE_EXIT_CODE`.
- Its data is in `C:\ProgramData\Throne`, logs in `C:\ProgramData\Throne\config\logs\throne.log`.
- `uninstall-service.bat` stops and deletes the service; the data in `C:\ProgramData\Throne` stays.
  Stop the service before you update Throne.

[Windows notes](#windows-notes) has the details: data folder permissions, running it from a console,
and how to run `--health` and thronectl against the service. A scheduled task (Task Scheduler) is no
substitute: Windows stops a task with a hard kill and cannot start it again on an exit code.

## Command line

### Options and variables

`Throne --headless --help` lists them. One or two dashes both work (`-data-dir` is `--data-dir`),
and a value follows a space or `=`. A flag wins over its variable. A variable set to an empty value
counts as unset, except `THRONE_API_ALLOW`.

| Option | Variable | |
|---|---|---|
| `--headless` | `THRONE_HEADLESS=1` | Run without a window. Any value of the variable except empty, `0`, `false`, `no` and `off` turns it on |
| `--data-dir DIR` (`-appdata DIR`) | `THRONE_DATA_DIR` | The [data directory](#data-directory) |
| `--restart-mode MODE` | `THRONE_RESTART_MODE` | `auto` (default), `exit` or `exec`: see [Restart modes](#restart-modes) |
| `--no-resume` | `THRONE_RESUME=0` | Do not start the last profile at start |
| `--no-kill-switch` | `THRONE_KILL_SWITCH=0` | Keep the kill switch off for this run even when `kill_switch` is on; profiles stay refused until `kill_switch` is turned off. For [recovery](#lockout-protection-and-recovery) |
| `--log-format FORMAT` | `THRONE_LOG_FORMAT` | `auto` (default), `journal` or `plain`: see [Logs](#logs) |
| `--api-listen LISTEN` | `THRONE_API_LISTEN` | API listener: `PORT` (on 127.0.0.1), `IPv4:PORT`, `[IPv6]:PORT`, `*:PORT` (every address) or `off`. Default: `127.0.0.1:9095`, unless the settings turn the API on |
| `--api-key-file FILE` | `THRONE_API_KEY_FILE` | Read the API key from this file, which must exist (`thronectl keygen -o FILE` writes one). Default: `config/api.key` in the data directory, created at the first start |
| `--api-admin-lan` | `THRONE_API_ADMIN_LAN=1` | Answer admin requests (imports, settings, backups, restart) from the network too, not only from this device |
| `--api-allow LIST` | `THRONE_API_ALLOW` | Addresses and subnets that may use a non-loopback listener, separated by commas or spaces. An empty list exits with 64; leave the variable unset to allow every address |
| `--debug` | | Debug logging |
| `--service` | | Windows only: run under the Service Control Manager, headless even without `--headless`. Started any other way it exits with 64 |
| `--health` | | [Ask the running Throne how it is doing](#throne---health) |
| `--print-connection` | | [Print `THRONE_ADDR` and `THRONE_KEY_FILE` for thronectl](#connecting-thronectl-and-setting-throne-up) |
| `--check` | | [Check whether this host can run headless Throne](#throne---check) |
| `--help`, `--version` | | |

The yes/no variables take `1`, `true`, `yes`, `on`, `0`, `false`, `no` or `off`; anything else exits
with 64. The `--api-*` options and variables override the API settings saved in the data directory
at every start and are never saved. `--health`, `--print-connection` and `--check` work without
`--headless`, and only one of them at a time, never with `--service`. A link or file on the command
line exits with 64: import it with `thronectl import`.

### Data directory

The first that applies:

1. `--data-dir`, then `THRONE_DATA_DIR`. A relative path counts from the directory Throne starts in.
2. `STATE_DIRECTORY`, which systemd sets for `StateDirectory=`.
3. As root: `/var/lib/throne` on Linux, `/Library/Application Support/Throne` on macOS. On Windows,
   with `--service` or as LocalSystem: `C:\ProgramData\Throne`.
4. The user's configuration directory, which the desktop app also uses: `~/.config/Throne` on Linux,
   `~/Library/Preferences/Throne` on macOS, `%LOCALAPPDATA%\Throne` on Windows. On Linux and macOS
   without a `HOME` this exits with 78: name the directory.

Headless Throne never keeps its data next to the program, as a portable desktop install does: pass
`--data-dir <program directory>` to share such an install's data. On Linux and macOS it creates
missing directories readable only by its user, and run as root it refuses a data directory that
other users can write to (exit 78), since the settings name programs that Throne starts. On Windows,
`C:\ProgramData\Throne` is kept for SYSTEM and Administrators only (see
[Windows notes](#windows-notes)); another directory that ordinary users can write to only gets a
warning in the log.

What is in it:

| Path | |
|---|---|
| `config/throne.db` | Profiles, groups, routes and settings, as in the desktop app |
| `config/api.key` | The API key, readable only by Throne's user |
| `config/logs/throne.log` | This run's log; `crashed-<time>.log` keeps the log of a run that did not end cleanly |
| `config/crashes/` | Crash reports |
| `config/headless-state.json` | Boot resume and crash counters, and how the last run ended |
| `config/hwid` | The [device ID](#device-id), when the system has none |
| `config/.lock`, `config/.instance` | The data directory's lock and the socket `--health` asks. On Windows that is a named pipe instead, not a file |

### Restart modes

`thronectl restart`, a backup restore and SIGHUP restart Throne. `--restart-mode` says how:

| Mode | |
|---|---|
| `exit` | Exit with 75; the service manager starts Throne again |
| `exec` | Start Throne again in the same process (Linux, macOS; the PID stays). On Windows, start a new Throne process and exit with 0. A Windows service always exits |
| `auto` | `exit` when systemd (its notify socket, or the user manager), launchd or a container init started Throne, or as a Windows service; `exec` otherwise, as from a terminal |

The shipped units and the image need `auto` or `exit`. `Throne --check` shows what `auto` picks. In
a terminal, a hang-up of the terminal stops Throne (exit 0) instead of restarting it.

### Exit codes

Throne logs `EXIT <code>: <reason>` before it exits. What the shipped files make of each code:

| Code | Meaning | systemd unit | Docker `unless-stopped` | launchd plist | Windows service |
|---|---|---|---|---|---|
| 0 | Stopped: SIGTERM, SIGINT, `thronectl shutdown`; on Windows a service stop, Ctrl+C, closing the console, signing out or `taskkill` without `/F`. Also a Windows restart outside the service, which has started a new Throne | stays down | starts again, unless `docker stop` | stays down | stays stopped |
| 1 | Start failed, for a reason that may pass: the lock, the instance socket, the database, the core's IPC listener, or an `--api-listen` address that did not appear within 30 seconds | starts again after 3 s | starts again | starts again after 10 s | recovery |
| 3 | Another Throne uses the data directory | stays down | starts again | retried every 10 s | recovery |
| 64 | Bad option, option value or variable, or `--service` not started by the Service Control Manager | stays down | starts again | retried every 10 s | recovery |
| 70 | The core kept failing and Throne gave up, or the core was not usable for 60 seconds | starts again | starts again | starts again | recovery |
| 71 | A profile was connecting for 180 seconds or stopping for 90 | starts again | starts again | starts again | recovery |
| 75 | Restart requested, with `--restart-mode exit` or as a Windows service, or an `exec` restart that could not happen | starts again | starts again | starts again | recovery |
| 78 | Configuration error: the data directory (also no `HOME` to find one, or on Windows a `C:\ProgramData\Throne` that another user owns, that other users can write to, or that is a link), the API key file, the `--api-listen` listener, an unusable database file | stays down | starts again | retried every 10 s | recovery |

The unit has no start limit (`StartLimitIntervalSec=0`). Every `thronectl restart`, restore and
SIGHUP is a new start, and a limit would leave Throne down with no API. Docker waits longer before
each restart in a row. After three starts in a row without a working core, Throne stops exiting
with 70 and 71 and [stays up](#lockout-protection-and-recovery) instead.

For 78 with a database, the log says what to do: move a damaged `config/throne.db` aside together
with its `-wal` and `-shm` files, or put back a copy; make read-only ones writable by Throne's user.

The other modes exit with their own codes:

| Mode | 0 | 1 | 2 | 64 |
|---|---|---|---|---|
| `--health` | healthy | not healthy, or still starting | no Throne answered, or a Windows service refused a prompt that is not elevated | bad option |
| `--check` | no `[FAIL]` line | a `[FAIL]` line | | bad option |
| `--print-connection` | printed both lines | | the API is off or not listening, or its key is not in a file; or a Windows service refused the prompt | bad option |

`--health` and `--print-connection` exit with 78 when they find no data directory (no `HOME`): name it
with `--data-dir`.

## Connecting thronectl and setting Throne up

thronectl reads the address from `THRONE_ADDR` and the key from `THRONE_KEY_FILE` (or `THRONE_KEY`,
or `--key-file`). `Throne --print-connection`, run as Throne's user and for the same data directory,
prints both as `export` lines, never the key itself:

```sh
$ sudo /opt/Throne/Throne --print-connection
export THRONE_ADDR=127.0.0.1:9095
export THRONE_KEY_FILE=/var/lib/throne/config/api.key
```

For a user unit, `eval "$(/opt/Throne/Throne --print-connection)"` sets both. `sudo` drops these variables,
and root's key file is readable only by root, so copy the key for yourself as in the
[quick start](#quick-start), or run `sudo thronectl --key-file /var/lib/throne/config/api.key ...`.

Admin requests (import, groups, settings, backups, restart, shutdown) are answered only from the
device itself, unless Throne runs with `--api-admin-lan`. From another device they get `forbidden`.

**Profiles and subscriptions**

```sh
thronectl import links.txt                     # share links, a subscription body, Clash, sing-box, Xray, WireGuard or OpenVPN
thronectl sub add 'https://sub.example.com/abc' --name Provider
thronectl profiles --group 3
thronectl connect 12                           # or: thronectl connect best --group 3
thronectl tun on
```

The running profile starts again at every start of Throne, with Tun if it was on, until you
`thronectl disconnect`. `--no-resume` turns that off for a run.

**From a desktop Throne.** Set things up in the desktop app, then move them over with a backup:

```sh
# On the desktop: a backup from the settings window, or with its API on:
thronectl backup create -o desktop.thrbackup
scp desktop.thrbackup box:
# On the box:
thronectl backup restore desktop.thrbackup
```

`backup restore` shows the archive and asks first (`--yes` without a terminal). It restores
profiles, routes, OTP accounts and IP lists; add `--parts profiles,routes,otp,ip_lists,settings` to
bring settings too. A settings restore keeps what belongs to this device: the Throne API, the
inbound and ports, Tun, the kill switch, NTP, logging and the paths. Throne then restarts. Through
the API, Throne refuses archives whose profiles would run programs or read files on its device, such
as Extra Core profiles (`invalid_archive`). Archives hold credentials in plain text; delete them
when you are done.

**Settings.** `thronectl settings` lists the settings the API can change, and
`thronectl settings set kill_switch=true log_level=debug` changes them. They include the ones that
`Throne --check` and a `tun_conflict` name: Tun's ranges (`tun_ipv4_cidr`, `tun_ipv6_cidr`), the
core's DNS port (`dns_in_port`), the Clash API (`clash_api_port`, `clash_listen_addr`) and the
sing-box API (`singbox_api_port`). The
[settings table](https://github.com/throneproj/Throne-api/blob/main/spec/api.md#settings) says which
need the profile to start again.

**From another device.** Listen on the network with an allow list, and allow admin requests when
that device manages Throne, such as in `/etc/throne/throne.env`:

```sh
THRONE_API_LISTEN=*:9095
THRONE_API_ALLOW=192.168.1.0/24
THRONE_API_ADMIN_LAN=1
```

Copy the key to that device and set `THRONE_ADDR=<box>:9095`. The kill switch lets the API's replies
out, so clients reach it wherever they connect from.

## Checking and monitoring

### `Throne --check`

Run it as Throne's user, with the flags and variables Throne starts with, before the first start or
when something does not work. It changes nothing (no file, no lock, the database read-only) and runs
beside a running Throne. One line per item, with `fix:` lines where there is a fix:

```text
$ sudo /opt/Throne/Throne --check
[INFO] build: Throne <version>, Qt 6.x, Debian GNU/Linux 12 (bookworm) x86_64, kernel 6.1.0-28-amd64, euid 0, ruid 0
[INFO] data dir /var/lib/throne (root default): does not exist yet; the first start creates it
[ OK ] instance: none (no data dir yet)
...
[INFO] API key: /var/lib/throne/config/api.key is created on the first start
[ OK ] port 127.0.0.1:9095 (the Throne API, the headless default): free
[ OK ] port 127.0.0.1:2080 (the mixed proxy, inbound_port): free
...
result: 0 failed, 0 warnings
```

It covers the build, the data directory, a running instance, the settings, TLS, the core, Tun, the
kill switch, the API key, the ports (API, mixed inbound, DNS port, Clash and sing-box APIs when on),
other headless Thrones and Tun owners on the host, the service manager and restart mode, and the
device ID. Only what keeps Throne from working at all is `[FAIL]`. Missing privileges for Tun or the
kill switch are `[WARN]`, with the `setcap` or `chmod u+s` commands, or `--cap-add NET_ADMIN` and
`--device /dev/net/tun` in a container.

### `Throne --health`

Asks the running Throne over its instance socket, as Throne's user and for the same data directory,
and prints one line of JSON:

```sh
$ sudo /opt/Throne/Throne --health
{"api":"listening","core_restarts":0,"degraded":null,"headless_dialogs":0,"ok":true,"previous_crash":false,"previous_exit":{"at":1760700000,"code":75,"reason":"restart requested"},"ready":true,"state":"running","stuck_s":0}
```

| Field | |
|---|---|
| `ok` | Throne works: it has started, the core is connected, nothing is stuck, the core has not been given up on, and the kill switch is off or works. A kill switch that cannot work as set up, or that `--no-kill-switch` turned off, leaves it true, since a restart cannot fix it. It never depends on a remote server or on a profile running, so restarting Throne when it is false does not loop during a server outage |
| `ready` | Throne has finished starting |
| `state` | `idle`, `connecting`, `running`, `stopping`, or `starting` until Throne has started |
| `degraded` | `null`, or why Throne cannot work as set up: `core_gave_up`, `core_unavailable`, `kill_switch_failed`, `api_unavailable`, `tun_unavailable`, `resume_suppressed`, `vpn_auth_failed`, `vpn_challenge_pending` ([meanings](https://github.com/throneproj/Throne-api/blob/main/spec/api.md#headless-throne)) |
| `stuck_s` | Seconds a profile has been connecting longer than 90 seconds, or stopping longer than 45; then `ok` is false |
| `core_restarts` | How often the core was restarted |
| `headless_dialogs` | Questions skipped for lack of a window; should stay 0. The log names them in `HEADLESS-DIALOG` lines |
| `previous_crash` | The previous run did not end cleanly (a crash, `kill -9`, power loss) |
| `previous_exit` | How the previous run ended, `{code, reason, at}` (seconds since the epoch), or `null`. After a service manager restarted Throne (exit 75, 70, 71) this is where the reason survives |
| `api` | The API listener: `off`, `listening`, `key_invalid`, `failed`, `retrying` (waiting up to 30 seconds for its address) or `address_missing` (its address left every interface) |

In the first moments of a start, the line has only `ok`, `ready`, `state` and `headless_dialogs`.
The Docker image uses `--health` as its health check. A monitoring script that wants more can send
the line `HEALTH` to the socket `config/.instance` in the data directory, such as with
`echo HEALTH | sudo socat -t 3 - UNIX-CONNECT:/var/lib/throne/config/.instance`: the answer adds
`mode`, `pid`, `uptime_s`, `version`, `previous_exit` (code, reason and time of the last exit),
`tun_running`, the full `api` block and the fields of `thronectl status`. A data directory path of
100 bytes or more, or a file system without sockets, moves the socket to `headless-<hash>` in
`/run/throne` (`/var/run/throne` on macOS) for root or `/tmp/throne-<uid>` for other users. On
Windows, use `--health`.

### systemd: readiness, status and watchdog

The unit is `Type=notify`. Throne reports to systemd from its main process:

- `READY=1` once it holds the data directory, the database is open, the API listens or failed, the
  core is connected, the kill switch has settled and the resumed profile is on its way: usually
  within 20 seconds. Until then it extends the start timeout every 20 seconds, and by 2 minutes
  before it opens the database, so `TimeoutStartSec=90` ends only a start that hangs. After three
  starts without a working core it reports ready anyway.
- `STATUS=`, which `systemctl status` shows: `Starting`, `Starting core`, `Core restarting 2/5`,
  `Idle`, `Connecting <profile>`, `Connected <profile>` (with ` (Tun)`), `Stopping <profile>`,
  `Degraded: <reason>`, `Stopping`, `Restarting`.
- `WATCHDOG=1` every third of `WatchdogSec=60` while its main loop runs. A hung main loop gets
  SIGABRT from systemd and is started again.
- `STOPPING=1` when it begins to exit, or `RELOADING=1` before it restarts in place
  (`--restart-mode exec`).

Throne also watches its own progress, with or without systemd: a profile connecting for 180 seconds
or stopping for 90 exits with 71, and a core that has not been usable for 60 seconds exits with 70.

`KillMode=mixed` is required: systemd sends SIGTERM to Throne alone, and Throne stops its core and
the kill switch in order, then exits, usually within seconds (`TimeoutStopSec=45`). A second SIGTERM
or SIGINT kills the core at once. Do not use `systemctl kill`: it signals the core and the kill switch
helper directly. Throne does not pass systemd's variables or its own `THRONE_*` variables on to the
core.

### Logs

Throne writes its log to standard error and to `config/logs/throne.log` in the data directory.
`--log-format` sets the standard error format:

| Format | |
|---|---|
| `auto` | `journal` when standard error is systemd's journal stream, else `plain` |
| `journal` | Each line starts with a syslog priority such as `<4>`, which journald turns into the message's priority; journald adds the time |
| `plain` | Each line starts with the local time |

`journalctl -u throne`, `docker compose logs throne` and the LaunchDaemon's `/Library/Logs/Throne/throne.log`
collect it. A Windows service has no standard error: read `config\logs\throne.log`. Headless Throne
logs in English. `--debug` adds debug lines. Lines to look for: `EXIT <code>: <reason>`,
`Ready <n> ms after launch`, `[API] Listening on ...`, `[Resume] ...`, `[Kill switch] Not active (<code>): ...`,
`[Watchdog] ...`, and `HEADLESS-DIALOG` (a question nobody could answer).

## Without a window

Headless Throne asks nothing. What the desktop app asks in a dialog, it decides or logs:

| Situation | Headless Throne |
|---|---|
| A notice or an error | Writes it to the log |
| Tun needs privileges | Refuses (`needs_privileges`); `Throne --check` says what is missing |
| An Xray profile needs the geoip or geosite files | Downloads them and tries the start once more |
| The kill switch cannot work | Logs why; `degraded` is `kill_switch_failed`, and profiles are refused |
| A VPN profile asks for a one-time code | Answers it from the profile's OTP account, if it has one |
| A VPN profile asks for anything else, such as a web sign-in | Cancels the question when it expires, after 5 minutes at most (`vpn_challenge_pending`); rejected saved credentials are `vpn_auth_failed` |
| A setting needs a program restart | Logs it; it applies after the next restart (`thronectl restart`) |
| The WARP terms of service | Never accepts them |

Not available without a window:

- the system proxy (`system_proxy_unsupported`), the tray, global hotkeys and the editors;
- the built-in updater, and restarting elevated or with Tun: update through the package, the image or
  the installer;
- `throne://` links and file associations: import with `thronectl import`;
- sign-in prompts for VPN profiles, such as SSO, beyond one-time codes from an OTP account;
- WARP registration;
- settings beyond those the API changes, and Extra Core profiles through an API restore.

Set these up in a desktop Throne and bring them with a [backup](#connecting-thronectl-and-setting-throne-up),
within its limits. For a user unit, stop it and open the desktop app: they share the data.

## Lockout protection and recovery

A box without a screen must stay reachable. Headless Throne keeps the API within reach:

- The API is on from the first start, on `127.0.0.1:9095` with a generated key. The `--api-*` flags
  and variables override the saved API settings at every start, so a bad saved listener is fixed
  from the command line.
- A settings restore keeps this device's API, inbound, Tun and kill switch settings.
- The kill switch always lets loopback through, and for an API listener on another address it lets
  out the replies from the API's port (Linux, macOS and Windows).
- When another program holds the default or saved API port, Throne runs on without the API
  (`degraded` is `api_unavailable`) and tries again every 30 seconds. When another Throne's API
  holds it, Throne logs that and leaves it: give this one `--api-listen PORT`. A listener given with
  `--api-listen` that cannot listen exits with 78 instead, so you notice; one whose address is not
  on the computer yet is waited for up to 30 seconds, then exits with 1.
- An empty `api.key`, as a cut-short first start leaves, gets a new key. Any other unusable key file
  keeps the API off; delete it to have a new key created at the next start.
- After 5 failed authentications within a minute from one address, Throne refuses that address for
  60 seconds and logs it. It never refuses loopback or an address where a client authenticated in the
  last 10 minutes.
- On Linux, when Tun starts while the API listens on a network address, Throne warns once if remote
  clients may lose the API: replies to clients outside the bypassed address ranges go into the
  tunnel unless `tun_auto_redirect` is on, and strict route without `tun_ipv6` drops IPv6 clients.
- When Throne or its core stopped within a minute of resuming the same profile at each of the last
  three starts, Throne does not resume it (`resume_suppressed`). Start it once with thronectl; after
  it has run for a minute, this clears.
- After three starts in a row in which the core never stayed connected for a minute, Throne no longer
  exits with 70 or 71. It stays up with the API (`degraded` is `core_gave_up`) and tries the core again
  every 5 minutes, until a core has stayed connected for a minute.
- When the core cannot take on the kill switch's group (GID 1950896719, which needs CAP_SETGID and
  that GID mapped, as rootless and user-namespaced containers lack), the core exits with 86. Throne
  does not count that as a crash: it runs the core without the group, the kill switch fails closed
  with code `unsupported` and keeps blocking, and profiles are refused (`kill_switch_inactive`) until
  `kill_switch` is turned off.

**Recovery.** If saved settings still lock you out, such as a kill switch that ends your session or a
Tun that takes the API's replies, start Throne once with `--no-resume --no-kill-switch`: no profile
starts, the kill switch stays off for that run (profiles stay refused until `kill_switch` is off),
and thronectl on the same device can fix the settings. With the systemd unit:

```sh
printf 'THRONE_RESUME=0\nTHRONE_KILL_SWITCH=0\n' | sudo tee -a /etc/throne/throne.env >/dev/null
sudo systemctl restart throne
thronectl settings set kill_switch=false        # or: thronectl tun off
# remove the two lines from /etc/throne/throne.env again, then:
sudo systemctl restart throne
```

In Docker, set the two variables in `environment:` and run `docker compose up -d`; on macOS, add them
to the plist's `EnvironmentVariables` and bootstrap it again; for the Windows service, see
[Windows notes](#windows-notes).

## One Tun per computer

Tun's interface (`throne-tun`), the DNS port (`dns_in_port`, 5533) and Tun's routes belong to the
whole computer, so one Throne at a time can run Tun on it. Headless Throne turns Tun on only when no
other `throne-tun` interface exists (Linux and Windows), no interface holds the Tun address
(`tun_ipv4_cidr`, `172.19.0.1/24` by default) and, while no profile runs, the DNS port is free.
Otherwise `thronectl tun on` fails with `tun_conflict`, `thronectl status` says what holds Tun, and a
Tun that cannot come back at start leaves `degraded` at `tun_unavailable`.

The Tun address and the DNS port are API settings. When a network of the computer, such as a Docker
bridge, uses the Tun address, move Tun to a range no network there uses with
`thronectl settings set tun_ipv4_cidr=172.29.0.1/24`; when another program holds the DNS port, give
Throne another one with `thronectl settings set dns_in_port=PORT`. Then run `thronectl tun on`
again, and `thronectl connect` when Tun held the profile back at start (`tun_unavailable`).

Headless Throne also marks that it runs: in `/run/throne` (`/var/run/throne` on macOS) as root, in
`$XDG_RUNTIME_DIR/throne` for a user, and on Windows as `Global\ThroneHeadless`, or as
`Local\ThroneHeadless`, seen only in the same Windows session, when it is neither elevated nor a
service. A desktop Throne on the same computer then warns at start, refuses Tun while the headless
one runs, and does not update itself while it runs from the same installation. A second headless
Throne with another data directory still runs; the checks above keep the two from running Tun at
once.

## Device ID

Subscriptions can send a device ID (`x-hwid`). On Linux it comes from `/etc/machine-id`, else
`/var/lib/dbus/machine-id`. A system without either, such as a container, gets a generated ID in
`config/hwid` at the first start, which stays with the data directory. Do not put a machine-id into
an image you share: every container of it would send the same ID. macOS uses the hardware UUID,
Windows the MachineGuid.

## Windows notes

**A GUI program.** `Throne.exe` is a Windows GUI program, so a Command Prompt does not wait for it.
Throne writes its output to the console of the program that started it; to wait for it and read its
exit code:

```bat
start "" /wait "C:\Program Files\Throne\Throne.exe" --health --data-dir C:\ProgramData\Throne
echo %ERRORLEVEL%
```

```powershell
$p = Start-Process 'C:\Program Files\Throne\Throne.exe' -ArgumentList '--health', '--data-dir', 'C:\ProgramData\Throne' -Wait -PassThru
$p.ExitCode
```

**The service from a prompt.** Run `--health`, `--print-connection`, `--check` and thronectl from an
elevated prompt, and give the client modes `--data-dir C:\ProgramData\Throne`: an administrator is
not LocalSystem, so its default data directory is another. Only Administrators and SYSTEM can open
the service's pipe and read its key file: from a prompt that is not elevated, `--health` logs
`[Instance] Not talking to <pipe>: access denied` and exits with 2. A client also refuses to talk to a pipe that runs as another
user than its own or SYSTEM. `--print-connection` prints POSIX `export` lines; in PowerShell, set the
values yourself:

```powershell
$env:THRONE_ADDR = '127.0.0.1:9095'
$env:THRONE_KEY_FILE = 'C:\ProgramData\Throne\config\api.key'
thronectl status
```

**The data folder.** The service keeps `C:\ProgramData\Throne` for SYSTEM and Administrators only:
it creates the folder so, and sets that access again at every start, for the files in it too. If the
folder already exists but belongs to another user, lets other users write to it (one made by hand under
`C:\ProgramData` does), or is a link or junction, or holds a file or folder that is, the service exits with 78 and names it: remove it. A `--data-dir` that ordinary users can write to only gets a warning in the log.

**From a console.** For a test, `start "" /wait "C:\Program Files\Throne\Throne.exe" --headless`.
Ctrl+C stops it gracefully, a second Ctrl+C kills the core at once, and closing the console window
stops it too, within the few seconds Windows allows. `taskkill /PID <pid>` without `/F`, signing out
and shutting down also stop it gracefully. Outside the service, the data directory is
`%LOCALAPPDATA%\Throne` (`C:\ProgramData\Throne` when it runs as LocalSystem), and a restart starts a
new Throne process and exits with 0, so `start /wait` returns while the new one runs on.

**Recovery** with the service: `sc.exe stop Throne`, then from an elevated prompt
`start "" /wait "C:\Program Files\Throne\Throne.exe" --headless --data-dir C:\ProgramData\Throne --no-resume --no-kill-switch`,
fix the settings with thronectl from another elevated prompt, stop it with Ctrl+C and
`sc.exe start Throne`.

## Troubleshooting

| Symptom | Cause and fix |
|---|---|
| The unit fails at once; the log says `refusing to run as root with /var/lib/throne` | Other users can write to the data directory: `sudo chmod go-w /var/lib/throne /var/lib/throne/config` |
| Exit 3: `... is in use by ...` | Another Throne holds the data directory: the desktop app, a user unit, or a second service |
| The desktop app says `Throne is running in the background` | A headless Throne (a user unit) uses its data directory: `systemctl --user stop throne`, or use thronectl |
| `degraded: api_unavailable`; `[API] Not listening on 127.0.0.1:9095` | Another program holds port 9095. Throne retries every 30 seconds; `--api-listen PORT` picks another |
| `--print-connection`: `The API key is not in a file` | The settings of the data directory turned the API on with a key of their own, as the desktop app does. Use its key (Preferences, Integration Settings, Throne API, Copy for scripts) as `THRONE_KEY`, or start Throne with `--api-key-file` |
| `--health` or `--print-connection`: `No headless Throne answered` | Run it as Throne's user, with the same `--data-dir` |
| Windows: `[Instance] Not talking to ...: access denied` | The service's pipe: run it from an elevated prompt, with `--data-dir C:\ProgramData\Throne` |
| thronectl: `Throne closed the connection without a response` | A wrong key, or the address is refused for 60 seconds after 5 failed attempts, as by another client with an old key |
| thronectl: `forbidden` | An admin request from another device: start Throne with `--api-admin-lan` |
| `thronectl tun on`: `needs_privileges` | Run `Throne --check`; see [Privileges on Linux](#privileges-on-linux) |
| `tun_conflict`: `the Tun address 172.19.0.1 is already used by the network interface br-...` | A Docker network (or another LAN) uses Throne's Tun range. Move Tun to an unused range with `thronectl settings set tun_ipv4_cidr=172.29.0.1/24` (any range no network interface here uses), then `thronectl tun on` again; or give that Docker network another subnet |
| `tun_conflict`: `port 5533 on 127.0.0.1 (the core's DNS server, dns_in_port) is in use` | Another program holds the DNS port: `thronectl settings set dns_in_port=PORT` with a free port, then `thronectl tun on` again |
| Profiles refused: `kill_switch_inactive` | The kill switch is on but cannot work; `kill_switch_error` in `thronectl --json status` and the log line `[Kill switch] Not active (<code>)` say why (`privilege`, `unsupported`, `config`, `overridden`). `thronectl settings set kill_switch=false` ends it |
| `degraded: resume_suppressed` | The resumed profile crashed Throne or its core three starts in a row; start it with `thronectl connect <id>` once it is fixed |
| `degraded: core_gave_up` | The core keeps failing, and Throne [stays up](#lockout-protection-and-recovery) and retries it every 5 minutes. The give-up line in the log ends with the core's last error |
| `previous_crash: true` | The last run did not end cleanly; its log is `config/logs/crashed-<time>.log` |
| Docker: the published port 2080 does not answer | `thronectl settings set inbound_address=0.0.0.0` and start the profile again |
| macOS: Throne starts every 10 seconds and exits | A configuration error that launchd retries: read `/Library/Logs/Throne/throne.log` |
| Windows: the service stops at once | `sc.exe query Throne` shows the exit code as `SERVICE_EXIT_CODE`; the log is in `C:\ProgramData\Throne\config\logs` |

## CI coverage

The build workflow ([`.github/workflows/build.yml`](../.github/workflows/build.yml)) tests headless
Throne on GitHub's runners with the builds and packages of the same run; the end-to-end tests are in
[`script/ci/e2e`](../script/ci/e2e). Run the workflow with any tag and an empty `publish` to test a
change. The `e2e-*` jobs only report: a failure does not hold back a release. Each one runs when
the build it tests exists, even if another build of the run failed; the two that test packages need
every build, as `pack` does. A failed `e2e-*` job uploads its logs as the artifact `<job>-logs`,
such as `e2e-macos-logs`. Local fixtures stand in
for proxy servers: an HTTP server and a SOCKS5 proxy that logs every connection, so a test sees
that the traffic went through Throne. Tun and the kill switch run only in containers with their own
network, since on the runner itself they would cut the runner off.

| Job | Runs on | What it tests |
|---|---|---|
| `headless-smoke` | Ubuntu 22.04 | The linux-amd64 build as a user and as a systemd notify unit: `--check`, `--health`, a profile through thronectl, the watchdog, a SIGHUP restart, a clean stop |
| `package-smoke` | Debian 12 containers, amd64 and arm64 | The amd64 deb and the arm64 system-qt deb install, their programs load, `--version` |
| `docker-image` | Ubuntu, arm64 under QEMU | The image for both platforms; the amd64 one starts healthy, answers thronectl and stops with 0 |
| `e2e-linux-packages` | Ubuntu 24.04, root | The amd64 deb: apt install, the unit installed disabled, ready within 90 seconds, the watchdog, `--health`, a profile, `thronectl restart` and SIGHUP restarts that resume it, `systemctl stop` with exit 0 and the core stopped by Throne, remove and purge |
| `e2e-linux-scenarios` | Ubuntu 24.04, a container per scenario | Exit codes; signals; a profile; Tun; the kill switch and its API passthrough; the kill switch without CAP_SETGID; staying up after a core that keeps failing; the resume guard; a Tun conflict; `--no-kill-switch`; the Docker image's health check and `docker stop` |
| `e2e-linux-arm64` | Ubuntu 24.04 arm64, Debian bookworm container | The arm64 system-qt deb as on Raspberry Pi OS bookworm: `--health`, a profile, SIGTERM; apt refuses the bundled arm64 deb, which needs a newer glibc |
| `e2e-macos` | macOS arm64, the image the darwin-arm64 build comes from | The darwin-arm64 app as a LaunchDaemon: `--check`, `--health`, a profile, a SIGHUP restart by launchd that resumes it, `bootout`; in a terminal SIGTERM, a SIGHUP restart in place, `--restart-mode exit`, a second SIGINT that cuts a stuck teardown short |
| `e2e-windows` | Windows Server, administrator | The windows-amd64 build as a service: install, the data folder's access, `--health`, `--check`, a profile, `thronectl restart` through the service recovery, `sc.exe stop` with exit 0, uninstall; a console run stopped by `taskkill` |

Not covered; test these by hand:

- Raspberry Pi hardware. The arm64 runner is a cloud VM, and Debian bookworm in a container stands in
  for Raspberry Pi OS: the same glibc and Qt, not its kernel or its own packages.
- Reboots: the start at boot (`systemctl enable`, the delayed Windows service start, `RunAtLoad`) and
  power loss.
- Tun and the kill switch on a whole computer: on Linux they run only in containers, on the macOS
  and Windows runners not at all (utun, pf, wintun, WFP).
- The window app. The runners have no display, so neither the desktop app nor how it behaves beside
  a headless Throne is tested.
- The rpm packages, the amd64 system-qt deb, the macOS amd64 and legacy builds, the Windows arm64
  and legacy builds, and the arm64 image beyond `--version`.
- Real servers, subscriptions and VPN profiles.
