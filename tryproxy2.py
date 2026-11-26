import subprocess, sys, os, time
root = os.path.dirname(os.path.abspath(__file__))
for extra in ([], ["-q"]):
    o = open(os.path.join(os.environ["TEMP"], "mout.txt"), "wb")
    e = open(os.path.join(os.environ["TEMP"], "merr.txt"), "wb")
    p = subprocess.Popen([sys.executable, "-m", "mitmproxy.tools.main", "mitmdump",
                          "--listen-port", "49321", "--set", "confdir=" + os.path.join(os.environ["TEMP"], "mitm5")] + extra,
                         stdout=o, stderr=e, cwd=root, stdin=subprocess.DEVNULL)
    time.sleep(5)
    print(extra, "rc", p.poll())
    if p.poll() is None:
        p.terminate()
    o.close(); e.close()
    print(open(os.path.join(os.environ["TEMP"], "mout.txt")).read()[-1500:])
    print("ERR", open(os.path.join(os.environ["TEMP"], "merr.txt")).read()[-2000:])
