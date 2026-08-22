#!/usr/bin/env bash
set -u

ROOT=/tmp/nya-test
REPO=$ROOT/repo
CFG=$ROOT/nya-test.conf
NYA=${NYA:-./nya}
CFGARG="--config $CFG"

fail() { echo "FAIL: $1"; exit 1; }
ok() { echo "ok: $1"; }

rm -rf "$ROOT"
mkdir -p "$REPO" "$ROOT/build"

make_mtree() {
	local d=$1
	shift
	local f
	for f in "$@"; do
		local size sha
		size=$(stat -c%s "$d/$f")
		sha=$(sha256sum "$d/$f" | awk '{print $1}')
		echo "./$f time=1700000000.0 size=$size sha256digest=$sha type=file"
	done
}

pkg_hello() {
	local ver=$1
	local d=$ROOT/build/hello-$ver
	mkdir -p "$d/usr/bin" "$d/etc" "$d/usr/share/doc/hello"
	cat > "$d/usr/bin/hello" <<'EOF'
#!/bin/sh
echo "hello from nya test"
EOF
	chmod 755 "$d/usr/bin/hello"
	cp "$d/usr/bin/hello" "$d/usr/bin/hello-su"
	chmod 4755 "$d/usr/bin/hello-su"
	printf 'HelloConf %s\n' "${ver%%.*}" > "$d/etc/hello.conf"
	printf 'README %s\n' "$ver" > "$d/usr/share/doc/hello/README"
	mkdir -p "$d/usr/share/doc/hello/this-is-a-very-long-directory-name-that-forces-pax-or-gnu-longname-encoding-in-the-tar-archive-1234567890"
	printf 'long path file\n' > "$d/usr/share/doc/hello/this-is-a-very-long-directory-name-that-forces-pax-or-gnu-longname-encoding-in-the-tar-archive-1234567890/readme-with-a-very-long-name-that-exceeds-100-bytes-total-and-triggers-extended-tar-headers.txt"
	cat > "$d/.PKGINFO" <<EOF
pkgname = hello
pkgbase = hello
pkgver = $ver
pkgdesc = Test hello package
url = https://example.com
builddate = 1700000000
packager = nya test
size = 1024
arch = x86_64
license = MIT
depend = liba
backup = etc/hello.conf
EOF
	cat > "$d/.INSTALL" <<'EOF'
post_install() {
  mkdir -p usr/share/nya
  echo "$1" > usr/share/nya/installed-ver
}
post_upgrade() {
  mkdir -p usr/share/nya
  echo "$1 $2" > usr/share/nya/upgraded
}
pre_remove() {
  mkdir -p usr/share/nya
  echo removed > usr/share/nya/removed-marker
}
post_remove() {
  echo post-removed > usr/share/nya/post-removed-marker
}
EOF
	{
		echo "#mtree"
		echo "/set type=file uid=0 gid=0 mode=644"
		echo "./usr time=1700000000.0 type=dir"
		echo "./usr/bin time=1700000000.0 type=dir"
		make_mtree "$d" usr/bin/hello | sed 's/type=file/type=file mode=755/'
		make_mtree "$d" usr/bin/hello-su | sed 's/type=file/type=file mode=4755/'
		echo "./etc time=1700000000.0 type=dir"
		make_mtree "$d" etc/hello.conf
		echo "./usr/share time=1700000000.0 type=dir"
		echo "./usr/share/doc time=1700000000.0 type=dir"
		echo "./usr/share/doc/hello time=1700000000.0 type=dir"
		make_mtree "$d" usr/share/doc/hello/README
	} > "$d/.MTREE"
	( cd "$d" && tar --zstd -cf "$REPO/hello-$ver-x86_64.pkg.tar.zst" . )
}

pkg_liba() {
	local d=$ROOT/build/liba-1.0-1
	mkdir -p "$d/usr/lib" "$d/usr/lib64/liba"
	printf 'liba v1\n' > "$d/usr/lib/liba.so.1.0"
	ln -s liba.so.1.0 "$d/usr/lib/liba.so"
	printf 'data\n' > "$d/usr/lib64/liba/data.txt"
	cat > "$d/.PKGINFO" <<EOF
pkgname = liba
pkgbase = liba
pkgver = 1.0-1
pkgdesc = Test library A
url = https://example.com
builddate = 1700000000
packager = nya test
size = 2048
arch = x86_64
license = MIT
provides = liba
EOF
	( cd "$d" && tar --zstd -cf "$REPO/liba-1.0-1-x86_64.pkg.tar.zst" . )
}

pkg_kitty() {
	local d=$ROOT/build/kitty-1.0-1
	mkdir -p "$d/usr/bin"
	cat > "$d/usr/bin/kitty" <<'EOF'
#!/bin/sh
echo "meow"
EOF
	chmod 755 "$d/usr/bin/kitty"
	cat > "$d/.PKGINFO" <<EOF
pkgname = kitty
pkgbase = kitty
pkgver = 1.0-1
pkgdesc = Test terminal emulator
url = https://example.com
builddate = 1700000000
packager = nya test
size = 512
arch = x86_64
license = MIT
EOF
	( cd "$d" && tar --zstd -cf "$REPO/kitty-1.0-1-x86_64.pkg.tar.zst" . )
}

