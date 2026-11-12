"""Run the standalone M1 host CMake/CTest suite from pytest."""

import shutil
import subprocess
from pathlib import Path
from typing import List

import pytest


REPO_ROOT = Path(__file__).resolve().parents[2]


def _run_checked(command: List[str]) -> None:
    result = subprocess.run(
        command,
        cwd=REPO_ROOT,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        universal_newlines=True,
    )

    assert result.returncode == 0, (
        f"Command failed ({result.returncode}): {' '.join(command)}\n"
        f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
    )


def test_control_host_cmake_and_ctest(tmp_path: Path) -> None:
    """Configure, build, and run host tests without polluting the repository."""
    cmake = shutil.which("cmake")
    ctest = shutil.which("ctest")

    if cmake is None or ctest is None:
        pytest.skip("CMake and CTest are required for M1 host tests")

    build_dir = tmp_path / "control-host-build"

    _run_checked(
        [
            cmake,
            "-S",
            str(REPO_ROOT / "tests" / "control"),
            "-B",
            str(build_dir),
        ]
    )
    _run_checked([cmake, "--build", str(build_dir), "--parallel"])
    _run_checked(
        [ctest, "--test-dir", str(build_dir), "--output-on-failure"]
    )
