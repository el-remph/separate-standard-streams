#ifndef TIMESTAMP_H
#define TIMESTAMP_H

#define TIMESTAMP_SIZE (sizeof "[00:00:00.000000] ")

#include "compat/__attribute__.h"

extern void sprint_time(char buf[TIMESTAMP_SIZE])
	__attribute__((nonnull, __access__(write_only, 1)));

#endif /* TIMESTAMP_H */
