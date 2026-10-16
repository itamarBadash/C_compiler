#!/bin/sh
set -eu

staging="$1"
compiler="$2"
llvm="$3"
readme="$4"
sources="$5"
output="$6"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
root="$work/MMC99"

mkdir -p "$root/bin"
cp -r "$staging/include" "$staging/targets" "$staging/licenses" "$root/"
cp "$readme" "$root/README.txt"
cp "$sources" "$root/SOURCES.txt"
cp "$staging/CONFORMANCE.md" "$root/CONFORMANCE.md"
cp "$compiler" "$root/bin/MMC99"
cp "$llvm/clang" "$llvm/ld.lld" "$root/bin/"
find "$root" -type d -exec chmod 755 {} +
find "$root" -type f -exec chmod 644 {} +
chmod 755 "$root/bin/MMC99" "$root/bin/clang" "$root/bin/ld.lld"
tar -czf "$work/package.tar.gz" --owner=0 --group=0 --numeric-owner -C "$work" MMC99
cp "$work/package.tar.gz" "$output"
