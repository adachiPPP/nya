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
$NYA --version | grep -q "1.0.0" || fail "--version"
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

	echo
echo "ALL TESTS PASSED"
