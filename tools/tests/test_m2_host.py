"""Run the standalone M2 CMake/CTest suite from pytest."""

import shutil
import subprocess

import pytest



def test_m2_protocol_ctest(m2_protocol_build):
    ctest = shutil.which("ctest")
    if ctest is None:
        pytest.skip("CTest is required for M2 protocol tests")
    result = subprocess.run(
        [ctest, "--test-dir", str(m2_protocol_build), "--output-on-failure"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        universal_newlines=True,
    )
    assert result.returncode == 0, "CTest failed:\n%s\n%s" % (result.stdout, result.stderr)
