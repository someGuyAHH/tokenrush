"""Starts TokenRush, and updates it when you say so.

    python play.py [game options]      e.g. python play.py --profile Bora

Started from here, the game checks GitHub for new commits in the background and asks on the
profile screen (or press u in the game). On a yes it saves and quits with exit code 42, and this
script pulls the new code, builds it and starts the game again. If the pull or the build fails,
it says why and starts the version you already had.

The game can't do this itself: Windows won't let a running .exe be overwritten.
Needs git and everything build.py needs. --no-update-check (passed on to the game) skips the check.
"""

import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
EXE = ROOT / "target" / "debug" / "tokenrush-tui.exe"
UPDATE_EXIT_CODE = 42   # tui::UPDATE_EXIT_CODE


def build() -> bool:
    return subprocess.run([sys.executable, str(ROOT / "build.py")]).returncode == 0 and EXE.exists()


def update() -> bool:
    print("\nUpdating TokenRush...")
    if subprocess.run(["git", "-C", str(ROOT), "pull", "--ff-only"]).returncode != 0:
        print("\nThe update couldn't be pulled (see above). Starting the version you have.")
    elif not build():
        print("\nThe new version didn't build (see above). Starting the version you have.")
    else:
        return True
    input("Press Enter to continue...")
    return False


def play(args: list[str]) -> int:
    env = dict(os.environ, TOKENRUSH_REPO=str(ROOT), GIT_TERMINAL_PROMPT="0")
    env.setdefault("GIT_SSH_COMMAND", "ssh -o BatchMode=yes")   # never stop to ask for a password
    game = subprocess.Popen([str(EXE), *args], env=env)
    while True:
        try:
            return game.wait()
        except KeyboardInterrupt:   # Ctrl+C is the game's to handle (it saves first); keep waiting
            continue


def main() -> None:
    args = sys.argv[1:]
    if not EXE.exists() and not build():
        sys.exit("TokenRush isn't built, and building it failed (see above).")
    retry = False   # after a failed update, don't ask again until the next start
    while True:
        code = play(args + (["--no-update-check"] if retry else []))
        if code != UPDATE_EXIT_CODE:
            sys.exit(code)
        retry = not update()


if __name__ == "__main__":
    main()
