"""Downloads FTXUI and prepares dependencies/ for building TokenRush (Windows, Linux, macOS).

    python tools/fetch_ftxui.py              FTXUI 7.0.3: dependencies/ftxui.hpp, ftxui.cpp and ftxui.o
    python tools/fetch_ftxui.py --version X  another release
    python tools/fetch_ftxui.py --no-compile only the .hpp and .cpp (make builds the rest)

FTXUI's own tools/amalgamate.py makes the single header and source; ftxui.o is the .cpp compiled
once with the same flags build.py uses (-O2), so build.py links it as is. Needs Python 3.10+, a C++
compiler (CXX or g++) and internet.
"""

import argparse
import io
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEPS = ROOT / "dependencies"
VERSION = "7.0.3"


def main() -> None:
    parser = argparse.ArgumentParser(description="Downloads and prepares FTXUI in dependencies/.")
    parser.add_argument("--version", default=VERSION, help=f"FTXUI release (default {VERSION})")
    parser.add_argument("--no-compile", action="store_true", help="skip compiling ftxui.o")
    args = parser.parse_args()

    url = f"https://github.com/ArthurSonzogni/FTXUI/archive/refs/tags/v{args.version}.tar.gz"
    print(f"Downloading {url}")
    with urllib.request.urlopen(url) as response:
        data = response.read()

    with tempfile.TemporaryDirectory() as tmp:
        with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as tar:
            tar.extractall(tmp, filter="data")
        source = next(Path(tmp).iterdir())
        subprocess.run([sys.executable, "-I", str(source / "tools" / "amalgamate.py")], cwd=source, check=True)
        DEPS.mkdir(exist_ok=True)
        for name in ("ftxui.hpp", "ftxui.cpp"):
            shutil.copy2(source / name, DEPS / name)
        shutil.copy2(source / "LICENSE", DEPS / "FTXUI-LICENSE")

    if args.no_compile:
        return
    cxx = os.environ.get("CXX") or "g++"
    flags = ["-std=c++20", "-O2", "-DUNICODE", "-D_UNICODE", "-DNOMINMAX"] if os.name == "nt" else ["-std=c++20", "-O2", "-pthread"]
    print("Compiling ftxui.o (a minute or so)...")
    subprocess.run([cxx, *flags, "-c", str(DEPS / "ftxui.cpp"), "-o", str(DEPS / "ftxui.o")], check=True)
    print(f"Done: {DEPS}")


if __name__ == "__main__":
    main()
