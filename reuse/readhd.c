/* -*- mode: C -*- */
/* $Id$ */

/* Copyright (C) 2002-2014 Alexander Chernov <cher@ejudge.ru> */

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */

#define __REUSE__ 1

/* reuse include directives */
#include "ejudge/integral.h"
#include "ejudge/number_io.h"
#include "ejudge/fp_props.h"

#include <string.h>
#include <ctype.h>

extern const signed char _reuse_letter_to_digit_table[];

int
reuse_readhd(char const *str, char **endptr, double *pval)
{
  int is_neg = 0;
  int is_exp_neg = 0;
  int ret_val = 0;
  rullong_t lval = 0;
  long exp = 0, exp_add = 0;
  int shift_val = 60;
  rullong_t im = 0;
  rullong_t fm = 0;
  int d;

  if (!str) goto _format_error;
  while (isspace(*str)) str++;
  if (!*str) goto _format_error;
  if ((*str == 'n' || *str == 'N')
      && str[1] && (str[1] == 'a' || str[1] == 'A')
      && str[2] && (str[2] == 'n' || str[2] == 'N')) {
    lval = R_I64(0x7ff8000000000000);
    str += 3;
    goto _normal_exit;
  }
  if (*str == '+') {
    str++;
  } else if (*str == '-') {
    is_neg = 1;
    str++;
  }
  if ((*str == 'i' || *str == 'I')
      && str[1] && (str[1] == 'n' || str[1] == 'N')
      && str[2] && (str[2] == 'f' || str[2] == 'F')) {
    lval = R_I64(0x7ff0000000000000);
    str += 3;
    goto _normal_exit;
  }
  if (*str != '0') goto _format_error;
  str++;
  if (*str != 'X' && *str != 'x') goto _format_error;
  str++;

  d = _reuse_letter_to_digit_table[*(unsigned char const*) str];
  if (d < 0 || d >= 16) goto _format_error;
  str++;
  if (d) {
    if (d >= 8) {
      fm = (rullong_t) d << 61;
      exp_add += 3;
      shift_val -= 3;
    } else if (d >= 4) {
      fm = (rullong_t) d << 62;
      exp_add += 2;
      shift_val -= 2;
    } else if (d >= 2) {
      fm = (rullong_t) d << 63;
      exp_add += 1;
      shift_val -= 1;
    }
    im = 1;
  }
  while (1) {
    d = _reuse_letter_to_digit_table[*(unsigned char const *) str];
    if (d < 0 || d >= 16) break;
    str++;
    if (im) {
      if (shift_val >= 0) fm |= (rullong_t) d << shift_val;
      else if (shift_val > -4) fm |= (rullong_t) d >> -shift_val;
      exp_add += 4;
      shift_val -= 4;
    } else if (!im && d) {
      if (d >= 8) {
        fm = (rullong_t) d << 61;
        exp_add += 3;
        shift_val -= 3;
      } else if (d >= 4) {
        fm = (rullong_t) d << 62;
        exp_add += 2;
        shift_val -= 2;
      } else if (d >= 2) {
        fm = (rullong_t) d << 63;
        exp_add += 1;
        shift_val -= 1;
      }
      im = 1;
    }
  }

  if (*str == '.') {
    str++;
    while (1) {
      d = _reuse_letter_to_digit_table[*(unsigned char const *) str];
      if (d < 0 || d >= 16) break;
      str++;
      if (im) {
        if (shift_val >= 0) fm |= (rullong_t) d << shift_val;
        else if (shift_val > -4) fm |= (rullong_t) d >> -shift_val;
        shift_val -= 4;
      } else if (!im && d > 0) {
        if (d >= 8) {
          fm = (rullong_t) d << 61;
          exp_add -= 1;
          shift_val -= 3;
        } else if (d >= 4) {
          fm = (rullong_t) d << 62;
          exp_add -= 2;
          shift_val -= 2;
        } else if (d >= 2) {
          fm = (rullong_t) d << 63;
          exp_add -= 3;
          shift_val -= 1;
        } else {
          exp_add -= 4;
        }
        im = 1;
      } else if (!im) {
        exp_add -= 4;
      }
    }
  }

  if (*str != 'p' && *str != 'P') goto _format_error;
  str++;
  if (*str == '+') {
    str++;
  } else if (*str == '-') {
    is_exp_neg = 1;
    str++;
  }
  d = _reuse_letter_to_digit_table[*(unsigned char const *) str];
  if (d < 0 || d >= 10) goto _format_error;
  exp = d;
  str++;
  while (1) {
    d = _reuse_letter_to_digit_table[*(unsigned char const *) str];
    if (d < 0 || d >= 10) break;
    str++;
    if (exp < 100000) exp = exp * 10 + d;
  }
  if (*str && endptr == (char**) 1) goto _format_error;
  if (is_exp_neg) exp = -exp;

  if (!im && !fm) {
    lval = 0;
    goto _normal_exit;
  }
  if (im != 1) goto _format_error;
  exp += 0x3FF + exp_add;
  if (exp < 0) {
    /* underflow */
    lval = 0;
    ret_val = 2;
    goto _normal_exit;
  }
  if (exp > 0x7FF) {
    /* overflow */
    lval = R_I64(0x7ff0000000000000);
    ret_val = 1;
    goto _normal_exit;
  }

  lval = fm >> 12;
  lval |= (rullong_t) exp << 52;

 _normal_exit:
  if (is_neg) lval |= R_I64(0x8000000000000000);
  memmove(pval, &lval, 8);
  if (endptr && endptr != (char**) 1) *endptr = (char*) str;
  return ret_val;

 _format_error:
  if (pval) *pval = 0.0;
  if (endptr && endptr != (char**) 1) *endptr = (char*) str;
  return -1;
}
