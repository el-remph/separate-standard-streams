#include "config.h"

#include <stdio.h>	/* snprintf(3) */
#include <sys/time.h>	/* gettimeofday(2) */
#include <time.h>	/* localtime(3), strftime(3) */

#include "timestamp.h"

extern void __attribute__((nonnull, __access__(write_only, 1)))
sprint_time(char buf[TIMESTAMP_SIZE])
/* This uses gettimeofday(2) for compatibility with old systems, cause
 * adoption of clock_gettime(2) was a bit of a minefield. It was POSIXed in
 * 1993, but the BSDs didn't implement it until the late 90s and Linux
 * didn't implement it for the better part of a decade, and then when it
 * did, glibc wanted LDFLAGS+=-lrt for a while, until it didn't. Now
 * everyone's caught up, POSIX is very eager to `obsolete' gettimeofday(2)
 * but considering the speed of response to the introduction of
 * clock_gettime(2), I'm not holding my breath */
{
	struct timeval t;
	gettimeofday(&t, NULL);
	strftime(buf, TIMESTAMP_SIZE, "[%H:%M:%S", localtime(&t.tv_sec));
	snprintf(buf + sizeof "[00:00:00" - 1, sizeof ".000000] ",
		/* -1 is to overwrite NUL ^^^ */
		".%06ld] ", t.tv_usec);
}
