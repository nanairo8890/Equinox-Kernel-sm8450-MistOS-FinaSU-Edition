# Makefile for NoMount
# NoMount v2.2.0 by maxsteeel (https://github.com/maxsteeel/nomount, commit bf1847f),
# vendored in-tree, plus the Equinox FinaSU edition changes: processes that
# FinaSU/SUSFS umounted modules for see the stock filesystem, and no
# /sys/module/nomount/version when built in.
# Upstream reads the commit count with git, which here would count this kernel tree
# instead; it is fixed to the upstream count so `nm version` reports the same version
# as the v2.2.0 (605) release.
# Upstream also passes -fno-stack-protector, which is for its prebuilt LKMs; built in,
# NoMount keeps the kernel's stack protector.

obj-$(CONFIG_NOMOUNT) += nomount.o
ccflags-y += -DNOMOUNT_COMMIT_COUNT=\"605\"
ccflags-y += -std=gnu11 -Wno-declaration-after-statement -O3

ifeq ($(shell grep -qE '\(\*[[:space:]]*__get\)' $(srctree)/include/linux/xattr.h 2>/dev/null && echo 1 || echo 0),1)
ccflags-y += -DNM_HAS_XATTR__GET
endif
