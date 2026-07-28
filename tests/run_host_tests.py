"""Compile and run the platform-independent C unit tests with the host GCC."""
import shutil
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SOURCES = [
    "tests/test_main.c",
    "src/app_state.c",
    "src/motor_control.c",
    "src/command_api.c",
    "src/device_config.c",
    "src/safety.c",
    "src/stallguard_calibration.c",
    "src/tmc_current.c",
]


def main() -> None:
    compiler = shutil.which("gcc")
    if compiler is None:
        raise SystemExit("Host GCC not found in PATH")
    with tempfile.TemporaryDirectory(prefix="paste-dispenser-tests-") as directory:
        executable = Path(directory) / "unit_tests.exe"
        command = [
            compiler,
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-DUNIT_TEST=1",
            f"-I{ROOT / 'include'}",
            *(str(ROOT / source) for source in SOURCES),
            "-lm",
            "-o",
            str(executable),
        ]
        subprocess.run(command, check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
