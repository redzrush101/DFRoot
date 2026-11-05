#!/system/bin/sh
set -eu
[ "$(id -u)" = 0 ] || { echo 'Run this script as root.' >&2; exit 1; }
cd "$(dirname "$0")"
case "$(uname -r)" in
    5.10.*-android12-*) ;;
    *) echo 'This build targets Android 12 / Linux 5.10 GKI.' >&2; exit 1 ;;
esac
if [ -d /sys/module/kexec_mod ]; then
    echo 'kexec_mod is already loaded. Unload its image before replacing it.'
    exit 0
fi
insmod ./kexec_mod.ko enable_exec=1
echo 'kexec module loaded.'
./kexec-load --status
