/* SPDX-FileCopyrightText:  2023-2024 The Remph <lhr@disroot.org>
   SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef COMPAT_BOOL_H
#define COMPAT_BOOL_H

# if __STDC_VERSION__ >= 199900L
#  include <stdbool.h>
# elif defined(__GNUC__) && !defined(__STRICT_ANSI__)
#  define bool _Bool
#  define true 1
#  define false 0
# else /* Neither -std=c99 nor -std=gnu89 */
typedef enum { false = 0, true = 1 } bool;
# endif /* -std=c99 or -std=gnu89 */

#endif
