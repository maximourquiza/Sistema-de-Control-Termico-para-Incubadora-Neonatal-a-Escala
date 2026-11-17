"""Run controller, firmware integration and PWM tests on emulated Cortex-M3.

Requires Unicorn and the MCUXpresso toolchain. Optional argument: tools/bin path.
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
LIBRARY = ROOT.parent / "CMSISv2p00_LPC17xx"
SUITES = {
    "controller": ["src/temperature_controller.c", "tests/temperature_controller_test.c"],
    "control_integration": ["src/temperature_controller.c", "tests/control_integration_test.c"],
    "actuators_pwm": ["src/actuators_pwm.c", "tests/actuators_pwm_test.c"],
}


def run(command):
    return subprocess.check_output([str(part) for part in command], text=True)


with tempfile.TemporaryDirectory(prefix="temperature-control-") as directory:
    target = Path(directory)
    for suite, sources in SUITES.items():
        for optimization in ("-O0", "-Os"):
            executable = target / "test.elf"
            binary = target / "test.bin"
            entry_name = f"run_{suite}_tests"
            run([
                TOOLS / "arm-none-eabi-gcc.exe", "-mcpu=cortex-m3", "-mthumb",
                "-std=c99", "-ffreestanding", "-fno-builtin", "-nostdlib", optimization,
                "-ffunction-sections", "-fdata-sections", "-D__REDLIB__", "-nostdinc",
                "-isystem", TOOLS.parent / "redlib/include",
                "-isystem", TOOLS.parent / "features/include",
                "-Wall", "-Wextra", "-Werror", "-I", ROOT / "src",
                "-I", LIBRARY / "inc", "-I", LIBRARY / "Drivers/inc",
                *[ROOT / source for source in sources],
                "-Wl,-Ttext=0x1000", f"-Wl,-e,{entry_name}", "-Wl,--gc-sections",
                "-Wl,--defsym,_pvHeapStart=0x10008000",
                "-Wl,--start-group", "-lcr_c", "-lcr_eabihelpers", "-lcr_semihost", "-lgcc",
                "-Wl,--end-group", "-o", executable,
            ])
            run([TOOLS / "arm-none-eabi-objcopy.exe", "-O", "binary", executable, binary])
            symbols = run([TOOLS / "arm-none-eabi-nm.exe", executable])
            entry = next(int(line.split()[0], 16) for line in symbols.splitlines()
                         if line.split()[-1] == entry_name)
            emulator = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
            emulator.mem_map(0, 0x100000)
            emulator.mem_write(0x1000, binary.read_bytes())
            emulator.mem_map(0x10000000, 0x10000)
            emulator.mem_map(0x40000000, 0x100000)  # Peripheral registers for PWM tests.
            emulator.mem_map(0xE000E000, 0x2000)  # NVIC for PWM initialization.
            emulator.reg_write(UC_ARM_REG_SP, 0x1000FFF0)
            emulator.reg_write(UC_ARM_REG_LR, 0xF0001)
            emulator.emu_start(entry | 1, 0xF0000, timeout=10_000_000, count=4_000_000)
            if emulator.reg_read(UC_ARM_REG_PC) != 0xF0000:
                raise AssertionError(f"{suite} {optimization}: test did not return")
            result = emulator.reg_read(UC_ARM_REG_R0)
            if result:
                raise AssertionError(f"{suite} {optimization}: C assertion at line {result}")
            print(f"{suite} {optimization}: passed on emulated Cortex-M3 with Redlib")
