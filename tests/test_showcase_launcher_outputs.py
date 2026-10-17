import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import run_showcase_demo as launcher


class LauncherOutputTests(unittest.TestCase):
    def test_output_paths_survive_isolated_retry(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            demo = root / "bin/demo.exe"
            demo.parent.mkdir()
            demo.touch()
            isolated = root / "isolated/demo.exe"
            args = ["--screenshot=frames/detail.png", "--stats=stats/detail.json", "--preset=zhumadian"]
            with patch.object(launcher, "run_once", side_effect=[
                subprocess.CompletedProcess([], 1), subprocess.CompletedProcess([], 0)
            ]) as run, patch.object(launcher, "copy_runtime_to_isolated_dir", return_value=isolated):
                launcher.run_showcase_process(demo, args, root, attempts=1)
            expected = [f"--screenshot={root / 'frames/detail.png'}", f"--stats={root / 'stats/detail.json'}",
                        "--preset=zhumadian"]
            self.assertEqual(run.call_args_list[0].args[1], expected)
            self.assertEqual(run.call_args_list[1].args[1], expected)
            self.assertNotEqual(run.call_args_list[0].args[2], run.call_args_list[1].args[2])
            self.assertTrue((root / "frames").is_dir())
            self.assertTrue((root / "stats").is_dir())
            self.assertEqual(args[0], "--screenshot=frames/detail.png")

    def test_absolute_output_is_preserved(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory).resolve() / "image.png"
            args = [f"--screenshot={target}", "--frames=3"]
            self.assertEqual(launcher.resolve_output_args(args, Path(directory) / "elsewhere"), args)


if __name__ == "__main__":
    unittest.main()
