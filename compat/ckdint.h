#ifndef COMPAT_CKDINT_H
#define COMPAT_CKDINT_H

#if __STDC_VERSION__ >= 202311L
# include <stdckdint.h>
#elif __GNUC__
# define ckd_add(r, a, b) __builtin_add_overflow(a, b, r)
# define ckd_sub(r, a, b) __builtin_sub_overflow(a, b, r)
# define ckd_mul(r, a, b) __builtin_mul_overflow(a, b, r)
#endif

#endif
