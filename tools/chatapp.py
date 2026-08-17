#!/usr/bin/env python3
"""Manage the AI chat app on the reMarkable.

    ./tools/chatapp.py deploy      upload binary, wrapper, systemd unit
    ./tools/chatapp.py start       run it now (stops xochitl until exit)
    ./tools/chatapp.py stop        stop it, restore the tablet UI
    ./tools/chatapp.py status      unit / config / network state
    ./tools/chatapp.py logs        recent app output
    ./tools/chatapp.py screenshot  render the UI offscreen, pull a PNG
    ./tools/chatapp.py test        full e2e: virtual pen scribble -> fake
                                   OpenAI server on this host -> reply rendered
    ./tools/chatapp.py selftest    ask the TABLET to call your real API with the
                                   stored credentials; reports key length/shape
                                   (never the key) and the exact HTTP result
    ./tools/chatapp.py setkey      type the key here instead of on e-paper
                                   (hidden input; see the warning it prints)

The API key is entered ON THE TABLET (Settings page) and stays there.
"""
import argparse
import os
import subprocess
import sys
import time

import pexpect

HOST = os.environ.get("RM_HOST", "10.11.99.1")
USER = os.environ.get("RM_USER", "root")


def _password():
    """Device root password.

    Never hard-code it: set RM_PASSWORD, or be prompted. Find it on the tablet
    under Settings -> Help -> About -> Copyrights and licenses (bottom).
    """
    pw = os.environ.get("RM_PASSWORD")
    if not pw:
        import getpass
        pw = getpass.getpass(f"root password for {HOST}: ")
    return pw


PASSWORD = None  # resolved lazily by _pw()


def _pw():
    global PASSWORD
    if PASSWORD is None:
        PASSWORD = _password()
    return PASSWORD
HOST_IP_FOR_DEVICE = os.environ.get("RM_HOST_IP", "10.11.99.6")
APP_DIR = "/home/root/rm-chat"
UNIT = "/etc/systemd/system/rm-chat.service"

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, ".."))
SSH_OPTS = ["-o", "StrictHostKeyChecking=no", "-o", "PubkeyAuthentication=no",
            "-o", "ConnectTimeout=10"]


def _spawn(prog, argv, timeout=180):
    child = pexpect.spawn(prog, SSH_OPTS + argv, timeout=timeout,
                          encoding="utf-8", codec_errors="replace")
    if child.expect(["[Pp]assword:", pexpect.EOF, pexpect.TIMEOUT]) == 0:
        child.sendline(_pw())
    child.expect(pexpect.EOF)
    out = (child.before or "").strip()
    child.close()
    return child.exitstatus, out


def ssh(cmd, timeout=180):
    return _spawn("ssh", [f"{USER}@{HOST}", cmd], timeout)


def require_device():
    """Fail loudly and early when the tablet is not reachable.

    Without this a failed ssh just returns empty output and callers happily
    report success for work that never happened.
    """
    rc, out = ssh("echo rm2-alive", timeout=25)
    if rc in (0, None) and "rm2-alive" in out:
        return True
    print(f"Cannot reach the tablet at {HOST}.", file=sys.stderr)
    print("Check that the USB cable is connected at both ends and the tablet",
          file=sys.stderr)
    print("is powered on, then try again.", file=sys.stderr)
    return False


def push(local, remote):
    rc, out = _spawn("scp", [local, f"{USER}@{HOST}:{remote}"], 300)
    if rc not in (0, None):
        raise RuntimeError(f"upload of {local} failed: {out}")


def pull(remote, local):
    rc, out = _spawn("scp", [f"{USER}@{HOST}:{remote}", local], 300)
    if rc not in (0, None):
        raise RuntimeError(f"download of {remote} failed: {out}")


