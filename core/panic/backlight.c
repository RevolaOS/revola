/* SPDX-License-Identifier: GPL-3.0-only */

/* Polling transfer sequence follows Rockbox's s5l8702 I2C driver by
 * Bertrik Sikken, GPL-2.0-or-later. Only LEDENA is written: brightness,
 * overvoltage protection, current limit and power rails retain their values. */
#include "osos-2.0.5.h"
#include <stdint.h>

#define I2C(field) I2C0Controller[I2C0Controller_##field / sizeof *I2C0Controller]
#define POLL_LIMIT 100000u

static int ready(void)
{
    for (unsigned n = 0; n < POLL_LIMIT; n++)
        if (!I2C(busy))
            return 1;
    return 0;
}

static int completed(void)
{
    for (unsigned n = 0; n < POLL_LIMIT; n++) {
        if (I2C(busy))
            continue;
        if (!(I2C(status) & I2C_StatusBusy) ||
            (I2C(transfer_status) & I2C_ByteDone)) {
            I2C(transfer_status) |= I2C_TransferDone;
            return 1;
        }
    }
    return 0;
}

static int settle(void)
{
    uint32_t start = Clock_GetMicroseconds();
    for (unsigned n = 0; n < POLL_LIMIT; n++)
        if ((uint32_t)(Clock_GetMicroseconds() - start) >= 5)
            return 1;
    return 0;
}

/* Report result: 0 not attempted, 1 ACKed, 2 busy transaction, 3 controller
 * busy timeout, 4 transfer timeout, 5 NACK, 6 stopped timer, 7 STOP timeout.
 * The upper byte identifies a failed address/register/value stage (1/2/3). */
static unsigned send_byte(uint8_t value)
{
    if (!ready()) return 3;
    I2C(data) = value;
    if (!settle()) return 6;
    if (!ready()) return 3;
    I2C(control) = I2C(control);
    if (!completed()) return 4;
    return (I2C(status) & I2C_StatusNack) ? 5 : 1;
}

void core_panic_backlight(uint32_t result[4])
{
    result[0] = 3;
    result[1] = ClockRegister_PowerGate1;
    result[2] = result[3] = 0;
    /* Rockbox gates the I2C clock around transfers. IRQ/FIQ are masked here;
     * leave this controller clock running until reset, without changing any
     * other clock gate. Never take over an interrupted PMU transaction. */
    ClockRegister_PowerGate1 &= ~PowerGate1_I2C0;
    if (!ready()) return;
    result[2] = I2C(status);
    result[3] = I2C(control);
    if (I2C(status) & I2C_StatusBusy) {
        result[0] = 2;
        return;
    }
    /* Apple's driver consumes byte completion but can leave STOP pending. */
    I2C(transfer_status) |= I2C_TransferDone;
    if (!ready()) return;
    I2C(control) = (I2C(control) & ~I2C_ControlInterrupts) | I2C_ControlAck;
    if (!ready()) return;
    I2C(status) = I2C_StatusMasterTransmit;
    if (!ready()) return;
    I2C(data) = PMU_I2CWriteAddress;
    if (!ready()) return;
    I2C(status) = I2C_StatusMasterTransmit | I2C_StatusStart;
    unsigned status = !completed() ? 4 : (I2C(status) & I2C_StatusNack) ? 5 : 1;
    unsigned stage = 1;
    if (status == 1) {
        stage = 2;
        status = send_byte(PMU_BacklightEnableRegister);
    }
    if (status == 1) {
        stage = 3;
        status = send_byte(PMU_BacklightEnabled);
    }
    result[0] = status == 1 ? 1 : status | stage << 8;
    if (!ready()) return;
    I2C(status) &= ~I2C_StatusBusy;
    if (!ready()) return;
    I2C(control) = I2C_ControlContinue;
    if (!completed()) result[0] = 7;
}
