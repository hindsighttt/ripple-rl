import os, sys, subprocess, winreg, time, shutil
from . import store, titles_config

PORT = 49320
FORWARD_PORT = 49321
PROC = None
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONF = os.path.join(store.STATE_DIR, "mitm")


def _reg(enable, server=None):
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER,
                            r"Software\Microsoft\Windows\CurrentVersion\Internet Settings",
                            0, winreg.KEY_SET_VALUE) as k:
            winreg.SetValueEx(k, "ProxyEnable", 0, winreg.REG_DWORD, 1 if enable else 0)
            if server is not None:
                winreg.SetValueEx(k, "ProxyServer", 0, winreg.REG_SZ, server)
    except Exception:
        pass


def _machine_has(thumb):
    try:
        out = subprocess.run(["certutil", "-store", "Root"], capture_output=True, text=True).stdout
        return thumb in out or "mitmproxy" in out
    except Exception:
        return False


def ca_installed():
    ca = os.path.join(CONF, "mitmproxy-ca-cert.cer")
    if not os.path.exists(ca):
        return False
    try:
        from cryptography import x509
        thumb = x509.load_pem_x509_certificate(open(ca.replace(".cer", ".pem"), "rb").read()).fingerprint(__import__("cryptography.hazmat.primitives.hashes", fromlist=["SHA256"]).SHA256()).hex().upper()
    except Exception:
        return _machine_has("mitmproxy")
    return _machine_has(thumb)


def install_ca():
    """Install the mitmproxy root into the machine store (RL/Schannel ignores user roots)."""
    ca = os.path.join(CONF, "mitmproxy-ca-cert.cer")
    if not os.path.exists(ca):
        return
    if ca_installed():
        return
    try:
        subprocess.run(["certutil", "-addstore", "-f", "Root", ca], capture_output=True)
    except Exception:
        pass
    if ca_installed():
        return
    try:
        subprocess.Popen(["certutil", "-addstore", "-f", "Root", ca], shell=False,
                         creationflags=0x00000040)  # CREATE_NO_WINDOW won't elevate
        subprocess.run(["powershell", "-NoProfile", "-Command",
                        "Start-Process certutil -Verb RunAs -ArgumentList '-addstore','-f','Root','\"%s\"'" % ca],
                       capture_output=True)
    except Exception:
        pass


def running():
    return PROC is not None and PROC.poll() is None


def start():
    global PROC
    if running():
        return True
    for port in (PORT, FORWARD_PORT):
        if _port_open(port):
            raise RuntimeError("Port %d is occupied. Close the other Ripple RL instance first." % port)
    os.makedirs(CONF, exist_ok=True)
    env = dict(os.environ)
    env["PYTHONUNBUFFERED"] = "1"
    env["PYTHONPATH"] = ROOT + os.pathsep + env.get("PYTHONPATH", "")
    exe = os.path.join(os.path.dirname(sys.executable), "Scripts", "mitmdump.exe")
    PROC = subprocess.Popen(
        [exe, "--set", "flow_detail=0", "--set", "termlog_verbosity=info", "--listen-host", "127.0.0.1",
         "--mode", "reverse:http://api.rlpp.psynet.gg@%d" % PORT,
         "--mode", "regular@%d" % FORWARD_PORT,
         "--set", "http3=false",
         "--set", "connection_strategy=lazy",
         "--set", "confdir=" + CONF, "-s", os.path.join(ROOT, "ripple", "proxy_addon.py")],
        env=env, cwd=ROOT, stdout=open(os.path.join(store.STATE_DIR, "mitm_stdout.log"), "ab"),
        stderr=open(os.path.join(store.STATE_DIR, "mitm.log"), "ab"),
        creationflags=0x08000000,
    )
    for _ in range(120):
        time.sleep(0.1)
        if all(_port_open(port) for port in (PORT, FORWARD_PORT)):
            break
        if PROC.poll() is not None:
            break
    if not running() or not all(_port_open(port) for port in (PORT, FORWARD_PORT)):
        if running():
            PROC.terminate()
            PROC.wait(3)
        raise RuntimeError("Proxy failed to listen on port %d. See mitm.log." % PORT)
    install_ca()
    patch_webcache()
    try:
        titles_config.refresh_webcache()
    except Exception:
        pass
    _reg(True, "127.0.0.1:%d" % FORWARD_PORT)
    return True


def stop():
    """Fully tear down the proxy: kill mitmdump, turn the Windows proxy off and
    clear its address, and put RL's patched WebCache back. Every step is
    independent so a failure in one still runs the rest."""
    global PROC
    if PROC and PROC.poll() is None:
        try:
            PROC.terminate()
            try:
                PROC.wait(3)
            except Exception:
                PROC.kill()
        except Exception:
            pass
    PROC = None
    # Disable the system proxy and clear the stored "127.0.0.1:49321" address so
    # nothing is left pointing at a listener that is now gone.
    _reg(False, "")
    # Restore title-injected cache files first; the state backup pass below puts
    # the pristine (pre-URL-retarget) copies back on top of them.
    try:
        titles_config.restore_webcache()
    except Exception:
        pass
    restore_webcache()


def restore_webcache():
    """Revert the WebCache files patch_webcache() rewrote, from their backups.
    Without this, RL's cached PsyNet config keeps pointing at our (now stopped)
    proxy, which would break RL until the files were repatched or regenerated."""
    backup_dir = os.path.join(store.STATE_DIR, "webcache_backups")
    if not os.path.isdir(backup_dir):
        return
    base = os.path.join(os.environ.get("USERPROFILE", ""), "Documents", "My Games", "Rocket League",
                        "TAGame", "Cache", "WebCache")
    for name in os.listdir(backup_dir):
        src = os.path.join(backup_dir, name)
        dst = os.path.join(base, name)
        if not os.path.isfile(src):
            continue
        try:
            original = open(src, "rb").read()
            if not os.path.exists(dst) or open(dst, "rb").read() != original:
                shutil.copy2(src, dst)
        except Exception:
            pass


def _port_open(port=PORT):
    import socket
    s = socket.socket()
    s.settimeout(0.3)
    try:
        s.connect(("127.0.0.1", port))
        return True
    except Exception:
        return False
    finally:
        s.close()

def patch_webcache(port=PORT):
    """Ripple RL caches the PsyNet config with 127.0.0.1:49320 baked in; retarget it to us."""
    import glob
    base = os.path.join(os.environ.get("USERPROFILE", ""), "Documents", "My Games", "Rocket League",
                        "TAGame", "Cache", "WebCache")
    for p in glob.glob(os.path.join(base, "*")):
        if not os.path.isfile(p):
            continue
        try:
            b = open(p, "rb").read()
        except Exception:
            continue
        endpoint = ("127.0.0.1:%d" % port).encode()
        nb = b.replace(b"127.0.0.1:49320", endpoint).replace(b"127.0.0.1:49321", endpoint)
        if nb == b:
            continue
        try:
            backup_dir = os.path.join(store.STATE_DIR, "webcache_backups")
            os.makedirs(backup_dir, exist_ok=True)
            backup = os.path.join(backup_dir, os.path.basename(p))
            if not os.path.exists(backup):
                shutil.copy2(p, backup)
            open(p, "wb").write(nb)
        except Exception:
            pass