def cmd_deploy(args):
    binary = os.path.join(REPO, "rm-chat", "build", "rm_chat")
    wrapper = os.path.join(REPO, "rm-chat", "run-on-device.sh")
    unit = os.path.join(REPO, "rm-chat", "rm-chat.service")
    for f in (binary, wrapper, unit):
        if not os.path.exists(f):
            print(f"Missing {f} - run ./tools/build.sh rm-chat first", file=sys.stderr)
            return 1

    if not require_device():
        return 1
    print(f"[*] uploading to {HOST}:{APP_DIR}")
    ssh(f"mkdir -p {APP_DIR}")
    push(binary, f"{APP_DIR}/.rm_chat.new")
    ssh(f"mv {APP_DIR}/.rm_chat.new {APP_DIR}/rm_chat && chmod 755 {APP_DIR}/rm_chat")
    push(wrapper, f"{APP_DIR}/run-on-device.sh")
    ssh(f"chmod 755 {APP_DIR}/run-on-device.sh")
    push(unit, UNIT)
    ssh("systemctl daemon-reload")
    print("[*] deployed; unit installed (started on demand, not at boot)")
    print("[*] config lives at", f"{APP_DIR}/config.json", "- enter it in the")
    print("    app's Settings page on the tablet, or edit that file over ssh.")
    return 0


def cmd_start(args):
    _, out = ssh("systemctl start rm-chat.service; sleep 3; "
                 "pidof rm_chat >/dev/null && echo RUNNING || echo FAILED")
    print(out)
    if "RUNNING" not in out:
        print(ssh("journalctl -u rm-chat.service -n 20 --no-pager")[1], file=sys.stderr)
        ssh("systemctl start xochitl")
        return 1
    print("[*] chat is on screen. 4-finger hold (or `stop`) returns to the tablet.")
    return 0


def cmd_stop(args):
    ssh("systemctl stop rm-chat.service; sleep 1; systemctl start xochitl; true")
    _, out = ssh("systemctl is-active xochitl")
    print(f"xochitl: {out.splitlines()[-1] if out else '?'}")
    return 0


def cmd_status(args):
    _, out = ssh(
        "echo \"rm-chat unit:  $(systemctl is-active rm-chat.service 2>/dev/null)\"; "
        "echo \"xochitl:       $(systemctl is-active xochitl)\"; "
        f"echo \"config:        $(test -f {APP_DIR}/config.json && echo present || echo none yet)\"; "
        f"echo \"history:       $(test -f {APP_DIR}/history.json && echo present || echo none)\"; "
        "echo \"wifi:          $(nmcli -t -f NAME,DEVICE connection show --active 2>/dev/null "
        "| head -n 1 || echo unknown)\"")
    print(out)
    return 0


def cmd_selftest(args):
    """Run the real credentials check from the device, using the real stack."""
    if not require_device():
        return 1
    print("[*] asking the tablet to call your API with the stored config")
    rc, out = ssh(f"cd {APP_DIR} && QT_QUICK_BACKEND=software "
                  f"./rm_chat -platform offscreen --dir {APP_DIR} --selftest 2>&1",
                  timeout=120)
    print(out)
    if "PASS" in out:
        print("\nRESULT: credentials accepted")
        return 0
    print("\nRESULT: rejected - compare the reported length with your real key.")
    print("If the length differs, characters were lost or mistyped on the")
    print("tablet keyboard; ./tools/chatapp.py setkey avoids retyping it.")
    return 1


