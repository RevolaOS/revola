# SPDX-License-Identifier: GPL-3.0-only
"""Panic backlight polling must return when hardware cannot make progress."""

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class PanicBacklightTests(unittest.TestCase):
    def test_stopped_bus_and_clock(self):
        source = (ROOT / "core/panic/backlight.c").read_text()
        source = source.replace('#include "osos-2.0.5.h"', "")
        constants = "\n".join(
            line
            for line in (ROOT / "firmware/osos-2.0.5.h").read_text().splitlines()
            if line.startswith(
                ("#define I2C", "#define PMU_", "#define PowerGate1_")
            )
        )
        harness = r"""
#include <assert.h>
#include <stdint.h>
#include <string.h>
volatile uint32_t I2C0Controller[9], ClockRegister_PowerGate1;
static uint32_t ticks;
static int clock_stopped;
uint32_t Clock_GetMicroseconds(void) {
    return clock_stopped ? ticks : ticks++;
}
"""
        tests = r"""
int main(void) {
    uint32_t result[4];
    ClockRegister_PowerGate1 = PowerGate1_I2C0 | 0x80000000;
    I2C0Controller[4] = 1;
    I2C0Controller[3] = 0xfeed;
    core_panic_backlight(result);
    assert(I2C0Controller[3] == 0xfeed);
    assert(ClockRegister_PowerGate1 == 0x80000000);
    assert(result[0] == 3);
    I2C0Controller[4] = 0;
    ClockRegister_PowerGate1 = 0;
    I2C0Controller[1] = I2C_StatusBusy;
    core_panic_backlight(result);
    assert(I2C0Controller[3] == 0xfeed);
    I2C0Controller[1] = 0;
    I2C0Controller[4] = 1;
    core_panic_backlight(result);
    assert(I2C0Controller[3] == 0xfeed);
    I2C0Controller[4] = 0;
    core_panic_backlight(result); /* no completion: bounded, then STOP */
    assert(!(I2C0Controller[1] & I2C_StatusBusy));
    I2C0Controller[8] = I2C_TransferDone;
    clock_stopped = 1;
    core_panic_backlight(result); /* frozen clock: bounded, then STOP */
    assert(!(I2C0Controller[1] & I2C_StatusBusy));
    assert(I2C0Controller[3] == PMU_BacklightEnableRegister);
    clock_stopped = 0;
    core_panic_backlight(result);
    assert(I2C0Controller[3] == PMU_BacklightEnabled);
    assert(!(I2C0Controller[1] & I2C_StatusBusy));
}
"""
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            c = path / "test.c"
            c.write_text(harness + constants + "\n" + source + tests)
            exe = path / "test"
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-fsanitize=address,undefined",
                    str(c),
                    "-o",
                    str(exe),
                ],
                check=True,
            )
            subprocess.run([str(exe)], check=True, timeout=5)


if __name__ == "__main__":
    unittest.main()
