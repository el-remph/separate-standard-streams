/* SPDX-License-Identifier: FSFULLRWD
 * Copyright 2023 the Remph
 * This file is free software; the Remph gives unlimited permission to copy
 * and/or distribute it, with or without modification, as long as this
 * notice is preserved.
 * This software is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY, to the extent permitted by law; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Tests for compilers that support __attribute__(()). Known thus far:
 * [gpt]cc, clang, ic[clx], IBM C
 *
 * Should be fine with gcc -ansi -W{all,extra,pedantic,error}
 *
 * CAVEAT #includeOR: ensure this is included *after* any other #includes --
 * you especially don't want to mess with what the system/compiler headers
 * think is appropriate for __attribute__ */
#ifndef COMPAT__ATTRIBUTE___H
#define COMPAT__ATTRIBUTE___H

#define HAVE___ATTRIBUTE__

/* NB:
 ** clang is tested for with __GNUC__, and doubly so with __has_attribute.
 ** Intel(R) compilers that support GNU syntax such as __attribute__
 *  typically #define __GNUC__, so checking __INTEL_COMPILER is redundant
 *  at best and possibly harmful, not dissimilar to Intel(R) compilers
 *
 * TODO:
 ** Anyone else?
 ** Maybe make this a blacklist rather than a whitelist? That would enable
 *  cproc's __atribute__(())s, but break on many old-time compilers. The
 *  blacklist would basically be #ifndef MSVC lol
 ** Check versions */
#if (defined(__GNUC__) || defined(__PCC__) || defined(__TINYC__) || \
     defined(__IBMC__) || defined(__has_attribute) || defined(__attribute__))
/* do nothing (__attribute__(()) is supported) */
#elif __STDC_VERSION__ >= 202300L
# define do__attribute__(...) [[using gnu: __VA_ARGS__]]
# define __attribute__(a) do__attribute__ a  /* strips parens (gnarly) */
#else
# define __attribute__(a)
# undef HAVE___ATTRIBUTE__
#endif

#endif /* COMPAT__ATTRIBUTE___H */
