import subprocess, sys, os, time
root = os.path.dirname(os.path.abspath(__file__))
exe = os.path.join(os.path.dirname(sys.executable), "Scripts", "mitmdump.exe")
print("exe", os.path.exists(exe))
o = open(os.path.join(os.environ["TEMP"], "mout.txt"), "wb")
e = open(os.path.join(os.environ["TEMP"], "merr.txt"), "wb")
p = subprocess.Popen([exe, "--listen-port", "49321", "--set", "confdir=" + os.path.join(os.environ["TEMP"], "mitm6"), "-s", os.path.join(root, "ripple", "proxy_addon.py")],
                     stdout=o, stderr=e, cwd=root, stdin=subprocess.DEVNULL)
time.sleep(6)
print("rc", p.poll())
if p.poll() is None:
    p.terminate()
o.close(); e.close()
print(open(os.path.join(os.environ["TEMP"], "mout.txt")).read()[-1500:])
print("ERR", open(os.path.join(os.environ["TEMP"], "merr.txt")).read()[-2500:])
