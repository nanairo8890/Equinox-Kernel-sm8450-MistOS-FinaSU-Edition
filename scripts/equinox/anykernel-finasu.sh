### AnyKernel3 Ramdisk Mod Script
## Equinox Kernel by LF52406 for POCO F5 Pro / Redmi K60 (mondrian),
## modified by Nanairo with the author's permission: FinaSU (by LyraVoid)
## instead of KernelSU-Next, BBR by default, NoMount by maxsteeel built in.
## See CREDITOS.md.

properties() { '
kernel.string=Equinox Kernel 5.10.269 by LF52406 - FinaSU mod by Nanairo
do.devicecheck=1
do.modules=0
do.systemless=0
do.cleanup=1
do.cleanuponabort=0
device.name1=mondrian
device.name2=mondrianin
supported.versions=
supported.patchlevels=
supported.vendorpatchlevels=
'; }

BLOCK=boot
IS_SLOT_DEVICE=1
RAMDISK_COMPRESSION=auto
PATCH_VBMETA_FLAG=auto

. tools/ak3-core.sh

case "$SLOT" in
    _a) active_slot="A" ;;
    _b) active_slot="B" ;;
    *) abort "Unable to determine active A/B slot. Aborting..." ;;
esac

# Vendor module check. The kernel keeps the ROM's vendor_boot and vendor_dlkm
# modules, which only load if they were built for the same symbol CRCs (KMI).
# kmicheck reads those modules and compares them with kmi.symvers, the kernel's
# own table, before anything is written. A zip whose name contains "nocheck"
# skips it.
KMIDIR=$AKHOME/kmicheck

kmi_unmount() {
    [ "$kmi_mounted" ] && umount $KMIDIR/vendor_dlkm 2>/dev/null
    kmi_mounted=
    cd $AKHOME
}

