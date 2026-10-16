"""Builds TokenRush into target/debug/.

    python build.py              tokenrush-tui.exe (needs dependencies/ftxui.hpp + ftxui.o)
    python build.py --tui        the same
    python build.py --sim        the balance simulator, sim.exe (see README)
    python build.py --rebuild    ignore the saved hashes and compile everything again

Incremental: every .cpp becomes its own .o in target/debug/obj/, next to a .hash file. The hash
covers the .cpp, every header it includes (g++ writes that list into a .d file while compiling)
and the compiler flags. A file is only compiled again when that hash changes, so editing one
.cpp recompiles one file, and editing a header recompiles exactly the files that include it.
The core is compiled once and shared by the game and the simulator. An exe is only relinked when
one of its objects changed.

Every build that does something is timed and appended to target/debug/build-times.log.
Needs Python 3.10+ and g++ on the PATH; nothing else.
"""

import argparse
import hashlib
import re
import subprocess
import sys
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SRC = ROOT / "src"
DEPS = ROOT / "dependencies"
OUT = ROOT / "target" / "debug"
OBJ_DIR = OUT / "obj"
LOG = OUT / "build-times.log"

# UNICODE/NOMINMAX: FTXUI requires them on Windows, so everything is built the same way. Every file
# gets the same flags (FTXUI's include path too), so the core's objects are shared by every exe.
FLAGS = ["-std=c++20", "-O2", "-Wall", "-Wextra", "-DUNICODE", "-D_UNICODE", "-DNOMINMAX", f"-I{SRC}", f"-I{DEPS}"]


class BuildError(Exception):
    pass


@dataclass
class Target:
    name: str
    sources: list[Path]
    link_extra: list[Path]


def sources(folder: str, /) -> list[Path]:
    return sorted((SRC / folder).glob("*.cpp"))


def relative(path: Path, /) -> str:
    return str(path.relative_to(ROOT))


def object_path(source: Path, /) -> Path:
    """target/debug/obj/core/game.o for src/core/game.cpp, obj/tools/sim.o for tools/sim.cpp."""
    rel = source.relative_to(ROOT)
    if rel.parts[0] == "src":
        rel = Path(*rel.parts[1:])
    return (OBJ_DIR / rel).with_suffix(".o")


def hash_text(text: str, /) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def deps_of(dep_file: Path, /) -> list[str]:
    """The files a .d file lists: the .cpp first, then every header it pulled in.

    The format is make's: "obj.o: a.cpp b.hpp \\<newline> c.hpp", with spaces in paths written
    as "\\ ". Paths can contain "C:", so the target is cut off at its ".o:".
    """
    text = dep_file.read_text(encoding="utf-8", errors="replace")
    text = re.sub(r"\\\r?\n", " ", text)
    text = re.sub(r"^.*?\.o:\s", "", text, count=1)
    return [part.replace("\\ ", " ") for part in re.split(r"(?<!\\)\s+", text) if part]


def last_time(name: str, /) -> float | None:
    """The seconds of the last successful build of `name`, from the log."""
    if not LOG.exists():
        return None
    pattern = re.compile(r"\s" + re.escape(name) + r"\s+([\d.]+)s\s+ok\b")
    times = [float(m.group(1)) for line in LOG.read_text(encoding="utf-8").splitlines() if (m := pattern.search(line))]
    return times[-1] if times else None


