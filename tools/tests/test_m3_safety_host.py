"""Run the M3 OS-free safety host CMake/CTest suite from pytest."""

import shutil
import subprocess
from pathlib import Path

import pytest


REPO_ROOT = Path(__file__).resolve().parents[2]


def _run_checked(command):
    result = subprocess.run(
        command, cwd=str(REPO_ROOT), stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, universal_newlines=True,
    )
    assert result.returncode == 0, (
        "Command failed (%d): %s\nstdout:\n%s\nstderr:\n%s"
        % (result.returncode, " ".join(command), result.stdout, result.stderr)
    )


def test_m3_safety_cmake_and_ctest(tmp_path):
    cmake, ctest = shutil.which("cmake"), shutil.which("ctest")
    if cmake is None or ctest is None:
        pytest.skip("CMake and CTest are required for M3 host tests")
    build = tmp_path / "m3-safety-build"
    _run_checked([cmake, "-S", str(REPO_ROOT / "tests" / "safety"), "-B", str(build)])
    _run_checked([cmake, "--build", str(build), "--parallel"])
    _run_checked([ctest, "--test-dir", str(build), "--output-on-failure"])
