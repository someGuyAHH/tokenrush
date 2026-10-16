"""Starts TokenRush, and updates it when you say so.

    python play.py [game options]      e.g. python play.py --profile Bora

Started from here, the game checks GitHub for new commits in the background and asks on the
profile screen (or press u in the game). On a yes it saves and quits with exit code 42, and this
script clears the screen, says what's new, pulls it and builds it behind a progress bar, then
starts the new launcher (and through it the game). If the pull or the build fails, it shows what
git or the compiler said and starts the version you already had.

The game can't do this itself: Windows won't let a running .exe be overwritten.
Needs git and everything build.py needs. --no-update-check (passed on to the game) skips the check.
"""

import os
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent
EXE = ROOT / "target" / "debug" / "tokenrush-tui.exe"
UPDATE_EXIT_CODE = 42   # UPDATE_EXIT_CODE in src/core/update.hpp
BAR_WIDTH = 30
GIT_ENV = dict(os.environ, GIT_TERMINAL_PROMPT="0", GIT_SSH_COMMAND=os.environ.get("GIT_SSH_COMMAND", "ssh -o BatchMode=yes"))


@dataclass
class Step:
    ok: bool
    log: list[str] = field(default_factory=list)   # what it printed, kept out of sight unless it failed


def clear_screen() -> None:
    os.system("cls" if os.name == "nt" else "clear")


def run_quiet(command: list[str], /) -> Step:
    try:
        result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8", errors="replace", env=GIT_ENV)
    except FileNotFoundError:
        return Step(False, [f"{command[0]} isn't installed (or isn't on the PATH)."])
    return Step(result.returncode == 0, (result.stdout + result.stderr).splitlines())


def draw_bar(done: int, total: int, what: str, /) -> None:
    """One line, redrawn in place with a carriage return (no escape codes: any console can show it)."""
    share = done / total if total else 1.0
    filled = round(BAR_WIDTH * share)
    line = f"  Building  [{'#' * filled}{'-' * (BAR_WIDTH - filled)}] {share:4.0%}  {done}/{total}  {what}"
    width = shutil.get_terminal_size((80, 24)).columns - 1
    print("\r" + line[:width].ljust(width), end="", flush=True)


def build() -> Step:
    """build.py with its output captured and a progress bar drawn from its @progress lines."""
    log: list[str] = []
    process = subprocess.Popen([sys.executable, str(ROOT / "build.py"), "--progress"], stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace", bufsize=1)
    assert process.stdout is not None
    for line in process.stdout:
        line = line.rstrip("\r\n")
        if line.startswith("@progress "):
            parts = line.split(" ", 3)
            draw_bar(int(parts[1]), int(parts[2]), parts[3] if len(parts) > 3 else "")
        else:
            log.append(line)
    print()
    return Step(process.wait() == 0 and EXE.exists(), log)


def show_failure(step: Step, what: str, /) -> None:
    print(f"\n{what} Here's what it said:\n")
    for line in step.log[-60:]:
        print(f"  {line}")
    print()


def update() -> bool:
    clear_screen()
    print("Updating TokenRush\n")
    news = run_quiet(["git", "-C", str(ROOT), "log", "--format=%s", "HEAD..@{u}"])
    titles = news.log if news.ok else []
    if titles:
        print("What's new:")
        for title in titles[:10]:
            print(f"  - {title}")
        if len(titles) > 10:
            print(f"  ...and {len(titles) - 10} more")
        print()

    print("  Downloading...", end="", flush=True)
    pulled = run_quiet(["git", "-C", str(ROOT), "pull", "--ff-only"])
    print(" done." if pulled.ok else " failed.")
    if not pulled.ok:
        show_failure(pulled, "The update couldn't be downloaded.")
    else:
        built = build()
        if built.ok:
            print("\nUpdated. Starting the new version...")
            return True
        show_failure(built, "The new version didn't build.")
    input("Starting the version you have. Press Enter to continue...")
    return False


def wait_for(process: subprocess.Popen, /) -> int:
    while True:
        try:
            return process.wait()
        except KeyboardInterrupt:   # Ctrl+C is the child's to handle (the game saves first); keep waiting
            continue


def restart(args: list[str], /) -> None:
    """Hands over to the play.py that was just downloaded, so the launcher is new too."""
    command = [sys.executable, str(ROOT / "play.py"), *args]
    if os.name == "nt":   # (os.execv on Windows leaves two programs reading the same console)
        sys.exit(wait_for(subprocess.Popen(command)))
    os.execv(sys.executable, command)


def console_modes() -> list:
    """The Windows console's input and output modes, to put back after the game.

    The game switches the console into its own modes (no line editing, and a line feed that
    doesn't return to the start of the line). If any of that is left behind, git's and g++'s
    output comes out as a staircase. Elsewhere there's nothing to save: [].
    """
    if os.name != "nt":
        return []
    import ctypes
    from ctypes import wintypes
    kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel32.GetStdHandle.restype = wintypes.HANDLE
    kernel32.GetConsoleMode.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
    kernel32.SetConsoleMode.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    modes = []
    for which in (-10, -11):   # STD_INPUT_HANDLE, STD_OUTPUT_HANDLE
        handle = kernel32.GetStdHandle(which)
        mode = wintypes.DWORD()
        if kernel32.GetConsoleMode(handle, ctypes.byref(mode)):
            modes.append((kernel32, handle, mode.value))
    return modes


def restore_console(modes: list) -> None:
    for kernel32, handle, mode in modes:
        kernel32.SetConsoleMode(handle, mode)


def play(args: list[str], /) -> int:
    env = dict(GIT_ENV, TOKENRUSH_REPO=str(ROOT))   # git never stops to ask for a password
    modes = console_modes()
    try:
        return wait_for(subprocess.Popen([str(EXE), *args], env=env))
    finally:
        restore_console(modes)


def main() -> None:
    args = sys.argv[1:]
    if not EXE.exists():
        print("Building TokenRush for the first time...")
        built = build()
        if not built.ok:
            show_failure(built, "It didn't build.")
            sys.exit(1)
    retry = False   # after a failed update, don't ask again until the next start
    while True:
        code = play(args + (["--no-update-check"] if retry else []))
        if code != UPDATE_EXIT_CODE:
            sys.exit(code)
        if update():
            restart(args)
        retry = True


if __name__ == "__main__":
    main()
