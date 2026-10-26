"""Disc-only application regressions. Requires a GPU; opt in with OPENDASH_DISC_GAMEPLAY=1.

No PC data, executable, language-pack folder or pre-existing save is supplied to the positive cases.
The negative case must exit with failure rather than quietly fall back to the stage viewer.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--disc", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--failure-exit-only", action="store_true")
    parser.add_argument("--sweep", action="store_true")
    args = parser.parse_args()
    if not args.force and os.environ.get("OPENDASH_DISC_GAMEPLAY") != "1":
        print("Set OPENDASH_DISC_GAMEPLAY=1 to run GPU gameplay checks")
        return 77
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env.update(OPENDASH_DATA="disc", OPENDASH_DISC=str(args.disc.resolve()),
               OPENDASH_DAT_DIR=str(work / "no-pc/dat"), OPENDASH_EXE=str(work / "no-pc/dash2.exe"),
               OPENDASH_LANG_DIR=str(work / "no-language-packs"))
    common = [str(args.exe.resolve()), "--lang", "en", "--net", "off", "--story-save", "none", "--pt-speed", "30"]
    cases = [("missing-save", ["--load", str(work / "missing.DAT"), "--playscript", "1 -"], True)]
    if not args.failure_exit_only:
        cases += [
            ("new-game", ["--newgame", "1", "--playscript", "1200 -"], False),
            ("town", ["--stage", "09", "--playscript", "120 -"], False),
            ("landing", ["--stage", "08", "--playscript", "300 -"], False),
            ("junk-shop", ["--stage", "0a", "--playscript", "600 -"], False),
            ("status-menu", ["--stage", "09", "--playscript", "30 -; 1 select; 30 -; 1 right; 30 -; 1 circle; 30 -"], False),
        ]
    if args.sweep:
        cases += [(f"stage-{stage:02x}", ["--stage", f"{stage:02x}", "--playscript", "10 -"], False)
                  for stage in range(0x5d)]
    failures = []
    for name, flags, negative in cases:
        log_path = work / f"{name}.log"
        try:
            with log_path.open("w", encoding="utf-8") as log:
                result = subprocess.run(common + flags, env=env, stdout=log, stderr=log, timeout=30)
            text = log_path.read_text(encoding="utf-8", errors="replace")
            if negative:
                ok = result.returncode != 0
                why = f"exit {result.returncode}; expected failure"
            else:
                errors = re.findall(r"^.*(?:failed|stopped|not ported|fallback|without an end).*$", text, re.MULTILINE)
                ok = result.returncode == 0 and "pt end " in text and not errors
                why = "; ".join(errors[:4]) or f"exit {result.returncode}; completed={'pt end ' in text}"
        except subprocess.TimeoutExpired:
            ok, why = False, "timed out instead of completing or reporting failure"
        print(f"{name}: {'PASS' if ok else 'FAIL'}: {why}", flush=True)
        if not ok:
            failures.append(name)
    print(f"{len(cases) - len(failures)}/{len(cases)} disc-only application cases passed", flush=True)
    return bool(failures)


if __name__ == "__main__":
    sys.exit(main())
