"""Shared CMake build for M2 protocol host and interoperability tests."""

import shutil
import subprocess
from pathlib import Path

import pytest


REPO_ROOT = Path(__file__).resolve().parents[2]


def _run_checked(command):
    result = subprocess.run(
        command,
        cwd=str(REPO_ROOT),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        universal_newlines=True,
    )
    assert result.returncode == 0, (
        "Command failed (%d): %s\nstdout:\n%s\nstderr:\n%s"
        % (result.returncode, " ".join(command), result.stdout, result.stderr)
    )


@pytest.fixture(scope="session")
def m2_protocol_build(tmp_path_factory):
    cmake = shutil.which("cmake")
    if cmake is None:
        pytest.skip("CMake is required for M2 C host tests")

    build_dir = tmp_path_factory.mktemp("m2-protocol-build")
    _run_checked(
        [
            cmake,
            "-S",
            str(REPO_ROOT / "tests" / "protocol"),
            "-B",
            str(build_dir),
        ]
    )
    _run_checked([cmake, "--build", str(build_dir), "--parallel"])
    return build_dir
