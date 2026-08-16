#!/bin/sh
# Runs on the reMarkable. Stops xochitl, runs the chat app, and always brings
# xochitl back - whether the app quits, crashes, or is killed.
set -u

APP_DIR="$(cd "$(dirname "$0")" && pwd)"

restore() {
    echo "[wrapper] restarting xochitl"
    # --no-block is essential. When systemd is stopping this unit, a blocking
    # `systemctl start` from inside the unit's own cgroup waits for a job that
    # systemd will not schedule until the stop completes - a deadlock that
    # hangs until TimeoutStopSec (90 s by default) force-kills everything,
    # leaving the app's last frame frozen on the panel the whole time.
    systemctl --no-block start xochitl
}
trap restore EXIT INT TERM

echo "[wrapper] stopping xochitl"
systemctl stop xochitl

# Wait for the e-paper framebuffer lock to be released.
i=0
while [ "$i" -lt 30 ]; do
    pidof xochitl >/dev/null 2>&1 || break
    sleep 0.5
    i=$((i + 1))
done
if pidof xochitl >/dev/null 2>&1; then
    echo "[wrapper] xochitl still running after 15s, aborting" >&2
    exit 1
fi
sleep 1

export QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS="rotate=180:invertx"
export QT_QUICK_BACKEND=epaper

"$APP_DIR/rm_chat" -platform epaper --dir "$APP_DIR" "$@"
echo "[wrapper] app exited with status $?"
