#!/bin/sh
# Downloads OpenRCT2 v0.5.6 and applies the 3DS port on top of it.
# Result: ./openrct2 with the 3DS changes on a branch called "3ds".
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
git clone --branch v0.5.6 --depth 1 https://github.com/OpenRCT2/OpenRCT2.git openrct2
cd openrct2
git checkout -b 3ds
git -c user.name=build -c user.email=build@localhost am "$HERE/patches/openrct2-3ds-on-v0.5.6.patch"
echo "Done. Build instructions: see README.md (Building it yourself)."
