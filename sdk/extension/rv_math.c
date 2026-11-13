/* SPDX-License-Identifier: GPL-3.0-only */

#include "rv_math.h"

double rv_exp(double x)
{
    double sum = 1.0, term = 1.0;
    for (unsigned int n = 1; n <= 18; n++) {
        term *= x / n;
        sum += term;
    }
    return sum;
}

/* Range reduction by halving, then the atanh series. */
double rv_log(double value)
{
    int power = 0;
    while (value >= 2.0) {
        value *= 0.5;
        power++;
    }
    while (value < 1.0) {
        value *= 2.0;
        power--;
    }
    double z = (value - 1.0) / (value + 1.0), term = z, sum = z;
    for (unsigned int n = 3; n <= 23; n += 2) {
        term *= z * z;
        sum += term / n;
    }
    return 2.0 * sum + power * 0.693147180559945309;
}

void rv_sin_cos(double x, double *sine, double *cosine)
{
    double s = x, c = 1.0, st = x, ct = 1.0;
    for (unsigned int n = 1; n <= 11; n++) {
        st *= -x * x / ((2 * n) * (2 * n + 1));
        ct *= -x * x / ((2 * n - 1) * (2 * n));
        s += st;
        c += ct;
    }
    *sine = s;
    *cosine = c;
}

/* Newton iteration from above. */
double rv_sqrt(double value)
{
    if (!(value > 0.0))
        return 0.0;
    double root = value > 1.0 ? value : 1.0;
    for (unsigned int n = 0; n < 48; n++)
        root = 0.5 * (root + value / root);
    return root;
}

double rv_decibels(int tenths, unsigned int divisor)
{
    return rv_exp(2.302585092994045684 * tenths / divisor);
}
