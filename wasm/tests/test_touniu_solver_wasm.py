"""System test for the Touniu solver WASM ABI."""
from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

TESTS_ROOT = Path(__file__).resolve().parent


def runfiles_root() -> Path:
    for key in ("RUNFILES_DIR", "TEST_SRCDIR"):
        if key in os.environ:
            return Path(os.environ[key])
    raise RuntimeError("not running under Bazel test")


def rlocation(relpath: str) -> Path:
    root = runfiles_root()
    for candidate in (root / relpath, root / "_main" / relpath):
        if candidate.exists():
            return candidate
    raise FileNotFoundError(relpath)


@unittest.skipUnless(shutil.which("node"), "node not found")
class TouniuSolverWasmTest(unittest.TestCase):
    def test_module_exposes_and_runs_abi_v2(self) -> None:
        node = shutil.which("node")
        js = rlocation("wasm/touniu_solver.js")
        wasm = rlocation("wasm/touniu_solver.wasm")

        with tempfile.TemporaryDirectory() as tmpdir:
            dest = Path(tmpdir)
            js_copy = dest / "touniu_solver.js"
            wasm_copy = dest / "touniu_solver.wasm"
            shutil.copyfile(js, js_copy)
            shutil.copyfile(wasm, wasm_copy)

            proc = subprocess.run(
                [
                    node,
                    str(TESTS_ROOT / "touniu_solver_wasm_node.mjs"),
                    str(js_copy),
                    str(wasm_copy),
                ],
                capture_output=True,
                text=True,
                check=False,
            )

            self.assertEqual(proc.returncode, 0, msg=proc.stdout + proc.stderr)
            self.assertIn("touniu_solver_wasm_node: OK", proc.stdout)


if __name__ == "__main__":
    unittest.main()
