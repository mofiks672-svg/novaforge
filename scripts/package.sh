#!/usr/bin/env sh
# Build release archive: dist/novaforge-<version>-<os>-<arch>.tar.gz
set -e
cd "$(dirname "$0")/.."
VER=$(grep -o 'VERSION = "[0-9.]*"' src/config.hpp | grep -o '[0-9.]*')
OS=$(uname -s | tr A-Z a-z); ARCH=$(uname -m)
make clean >/dev/null; make STATIC=${STATIC:-0}
NAME=novaforge-$VER-$OS-$ARCH
rm -rf dist/$NAME && mkdir -p dist/$NAME
cp build/nova README.md LICENSE CHANGELOG.md dist/$NAME/
cp -r examples dist/$NAME/
cp scripts/install.sh dist/$NAME/install.sh
(cd dist && tar czf $NAME.tar.gz $NAME && sha256sum $NAME.tar.gz > $NAME.tar.gz.sha256)
echo "dist/$NAME.tar.gz"
