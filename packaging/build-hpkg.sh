#!/bin/sh
# build-hpkg.sh — builds hydrogen-<version>-<revision>-<arch>.hpkg on Haiku.
# Hydrogen, Copyright(c) 2008-2026 The hydrogen development team. GPL-2.0-or-later.
# Haiku port: Copyright(c) 2026 idumych (https://github.com/idumych)
#
#   sh packaging/build-hpkg.sh        (from the project root, on Haiku R1/beta6)
#
# The beos-app-platforms template packages one binary; Hydrogen also ships
# libhydrogen-core and the system data folder (drumkits, demo songs,
# patterns), so it keeps this script. Everything else comes from
# haiku-app.env. Set BUILD_DIR to build elsewhere (a volume with more room).
set -eu
cd "$(dirname "$0")/.."
ROOT=$(pwd)
. ./haiku-app.env

ARCH=$(getarch)
OUT="$ROOT/${APP_CMD}-${VERSION}-${REVISION}-${ARCH}.hpkg"
BUILD=${BUILD_DIR:-$ROOT/obj/build}
JOBS=$(nproc 2>/dev/null || echo 2)

for tool in cmake rc xres mimeset package getarch; do
	command -v "$tool" >/dev/null 2>&1 || {
		echo "build-hpkg: '$tool' not found (pkgman install haiku_devel $HPKG_BUILD_DEPS)" >&2; exit 1; }
done

# 1. Build the core, the app and the core's test suite. The data path is
#    compiled into the core: /boot/system/data/hydrogen/data.
#    Strip at link time: xres appends the resources after the ELF data,
#    so stripping afterwards would cut them off.
rm -rf "$BUILD"
mkdir -p "$BUILD"
(cd "$BUILD" && cmake "$ROOT" \
	-DCMAKE_INSTALL_PREFIX=/boot/system \
	-DCMAKE_INSTALL_DATAROOTDIR=data \
	-DCMAKE_INSTALL_LIBDIR=lib \
	-DWANT_DEBUG=OFF \
	-DWANT_CPPUNIT=ON \
	-DCMAKE_EXE_LINKER_FLAGS=-s \
	-DCMAKE_SHARED_LINKER_FLAGS=-s >/dev/null)
make -C "$BUILD" -j"$JOBS" Hydrogen tests translations
grep -q "^#define H2_SYS_PATH \"/boot/system/data/hydrogen\"" "$BUILD/src/core/config.h" || {
	echo "build-hpkg: the core's data path isn't /boot/system/data/hydrogen" >&2; exit 1; }

# The core's CppUnit suite, run from its build folder like upstream's CI.
(cd "$BUILD/src/tests" && ./tests)

# 2. Package tree, relative to the install location (/boot/system)
CORE=$(cd "$BUILD/src/core" && ls libhydrogen-core-*.so)
PKG="$ROOT/obj/hpkg"
DATA="$PKG/data/hydrogen/data"
rm -rf "$PKG"
mkdir -p "$PKG/apps" "$PKG/bin" "$PKG/lib" "$PKG/data/deskbar/menu/Applications" \
	"$PKG/documentation/packages/$APP_CMD" "$DATA/img" "$DATA/i18n"
cp "$BUILD/src/haiku/$APP_NAME" "$PKG/apps/$APP_NAME"
mimeset -f "$PKG/apps/$APP_NAME"
cp "$BUILD/src/core/$CORE" "$PKG/lib/"
ln -s "../apps/$APP_NAME" "$PKG/bin/$APP_CMD"
ln -s "../../../../apps/$APP_NAME" \
	"$PKG/data/deskbar/menu/Applications/${DESKBAR_NAME:-$APP_NAME}"
# What the core reads: kits, maps, demos, patterns, themes, schemas, the
# default configuration and the click. img/ and i18n/ only serve upstream's
# Qt GUI, but the core checks that both folders exist; the icon stays as
# the one file in img/.
for f in click.wav emptySample.wav hydrogen.default.conf demo_songs drumkit_maps \
		drumkits patterns themes xsd; do
	cp -R "data/$f" "$DATA/"
done
cp data/img/gray/h2-icon.svg "$DATA/img/"
cp README.md COPYING AUTHORS CHANGELOG.md "$PKG/documentation/packages/$APP_CMD/"

# 3. .PackageInfo
DESC=$(sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e '2,$s/^/\t/' "$DESCRIPTION_FILE")
REQ=""
for r in ${HPKG_REQUIRES_EXTRA:-}; do REQ="$REQ
	$r"; done
cat > "$PKG/.PackageInfo" <<EOF
name			$APP_CMD
version			$VERSION-$REVISION
architecture	$ARCH
summary			"$SUMMARY"
description		"$DESC"
packager		"$PACKAGER"
vendor			"$APP_NAME"
copyrights		{
	"$COPYRIGHT"
}
licenses		{
	"$LICENSE_NAME"
}
urls			{
	"$HOMEPAGE"
}
provides		{
	$APP_CMD = $VERSION
	app:$APP_NAME = $VERSION
	cmd:$APP_CMD
}
requires		{
	haiku >= r1~beta6$REQ
}
EOF

# 4. Package. The licence must be one Haiku knows or one shipped in the
#    package; `package create` says which when it isn't.
rm -f "$OUT"
package create -C "$PKG" "$OUT"
package list -i "$OUT" >/dev/null
echo "Built $OUT"
