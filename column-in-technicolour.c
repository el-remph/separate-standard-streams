#include "config.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h> /* realloc(3), free(3) */
#include <wchar.h>

#include <err.h>

#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#elif defined HAVE_IOCTL_H
#include <ioctl.h>
#elif defined HAVE_STROPTS_H
#include <stropts.h>
#else
#define NO_IOCTL
#endif /* HAVE_SYS_IOCTL_H */
#ifndef NO_IOCTL
#include <signal.h>
#endif /* NO_IOCTL */

#include "column-in-technicolour.h"
#include "process_cmdline.h"
#include "timestamp.h"

#include "compat/unlocked-stdio.h"
#include "compat/bool.h"
#include "compat/ckdint.h"
#include "compat/inline-restrict.h"
#include "compat/__attribute__.h"

#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

static void * __attribute__((returns_nonnull))
xreallocbuf(void * ptr, const size_t nmemb, const size_t size)
/* like glibc reallocarray(3), but sets errno to EOVERFLOW */
{
	size_t actualsize;
#ifdef ckd_mul
	if (ckd_mul(&actualsize, nmemb, size)) {
		errno = EOVERFLOW;
		goto fail;
	}
#else
	actualsize = nmemb * size; /* eh w/e */
#endif

	ptr = realloc(ptr, actualsize);
	if (!ptr)
fail:		err(-1, NULL);
	return ptr;
}

#define XREALLOCBUF(ptr, size) (ptr = xreallocbuf(ptr, size, sizeof *ptr))

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
		*outbuf = L'\0';
		if (!watch_file(stream))
			*watch &= ~fd;
	}

	return ret;
}

/* this is set from TIOCGWINSZ(2const), from a struct winsize .ws_col, an
 * unsigned short, so it is initialised to a value which it cannot have
 * been set to */
static volatile int ncolumns = -1;

#ifdef TIOCGWINSZ
static void
handler_set_ncolumns(int sigwinch __attribute__((unused)))
{
	struct winsize ws;
	if (ioctl(fileno(stdout), TIOCGWINSZ, &ws) == 0)
		ncolumns = ws.ws_col;
	else
		warn("ioctl(2)");
}
#endif

static int /* unsigned short, mayhaps? */
ncolumns_init(void)
{
#ifdef TIOCGWINSZ
	{
		struct winsize ws;
		if (ioctl(fileno(stdout), TIOCGWINSZ, &ws) == 0) {
			struct sigaction sa = { 0 };
			sa.sa_handler = handler_set_ncolumns;
			if (sigaction(SIGWINCH, &sa, NULL) != 0)
				warn("sigaction(2)");
			return ws.ws_col;
		}
	}
	if (errno != ENOTTY)
		warn("ioctl(2)");
#endif

	{
		const char *__restrict__ const env_ncols = getenv("COLUMNS");
		const int res = env_ncols ? atoi(env_ncols) : 0;
		if (res > 0) /* && res < USHRT_MAX ? */
			return res;
	}

	/* if all else fails, default to the good old */
	return 80;
}

/* TODO: respect values of watch in the parent loop so we don't repeatedly
 * make system calls to read(2) just to get EAGAIN every time. Or should we?
 * Is it worth checking every time? */
static int
print_columns (
	FILE *__restrict__ const o,
	FILE *__restrict__ const e,
	const unsigned char flags
) {
	static wchar_t *__restrict__ obuf = NULL, *__restrict__ ebuf = NULL;
	static int prev_cols = -1; /* see comment on initialisation of ncolumns */
	char timestamp[TIMESTAMP_SIZE] = "";
	const char
		*const red	= (flags & FLAG_COLOUR) ? "\033[31m" : "",
		*const green	= (flags & FLAG_COLOUR) ? "\033[32m" : "",
		*const nocolour	=
			((flags & (FLAG_COLOUR | FLAG_TIMESTAMPS)) == (FLAG_COLOUR | FLAG_TIMESTAMPS))
			? "\033[m"
			: "";
	int watch = STDOUT_FILENO | STDERR_FILENO;

	/* this is the only time that ncolumns is referenced at all (a
	 * read), so this is hopefully async-signal-safe (assuming ofc that
	 * the read itself cannot be interrupted, which it surely can't
	 * be..?) */
	const int cols = (ncolumns - (TIMESTAMP_SIZE * !!(flags & FLAG_TIMESTAMPS))) / 2;

	if (cols != prev_cols)
		XREALLOCBUF(obuf, cols), XREALLOCBUF(ebuf, cols),
		prev_cols = cols;

	/* should this be repeated on each loop? */
	if (flags & FLAG_TIMESTAMPS)
		sprint_time(timestamp);

	/* TODO: reset cols every loop if SIGWINCH has been (terminfo?) */
	while (get_wline_or_eof(obuf, cols, o, STDOUT_FILENO, &watch)
	    || get_wline_or_eof(ebuf, cols, e, STDERR_FILENO, &watch))
		wprintf(L"%s%s" L"%s%-*ls" L"%s%-*ls" L"\n",
			nocolour, timestamp,
			green, cols, obuf,
			red, cols, ebuf);

	return watch;
}

extern int
ugly_column_hack(const int ofd, const int efd, const unsigned char flags)
/* TODO: when the filestreams are opened, register a function to close them
 * with atexit(3)? */
{
	static const char mode[] = "r";
	static FILE *__restrict__ o = NULL, *__restrict__ e = NULL;

	if (!((o || (o = fdopen(ofd, mode))) && (e || (e = fdopen(efd, mode))))) /* woowee */
		err(-1, NULL);

	if (ncolumns == -1)
		ncolumns = ncolumns_init();

	return print_columns(o, e, flags);
}
