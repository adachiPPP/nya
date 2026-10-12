#!/bin/bash
set -e
NAME=$1
VER=$2
DEPS=$3
DIR=/tmp/nya-test2/build/$NAME-$VER
mkdir -p "$DIR/usr/bin" "$DIR/usr/lib"
echo "#!/bin/sh" > "$DIR/usr/bin/$NAME"
echo "echo running $NAME" >> "$DIR/usr/bin/$NAME"
chmod +x "$DIR/usr/bin/$NAME"
echo "libdata" > "$DIR/usr/lib/lib${NAME}.so"

cat > "$DIR/.PKGINFO" << PKGINFO
pkgname = $NAME
pkgbase = $NAME
pkgver = $VER
pkgdesc = Test package $NAME
url = https://example.com
builddate = 1700000000
packager = test
size = 1024
arch = x86_64
license = MIT
PKGINFO

if [ -n "$DEPS" ]; then
    for dep in $DEPS; do
        echo "depend = $dep" >> "$DIR/.PKGINFO"
    done
fi

( cd "$DIR" && tar --zstd -cf /tmp/nya-test2/repo/$NAME-$VER-x86_64.pkg.tar.zst . )
echo "Built $NAME-$VER"