def cmd_setkey(args):
    """Write the API key to the tablet without typing it on e-paper."""
    import getpass

    if not require_device():
        return 1

    print("This sends the key from THIS PC to the tablet over the USB cable.")
    print("It is not echoed, not logged, and not stored on this PC - but it")
    print("does pass through this machine's memory and your clipboard if you")
    print("paste it. Prefer typing it on the tablet if that matters to you.")
    key = getpass.getpass("API key (input hidden, Enter to cancel): ").strip()
    if not key:
        print("cancelled")
        return 1

    # scp a locally-built config file. Two device realities force this: a pty
    # echoes stdin back and sendeof() does not reliably close `cat` on the far
    # side (the stdin approach hangs), and the tablet has no python3 to merge
    # JSON with - so the JSON is assembled here.
    import json
    import stat
    import tempfile

    _, cur = ssh(f"cat {APP_DIR}/config.json 2>/dev/null || echo '{{}}'")
    try:
        cfg = json.loads(cur[cur.index("{"):cur.rindex("}") + 1])
    except Exception:
        cfg = {}
    cfg["api_key"] = key
    cfg.setdefault("api_base", "https://api.openai.com/v1")
    cfg.setdefault("model", "gpt-4o-mini")

    fd, tmp = tempfile.mkstemp()
    try:
        os.fchmod(fd, stat.S_IRUSR | stat.S_IWUSR)
        with os.fdopen(fd, "w") as fh:
            json.dump(cfg, fh, indent=2)
        try:
            ssh(f"mkdir -p {APP_DIR}")
            push(tmp, f"{APP_DIR}/config.json")
        except RuntimeError as exc:
            print(f"Transfer failed: {exc}", file=sys.stderr)
            return 1
    finally:
        os.unlink(tmp)

    awk = """awk -F'"' '/api_key/{print "stored key length: " length($4)}'"""
    _, out = ssh(f"chmod 600 {APP_DIR}/config.json; {awk} {APP_DIR}/config.json")
    print(out)

    if "stored key length:" not in out:
        print("The key was NOT stored - the tablet reported nothing back.",
              file=sys.stderr)
        return 1

    stored = int(out.split("stored key length:")[1].split()[0])
    if stored != len(key):
        print(f"Length mismatch: typed {len(key)}, stored {stored}.",
              file=sys.stderr)
        return 1

    print(f"[*] stored {stored} characters (matches what you entered).")
    print("[*] verify against your provider with: ./tools/chatapp.py selftest")
    return 0


def cmd_logs(args):
    _, out = ssh(f"journalctl -u rm-chat.service -n {args.lines} --no-pager "
                 "2>/dev/null || echo '(no journal)'")
    print(out)
    return 0


def cmd_screenshot(args):
    dest = os.path.abspath(args.output)
    print("[*] rendering offscreen on the device (display untouched)")
    push(os.path.join(REPO, "rm-chat", "build", "rm_chat"), f"{APP_DIR}/.rm_chat.shot")
    ssh(f"chmod 755 {APP_DIR}/.rm_chat.shot")
    _, log = ssh(f"cd {APP_DIR} && QT_QUICK_BACKEND=software ./.rm_chat.shot "
                 f"-platform offscreen --dir {APP_DIR} "
                 f"--screenshot {APP_DIR}/shot.png 2>&1 | tail -n 3; "
                 f"ls -l {APP_DIR}/shot.png")
    print(log)
    pull(f"{APP_DIR}/shot.png", dest)
    print(f"[*] saved {dest}")
    return 0


