#ifndef COMPAT_INLINE_RESTRICT_H
#define COMPAT_INLINE_RESTRICT_H

/* This is specific to ssss, only works because the GNU __inline__ form is
 * used throughout */

#ifndef __GNUC__
# if __STDC_VERSION__ >= 199901L
#  define __inline__ inline
#  define __restrict__ restrict
# else
#  define __inline__
#  define __restrict__
# endif /* C99 */
#endif /* GNU C */

#endif /* COMPAT_INLINE_RESTRICT_H */
