import subprocess, sys, os, time
root = os.path.dirname(os.path.abspath(__file__))
o = open(os.path.join(os.environ["TEMP"], "mout.txt"), "wb")
e = open(os.path.join(os.environ["TEMP"], "merr.txt"), "wb")
p = subprocess.Popen([sys.executable, "-m", "mitmproxy.tools.main", "mitmdump",
                      "--listen-port", "49321", "--set", "confdir=" + os.path.join(os.environ["TEMP"], "mitm4"),
                      "-s", os.path.join(root, "ripple", "proxy_addon.py")],
                     stdout=o, stderr=e, cwd=root)
time.sleep(6)
print("rc", p.poll())
if p.poll() is None:
    p.terminate()
o.close(); e.close()
print(open(os.path.join(os.environ["TEMP"], "mout.txt")).read()[-2000:])
print("ERR")
print(open(os.path.join(os.environ["TEMP"], "merr.txt")).read()[-3000:])
