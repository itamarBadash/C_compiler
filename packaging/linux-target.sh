#!/bin/sh
set -eu

target="$1"
licenses="$2"
versions="$3"
multiarch=/usr/lib/x86_64-linux-gnu
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/lib" "$stage/include" "$stage/licenses/glibc" "$stage/licenses/linux-libc-dev" \
  "$stage/licenses/libgcc-linux"

dpkg -L libc6-dev | grep "^$multiarch/[^/]*\$" | while read -r file; do
  name=$(basename "$file")
  case "$name" in
  libc.so) cp "$multiarch/libc.so.6" "$stage/lib/libc.so" ;;
  libm.so) cp "$multiarch/libm.so.6" "$stage/lib/libm.so" ;;
  *)
    if [ -f "$file" ] && ! grep -q "GNU ld script" "$file"; then
      cp -L "$file" "$stage/lib/$name"
    fi
    ;;
  esac
done
cp -L /lib64/ld-linux-x86-64.so.2 "$stage/lib/"

gcc_dir=$(dirname "$(gcc -print-libgcc-file-name)")
gcc_package=$(dpkg -S "$gcc_dir/libgcc.a" | cut -d: -f1)
dpkg -L "$gcc_package" | grep -E "^$gcc_dir/(libgcc\.a|libgcc_eh\.a|crt[^/]*\.o)\$" |
  grep -v "/crtoffload" | while read -r file; do
  cp "$file" "$stage/lib/"
done

dpkg -L libc6-dev linux-libc-dev | grep "^/usr/include/" | while read -r file; do
  if [ -f "$file" ] && [ ! -L "$file" ]; then
    relative=${file#/usr/include/}
    printf '%s|%s\n' "${relative#x86_64-linux-gnu/}" "$file"
  fi
done | LC_ALL=C sort -t '|' -k 1,1 | awk -F '|' '{ key = tolower($1) } !(key in seen) { seen[key] = 1; print }' |
  while IFS='|' read -r relative file; do
    mkdir -p "$stage/include/$(dirname "$relative")"
    cp "$file" "$stage/include/$relative"
  done

cp /usr/share/doc/libc6-dev/copyright "$stage/licenses/glibc/copyright"
cp /usr/share/doc/linux-libc-dev/copyright "$stage/licenses/linux-libc-dev/copyright"
cp "/usr/share/doc/$gcc_package/copyright" "$stage/licenses/libgcc-linux/copyright"

version() {
  dpkg-query -W -f '${Version}' "$1"
}
{
  echo "glibc=$(version libc6-dev)"
  echo "linux_libc_dev=$(version linux-libc-dev)"
  echo "libgcc_package=$gcc_package"
  echo "libgcc=$(version "$gcc_package")"
  echo "linux_gcc_source=$(dpkg-query -W -f '${source:Package}' "$gcc_package")"
  echo "linux_libc_source=$(dpkg-query -W -f '${source:Package}' libc6-dev)"
  echo "linux_headers_source=$(dpkg-query -W -f '${source:Package}' linux-libc-dev)"
} >"$stage/versions.txt"

mkdir -p "$target" "$licenses"
cp -r "$stage/lib" "$stage/include" "$target/"
cp -r "$stage/licenses/." "$licenses/"
cp "$stage/versions.txt" "$versions"