write_desc() {
	local name=$1 ver=$2 sha=$3 extra=$4
	local d=$ROOT/build/db/$name-$ver
	mkdir -p "$d"
	cat > "$d/desc" <<EOF
%NAME%
$name

%BASE%
$name

%VERSION%
$ver

%DESC%
Test $name package

%URL%
https://example.com

%ARCH%
x86_64

%BUILDDATE%
1700000000

%PACKAGER%
nya test

%SIZE%
1024

%CSIZE%
512

%SHA256SUM%
$sha

%LICENSE%
MIT

$extra
EOF
}

write_files() {
	local name=$1 ver=$2
	local d=$ROOT/build/db/$name-$ver
	cat > "$d/files" <<'EOF'
%FILES%
EOF
	local f
	for f in "${@:3}"; do
		echo "$f" >> "$d/files"
	done
	echo "" >> "$d/files"
}

build_db() {
	rm -rf "$ROOT/build/db"
	mkdir -p "$ROOT/build/db"
	local sha name ver pkg
	for pkg in "$@"; do
		name=${pkg%%:*}
		ver=${pkg##*:}
		sha=$(sha256sum "$REPO/$name-$ver-x86_64.pkg.tar.zst" | awk '{print $1}')
		if [ "$name" = "hello" ]; then
			write_desc "$name" "$ver" "$sha" $'%DEPENDS%\nliba'
		elif [ "$name" = "liba" ]; then
			write_desc "$name" "$ver" "$sha" $'%PROVIDES%\nliba'
		else
			write_desc "$name" "$ver" "$sha" ""
		fi
		if [ "$name" = "hello" ]; then
			write_files "$name" "$ver" usr/ usr/bin/ usr/bin/hello usr/bin/hello-su etc/ etc/hello.conf usr/share/ usr/share/doc/ usr/share/doc/hello/ usr/share/doc/hello/README usr/share/doc/hello/this-is-a-very-long-directory-name-that-forces-pax-or-gnu-longname-encoding-in-the-tar-archive-1234567890/ usr/share/doc/hello/this-is-a-very-long-directory-name-that-forces-pax-or-gnu-longname-encoding-in-the-tar-archive-1234567890/readme-with-a-very-long-name-that-exceeds-100-bytes-total-and-triggers-extended-tar-headers.txt
			echo "%BACKUP%" >> "$ROOT/build/db/$name-$ver/files"
			echo "etc/hello.conf" >> "$ROOT/build/db/$name-$ver/files"
		elif [ "$name" = "liba" ]; then
			write_files "$name" "$ver" usr/ usr/lib/ usr/lib/liba.so usr/lib/liba.so.1.0 usr/lib64/ usr/lib64/liba/ usr/lib64/liba/data.txt
		else
			write_files "$name" "$ver" usr/ usr/bin/ usr/bin/kitty
		fi
	done
	( cd "$ROOT/build/db" && tar -czf "$REPO/extra.db" . )
}

cat > "$CFG" <<EOF
[options]
RootDir = $ROOT/root
DBPath = var/lib/pacman
CacheDir = var/cache/pacman/pkg
LogFile = $ROOT/nya.log
Architecture = x86_64
ParallelDownloads = 4
Color
searchhost = false

[extra]
Server = file://$REPO
EOF

mkdir -p "$ROOT/root/usr/share/libalpm/hooks"
cat > "$ROOT/root/usr/share/libalpm/hooks/10-test-path.hook" <<'EOF'
[Trigger]
Operation = Install
Operation = Upgrade
Type = Path
Target = etc/hello.conf

[Action]
When = PostTransaction
Exec = touch /tmp/nya-test/root/var/run-nya-path-hook
EOF
cat > "$ROOT/root/usr/share/libalpm/hooks/20-test-targets.hook" <<'EOF'
[Trigger]
Operation = Install
Operation = Upgrade
Type = Path
Target = etc/hello.conf

[Action]
Description = Dumping hook targets
When = PostTransaction
Exec = /bin/sh -c 'cat > /tmp/nya-test/root/var/run-nya-hook-stdin.txt'
NeedsTargets
EOF
cat > "$ROOT/root/usr/share/libalpm/hooks/30-test-noon-targets.hook" <<'EOF'
[Trigger]
Operation = Install
Operation = Upgrade
Type = Path
Target = etc/hello.conf

[Action]
Description = Reader without NeedsTargets
When = PostTransaction
Exec = /bin/sh -c 'cat > /tmp/nya-test/root/var/run-nya-hook-nostdin.txt'
EOF

pkg_hello 1.0-1
pkg_liba
pkg_kitty
build_db hello:1.0-1 liba:1.0-1 kitty:1.0-1

echo "== refresh =="
$NYA $CFGARG -Sy --noconfirm || fail "refresh"
ok "refresh"

echo "== search =="
$NYA $CFGARG search hello | grep -q "extra/hello 1.0-1" || fail "search hello"
$NYA $CFGARG -Ss hello | grep -q "extra/hello" || fail "-Ss hello"
ok "search"

echo "== search multiple terms (AND) =="
$NYA $CFGARG search test hello | grep -q "extra/hello" || fail "AND search missed hello"
$NYA $CFGARG search test hello | grep -q "liba" && fail "AND search matched liba (should require all terms)"
$NYA $CFGARG search hello liba | grep -qE "extra/hello|extra/liba" && fail "AND search matched disjoint terms"
ok "search AND"

echo "== info =="
$NYA $CFGARG -Si hello | grep -q "Version         : 1.0-1" || fail "-Si hello"
$NYA $CFGARG info hello | grep -q "Depends On" || fail "nya info hello"
ok "info"

echo "== install with dependency =="
$NYA $CFGARG install hello --noconfirm || fail "install hello"
[ -f "$ROOT/root/usr/bin/hello" ] || fail "hello binary missing"
[ -f "$ROOT/root/usr/share/doc/hello/this-is-a-very-long-directory-name-that-forces-pax-or-gnu-longname-encoding-in-the-tar-archive-1234567890/readme-with-a-very-long-name-that-exceeds-100-bytes-total-and-triggers-extended-tar-headers.txt" ] || fail "long path (pax/longname) not extracted"
[ -f "$ROOT/root/var/run-nya-path-hook" ] || fail "path-type hook did not run"
grep -q "^etc/hello.conf$" "$ROOT/root/var/run-nya-hook-stdin.txt" || fail "NeedsTargets hook did not receive targets on stdin"
[ -f "$ROOT/root/var/run-nya-hook-nostdin.txt" ] || fail "non-NeedsTargets hook did not run"
[ ! -s "$ROOT/root/var/run-nya-hook-nostdin.txt" ] || fail "non-NeedsTargets hook should get empty stdin"
[ "$(stat -c %a "$ROOT/root/usr/bin/hello-su")" = "4755" ] || fail "setuid bit not preserved on extract"
[ -f "$ROOT/root/usr/lib/liba.so" ] || fail "liba symlink missing"
[ -f "$ROOT/root/usr/lib/liba.so.1.0" ] || fail "liba file missing"
[ -f "$ROOT/root/usr/share/nya/installed-ver" ] || fail "post_install scriptlet did not run"
grep -q "1.0-1" "$ROOT/root/usr/share/nya/installed-ver" || fail "scriptlet version arg wrong"
$NYA $CFGARG list | grep -q "hello 1.0-1" || fail "list hello"
$NYA $CFGARG list | grep -q "liba 1.0-1" || fail "liba not installed as dep"
ok "install with dep"

echo "== local db format (pacman compat) =="
[ -f "$ROOT/root/var/lib/pacman/local/hello-1.0-1/desc" ] || fail "local desc missing"
grep -q "%NAME%" "$ROOT/root/var/lib/pacman/local/hello-1.0-1/desc" || fail "desc has no %NAME%"
grep -q "usr/bin/hello" "$ROOT/root/var/lib/pacman/local/hello-1.0-1/files" || fail "files db missing entry"
grep -q "etc/hello.conf" "$ROOT/root/var/lib/pacman/local/hello-1.0-1/files" || fail "backup missing in files db"
[ -f "$ROOT/root/var/lib/pacman/local/hello-1.0-1/mtree" ] || fail "mtree not saved"
[ -f "$ROOT/root/var/lib/pacman/local/hello-1.0-1/install" ] || fail "install script not saved"
grep -q "%REASON%" "$ROOT/root/var/lib/pacman/local/hello-1.0-1/desc" || fail "no REASON in desc"
grep -q "^1$" <(grep -A1 "%REASON%" "$ROOT/root/var/lib/pacman/local/liba-1.0-1/desc" | tail -1) || fail "liba reason should be dependency"
ok "local db format"

echo "== files/check/owns =="
$NYA $CFGARG files hello | grep -q "usr/bin/hello" || fail "files hello"
$NYA $CFGARG check --noconfirm 2>/dev/null | grep -q "no problems found" || fail "check"
$NYA $CFGARG -Qk hello >/dev/null || fail "-Qk"
$NYA $CFGARG -Qkk hello >/dev/null || fail "-Qkk deep check"
$NYA $CFGARG owns "$ROOT/root/usr/bin/hello" | grep -q "owned by hello" || fail "owns"
ok "files/check/owns"

echo "== remove with pacsave =="
echo "user modification" >> "$ROOT/root/etc/hello.conf"
$NYA $CFGARG remove hello --noconfirm || fail "remove hello"
[ -f "$ROOT/root/etc/hello.conf.pacsave" ] || fail "pacsave not created"
[ ! -f "$ROOT/root/usr/bin/hello" ] || fail "hello binary still present"
[ -f "$ROOT/root/usr/share/nya/post-removed-marker" ] || fail "post_remove scriptlet did not run"
$NYA $CFGARG list | grep -q "liba 1.0-1" || fail "liba should remain after -R"
ok "remove with pacsave"

echo "== reinstall then upgrade with pacnew =="
$NYA $CFGARG install hello --noconfirm || fail "reinstall hello"
[ -f "$ROOT/root/etc/hello.conf" ] || fail "fresh conf not restored"
grep -q "HelloConf 1" "$ROOT/root/etc/hello.conf" || fail "fresh conf should be pristine"
echo "user modification 2" >> "$ROOT/root/etc/hello.conf"
pkg_hello 1.1-1
build_db hello:1.1-1 liba:1.0-1 kitty:1.0-1
$NYA $CFGARG -Sy --noconfirm || fail "refresh for upgrade"
$NYA $CFGARG upgrade --noconfirm || fail "upgrade"
$NYA $CFGARG list | grep -q "hello 1.1-1" || fail "hello not upgraded"
[ -f "$ROOT/root/etc/hello.conf.pacnew" ] || fail "pacnew not created on upgrade"
grep -q "HelloConf 1" "$ROOT/root/etc/hello.conf" || fail "modified conf kept"
grep -q "HelloConf 1" "$ROOT/root/etc/hello.conf.pacnew" || fail "pacnew has wrong content"
grep -q "1.1-1 1.0-1" "$ROOT/root/usr/share/nya/upgraded" || fail "post_upgrade args wrong"
[ -d "$ROOT/root/var/lib/pacman/local/hello-1.1-1" ] || fail "local db not updated"
[ ! -d "$ROOT/root/var/lib/pacman/local/hello-1.0-1" ] || fail "old local db dir not removed"
ok "pacnew on upgrade"

echo "== -U install from file =="
$NYA $CFGARG -U "$REPO/kitty-1.0-1-x86_64.pkg.tar.zst" --noconfirm || fail "-U kitty"
[ -f "$ROOT/root/usr/bin/kitty" ] || fail "kitty binary missing"
$NYA $CFGARG -Q | grep -q "kitty 1.0-1" || fail "kitty not listed"
ok "-U install"

echo "== recursive remove =="
$NYA $CFGARG remove -s hello --noconfirm || fail "remove -s hello"
$NYA $CFGARG -Q | grep -q hello && fail "hello still present"
$NYA $CFGARG -Q | grep -q liba && fail "liba should have been removed as unused dep"
$NYA $CFGARG -Q | grep -q "kitty 1.0-1" || fail "kitty should remain"
ok "recursive remove"

echo "== install multiple targets =="
$NYA $CFGARG install hello kitty --noconfirm || fail "install hello kitty"
$NYA $CFGARG -Q | grep -q "hello 1.1-1" || fail "hello not installed"
$NYA $CFGARG -Q | grep -q "kitty 1.0-1" || fail "kitty not installed"
ok "multiple targets"

echo "== nix and flatpak graceful errors =="
$NYA $CFGARG nix search firefox 2>&1 | grep -qi "nix" || fail "nix error message"
$NYA -fp search firefox 2>&1 | grep -qi "flatpak" || fail "flatpak error message"
ok "nix/fp graceful errors"

echo "== --read-paconfig =="
mkdir -p "$ROOT/pacconf.d"
cat > "$ROOT/pacconf.d/mirrorlist" <<EOF
#Server = https://disabled.example.com/\$repo/os/\$arch
Server = https://mirror.example.com/\$repo/os/\$arch
EOF
cat > "$ROOT/fake-pacman.conf" <<EOF
[options]
Architecture = auto
HoldPkg = pacman glibc
SigLevel = Required DatabaseOptional

[core]
Include = $ROOT/pacconf.d/mirrorlist

[extra]
Include = $ROOT/pacconf.d/mirrorlist
EOF
$NYA --read-paconfig "$ROOT/fake-pacman.conf" --config "$ROOT/nya-gen.conf" || fail "--read-paconfig"
grep -q "^\[extra\]" "$ROOT/nya-gen.conf" || fail "generated conf missing [extra]"
grep -q "Server = https://mirror.example.com" "$ROOT/nya-gen.conf" || fail "generated conf missing Server"
grep -q "#aur = true" "$ROOT/nya-gen.conf" || fail "generated conf missing #aur"
grep -q "#nix = true" "$ROOT/nya-gen.conf" || fail "generated conf missing #nix"
ok "--read-paconfig"

echo "== first-use config generation =="
mkdir -p "$ROOT/auto"
NYA_CONF="$ROOT/auto/nya.conf" $NYA config >/dev/null 2>&1 || true
[ -f "$ROOT/auto/nya.conf" ] || fail "config not auto-generated on first use"
ok "first-use config generation"

echo "== version/help =="
$NYA --version | grep -q "2.8.0" || fail "--version"
$NYA --help | grep -q "nya install" || fail "--help"
ok "version/help"	echo "== sync vs update =="
	mkdir -p "$ROOT/fakebin"
	cat > "$ROOT/fakebin/flatpak" <<'EOF'
#!/usr/bin/env bash
echo -e "Fake App\tA fake flatpak app\torg.fake.FakeApp\t1.0\tflathub"
EOF
	chmod +x "$ROOT/fakebin/flatpak"
	pkg_hello 1.2-1
	build_db hello:1.2-1 liba:1.0-1 kitty:1.0-1
	$NYA $CFGARG sync --noconfirm || fail "sync"
	$NYA $CFGARG -Q | grep -q "hello 1.1-1" || fail "sync should only refresh, not upgrade"
	PATH="$ROOT/fakebin:$PATH" $NYA $CFGARG update --noconfirm || fail "update"
$NYA $CFGARG -Q | grep -q "hello 1.2-1" || fail "update should upgrade"
[ -d "$ROOT/root/var/lib/pacman/local/hello-1.2-1" ] || fail "hello 1.2-1 local db missing"
ok "sync vs update"	echo "== search source flags (searchaur/searchnix/searchflatpak) =="
	cat > "$ROOT/search.conf" <<EOF
[options]
RootDir = $ROOT/root
DBPath = $ROOT/root/var/lib/pacman
CacheDir = $ROOT/cache
LogFile = $ROOT/nya.log
Architecture = x86_64
aur = false
nix = false
searchflatpak = true
searchhost = false

[extra]
Server = file://$ROOT/repo
EOF
	PATH="$ROOT/fakebin:$PATH" $NYA --config "$ROOT/search.conf" search org.fake 2>&1 | grep -q "flatpak/org.fake.FakeApp" || fail "searchflatpak not searched"
	ok "searchflatpak = true"

	echo "== reinstall same version =="
	$NYA $CFGARG install hello --noconfirm 2>&1 | grep -q "reinstalling hello" || fail "reinstall should say reinstalling"
	grep -q "1.2-1 1.2-1" "$ROOT/root/usr/share/nya/upgraded" || fail "reinstall should run post_upgrade with same versions"
	$NYA $CFGARG -Q | grep -q "hello 1.2-1" || fail "hello missing after reinstall"
	ok "reinstall same version"

	echo "== remove deletes cached tarballs =="
	[ -f "$ROOT/root/var/cache/pacman/pkg/hello-1.2-1-x86_64.pkg.tar.zst" ] || fail "hello tarball missing from cache before remove"
	$NYA $CFGARG remove hello --noconfirm || fail "remove hello"
	[ ! -f "$ROOT/root/var/cache/pacman/pkg/hello-1.2-1-x86_64.pkg.tar.zst" ] || fail "hello tarball still in cache after remove"
	[ -f "$ROOT/root/var/cache/pacman/pkg/kitty-1.0-1-x86_64.pkg.tar.zst" ] || fail "kitty tarball should remain (kitty not removed)"
	ok "remove deletes cached tarballs"

	echo "== install flatpak fallback (lowest priority) =="
	mkdir -p "$ROOT/fpbin"
	cat > "$ROOT/fpbin/flatpak" <<EOF
#!/usr/bin/env bash
echo "flatpak args: \$*" >> /tmp/nya-test/fp-args.log
exit 0
EOF
	chmod +x "$ROOT/fpbin/flatpak"
	rm -f "$ROOT/fp-args.log"
	out=$(PATH="$ROOT/fpbin:$PATH" $NYA $CFGARG install org.fake.FakeApp --noconfirm 2>&1) || fail "install should fall back to flatpak"
	grep -q "flatpak args: install org.fake.FakeApp" "$ROOT/fp-args.log" || fail "flatpak install not invoked with the right args"
	echo "$out" | grep -q "Packages (0)" && fail "empty txn summary should not print after flatpak fallback"
	echo "$out" | grep -q "Proceed with installation" && fail "empty txn prompt should not print after flatpak fallback"
	ok "install flatpak fallback"

	echo "== remove flatpak fallback =="
	rm -f "$ROOT/fp-args.log"
	out=$(PATH="$ROOT/fpbin:$PATH" $NYA $CFGARG remove org.fake.FakeApp --noconfirm 2>&1) || fail "remove should fall back to flatpak"
	grep -q "flatpak args: uninstall org.fake.FakeApp" "$ROOT/fp-args.log" || fail "flatpak uninstall not invoked with the right args"
	echo "$out" | grep -q "Packages (0)" && fail "empty txn summary should not print after flatpak uninstall fallback"
	echo "$out" | grep -q "Do you want to remove" && fail "empty txn prompt should not print after flatpak uninstall fallback"
	ok "remove flatpak fallback"

	# ---- nya host (nya-hosts git recipes) ----
	mkdir -p "$ROOT/host-src/hostapp" "$ROOT/host-src/bundled" "$ROOT/host-src/simple" "$ROOT/host-repo"
	cat > "$ROOT/host-src/hostapp/Makefile" <<'EOF'
build:
	@mkdir -p build && printf '#!/bin/sh\necho hello from hostapp\n' > build/hostapp && chmod +x build/hostapp
install:
	@install -Dm755 build/hostapp $(DESTDIR)/usr/bin/hostapp
EOF
	( cd "$ROOT/host-src/hostapp" && git init -q -b main && git add -A && git -c user.email=t@t -c user.name=t commit -qm init )
	cat > "$ROOT/host-src/bundled/Makefile" <<'EOF'
build:
	@mkdir -p build && printf '#!/bin/sh\necho bundled app\n' > build/bundled && chmod +x build/bundled && printf 'notreallyalib' > build/libbundled.so && printf 'junk' > build/CMakeCache.txt && touch build/compile_commands.json
EOF
	( cd "$ROOT/host-src/bundled" && git init -q -b main && git add -A && git -c user.email=t@t -c user.name=t commit -qm init )
	cat > "$ROOT/host-src/simple/Makefile" <<'EOF'
build:
	@mkdir -p build && printf '#!/bin/sh\necho simple tool\n' > build/tool && chmod +x build/tool
EOF
	( cd "$ROOT/host-src/simple" && git init -q -b main && git add -A && git -c user.email=t@t -c user.name=t commit -qm init )
	cat > "$ROOT/host-repo/hostapp" <<EOF
[repo]
file://$ROOT/host-src/hostapp

[folder]
hostapp

[instructions]
make build

[desc]
An example host package for testing nya hosts
EOF
	cat > "$ROOT/host-repo/bundled" <<EOF
[repo]
file://$ROOT/host-src/bundled

[folder]
bundled

[instructions]
make build

[binary]
./build/bundled
EOF
	cat > "$ROOT/host-repo/simple" <<EOF
[repo]
file://$ROOT/host-src/simple

[folder]
simple

[instructions]
make build

[binary]
./build/tool
EOF
	cat > "$ROOT/hosts.conf" <<EOF
[options]
RootDir = $ROOT/root
DBPath = var/lib/pacman
CacheDir = var/cache/pacman/pkg
LogFile = $ROOT/nya.log
hostsrepo = $ROOT/host-repo
EOF
	HCFG="--config $ROOT/hosts.conf"
	# hermetic: keep the build cache inside the sandbox instead of $HOME
	HHOME="HOME=$ROOT/home"

	echo "== host install (staged via project install rules) =="
	env $HHOME $NYA $HCFG host install hostapp --noconfirm || fail "host install hostapp"
	[ -x "$ROOT/root/usr/bin/hostapp" ] || fail "hostapp not installed to /usr/bin"
	"$ROOT/root/usr/bin/hostapp" | grep -q "hello from hostapp" || fail "hostapp binary broken"
	$NYA $HCFG -Q | grep -q "hostapp 1" || fail "hostapp not in local db"
	ok "host install staged"

	echo "== host install (bundle dir fallback) =="
	env $HHOME $NYA $HCFG host install bundled --noconfirm || fail "host install bundled"
	[ -L "$ROOT/root/usr/bin/bundled" ] || fail "bundled entry should be a symlink into /usr/lib/nya"
	[ -f "$ROOT/root/usr/lib/nya/bundled/libbundled.so" ] || fail "bundled runtime lib not copied"
	[ ! -e "$ROOT/root/usr/lib/nya/bundled/CMakeCache.txt" ] || fail "CMakeCache.txt junk should be filtered"
	[ ! -e "$ROOT/root/usr/lib/nya/bundled/compile_commands.json" ] || fail "compile_commands.json junk should be filtered"
	[ ! -e "$ROOT/root/usr/lib/nya/bundled/Makefile" ] || fail "Makefile junk should be filtered"
	ok "host bundle fallback"

	echo "== host install (single file) + host remove =="
	env $HHOME $NYA $HCFG host install simple --noconfirm || fail "host install simple"
	[ -f "$ROOT/root/usr/bin/simple" ] && [ ! -L "$ROOT/root/usr/bin/simple" ] || fail "simple should be a regular file in /usr/bin"
	$NYA $HCFG host remove simple --noconfirm || fail "host remove simple"
	[ ! -e "$ROOT/root/usr/bin/simple" ] || fail "simple not removed"
	$NYA $HCFG -Q | grep -q "simple" && fail "simple still in local db after remove"
	ok "host single-file + remove"

	echo "== host reinstall (upgrade path) =="
	env $HHOME $NYA $HCFG host install hostapp --noconfirm 2>&1 | grep -q "upgrading hostapp" || fail "reinstall should print upgrading"
	[ -x "$ROOT/root/usr/bin/hostapp" ] || fail "hostapp missing after reinstall"
	ok "host reinstall"

	echo "== host install with [app] (application menu + icons) =="
	mkdir -p "$ROOT/host-src/gapp" "$ROOT/host-src/gapp/build" "$ROOT/host-src/gapp/packaging/icons/hicolor/64x64/apps" "$ROOT/host-src/gapp/packaging/icons/hicolor/128x128/apps"
	cat > "$ROOT/host-src/gapp/Makefile" <<'EOF'
build:
	@mkdir -p build && printf '#!/bin/sh\necho gapp\n' > build/gapp && chmod +x build/gapp && printf 'lib' > build/libgapp.so
EOF
	printf '[Desktop Entry]\nType=Application\nName=Gapp\nExec=gapp\nIcon=gapp\nCategories=Utility;\n' > "$ROOT/host-src/gapp/packaging/gapp.desktop"
	printf 'PNG64' > "$ROOT/host-src/gapp/packaging/icons/hicolor/64x64/apps/gapp.png"
	printf 'PNG128' > "$ROOT/host-src/gapp/packaging/icons/hicolor/128x128/apps/gapp.png"
	( cd "$ROOT/host-src/gapp" && git init -q -b main && git add -A && git -c user.email=t@t -c user.name=t commit -qm init )
	cat > "$ROOT/host-repo/gapp" <<EOF
[repo]
file://$ROOT/host-src/gapp

[folder]
gapp

[instructions]
make build

[binary]
./build/gapp

[app]
./packaging
EOF
	env $HHOME $NYA $HCFG host install gapp --noconfirm || fail "host install gapp"
	[ -L "$ROOT/root/usr/share/applications/gapp.desktop" ] || fail "gapp.desktop not linked into the application menu"
	[ -f "$ROOT/root/usr/lib/nya/gapp/gapp.desktop" ] || fail "gapp.desktop not merged into the bundle"
	[ -f "$ROOT/root/usr/share/icons/hicolor/64x64/apps/gapp.png" ] || fail "gapp 64x64 icon not installed"
	[ -f "$ROOT/root/usr/share/icons/hicolor/128x128/apps/gapp.png" ] || fail "gapp 128x128 icon not installed"
	env $HHOME $NYA $HCFG remove gapp --noconfirm || fail "remove gapp"
	[ ! -e "$ROOT/root/usr/share/applications/gapp.desktop" ] || fail "gapp.desktop not removed"
	[ ! -e "$ROOT/root/usr/share/icons/hicolor/64x64/apps/gapp.png" ] || fail "gapp icon not removed"
	[ ! -e "$ROOT/root/usr/lib/nya/gapp" ] || fail "gapp bundle not removed"
	ok "host [app] menu + icons"

	echo "== host auto logo install (no [app]) =="
	mkdir -p "$ROOT/host-src/plain/build" "$ROOT/host-src/plain/icons"
	cat > "$ROOT/host-src/plain/Makefile" <<'EOF'
build:
	@mkdir -p build && printf '#!/bin/sh\necho plain\n' > build/plain && chmod +x build/plain && printf 'lib' > build/libplain.so
EOF
	printf 'ROOTLOGO' > "$ROOT/host-src/plain/logo.png"
	printf 'SVGLOGO' > "$ROOT/host-src/plain/icons/plain.svg"
	( cd "$ROOT/host-src/plain" && git init -q -b main && git add -A && git -c user.email=t@t -c user.name=t commit -qm init )
	cat > "$ROOT/host-repo/plain" <<EOF
[repo]
file://$ROOT/host-src/plain

[folder]
plain

[instructions]
make build

[binary]
./build/plain
EOF
	env $HHOME $NYA $HCFG host install plain --noconfirm || fail "host install plain"
	[ -f "$ROOT/root/usr/share/icons/hicolor/256x256/apps/logo.png" ] || fail "auto logo.png not installed"
	[ -f "$ROOT/root/usr/share/icons/hicolor/scalable/apps/plain.svg" ] || fail "auto icons/plain.svg not installed"
	env $HHOME $NYA $HCFG remove plain --noconfirm || fail "remove plain"
	[ ! -e "$ROOT/root/usr/share/icons/hicolor/256x256/apps/logo.png" ] || fail "auto logo not removed"
	[ ! -e "$ROOT/root/usr/share/icons/hicolor/scalable/apps/plain.svg" ] || fail "auto svg not removed"
	ok "host auto logo install"

	echo "== host search (searchhost = true) =="
	cat > "$ROOT/hostsearch.conf" <<EOF
[options]
RootDir = $ROOT/root
DBPath = var/lib/pacman
CacheDir = var/cache/pacman/pkg
LogFile = $ROOT/nya.log
searchhost = true
hostsrepo = $ROOT/host-repo
EOF
	# hostapp is already installed from the earlier fallback test; search should still list hosts entries
	env $HHOME $NYA --config $ROOT/hostsearch.conf search hostapp 2>&1 | grep -q "hosts/hostapp 1" || fail "host search hostapp"
	env $HHOME $NYA --config $ROOT/hostsearch.conf search hostapp 2>&1 | grep -q "An example host package" || fail "host search shows desc"
	env $HHOME $NYA --config $ROOT/hostsearch.conf search hostapp 2>&1 | grep -q "\[installed\]" || fail "host search should mark installed packages"
	env $HHOME $NYA --config $ROOT/hostsearch.conf search host example 2>&1 | grep -q "hosts/hostapp" || fail "host search AND terms"
	env $HHOME $NYA --config $ROOT/hostsearch.conf search host nope 2>&1 | grep -q "hosts/hostapp" && fail "host search should require all terms"
	ok "host search"

	echo "== host search via packages.info index =="
	# add an index: package name that maps to a differently-named recipe file
	cat > "$ROOT/host-repo/packages.info" <<EOF
[hostapp]
file=hostapp
desc=An indexed host app for testing

[aliasedhost]
file=hostapp
desc=Index alias pointing at the hostapp recipe
EOF
	env $HHOME $NYA --config $ROOT/hostsearch.conf search hostapp 2>&1 | grep -q "An indexed host app for testing" || fail "host search should use index desc"
	env $HHOME $NYA --config $ROOT/hostsearch.conf search alias 2>&1 | grep -q "hosts/aliasedhost" || fail "host search should find index aliases"
	env $HHOME $NYA --config $ROOT/hostsearch.conf search host indexed 2>&1 | grep -q "hosts/hostapp" || fail "host index AND terms"
	# install via the alias: index file= mapping must resolve the recipe file
	env $HHOME $NYA --config $ROOT/hostsearch.conf host install aliasedhost --noconfirm 2>&1 | grep -q "aliasedhost installed" || fail "host install via index alias"
	env $HHOME $NYA --config $ROOT/hostsearch.conf -Q 2>&1 | grep -q "aliasedhost 1" || fail "aliasedhost not in local db"
	[ -x "$ROOT/root/usr/bin/hostapp" ] || fail "aliasedhost binary missing"
	env $HHOME $NYA --config $ROOT/hostsearch.conf remove aliasedhost --noconfirm >/dev/null 2>&1 || fail "remove aliasedhost"
	[ ! -e "$ROOT/root/usr/bin/hostapp" ] || fail "aliasedhost not cleaned up on remove"
	# remove the index so the later update tests use plain recipe files again
	rm -f "$ROOT/host-repo/packages.info"
	ok "host search via index"

	echo "== host packages excluded from AUR updates =="
	[ -f "$ROOT/root/var/lib/pacman/local/hostapp-1/nya-host" ] || fail "host marker missing in local db"
	[ -f "$ROOT/root/var/lib/pacman/local/bundled-1/nya-host" ] || fail "bundled host marker missing in local db"
	cat > "$ROOT/hostauth.conf" <<EOF
[options]
RootDir = $ROOT/root
DBPath = var/lib/pacman
CacheDir = var/cache/pacman/pkg
LogFile = $ROOT/nya.log
aur = true
hostsrepo = $ROOT/host-repo

[extra]
Server = file://$REPO
EOF
	upout=$(env $HHOME $NYA --config $ROOT/hostauth.conf update --noconfirm 2>&1) || fail "update with host packages installed"
	echo "$upout" | grep -q "checking for AUR updates" && fail "host packages should not be checked against the AUR"
	ok "host excluded from AUR update"

	echo "== install fallback order (repos -> hosts -> aur -> flatpak) =="
	cat > "$ROOT/fallback1.conf" <<EOF
[options]
RootDir = $ROOT/root
DBPath = var/lib/pacman
CacheDir = var/cache/pacman/pkg
LogFile = $ROOT/nya.log
aur = true
hostsrepo = $ROOT/host-repo
EOF
	# hostapp is in the hosts repo but not in any pacman repo: default order installs from hosts, aur is never consulted
	env $HHOME $NYA --config $ROOT/fallback1.conf install hostapp --noconfirm 2>&1 | grep -qE "upgrading hostapp|hostapp installed" || fail "install should fall back to hosts"
	[ -x "$ROOT/root/usr/bin/hostapp" ] || fail "hostapp missing after hosts fallback"
	ok "install fallback hosts before aur"

	echo "== install fallback with aurfirst = true =="
	cat > "$ROOT/fallback2.conf" <<EOF
[options]
RootDir = $ROOT/root
DBPath = var/lib/pacman
CacheDir = var/cache/pacman/pkg
LogFile = $ROOT/nya.log
aur = false
aurfirst = true
hostsrepo = $ROOT/host-repo
EOF
	# aur is disabled so it is skipped; hosts still resolves the target in aur-first mode
	env $HHOME $NYA --config $ROOT/fallback2.conf install bundled --noconfirm 2>&1 | grep -qE "upgrading bundled|bundled installed" || fail "install should fall back to hosts with aurfirst"
	[ -L "$ROOT/root/usr/bin/bundled" ] || fail "bundled missing after hosts fallback (aurfirst)"
	ok "install fallback with aurfirst"

	echo "== host update (recipe change detection) =="
	cat > "$ROOT/hostup.conf" <<EOF
[options]
RootDir = $ROOT/root
DBPath = var/lib/pacman
CacheDir = var/cache/pacman/pkg
LogFile = $ROOT/nya.log
hostsrepo = $ROOT/host-repo

[extra]
Server = file://$REPO
EOF
	# hostapp is installed at version 1 from the earlier fallback test; bump its recipe
	cat > "$ROOT/host-repo/hostapp" <<EOF
[repo]
file://$ROOT/host-src/hostapp

[folder]
hostapp

[instructions]
make build

[desc]
An example host package for testing nya hosts

[version]
2
EOF
	upout=$(env $HHOME $NYA --config $ROOT/hostup.conf host update 2>&1) || fail "host update"
	echo "$upout" | grep -q "upgrading host hostapp" || fail "host update should rebuild changed recipe"
	env $HHOME $NYA --config $ROOT/hostup.conf -Q 2>&1 | grep -q "hostapp 2" || fail "hostapp should be version 2 after update"
	# unchanged recipe -> no rebuild on second run
	upout2=$(env $HHOME $NYA --config $ROOT/hostup.conf host update 2>&1) || fail "second host update"
	echo "$upout2" | grep -q "no host updates available" || fail "unchanged host should not rebuild"
	# nya update integrates hosts too
	upout3=$(env $HHOME $NYA --config $ROOT/hostup.conf update --noconfirm 2>&1) || fail "nya update with hosts"
	echo "$upout3" | grep -q "checking for nya-hosts updates" || fail "nya update should check hosts"
	ok "host update"

	echo "== host [dependencies] + \$SUDOBIN =="
	# fake sudo binary so $SUDOBIN expansion is verifiable without real root
	mkdir -p "$ROOT/fakebin"
	cat > "$ROOT/fakebin/sudo" <<EOF
#!/bin/sh
echo "sudo args: \$*" >> "$ROOT/sudo-args.log"
exec "\$@"
EOF
	chmod +x "$ROOT/fakebin/sudo"
	# fresh root so the repo dependency (liba) is not already installed
	mkdir -p "$ROOT/deproot"
	cat > "$ROOT/hostdeps.conf" <<EOF
[options]
RootDir = $ROOT/deproot
DBPath = var/lib/pacman
CacheDir = var/cache/pacman/pkg
LogFile = $ROOT/nya.log
sudobin = $ROOT/fakebin/sudo
hostsrepo = $ROOT/host-repo

[extra]
Server = file://$REPO
EOF
	env $HHOME $NYA --config "$ROOT/hostdeps.conf" sync --noconfirm || fail "sync for host deps"
	cat > "$ROOT/host-repo/withdeps" <<EOF
[repo]
file://$ROOT/host-src/simple

[folder]
withdeps

[instructions]
make build
\$SUDOBIN touch $ROOT/sudo-marker

[binary]
./build/tool

[dependencies]
simple
liba
EOF
	depout=$(env $HHOME $NYA --config "$ROOT/hostdeps.conf" host install withdeps --noconfirm 2>&1) || fail "host install withdeps"
	echo "$depout" | grep -q "Installing dependency simple" || fail "host dep simple should install first"
	echo "$depout" | grep -q "Installing dependency liba" || fail "host dep liba should install first"
	[ -x "$ROOT/deproot/usr/bin/simple" ] || fail "host dependency simple not installed"
	[ -f "$ROOT/deproot/usr/lib/liba.so.1.0" ] || fail "repo dependency liba not installed"
	[ -x "$ROOT/deproot/usr/bin/withdeps" ] || fail "withdeps binary missing"
	[ -f "$ROOT/sudo-marker" ] || fail "\$SUDOBIN was not expanded"
	grep -q "sudo args: touch" "$ROOT/sudo-args.log" || fail "fake sudo should have been invoked for the \$SUDOBIN line"
	$NYA --config "$ROOT/hostdeps.conf" -Q | grep -q "liba 1.0-1" || fail "liba missing from deproot db"
	$NYA --config "$ROOT/hostdeps.conf" -Q | grep -q "simple 1" || fail "simple missing from deproot db"
	# liba was installed as a dependency of the host: it should show up under -Qd
	$NYA --config "$ROOT/hostdeps.conf" -Qd | grep -q "liba 1.0-1" || fail "liba should be marked as a dependency"
	ok "host dependencies + SUDOBIN"

	echo
echo "ALL TESTS PASSED"