def cmd_test(args):
    """Full pipeline test: virtual pen + fake server, nothing real touched."""
    server = subprocess.Popen([sys.executable,
                               os.path.join(HERE, "fake_openai_server.py")],
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True)
    print("[*] fake OpenAI server on :8765")
    time.sleep(0.5)
    # The host firewall drops inbound connections from the device, so expose
    # the server on the DEVICE side of the ssh link instead (reverse tunnel).
    tunnel = pexpect.spawn("ssh", SSH_OPTS + ["-N", "-R", "8765:127.0.0.1:8765",
                                              f"{USER}@{HOST}"],
                           timeout=30, encoding="utf-8")
    if tunnel.expect(["[Pp]assword:", pexpect.TIMEOUT]) == 0:
        tunnel.sendline(_pw())
    time.sleep(2)
    print("[*] reverse tunnel up: device 127.0.0.1:8765 -> host server")
    try:
        print("[*] cleaning up stale test processes")
        ssh("killall rm_chat 2>/dev/null; killall peninject 2>/dev/null; true")
        print("[*] preparing test config on device (separate dir, real config untouched)")
        ssh(f"mkdir -p /tmp/rm-chat-test && rm -f /tmp/rm-chat-test/*.json && "
            f"printf '%s' '{{\"api_base\":\"http://127.0.0.1:8765/v1\","
            f"\"api_key\":\"test-key\",\"model\":\"fake-vision-model\"}}' "
            f"> /tmp/rm-chat-test/config.json")

        print("[*] running app offscreen with a virtual pen scribble + auto-send")
        # No --screenshot on this run (its timer would quit the app after
        # 1.2 s, before the pen at ~6 s and the auto-send at 14 s). BusyBox
        # has no `timeout`, so background + kill.
        _, out = ssh(
            f"cd {APP_DIR} && "
            "( /home/root/apps/peninject 6000 >/tmp/peninject.log 2>&1 & ) && "
            "sleep 0.5 && DEV=$(ls -t /dev/input/event* | head -n 1) && "
            "echo \"virtual pen: $DEV\" && ls -l $DEV && "
            "RM_PEN_DEVICE=$DEV QT_QUICK_BACKEND=software ./rm_chat -platform offscreen "
            "--dir /tmp/rm-chat-test --auto-send-ms 14000 "
            ">/tmp/rm-chat-e2e.log 2>&1 & "
            "APPPID=$!; sleep 26; kill $APPPID 2>/dev/null; "
            "killall rm_chat 2>/dev/null; "
            "cat /tmp/rm-chat-e2e.log; cat /tmp/peninject.log",
            timeout=120)
        print(out)

        # Render the resulting conversation: history.json now holds it; a
        # fresh offscreen run with --screenshot shows it.
        _, out2 = ssh(
            f"cd {APP_DIR} && QT_QUICK_BACKEND=software ./rm_chat "
            "-platform offscreen --dir /tmp/rm-chat-test "
            "--screenshot /tmp/rm-chat-test/e2e.png 2>&1 | tail -n 2; "
            "ls -l /tmp/rm-chat-test/e2e.png /tmp/rm-chat-test/history.json 2>&1")
        print(out2)

        _, hist = ssh("cat /tmp/rm-chat-test/history.json 2>/dev/null || echo MISSING")
        ok = "pipeline works" in hist
        print("\n[*] history.json:", hist[:400])

        dest = os.path.join(REPO, "chat-e2e.png")
        try:
            pull("/tmp/rm-chat-test/e2e.png", dest)
            print(f"[*] saved {dest}")
        except RuntimeError as exc:
            print(f"    (no screenshot: {exc})")

        print("\nRESULT:", "PASS - ink drawn, encoded, sent, reply stored" if ok
              else "FAIL - see logs above")
        return 0 if ok else 1
    finally:
        # Never leave an offscreen app behind: a stray process makes the
        # loader's gesture think an app is in the foreground.
        ssh("killall rm_chat 2>/dev/null; rm -rf /tmp/rm-chat-test; true", timeout=30)
        tunnel.terminate(force=True)
        server.terminate()


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("deploy").set_defaults(func=cmd_deploy)
    sub.add_parser("start").set_defaults(func=cmd_start)
    sub.add_parser("stop").set_defaults(func=cmd_stop)
    sub.add_parser("status").set_defaults(func=cmd_status)
    sub.add_parser("test").set_defaults(func=cmd_test)
    sub.add_parser("selftest").set_defaults(func=cmd_selftest)
    sub.add_parser("setkey").set_defaults(func=cmd_setkey)

    p_logs = sub.add_parser("logs")
    p_logs.add_argument("-n", "--lines", type=int, default=40)
    p_logs.set_defaults(func=cmd_logs)

    p_shot = sub.add_parser("screenshot")
    p_shot.add_argument("output", nargs="?", default=os.path.join(REPO, "chat-preview.png"))
    p_shot.set_defaults(func=cmd_screenshot)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
