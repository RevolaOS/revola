/* SPDX-License-Identifier: GPL-3.0-only */
/* Adapted from RepriseOS game-sdk/runtime/time.c by lgreitis. */

#include "osos-2.0.5.h"
#include "rv_app.h"

static uint32_t last_us, elapsed_ms, remainder_us;
static int clock_started;

uint32_t rv_app_ticks_ms(void)
{
    uint32_t now;
    Clock_ReadMicroseconds(&now);
    if (!clock_started) {
        last_us = now;
        clock_started = 1;
    }

    uint32_t delta = now - last_us;
    last_us = now;
    elapsed_ms += delta / 1000;
    remainder_us += delta % 1000;
    elapsed_ms += remainder_us / 1000;
    remainder_us %= 1000;

    return elapsed_ms;
}

void rv_app_sleep_ms(uint32_t milliseconds)
{
    GameSystem_Sleep(milliseconds);
}

void rv_app_date_time(struct rv_app_date_time *out)
{
    /* Second, minute, hour, day, month, year, in the host's order. */
    uint32_t fields[6] = {0};
    if (!out)
        return;
    GameSystem_GetDateTime(fields);
    out->second = (uint8_t)fields[0];
    out->minute = (uint8_t)fields[1];
    out->hour = (uint8_t)fields[2];
    out->day = (uint8_t)fields[3];
    out->month = (uint8_t)fields[4];
    out->year = (uint16_t)fields[5];
}

int rv_app_clock_24h(void)
{
    /* The host answers "12" or "24" with the terminator. */
    char format[3] = {0};
    uint32_t size = sizeof format;
    return GameSettings_GetValue("TimeFormat", format, &size) == 0 && format[0] == '2' &&
           format[1] == '4';
}

static char *two_digits(char *out, unsigned value)
{
    *out++ = (char)('0' + value / 10 % 10);
    *out++ = (char)('0' + value % 10);
    return out;
}

char *rv_app_format_time(char *out, const struct rv_app_date_time *time)
{
    unsigned hour = time->hour % 24, minute = time->minute % 60;
    int clock_24h = rv_app_clock_24h();
    if (clock_24h) {
        out = two_digits(out, hour);
    } else {
        unsigned shown = hour % 12 ? hour % 12 : 12;
        if (shown >= 10)
            *out++ = '1';
        *out++ = (char)('0' + shown % 10);
    }
    *out++ = ':';
    out = two_digits(out, minute);
    if (!clock_24h) {
        *out++ = ' ';
        *out++ = hour < 12 ? 'A' : 'P';
        *out++ = 'M';
    }
    *out = 0;
    return out;
}
