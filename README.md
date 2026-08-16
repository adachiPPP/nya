# nya

a package manager written from scratch in C. nya reads and writes the same
databases, package format, and cache as pacman, so packages installed with nya
can be removed or upgraded with pacman and vice versa. it does **not** wrap or
call pacman — every operation (database parsing, dependency resolution,
download, extraction, scriptlets, hooks, removal) is implemented natively.

## Info
the package manager can automatically find packages **IF ENABLED IN THE CONFIG** from flatpak, aur, hosts, and pacman.

the install priority = **pacman** -> **hosts** <-> **aur** -> **flatpak**
(hosts and aur are swappable: set `aurfirst = true` in `[options]` for
pacman -> aur -> hosts -> flatpak)

*Note: you can search nix packages but the whole purpose of nix is being configured from flakes so you can use nya to search for nix packages not to install them*

## Building

dependencies: `libcurl`, `zlib`, `libzstd`, `liblzma` (dev headers) and a C
compiler.

```sh
make            # builds ./nya
sudo make install   # installs to /usr/local/bin/nya
```

## First use

on the first run nya generates its config file automatically from
`/etc/pacman.conf` (repositories, mirrorlists, and options are imported):

```sh
nya search firefox          # generates /etc/nya.conf from pacman.conf on first run
```

you can re-import the repository layout at any time with:

```sh
sudo nya --read-paconfig            # read /etc/pacman.conf
nya --read-paconfig /path/to/pacman.conf
```

the generated config looks like pacman.conf, with nya-specific options
commented out until you enable them:

```ini
[options]
RootDir = /
DBPath = /var/lib/pacman/
CacheDir = /var/cache/pacman/pkg/
LogFile = /var/log/nya.log
Architecture = auto
ParallelDownloads = 5
Color
#aur = true
#nix = true
#searchaur = true
#searchnix = true
#searchflatpak = true
#sudobin = sudo        # auto-elevation binary (sudo or doas)
#hostsrepo = https://adachippp.github.io/nya-hosts
#aurfirst = true       # install fallback: repos -> aur -> hosts -> flatpak

[core]
Include = /etc/pacman.d/mirrorlist

[extra]
Include = /etc/pacman.d/mirrorlist
```

config discovery order: `--config <path>`, `$NYA_CONF`, `./nya.conf`,
`~/.config/nya/nya.conf`, `/etc/nya.conf`.

## Usage

nya accepts pacman-style flags and plain English commands:

```sh
sudo nya install firefox kitty      # install multiple packages (like -S)
sudo nya -S firefox kitty
nya search firefox                  # search repos (like -Ss)
nya -Ss '^vim-'
nya info firefox                    # repo info (like -Si)
nya -Qi firefox                     # installed package info
sudo nya remove firefox             # remove (like -R)
sudo nya remove -s firefox          # remove + unneeded deps (like -Rs)
sudo nya remove -n firefox          # don't save config files
sudo nya update                     # refresh databases (like -Sy)
sudo nya upgrade                    # full system upgrade (like -Syu)
sudo nya -Syu
nya list                            # installed packages (like -Q)
nya list --foreign                  # packages not in repos (like -Qm)
nya files firefox                   # files of an installed package (like -Ql)
nya check                           # verify installed files (like -Qk)
nya check --check                   # deeper checksum verification (-Qkk)
nya owns /usr/bin/firefox           # which package owns a file (like -Qo)
sudo nya clean                      # remove cached packages (like -Sc)
sudo nya clean --all                # empty the cache (like -Scc)
sudo nya -U ./myapp.pkg.tar.zst     # install a package file (like -U)
```

every sync operation is available both ways (`-S`, `-Ss`, `-Si`, `-Sy`, `-Su`,
`-Syu`, `-Sw`, `-Sc`, `-Scc`, `-Q`, `-Qs`, `-Qi`, `-Ql`, `-Qk`, `-Qo`, `-Qm`,
`-Qd`, `-Qe`, `-Qt`, `-Qu`, `-R`, `-Rs`, `-Rn`, `-Rc`, `-Ru`, `-U`).

common options: `--noconfirm`, `--needed`, `--asdeps`, `--overwrite`,
`--root <dir>`, `--dbpath <path>`, `--cachedir <dir>`, `--ignore=pkg`,
`--ignoregroup=grp`, `--config <path>`, `--color`/`--nocolor`, `--verbose`,
`--quiet`.

root-requiring operations (`install`, `remove`, `update`, ...) automatically
re-run themselves through `sudobin` (default `sudo`, set `sudobin = doas` in
`[options]`) when run as a normal user, just like yay.

## pacman compatibility

