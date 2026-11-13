"""Compile and execute the real Cortex-M3 filter in Unicorn (no board access).

Requires the MCUXpresso toolchain and the Python package unicorn.
Optional first argument: directory containing arm-none-eabi-gcc.exe.
"""
from pathlib import Path
import subprocess
import sys
import tempfile

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_SP

ROOT = Path(__file__).resolve().parents[1]
TOOLS = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(
    r"C:\nxp\MCUXpressoIDE_25.6.136\ide\tools\bin"
)
REDLIB_HEADERS = TOOLS.parent / "redlib/include"
FEATURE_HEADERS = TOOLS.parent / "features/include"


def run(command):
    return subprocess.check_output([str(part) for part in command], text=True)


with tempfile.TemporaryDirectory(prefix="temperature-filter-") as directory:
    target = Path(directory)
    for optimization in ("-O0", "-Os"):
        executable = target / "filter.elf"
        binary = target / "filter.bin"
        run([
            TOOLS / "arm-none-eabi-gcc.exe", "-mcpu=cortex-m3", "-mthumb",
            "-ffreestanding", "-fno-builtin", "-nostdlib", optimization,
            "-D__REDLIB__", "-nostdinc", "-isystem", REDLIB_HEADERS,
            "-isystem", FEATURE_HEADERS,
            "-Wall", "-Wextra", "-Werror", "-I", ROOT / "src",
            ROOT / "src/temperature_filter.c", ROOT / "tests/temperature_filter_test.c",
            "-Wl,-Ttext=0x1000", "-Wl,-e,run_filter_tests",
            "-Wl,--start-group", "-lcr_c", "-lcr_eabihelpers", "-lgcc",
            "-Wl,--end-group", "-o", executable,
        ])
        run([TOOLS / "arm-none-eabi-objcopy.exe", "-O", "binary", executable, binary])
        symbols = run([TOOLS / "arm-none-eabi-nm.exe", executable])
        entry = next(int(line.split()[0], 16) for line in symbols.splitlines()
                     if line.split()[-1] == "run_filter_tests")
        emulator = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        emulator.mem_map(0, 0x100000)
        emulator.mem_write(0x1000, binary.read_bytes())
        emulator.mem_map(0x10000000, 0x10000)
        emulator.reg_write(UC_ARM_REG_SP, 0x1000FFF0)
        emulator.reg_write(UC_ARM_REG_LR, 0xF0001)
        emulator.emu_start(entry | 1, 0xF0000, timeout=10_000_000, count=2_000_000)
        if emulator.reg_read(UC_ARM_REG_PC) != 0xF0000:
            raise AssertionError(f"{optimization}: execution did not reach the return address")
        result = emulator.reg_read(UC_ARM_REG_R0)
        if result:
            raise AssertionError(f"{optimization}: C assertion failed at line {result}")
        print(f"{optimization}: 9 filter scenarios passed with Redlib headers and runtime on emulated Cortex-M3")
