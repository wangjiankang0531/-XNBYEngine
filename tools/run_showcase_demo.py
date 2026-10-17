#!/usr/bin/env python3
"""Stable launcher for the interactive showcase_demo on Windows."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path


NTSTATUS_SHARING_VIOLATION = 0xC0000043
WINERROR_SHARING_VIOLATION = 32
SHARING_VIOLATION_RETRY_DELAY_SEC = 0.4


class ShowcaseLaunchError(RuntimeError):
    pass


def repo_root() -> Path:
    return Path(__file__).resolve().parents[1]


def default_demo_path(root: Path) -> Path:
    return root / "build_noexe" / "apps" / "showcase_demo" / "Release" / "showcase_demo.exe"


def suppress_windows_error_dialogs() -> None:
    if os.name != "nt":
        return
    try:
        import ctypes

        sem_failcriticalerrors = 0x0001
        sem_nogpfaulterrorbox = 0x0002
        sem_noopenfileerrorbox = 0x8000
        ctypes.windll.kernel32.SetErrorMode(
            sem_failcriticalerrors | sem_nogpfaulterrorbox | sem_noopenfileerrorbox
        )
    except Exception:
        pass


def normalize_status(returncode: int | None) -> int | None:
    if returncode is None:
        return None
    return returncode & 0xFFFFFFFF


def is_sharing_violation(returncode: int | None) -> bool:
    return normalize_status(returncode) == NTSTATUS_SHARING_VIOLATION


def is_sharing_violation_os_error(exc: OSError) -> bool:
    return getattr(exc, "winerror", None) == WINERROR_SHARING_VIOLATION


def format_os_error(exc: OSError) -> str:
    winerror = getattr(exc, "winerror", None)
    if winerror is None:
        return str(exc)
    return f"{exc} (WinError {winerror})"


def has_arg(args: list[str], name: str) -> bool:
    return any(arg == name or arg.startswith(f"{name}=") for arg in args)


def add_default_demo_args(args: list[str]) -> list[str]:
    result = list(args)
    if not has_arg(result, "--scene"):
        result.insert(0, "--scene=industrial_park")
    if not has_arg(result, "--preset"):
        result.insert(1, "--preset=overview")
    return result


def demo_timeout_for(args: list[str], explicit_timeout: float | None) -> float | None:
    if explicit_timeout is not None:
        return explicit_timeout if explicit_timeout > 0.0 else None
    has_finite_run = has_arg(args, "--frames") or has_arg(args, "--screenshot")
    return 45.0 if has_finite_run else None


def copy_runtime_to_isolated_dir(demo: Path, root: Path) -> Path:
    last_error: str | None = None
    for attempt in range(1, 4):
        timestamp = time.strftime("%Y%m%d_%H%M%S")
        target_dir = root / "build_noexe" / "showcase_launch" / f"{timestamp}_{os.getpid()}_{attempt}"
        target_dir.mkdir(parents=True, exist_ok=True)
        target_demo = target_dir / demo.name
        try:
            shutil.copy2(demo, target_demo)
            for dll in demo.parent.glob("*.dll"):
                shutil.copy2(dll, target_dir / dll.name)
            return target_demo
        except OSError as exc:
            if not is_sharing_violation_os_error(exc):
                raise
            last_error = format_os_error(exc)
            if attempt < 3:
                print(
                    f"[showcase_launcher] isolated runtime copy attempt {attempt} hit a file lock; retrying",
                    file=sys.stderr,
                )
                time.sleep(SHARING_VIOLATION_RETRY_DELAY_SEC)
    raise ShowcaseLaunchError(f"isolated runtime copy failed after retries: {last_error}")


def run_once(demo: Path, demo_args: list[str], cwd: Path, timeout: float | None) -> subprocess.CompletedProcess[str]:
    command = [str(demo), *demo_args]
    return subprocess.run(command, cwd=str(cwd), timeout=timeout, text=True)


def resolve_output_args(demo_args: list[str], cwd: Path) -> list[str]:
    """Keep user output locations stable when launch/retry changes the working directory."""
    result = []
    for arg in demo_args:
        name, separator, value = arg.partition("=")
        if separator and value and name in ("--screenshot", "--stats"):
            path = Path(value)
            if not path.is_absolute():
                path = cwd / path
            path = path.resolve()
            path.parent.mkdir(parents=True, exist_ok=True)
            arg = f"{name}={path}"
        result.append(arg)
    return result


def run_showcase_process(
    demo: Path,
    demo_args: list[str],
    cwd: Path,
    *,
    attempts: int = 2,
    timeout: float | None = None,
    fallback_root: Path | None = None,
    copy_fallback: bool = True,
    label: str = "showcase_demo",
) -> int:
    suppress_windows_error_dialogs()
    if attempts < 1:
        attempts = 1
    if not demo.exists():
        raise ShowcaseLaunchError(f"{label} not found: {demo}")
    demo_args = resolve_output_args(demo_args, cwd)
    demo_cwd = demo.parent

    last_error: str | None = None
    for attempt in range(1, attempts + 1):
        try:
            completed = run_once(demo, demo_args, demo_cwd, timeout)
        except OSError as exc:
            if not is_sharing_violation_os_error(exc):
                raise
            last_error = format_os_error(exc)
        except subprocess.TimeoutExpired as exc:
            last_error = f"timeout after {exc.timeout}s"
        else:
            if completed.returncode == 0:
                return 0
            status = normalize_status(completed.returncode)
            last_error = f"exit code {completed.returncode}"
            if status is not None:
                last_error += f" (0x{status:08X})"

        if attempt < attempts:
            print(f"[showcase_launcher] {label} attempt {attempt} failed: {last_error}; retrying", file=sys.stderr)
            time.sleep(SHARING_VIOLATION_RETRY_DELAY_SEC)

    if copy_fallback:
        isolated_demo = copy_runtime_to_isolated_dir(demo, fallback_root or cwd)
        isolated_cwd = isolated_demo.parent
        print(f"[showcase_launcher] retrying from isolated runtime: {isolated_demo}", file=sys.stderr)
        for attempt in range(1, attempts + 1):
            try:
                completed = run_once(isolated_demo, demo_args, isolated_cwd, timeout)
            except OSError as exc:
                if not is_sharing_violation_os_error(exc):
                    raise
                last_error = format_os_error(exc)
            except subprocess.TimeoutExpired as exc:
                last_error = f"isolated timeout after {exc.timeout}s"
            else:
                if completed.returncode == 0:
                    return 0
                status = normalize_status(completed.returncode)
                last_error = f"isolated exit code {completed.returncode}"
                if status is not None:
                    last_error += f" (0x{status:08X})"

            if attempt < attempts:
                print(
                    f"[showcase_launcher] isolated attempt {attempt} failed: {last_error}; retrying",
                    file=sys.stderr,
                )
                time.sleep(SHARING_VIOLATION_RETRY_DELAY_SEC)

    raise ShowcaseLaunchError(f"{label} failed after retries: {last_error}")


def parse_args(argv: list[str]) -> tuple[argparse.Namespace, list[str]]:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--launcher-help", action="store_true")
    parser.add_argument("--launcher-demo")
    parser.add_argument("--launcher-attempts", type=int, default=2)
    parser.add_argument("--launcher-timeout", type=float)
    parser.add_argument("--launcher-no-copy-fallback", action="store_true")
    known, demo_args = parser.parse_known_args(argv)
    if known.launcher_help:
        print(
            "run_showcase_demo.py [launcher options] [showcase_demo options]\n"
            "\nLauncher options:\n"
            "  --launcher-demo=PATH          Override showcase_demo.exe path.\n"
            "  --launcher-attempts=N         Attempts before/after isolated copy fallback.\n"
            "  --launcher-timeout=SECONDS    0 disables timeout; default is finite only for --frames/--screenshot.\n"
            "  --launcher-no-copy-fallback   Disable isolated runtime fallback.\n"
            "  --launcher-help               Show this help.\n"
            "\nRelative --screenshot/--stats outputs are resolved from the project root before launch.\n"
            "Other showcase options are forwarded unchanged. With no showcase options, the launcher uses\n"
            "--scene=industrial_park --preset=overview.\n"
        )
        raise SystemExit(0)
    return known, demo_args


def main(argv: list[str]) -> int:
    root = repo_root()
    launcher_args, demo_args = parse_args(argv)
    demo = Path(launcher_args.launcher_demo) if launcher_args.launcher_demo else default_demo_path(root)
    if not demo.is_absolute():
        demo = root / demo
    demo_args = add_default_demo_args(demo_args)
    timeout = demo_timeout_for(demo_args, launcher_args.launcher_timeout)
    try:
        return run_showcase_process(
            demo,
            demo_args,
            root,
            attempts=launcher_args.launcher_attempts,
            timeout=timeout,
            fallback_root=root,
            copy_fallback=not launcher_args.launcher_no_copy_fallback,
            label=demo.name,
        )
    except ShowcaseLaunchError as exc:
        print(f"[showcase_launcher] {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