class Builder:
    def __init__(self, *, rebuild: bool = False, progress: bool = False):
        self.rebuild = rebuild
        self.progress = progress   # also print "@progress <done> <total> <what>" lines (play.py draws a bar from them)
        self.file_hashes: dict[str, str] = {}   # a header shared by many files is read once per build
        self.compiled = 0
        self.step = self.steps = 0

    def __repr__(self) -> str:
        mode = "rebuilding everything" if self.rebuild else "incremental"
        return f"Builder ({mode}): {self.compiled} files compiled so far"

    def hash_file(self, path: str, /) -> str:
        if path not in self.file_hashes:
            file = Path(path)
            self.file_hashes[path] = hashlib.sha256(file.read_bytes()).hexdigest() if file.is_file() else "missing"
        return self.file_hashes[path]

    def object_hash(self, dep_file: Path, /) -> str:
        """One hash for everything that goes into an object: the flags, then each input's path and contents."""
        parts = [" ".join(FLAGS)] + [f"{f}={self.hash_file(f)}" for f in deps_of(dep_file)]
        return hash_text("\n".join(parts))

    def needs_compile(self, source: Path, /) -> bool:
        """False if the object is there and its saved hash still matches everything that went into it."""
        obj = object_path(source)
        dep, saved = obj.with_suffix(".d"), obj.with_suffix(".hash")
        if self.rebuild or not (obj.exists() and dep.exists() and saved.exists()):
            return True
        return saved.read_text().strip() != self.object_hash(dep)

    def report(self, what: str, /) -> None:
        if self.progress:
            print(f"@progress {self.step} {self.steps} {what}", flush=True)

    def compile(self, source: Path, /) -> Path:
        """Compiles `source` unless its saved hash still matches. Returns the object's path."""
        obj = object_path(source)
        dep, saved = obj.with_suffix(".d"), obj.with_suffix(".hash")

        if not self.needs_compile(source):
            return obj

        obj.parent.mkdir(parents=True, exist_ok=True)
        saved.unlink(missing_ok=True)   # a failed compile must not look up to date
        self.report(relative(source))
        print(f"  compiling {relative(source)}", flush=True)
        run(["g++", *FLAGS, "-MMD", "-MF", str(dep), "-c", str(source), "-o", str(obj)],
            failure=f"compile failed: {relative(source)}")

        # Hash after compiling: the .d file now lists exactly the headers this compile used.
        self.file_hashes.clear()   # in case a file changed while we were compiling
        saved.write_text(self.object_hash(dep))
        self.compiled += 1
        self.step += 1
        return obj

    def build(self, target: Target, /) -> None:
        exe = OUT / target.name
        previous = last_time(target.name)
        print(f"Building {target.name} ...", flush=True)
        start = time.perf_counter()
        before = self.compiled
        if self.progress:   # every file that needs compiling, plus the link
            self.step, self.steps = 0, sum(self.needs_compile(s) for s in target.sources) + 1
            self.report("starting")

        try:
            objects = [self.compile(source) for source in target.sources]
            count = self.compiled - before

            # Relink only when an object (or an extra link input like ftxui.o) changed.
            link_hash_file = OBJ_DIR / f"{target.name}.link.hash"
            parts = [" ".join(map(str, target.link_extra))]
            parts += [f"{o}={o.with_suffix('.hash').read_text().strip()}" for o in objects]
            parts += [f"{l}={self.hash_file(str(l))}" for l in target.link_extra]
            link_hash = hash_text("\n".join(parts))
            fresh = exe.exists() and link_hash_file.exists() and link_hash_file.read_text().strip() == link_hash

            if fresh and not self.rebuild:
                # (files can compile and still match the exe, e.g. after undoing a change)
                note = "up to date" if count == 0 else f"{count} compiled, same as the current exe"
            else:
                link_hash_file.unlink(missing_ok=True)
                self.step = self.steps - 1
                self.report(f"linking {target.name}")
                print(f"  linking {target.name}", flush=True)
                run(["g++", *FLAGS, *map(str, objects), *map(str, target.link_extra), "-o", str(exe)],
                    failure=f"link failed: {target.name}")
                link_hash_file.write_text(link_hash)
                note = f"{count} of {len(objects)} files compiled"
            status = "ok"
            self.step = self.steps
            self.report("done")
        except BuildError as error:
            status, note = "failed", str(error)

        secs = f"{time.perf_counter() - start:.1f}"
        if note != "up to date":   # only builds that did something are worth timing
            stamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            with LOG.open("a", encoding="utf-8") as log:
                log.write(f"{stamp}  {target.name:<18} {secs:>6}s  {status}  ({note})\n")

        if status != "ok":
            print(f"Build of {target.name} failed after {secs}s: {note}")
            sys.exit(1)
        compare = f"; last time {previous:.1f}s" if previous is not None else ""
        print(f"Built {exe} in {secs}s ({note}{compare})")


def run(command: list[str], /, *, failure: str) -> None:
    """Runs a g++ command with its output going straight to the console."""
    try:
        result = subprocess.run(command)
    except FileNotFoundError:
        raise BuildError("g++ isn't on the PATH") from None
    if result.returncode != 0:
        raise BuildError(failure)


def main() -> None:
    parser = argparse.ArgumentParser(description="Builds TokenRush into target/debug/.")
    parser.add_argument("--tui", action="store_true", help="build tokenrush-tui.exe")
    parser.add_argument("--sim", action="store_true", help="build the balance simulator, sim.exe")
    parser.add_argument("--rebuild", action="store_true", help="ignore the saved hashes and compile everything")
    parser.add_argument("--progress", action="store_true", help="also print @progress lines (for play.py's progress bar)")
    args = parser.parse_args()
    if not (args.tui or args.sim):
        args.tui = True

    OBJ_DIR.mkdir(parents=True, exist_ok=True)
    builder = Builder(rebuild=args.rebuild, progress=args.progress)
    start = time.perf_counter()
    core = sources("core")
    shell = sources("shell")   # the typed commands, run by the TUI's Console tab

    if args.tui:
        ftxui_obj = DEPS / "ftxui.o"
        if not ftxui_obj.exists() or not (DEPS / "ftxui.hpp").exists():
            print("Skipping tokenrush-tui.exe: put ftxui.hpp and ftxui.o in dependencies/ first (see README).")
        else:
            builder.build(Target("tokenrush-tui.exe", core + shell + sources("tui"), [ftxui_obj]))

    if args.sim:
        builder.build(Target("sim.exe", core + [ROOT / "tools" / "sim.cpp"], []))

    print(f"Done in {time.perf_counter() - start:.1f}s")


if __name__ == "__main__":
    main()
