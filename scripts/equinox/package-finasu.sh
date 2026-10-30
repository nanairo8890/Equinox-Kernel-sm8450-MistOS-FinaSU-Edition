#!/usr/bin/env bash
# Equinox Kernel by LF52406 - FinaSU edition by Nanairo (with the author's permission).
#
# Packages the Image built by build-finasu.sh into a flashable AnyKernel3 zip that
# checks the ROM's vendor modules before flashing (anykernel-finasu.sh, kmicheck.c).
#
#   CLANG_DIR=/path/to/clang/bin scripts/equinox/package-finasu.sh [out dir] [zip]
#
# KMI_BASELINE=/path/to/vmlinux.symvers of a clean Equinox build makes the package
# also require 0 changed CRCs and 0 removed exports against it (symvers-diff.sh).
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=$(realpath -m "${1:-$ROOT/out}")
ZIP=$(realpath -m "${2:-$ROOT/Equinox-5.10.269-FinaSU-mondrian-$(date +%Y%m%d).zip}")
[ -n "${CLANG_DIR:-}" ] && export PATH="$CLANG_DIR:$PATH"
source "$ROOT/scripts/equinox/pins.env"

IMAGE=$OUT/arch/arm64/boot/Image
SYMVERS=$OUT/vmlinux.symvers
WORK=$OUT/anykernel

for cmd in clang ld.lld git zip unzip sha256sum grep; do
    command -v "$cmd" >/dev/null || { echo "[!] missing tool: $cmd"; exit 1; }
done
[ -s "$IMAGE" ] || { echo "[!] missing kernel Image: $IMAGE"; exit 1; }
[ -s "$SYMVERS" ] || { echo "[!] missing $SYMVERS (build the Image with build-finasu.sh)"; exit 1; }

release=$(cat "$OUT/include/config/kernel.release")
[ "$release" = 5.10.269-Nanairo-FinaSU ] || { echo "[!] unexpected kernel release: $release"; exit 1; }
# what the module loader compares, read from the Image itself
vermagic=$(grep -aoE -m1 "${release//./\\.} (SMP )?(preempt )?(mod_unload )?(modversions )?aarch64" "$IMAGE" | head -n1 || true)
[ -n "$vermagic" ] || { echo '[!] vermagic not found in the Image'; exit 1; }
case "$vermagic" in
    *modversions*) ;;
    *) echo '[!] the Image is built without MODVERSIONS; the module check needs it'; exit 1 ;;
esac

if [ -n "${KMI_BASELINE:-}" ]; then
    bash "$ROOT/scripts/equinox/symvers-diff.sh" "$KMI_BASELINE" "$SYMVERS" "$OUT/kmi-report.txt"
else
    echo '[*] KMI_BASELINE not set: the KMI against clean Equinox is not checked'
fi

rm -rf "$WORK"
git clone -q "$ANYKERNEL_REPO" "$WORK"
git -C "$WORK" checkout -q --detach "$ANYKERNEL_PIN"
[ "$(git -C "$WORK" rev-parse HEAD)" = "$ANYKERNEL_PIN" ] || { echo '[!] AnyKernel3 pin mismatch'; exit 1; }
rm -rf "$WORK/.git" "$WORK/.github" "$WORK/README.md" "$WORK/modules" "$WORK/patch" "$WORK/ramdisk"

cp "$IMAGE" "$WORK/Image"
sed "s/@KERNEL_RELEASE@/$release/" "$ROOT/scripts/equinox/anykernel-finasu.sh" > "$WORK/anykernel.sh"
sh -n "$WORK/anykernel.sh"
cp "$SYMVERS" "$WORK/kmi.symvers"
echo "$vermagic" > "$WORK/kmi.vermagic"
clang --target=aarch64-linux-gnu -Os -Wall -Werror -ffreestanding -fno-builtin \
    -fno-stack-protector -nostdlib -static -fuse-ld=lld -s \
    -o "$WORK/tools/kmicheck" "$ROOT/scripts/equinox/kmicheck.c"
cp "$ROOT/CREDITOS.md" "$ROOT"/CHANGELOG.*.txt "$WORK/"
chmod 0644 "$WORK/Image" "$WORK/kmi.symvers" "$WORK/kmi.vermagic"
chmod 0755 "$WORK/anykernel.sh" "$WORK/tools/kmicheck"

image_sha=$(sha256sum "$IMAGE" | awk '{print $1}')
cat > "$WORK/version" <<EOF
Equinox Kernel by LF52406 - FinaSU edition by Nanairo (with the author's permission)
https://github.com/LF52406/Equinox-Kernel-sm8450

Kernel: $release
Device: POCO F5 Pro / Redmi K60 (mondrian)
Edition: $(git -C "$ROOT" rev-parse --short=12 HEAD)
FinaSU by LyraVoid: $(git -C "$ROOT/KernelSU" describe --tags --always 2>/dev/null), built in
Build: $(grep -aoE -m1 'Linux version [^%][ -~]*' "$IMAGE" | head -n1 || true)
Vendor module check: kmicheck against kmi.symvers ($(grep -c . "$SYMVERS") exports), vermagic "$vermagic"
Image SHA256: $image_sha
EOF

rm -f "$ZIP" "$ZIP.sha256"
(cd "$WORK" && zip -qr9 "$ZIP" . -x '*.git*' '*.zip')
unzip -tq "$ZIP" >/dev/null
[ "$(unzip -p "$ZIP" Image | sha256sum | awk '{print $1}')" = "$image_sha" ] || { echo '[!] Image changed while packaging'; exit 1; }
zip_list=$(unzip -Z1 "$ZIP")
for f in Image anykernel.sh version kmi.symvers kmi.vermagic tools/kmicheck tools/ak3-core.sh \
         tools/magiskboot tools/busybox META-INF/com/google/android/update-binary; do
    grep -Fxq "$f" <<<"$zip_list" || { echo "[!] package is missing $f"; exit 1; }
done
(cd "$(dirname "$ZIP")" && sha256sum "$(basename "$ZIP")" > "$ZIP.sha256")

echo "[+] $ZIP"
echo "    SHA256  : $(awk '{print $1}' "$ZIP.sha256")"
echo "    Image   : $image_sha"
echo "    vermagic: $vermagic"