kmi_check() {
    local dirs= sources= img rd comp name dev line ret

    case "$(basename "$ZIPFILE")" in
        *nocheck*)
            ui_print " Vendor module check: skipped (nocheck in the zip name)"
            return
            ;;
    esac
    mkdir -p $KMIDIR
    chmod 755 $BIN/kmicheck

    # vendor_boot: the first-stage modules, in one of the vendor ramdisks
    img=/dev/block/bootdevice/by-name/vendor_boot$SLOT
    if [ -e $img ] && dd if=$img of=$KMIDIR/vendor_boot.img bs=1048576 2>/dev/null; then
        mkdir -p $KMIDIR/vendor_boot
        cd $KMIDIR/vendor_boot
        # exits with 3 for a vendor_boot image; the ramdisks it wrote are what count
        magiskboot unpack $KMIDIR/vendor_boot.img >/dev/null 2>&1
        for rd in vendor_ramdisk/*.cpio ramdisk.cpio; do
            [ -f $rd ] || continue
            comp=$(magiskboot decompress $rd 2>&1 | grep -v 'raw' | sed -n 's;.*\[\(.*\)\];\1;p')
            if [ "$comp" ]; then
                mv -f $rd $rd.$comp
                magiskboot decompress $rd.$comp $rd >/dev/null 2>&1 || continue
            fi
            name=$KMIDIR/ramdisk-$(basename $rd .cpio)
            mkdir -p $name
            (cd $name && EXTRACT_UNSAFE_SYMLINKS=1 cpio -d -F $KMIDIR/vendor_boot/$rd -i 2>/dev/null)
            if ls $name/lib/modules/*.ko >/dev/null 2>&1; then
                dirs="$dirs $name/lib/modules"
                sources=vendor_boot
            fi
        done
        cd $AKHOME
        rm -rf $KMIDIR/vendor_boot $KMIDIR/vendor_boot.img
    fi

    # vendor_dlkm: the second-stage modules; mounted already when the ROM is running
    if [ "$SLOT" = "$(getprop ro.boot.slot_suffix 2>/dev/null)" ] && ls /vendor_dlkm/lib/modules/*.ko >/dev/null 2>&1; then
        dirs="$dirs /vendor_dlkm/lib/modules"
        sources="$sources vendor_dlkm"
    else
        for dev in /dev/block/mapper/vendor_dlkm$SLOT /dev/block/mapper/vendor_dlkm; do
            [ -e $dev ] || continue
            mkdir -p $KMIDIR/vendor_dlkm
            mount -o ro $dev $KMIDIR/vendor_dlkm 2>/dev/null || continue
            kmi_mounted=1
            if ls $KMIDIR/vendor_dlkm/lib/modules/*.ko >/dev/null 2>&1; then
                dirs="$dirs $KMIDIR/vendor_dlkm/lib/modules"
                sources="$sources vendor_dlkm"
            fi
            break
        done
    fi

    if [ ! "$dirs" ]; then
        kmi_unmount
        ui_print " Vendor module check: the ROM's modules could not be read; not checked"
        return
    fi
    ui_print " Checking the ROM's vendor modules ($(echo $sources | sed 's/ / + /'))..."

    # every symbol a module needs can only be checked when all modules are there
    case "$sources" in
        "vendor_boot vendor_dlkm") $BIN/kmicheck -u -m "$(cat $AKHOME/kmi.vermagic)" $AKHOME/kmi.symvers $dirs > $KMIDIR/report 2>&1 ;;
        *) $BIN/kmicheck -m "$(cat $AKHOME/kmi.vermagic)" $AKHOME/kmi.symvers $dirs > $KMIDIR/report 2>&1 ;;
    esac
    ret=$?
    kmi_unmount

    case $ret in
        0)
            ui_print " $(tail -n 1 $KMIDIR/report)"
            ;;
        1)
            while IFS= read -r line; do
                ui_print " $line"
            done < $KMIDIR/report
            abort " This ROM's vendor modules were built for a different kernel" \
                " (another KMI). With this kernel they would not load: no touch," \
                " Wi-Fi, audio or display, or no boot. Nothing was flashed." \
                " Use a build of the kernel made for this ROM's kernel version."
            ;;
        *)
            while IFS= read -r line; do
                ui_print " $line"
            done < $KMIDIR/report
            ui_print " Vendor module check: could not run; not checked"
            ;;
    esac
}

ui_print " "
ui_print '  _____                      _____'
ui_print ' /     \                    /     \'
ui_print '|  .-.  |  ______________  |  .-.  |'
ui_print '| (   ) |-`              `-| (   ) |'
ui_print '|  `-`  /                  \  `-`  |'
ui_print ' \     / /\/\/\/\/\/\/\/\/\ \     /'
ui_print '  `---|  \  \          /  /  |---`'
ui_print '      |   \  \        /  /   |'
ui_print '      |    \__\      /__/    |'
ui_print '      |              .---.   |'
ui_print '      |   `---`     ( (@) )  |'
ui_print '      |              `---`   |'
ui_print '      |   ///  \_/\_/   ///  |'
ui_print '       \                    /'
ui_print '        `-._            _.-`'
ui_print '            `----------`'
ui_print ' ___ ___  _   _ ___ _  _  _____  __'
ui_print '| __/ _ \| | | |_ _| \| |/ _ \ \/ /'
ui_print '| _| (_) | |_| || || .` | (_) >  <'
ui_print '|___\__\_\\___/|___|_|\_|\___/_/\_\'
ui_print "     FinaSU and NoMount Edition"
ui_print "          by Nanairo8890"
ui_print " ==================================="
ui_print " Device   : POCO F5 Pro / Redmi K60"
ui_print " Codename : mondrian"
ui_print " Kernel   : @KERNEL_RELEASE@"
ui_print " Root     : FinaSU v1.2.1 (33074)"
ui_print " SUSFS    : 2.3.0"
ui_print " Spaces   : DroidSpaces support"
ui_print " TCP      : BBR default"
ui_print " NoMount  : v2.2.0 built-in"
ui_print " Slot     : ${active_slot}"
ui_print " Target   : ${BLOCK}"
ui_print " -----------------------------------"
ui_print " Equinox Kernel by LF52406"
ui_print " FinaSU mod by Nanairo8890, with permission"
ui_print " FinaSU by LyraVoid, NoMount by maxsteeel"
ui_print " ==================================="
ui_print " "

kmi_check

# a zip whose name contains "checkonly" only reports whether this ROM is compatible
case "$(basename "$ZIPFILE")" in
    *checkonly*)
        ui_print " " " Check only (checkonly in the zip name): nothing was flashed."
        exit 0
        ;;
esac

ui_print " "
ui_print " Replacing kernel Image..."
ui_print " "

# Image-only replacement. Preserve the ROM's existing ramdisk/header/bootconfig.
split_boot
flash_boot

ui_print " "
ui_print " Equinox installation complete."
ui_print " "