- **local database** (`/var/lib/pacman/local/<name>-<version>/`): writes
  `desc`, `files`, `mtree`, and `install` in the exact pacman format
  (`%NAME%`, `%VERSION%`, `%INSTALLDATE%`, `%REASON%`, `%FILES%`, `%BACKUP%`,
  ...). pacman can remove, query, and check packages installed by nya.
- **sync databases** (`/var/lib/pacman/sync/<repo>.db`): gzip-compressed tar
  archives with per-package `desc`/`files`/`depends`/`mtree`, downloaded and
  parsed natively. `.db` files compressed with zstd or xz are detected too.
- **package format**: `.pkg.tar.zst` (also `.pkg.tar.xz`/`.gz`/plain tar),
  including `.PKGINFO`, `.MTREE`, `.INSTALL`, symlinks, hardlinks, and
  ownership/perms.
- **shared cache**: by default nya downloads into `/var/cache/pacman/pkg`, so
  both tools reuse the same downloaded packages.
- **lifecycle**: dependency resolution (including `provides` and soname deps),
  conflict/replace handling, `pre_install`/`post_upgrade`/... scriptlets,
  `.pacnew` on install/upgrade and `.pacsave` on removal, `HoldPkg`,
  `IgnorePkg`/`IgnoreGroup`, `NoUpgrade`, pre/post transaction hooks from
  `/usr/share/libalpm/hooks` and `/etc/pacman.d/hooks`, parallel downloads, and
  SHA-256 verification of downloaded packages.
- **Locking**: nya takes the same `/var/lib/pacman/db.lck` lock as pacman, so
  the two never run a transaction at the same time.

## AUR

Enable with `aur = true` in `[options]`, then:

```sh
nya aur search firefox
nya aur info yay
sudo nya aur install yay
sudo nya install <aur-only-name>   # falls back through hosts then AUR when not in repos
```

AUR packages are built with `makepkg` (install `base-devel`) and the resulting
`.pkg.tar.zst` is installed through nya itself.

## ni(ck)x

enable with `nix = true` in `[options]`, then:

```sh
nya nix search firefox
nya nix info blender
nya nix update
```

`nya nix search` evaluates `nix search nixpkgs` (requires the `nix` package
manager) and prints matches with versions and descriptions. nix packages are
installed with nix itself; nya provides the search.

## flatpak

```sh
nya -fp search vlc
nya -fp install org.videolan.VLC
nya -fp remove org.videolan.VLC
nya -fp list
nya -fp update
```

these shell out to the `flatpak` CLI (requires flatpak installed). `nya
install` and `nya remove` also fall back to flatpak for app IDs that aren't in
any repo, host, or the AUR, and `nya update` runs `flatpak update` at the end.

## nya-hosts

hosts are build-from-source recipes hosted in a git repo (default
`https://adachippp.github.io/nya-hosts`, change it with
`hostsrepo = <url-or-dir>` in `[options]`):

```sh
sudo nya host install mocktail      # fetch recipe, clone, build, install
sudo nya host remove mocktail       # remove everything it installed
```

a recipe file is named after the package and looks like:

```ini
[repo]
https://github.com/komaruworld/mocktail.git

[folder]
mocktail

[instructions]
make build

[binary]
./build/mocktail

[version]
1.0.0

[desc]
Play Roblox on Linux

[app]
./packaging
```

- `[repo]` is required; everything else is optional.
- nya clones into `~/.cache/nya/hosts-build/<name>/`, runs `[instructions]`, and
  picks the best install strategy automatically:
  - **project install rules**: if the project has `make install` or a CMake build
    dir, nya stages it with `DESTDIR` and merges the result (like a PKGBUILD);
  - **bundle**: if the binary sits next to shared libraries or helpers, the
    whole folder goes to `/usr/lib/nya/<name>/` with a symlink in `/usr/bin`;
  - **single file**: plain binaries are copied straight to `/usr/bin`;
  - with no `[binary]`, the executable is auto-detected in common build dirs.
- the app's **logo** is installed into `/usr/share/icons/hicolor/` automatically
  (files named `logo.*`/`icon.*` or after the app, or images inside
  `icons/`/`logo/`/`branding/` folders).
- `[app]` links the app's `.desktop` file into `/usr/share/applications/` and
  installs its icons, so it shows up in the application menu.
- hosts packages are tracked in the local database, so `nya remove <name>` (or
  `pacman -R`) cleans them up completely, and re-installing prints an upgrade.

`nya install <name>` also falls back to nya-hosts (after repos, before AUR)
when a package isn't found anywhere else.

## Tests

```sh
make test
```

The test suite builds a fake repository and packages, then exercises
refresh/search/info/install/remove/upgrade/`-U`, dependency resolution,
scriptlets, `.pacnew`/`.pacsave` behavior, database format compatibility,
`--read-paconfig`, and first-use config generation against a `file://` mirror
— no network or root required.
