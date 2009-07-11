/*
 * Copyright (C) 2009 Arnaud "arnau" Fontaine <arnau@mini-dweeb.org>
 *
 * This  program is  free  software: you  can  redistribute it  and/or
 * modify  it under the  terms of  the GNU  General Public  License as
 * published by the Free Software  Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT  ANY  WARRANTY;  without   even  the  implied  warranty  of
 * MERCHANTABILITY or  FITNESS FOR A PARTICULAR PURPOSE.   See the GNU
 * General Public License for more details.
 *
 * You should have  received a copy of the  GNU General Public License
 *  along      with      this      program.      If      not,      see
 *  <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include "util.h"

#define DO_DISPLAY_MESSAGE(LABEL)			\
  va_list ap;						\
  va_start(ap, fmt);					\
  fprintf(stderr, LABEL ": %s:%d: ", func, line);	\
  vfprintf(stderr, fmt, ap);				\
  va_end(ap);						\
  putc('\n', stderr);

void
_fatal(const int line, const char *func, const char *fmt, ...)
{
  DO_DISPLAY_MESSAGE("FATAL");
  exit(EXIT_FAILURE);
}

void
_warn(const int line, const char *func, const char *fmt, ...)
{
  DO_DISPLAY_MESSAGE("WARN");
}

void
#ifdef __DEBUG__
_debug(const int line, const char *func, const char *fmt, ...)
{
  DO_DISPLAY_MESSAGE("DEBUG")
}
#else
_debug(const int line __attribute__((unused)),
       const char *func __attribute__((unused)),
       const char *fmt __attribute__((unused)),
       ...)
{
}
#endif