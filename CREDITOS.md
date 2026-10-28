# Equinox Kernel — edición FinaSU

**Equinox Kernel** es el kernel de **[LF52406](https://github.com/LF52406)** para el
POCO F5 Pro / Redmi K60 (`mondrian`): https://github.com/LF52406/Equinox-Kernel-sm8450

Esta es una modificación de Equinox 5.10.269 hecha por **Nanairo**, con permiso de LF52406.
Todo el trabajo del kernel base (Linux 5.10.269, config de mondrian, DroidSpaces, arreglos
del touch Goodix y la comprobación de KMI) es de LF52406. Si te gusta, apoya al
desarrollador original: https://www.paypal.me/LF52406 · https://patreon.com/InfernalGT ·
https://boosty.to/infernalgt/donate

Los problemas de esta edición repórtalos a Nanairo, no a LF52406 ni a LyraVoid.

## Qué cambia respecto de Equinox oficial

- **FinaSU** v1.2.1 de LyraVoid en lugar de KernelSU-Next, integrado en el kernel. Necesita
  el manager oficial de FinaSU (v1.2.1, 33074, o más nuevo con la misma firma).
- **SUSFS v2.3.0** desde la rama `gki-android12-5.10` de simonpunk, con la interfaz de SUSFS
  de FinaSU y todas las funciones activas (sus_path, sus_mount, sus_kstat, sus_map, spoof
  uname, spoof cmdline/bootconfig, open_redirect, log y ocultación de símbolos).
- **BBR** como control de congestión TCP por defecto (Equinox trae BBR, pero usa CUBIC
  por defecto).
- **NoMount v2.2.0** de maxsteeel integrado en el kernel. Lo usa el backend NoMount de
  FinaSU, sin metamódulo. Esta edición hace que las apps a las que FinaSU/SUSFS desmonta los
  módulos vean el sistema original, y oculta NoMount de `/sys/module` y `/proc/kallsyms`.
- Compilado con Clang de AOSP y ThinLTO (Equinox usa Neutron Clang y LTO completo). Clang
  CFI y MODVERSIONS siguen activos.

Todo lo demás es Equinox tal cual, incluidos DroidSpaces y las reubicaciones KABI de
SYSVIPC/POSIX_MQUEUE.

## Créditos

- **[LF52406](https://github.com/LF52406)**: Equinox Kernel, la base de todo esto.
- [LineageOS](https://github.com/LineageOS/android_kernel_xiaomi_sm8450): kernel base de mondrian.
- [Android Common Kernel](https://android.googlesource.com/kernel/common) y [Linux](https://www.kernel.org/).
- [FinaSU](https://github.com/LyraVoid/FinaSU) de **LyraVoid**: root en el kernel, basado en
  [KernelSU](https://github.com/tiann/KernelSU) de **tiann** (weishu).
- [SUSFS](https://gitlab.com/simonpunk/susfs4ksu) de **simonpunk**.
- [DroidSpaces](https://github.com/ravindu644/Droidspaces-OSS) de **ravindu644**.
- [NoMount](https://github.com/maxsteeel/nomount) de **maxsteeel**: redirección de rutas en
  VFS (v2.2.0); incluye el arreglo `7e7342c` de **deref3046**.
- [AnyKernel3](https://github.com/osm0sis/AnyKernel3) de **osm0sis**.
- **[Nanairo8890](https://github.com/nanairo8890)**: integración de FinaSU, SUSFS, BBR y NoMount en Equinox.

---

# Equinox Kernel — FinaSU edition (English)

Equinox Kernel is **LF52406**'s kernel for mondrian. This build is Equinox 5.10.269 modified
by Nanairo with LF52406's permission: LyraVoid's FinaSU v1.2.1 (based on KernelSU) replaces
KernelSU-Next and is built in, SUSFS v2.3.0 uses FinaSU's SUSFS interface, TCP defaults to
BBR, and maxsteeel's NoMount v2.2.0 (which includes deref3046's fix 7e7342c) is built in for
FinaSU's NoMount backend. It needs the official FinaSU manager. Everything else is Equinox as
released, DroidSpaces included. All base kernel work is LF52406's; please report issues with
this edition to Nanairo, not to LF52406 or LyraVoid.
