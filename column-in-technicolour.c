#include "config.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h> /* calloc(3), free(3) */
#include <wchar.h>

#include <err.h>

#include "column-in-technicolour.h"
#include "process_cmdline.h"
#include "timestamp.h"

#include "compat/bool.h"
#include "compat/inline-restrict.h"
#include "compat/__attribute__.h"

#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

static void * __attribute__((__malloc__, returns_nonnull))
xcalloc(const size_t nmemb, const size_t size)
{
	void *const ptr = calloc(nmemb, size);
	if (!ptr)
		err(-1, NULL);
	return ptr;
}

static bool
watch_file(FILE *const file)
/* determines whether to keep watching file after eg. fgetws(3) returns NULL */
{
	if (feof(file))
		return false;
	assert(ferror(file));
	return errno == EAGAIN; /* if EAGAIN, keep watching */
}

static bool
get_wline_or_eof(
	wchar_t *__restrict__ const outbuf,
	const int cols,
	FILE *__restrict__ const stream,
	const int fd,
	int *__restrict__ const watch
) {
	const bool ret = !!fgetws(outbuf, cols, stream);

	if (ret) {
		/* Not too happy about having to make all these passes */
		const size_t len = wcslen(outbuf);
		if (len && outbuf[len - 1] == L'\n')
			outbuf[len - 1] = '\0';
	} else {
		if (!watch_file(stream))
			*watch &= ~fd;
	}

	return ret;
}

/* TODO: respect values of watch in the parent loop so we don't repeatedly
 * make system calls to read(2) just to get EAGAIN every time. Or should we?
 * Is it worth checking every time? */
static int
print_columns (
	FILE *__restrict__ const o,
	FILE *__restrict__ const e,
	const int ncolumns,
	const unsigned char flags
) {
	const char
		*const red	= (flags & FLAG_COLOUR) ? "\033[31m" : "",
		*const green	= (flags & FLAG_COLOUR) ? "\033[32m" : "",
		*const nocolour	=
			((flags & (FLAG_COLOUR | FLAG_TIMESTAMPS)) == (FLAG_COLOUR | FLAG_TIMESTAMPS))
			? "\033[m"
			: "";

	int watch = STDOUT_FILENO | STDERR_FILENO;

	int cols = (ncolumns - (TIMESTAMP_SIZE * !!(flags & FLAG_TIMESTAMPS))) / 2;
	wchar_t	*__restrict__ const obuf = xcalloc(cols, sizeof *obuf),
		*__restrict__ const ebuf = xcalloc(cols, sizeof *ebuf);

	char timestamp[TIMESTAMP_SIZE] = "";

	/* should this be repeated on each loop? */
	if (flags & FLAG_TIMESTAMPS)
		sprint_time(timestamp);

	/* TODO: reset cols every loop if SIGWINCH has been (terminfo?) */
	while (get_wline_or_eof(obuf, cols, o, STDOUT_FILENO, &watch)
	    || get_wline_or_eof(ebuf, cols, e, STDERR_FILENO, &watch))
		wprintf(L"%s%s%s%-*ls%s%-*ls\n", nocolour, timestamp, green, cols, obuf, red, cols, ebuf);

	free(obuf);
	free(ebuf);
	return watch;
}

extern int
ugly_column_hack(const int ofd, const int efd, const unsigned char flags)
/* TODO: when the filestreams are opened, register a function to close them
 * with atexit(3)? */
{
	static const char mode[] = "r";
	static FILE *__restrict__ o = NULL, *__restrict__ e = NULL;
	static int ncolumns = -1; /* `int' for potential terminfo/curses compatibility */

	if (!((o || (o = fdopen(ofd, mode))) && (e || (e = fdopen(efd, mode))))) /* woowee */
		err(-1, NULL);

	if (ncolumns == -1) {
		/* TODO: detect number of columns properly */
		const char *__restrict__ const env_ncols = getenv("COLUMNS");
		if (!(env_ncols && (ncolumns = atoi(env_ncols)) > 0))
			ncolumns = 80;
	}

	return print_columns(o, e, ncolumns, flags);
}
