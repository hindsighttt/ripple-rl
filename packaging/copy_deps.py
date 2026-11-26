# Copy mitmproxy + pycryptodome and their full dependency closure (the exact
# installed files) into a target site-packages dir. Offline: reads installed
# package metadata, evaluates environment markers, skips extras.
import importlib.metadata as im
import os, shutil, sys

try:
    from packaging.requirements import Requirement
    HAVE_PACKAGING = True
except Exception:
    HAVE_PACKAGING = False

TARGET = sys.argv[1]
ROOTS = ["mitmproxy", "pycryptodome"]

seen = set()
dists = []


def canon(n):
    return n.lower().replace("_", "-").replace(".", "-")


def visit(name):
    c = canon(name)
    if c in seen:
        return
    seen.add(c)
    try:
        dist = im.distribution(name)
    except Exception:
        return
    dists.append(dist)
    for req in (dist.requires or []):
        if HAVE_PACKAGING:
            try:
                r = Requirement(req)
            except Exception:
                continue
            if r.marker and not r.marker.evaluate({"extra": ""}):
                continue
            visit(r.name)
        else:
            dep = req.split(";")[0].split("[")[0].strip()
            dep = dep.split("<")[0].split(">")[0].split("=")[0].split("!")[0].split("~")[0].strip()
            if ";" in req and "extra" in req.split(";", 1)[1]:
                continue
            if dep:
                visit(dep)


for r in ROOTS:
    visit(r)

copied = 0
for dist in dists:
    for f in (dist.files or []):
        try:
            src = str(dist.locate_file(f))
            dst = os.path.join(TARGET, str(f))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(src, dst)
            copied += 1
        except Exception:
            pass

print("DISTS:", ", ".join(sorted(d.metadata["Name"] for d in dists)))
print("FILES_COPIED:", copied)
