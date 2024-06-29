#ifndef WREAD_H
#define WREAD_H

#ifndef BUFSIZ
# include <stdio.h>
#endif

#include "compat/__attribute__.h"

extern ssize_t wread(int fd, wchar_t wptr[BUFSIZ])
	__attribute__((__access__(write_only, 2), leaf, nonnull,
			warn_unused_result));
/* reads BUFSIZ chars from fd, blindly writes corresponding widechars to
 * wptr, which must point to BUFSIZ widechars. Mimics read(2)'s return
 * value. */

#endif
