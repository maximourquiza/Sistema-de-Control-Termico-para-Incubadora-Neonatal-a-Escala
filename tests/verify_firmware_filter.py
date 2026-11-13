"""Check filter functions in final MCUXpresso AXFs without touching the board.

Requires Unicorn. Arguments: one or more AXF paths.
Outputs one JSON object per AXF; packets can also be checked by the web parser.
"""
from pathlib import Path
import json
import struct
import subprocess
import sys

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import (
    UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
    UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC,
)

NM = Path(r"C:\nxp\MCUXpressoIDE_25.6.136\ide\tools\bin\arm-none-eabi-nm.exe")


def verify(filename):
    image = Path(filename).read_bytes()
    listing = subprocess.check_output([str(NM), str(filename)], text=True)
    symbols = {parts[-1]: int(parts[0], 16) for line in listing.splitlines()
               if len(parts := line.split()) == 3}
    emulator = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
    emulator.mem_map(0, 0x100000)
    emulator.mem_map(0x10000000, 0x10000)
    emulator.mem_map(0x2007C000, 0x8000)
    phoff = struct.unpack_from('<I', image, 28)[0]
    phsize, phcount = struct.unpack_from('<HH', image, 42)
    for index in range(phcount):
        kind, offset, address, _, size, _, _, _ = struct.unpack_from(
            '<8I', image, phoff + index * phsize)
        if kind == 1 and size:
            emulator.mem_write(address, image[offset:offset + size])

    def call(name, *arguments):
        for register, value in zip(
            [UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3], arguments
        ):
            emulator.reg_write(register, value & 0xFFFFFFFF)
        emulator.reg_write(UC_ARM_REG_SP, 0x1000FFF0)
        emulator.reg_write(UC_ARM_REG_LR, 0xF0001)
        emulator.emu_start(symbols[name] | 1, 0xF0000,
                           timeout=10_000_000, count=2_000_000)
        assert emulator.reg_read(UC_ARM_REG_PC) == 0xF0000, name
        return emulator.reg_read(UC_ARM_REG_R0)

    state, result = 0x10001000, 0x10002000

    def process(sample):
        call('TemperatureFilter_Process', result, state, sample)
        return struct.unpack('<ii', emulator.mem_read(result, 8))

    for sample in [31000, -1234, 0, 2147483647, -2147483648]:
        call('TemperatureFilter_Reset', state)
        for _ in range(10):
            assert process(sample) == (sample, sample), sample
    call('TemperatureFilter_Reset', state)
    assert process(35000) == (35000, 35000)
    assert process(80000) == (35000, 35000)
    assert process(35000) == (35000, 35000)
    call('TemperatureFilter_Reset', state)
    process(35000)
    assert process(36000) == (35000, 35000)
    assert process(36000) == (36000, 35200)
    assert process(36000) == (36000, 35360)
    call('TemperatureFilter_Reset', state)
    assert process(-2500) == (-2500, -2500)

    packets = []

    # Interceptar solo la salida: nunca ejecutar acceso a UART ni semihosting.
    def capture_uart(cpu, address, size, data):
        pointer = cpu.reg_read(UC_ARM_REG_R1)
        length = cpu.reg_read(UC_ARM_REG_R2)
        packets.append(bytes(cpu.mem_read(pointer, length)).decode('ascii'))
        cpu.reg_write(UC_ARM_REG_R0, length)
        cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))

    def skip_printf(cpu, address, size, data):
        cpu.reg_write(UC_ARM_REG_R0, 0)
        cpu.reg_write(UC_ARM_REG_PC, cpu.reg_read(UC_ARM_REG_LR))

    if 'report_sample' in symbols:
        uart = symbols['UART_Send'] & ~1
        emulator.hook_add(UC_HOOK_CODE, capture_uart, begin=uart, end=uart)
        # Redlib puede resolver printf con su nombre o su alias interno.
        for name in ('printf', '__printf'):
            if name in symbols:
                address = symbols[name] & ~1
                emulator.hook_add(UC_HOOK_CODE, skip_printf, begin=address, end=address)
        measurement = 0x10003000
        emulator.mem_write(measurement, struct.pack('<H2xIi', 1004, 809, 31000))
        for temperature, expected in [
            (31000, '31.000\r\n'), (-1234, '-1.234\r\n'), (0, '0.000\r\n'),
            (-2147483648, '-2147483.648\r\n'), (2147483647, '2147483.647\r\n'),
        ]:
            emulator.mem_write(result, struct.pack('<ii', temperature, temperature))
            before = len(packets)
            call('report_sample', measurement, result)
            assert len(packets) == before + 1, temperature
            assert packets[-1] == expected, (temperature, packets[-1])
            assert len(packets[-1]) < 16

    print(json.dumps({'firmware': str(filename), 'filter': 'passed',
                      'report_sample': 'passed' if packets else 'inlined',
                      'packets': packets}))


if len(sys.argv) < 2:
    raise SystemExit('Pass Debug and/or Release AXF paths.')
for firmware in sys.argv[1:]:
    verify(firmware)
