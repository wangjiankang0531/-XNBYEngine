"""Exercise the production Windows cache lock with a genuinely competing process."""
import json
import msvcrt
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    package = ROOT / "work/releases/landscape_apps_20261011"
    out = ROOT / "work/landscape_apps_checks/lock_case"
    cache = out / "cache"
    cache.mkdir(parents=True, exist_ok=True)
    child = out / "competing_decoder.py"
    child.write_text(
        "import sys\nfrom pathlib import Path\n"
        "sys.path.insert(0, sys.argv[1])\n"
        "import convert_zhumadian_native_chunks as decoder\n"
        "decoder.CACHE = Path(sys.argv[2])\n"
        "decoder.convert_locked = lambda tile, depth: print('DECODE_AFTER_LOCK', flush=True)\n"
        "print('READY', flush=True)\n"
        "decoder.convert(141, 3)\n", encoding="utf-8")
    lock_path = cache / "decoder.lock"
    if not lock_path.exists():
        lock_path.write_bytes(b"0")
    with lock_path.open("r+b") as lock:
        lock.seek(0)
        msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        process = subprocess.Popen(
            [str(package / "runtime/python/python.exe"), "-X", "utf8", str(child),
             str(package / "tools"), str(cache)], stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, encoding="utf-8")
        try:
            first = process.stdout.readline().strip()
            assert first == "READY", first
            time.sleep(0.3)
            assert process.poll() is None, "A competing byte lock must wait, rather than fail its initial read"
        finally:
            lock.seek(0)
            msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)
    output, _ = process.communicate(timeout=30)
    assert process.returncode == 0 and "DECODE_AFTER_LOCK" in output, output
    # The production finally block must release the byte for the next decoder.
    with lock_path.open("r+b") as lock:
        msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)
    (out.parent / "decoder_lock.json").write_text(json.dumps(
        {"passed": True, "real_competing_process": True, "waited_for_lock": True,
         "released_after_decode": True, "decoder": str(package / "tools/convert_zhumadian_native_chunks.py")},
        indent=2), encoding="utf-8")
    print("PRODUCTION CACHE BYTE LOCK / REAL COMPETING PROCESS / RELEASE PASS")


if __name__ == "__main__":
    main()
