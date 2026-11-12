/* SPDX-License-Identifier: GPL-3.0-only */

/* Double precision without a math library, for filter designs and the curves
 * the editors draw: series that hold to double precision inside the stated
 * ranges. Soft float: for the UI task, never for a sample loop. */

#ifndef RV_MATH_H
#define RV_MATH_H
#include "rv_libc.h"

RV_MODULE_CALL double rv_exp(double x);     /* |x| below 1.5 */
RV_MODULE_CALL double rv_log(double value); /* value above 0 */
RV_MODULE_CALL void rv_sin_cos(double x, double *sine, double *cosine); /* 0 <= x < pi */
RV_MODULE_CALL double rv_sqrt(double value); /* 0 for a value at or below 0 */
/* 10^(tenths / divisor): 200 turns tenths of a decibel into an amplitude,
 * 400 into the RBJ gain A. */
RV_MODULE_CALL double rv_decibels(int tenths, unsigned int divisor);

#endif
