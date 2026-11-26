"""Stage the offline Windows runtime and the files used by the portable app."""

from importlib.metadata import distribution
from pathlib import Path
import shutil
import subprocess
import sys
import sysconfig


ROOT = Path(__file__).resolve().parents[1]
PATCH_MODULES = ("bootstrap", "garage_recipe", "package_writer", "patch_recipe", "patches", "store", "upk")


def main(package: Path) -> None:
    if sys.version_info[:2] != (3, 12) or sysconfig.get_platform() != "win-amd64":
        raise SystemExit("Use 64-bit Windows Python 3.12 with pycryptodome installed.")

    py_home = Path(sys.base_prefix)
    runtime = package / "runtime"
    runtime.mkdir()
    for name in ("python.exe", "python312.dll", "python3.dll", "vcruntime140.dll", "vcruntime140_1.dll", "LICENSE.txt"):
        shutil.copy2(py_home / name, runtime / name)
    shutil.copytree(py_home / "DLLs", runtime / "DLLs", ignore=shutil.ignore_patterns("_test*", "_ctypes_test*", "*.ico"))
    shutil.copytree(py_home / "Lib", runtime / "Lib", ignore=shutil.ignore_patterns(
        "site-packages", "__pycache__", "ensurepip", "idlelib", "test", "tkinter", "turtledemo", "venv",
    ))

    crypto = distribution("pycryptodome")
    crypto_files = crypto.files
    if not crypto_files:
        raise SystemExit("The pycryptodome installation has no file manifest.")
    site_packages = runtime / "Lib/site-packages"
    for file in crypto_files:
        if (file.parts[0] != "Crypto" and not file.parts[0].endswith(".dist-info")) or "__pycache__" in file.parts:
            continue
        target = site_packages / file
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(file.locate(), target)
    (runtime / "python312._pth").write_text("Lib\nDLLs\nLib/site-packages\n", encoding="ascii")

    tracked = subprocess.check_output(["git", "ls-files", "--", "data"], cwd=ROOT, text=True)
    (package / "data").mkdir()
    for filename in tracked.splitlines():
        shutil.copy2(ROOT / filename, package / filename)
    (package / "ripple").mkdir()
    for module in PATCH_MODULES:
        filename = f"ripple/{module}.py"
        shutil.copy2(ROOT / filename, package / filename)

    licenses = package / "licenses"
    licenses.mkdir()
    for source, target in (
        ("ui-cpp/assets/InstrumentSans-OFL.txt", "InstrumentSans-OFL.txt"),
        ("ui-cpp/vendor/imgui/LICENSE.txt", "Dear-ImGui-LICENSE.txt"),
        ("ui-cpp/vendor/json/LICENSE.MIT", "nlohmann-json-LICENSE.txt"),
    ):
        shutil.copy2(ROOT / source, licenses / target)
    stb = (ROOT / "ui-cpp/vendor/stb/stb_image.h").read_text(encoding="utf-8")
    license_start = stb.index("This software is available under 2 licenses")
    (licenses / "stb_image-LICENSE.txt").write_text(stb[license_start:stb.rindex("*/")].strip() + "\n", encoding="utf-8")
    (package / "Launch Ripple RL.cmd").write_text(
        '@echo off\nsetlocal\nset "RIPPLE_RL_NO_PROXY=1"\ncd /d "%~dp0"\nstart "" "%~dp0ripple_rl.exe"\n',
        encoding="ascii",
    )
    (package / "README.txt").write_text(
        "Extract the entire ZIP and open Launch Ripple RL.cmd.\n"
        "The portable launcher disables the network proxy.\n"
        "For Garage patch compatibility, see https://github.com/hindsighttt/ripple-rl#garage-patches\n",
        encoding="ascii",
    )
    subprocess.run([str(runtime / "python.exe"), "-I", "-B", "-c",
                    "import sys; sys.path.insert(0, sys.argv[1]); from ripple import garage_recipe, patch_recipe",
                    str(package)], check=True, cwd=package)


if __name__ == "__main__":
    main(Path(sys.argv[1]))
