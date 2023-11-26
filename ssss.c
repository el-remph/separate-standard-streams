/* SPDX-License-Identifier: GPL-3.0-or-later */
#define SPIEL "\
ssss -- split standard streams: highlight the stdout and stderr of a process\n\
Copyright 2023 the Remph\n\n\
This is free software; permission is given to copy and/or distribute it,\n\
with or without modification, under the terms of the GNU General Public\n\
Licence, version 3 or later. For more information, see the GNU GPL, found\n\
distributed with this in the file `GPL', and at https://gnu.org/licenses/gpl"

#if 0
static __inline__ void __attribute__((__noreturn__, nonnull))
foo(const unsigned char *__restrict__ bar __attribute__((nonstring)));
#endif
/* ^ might have gone a bit heavy on that stuff, is what I'm saying
 *
 * This program outputs into either unix file descriptors, stdio FILE*
 * streams, or curses WINDOW* objects, depending on the options it's called
 * with, *but never mixes them*. If using FILE* output, it adjusts the
 * buffering: in these situations, the buffers are used to store the pretty
 * stuff that is printed (if only -c but not -t or -p are set, then we can
 * real cutely sidestep stdio). Unlike normal, the standard streams are
 * fully buffered, and flushed to match the buffering of the child process
 * (`PROG' according to --help). Best beware of this and keep it consistent
 *
 * TODO:
 ** POSIX recommends (but does not require) sys/time.h to include
 *  sys/select.h -- what are the odds of having the former but the latter
 *  failing to compile on a future system, compared to the odds of anyone
 *  finding a system old enough to not have sys/select.h? And who would get
 *  pissier about it?
 ** The curses thing murderises the scrollback buffer. Use curses pads
 *  instead of windows? Or maybe some kind of auto-growing window, so that
 *  we don't use the whole screen immediately. Find out how less does it
 ** waddnstr(3X) can't deal with embedded NUL chars, but waddch(3X) syncs
 *  we can't just call that iteratively. waddnstr(3X) also doesn't tell us
 *  where it left off (such as eg. strchrnul(3)) so we'd have to make a
 *  second pass over the string with memchr(3) to find NUL, and restart
 *  from there, until memchr(3) returned NULL; or, make one big pass over
 *  the string with memchr(3) iteratively, replacing NULs (with what?),
 *  then call waddnstr(3X). That minimises calls to waddnstr(3X) but it's
 *  still one pass too many. Could make an option like -0 to `not break
 *  when input containing embedded NULs is passed,' but what the fuck is
 *  the use case for that? */

/* feature_test_macros(7):
 ** _XOPEN_SOURCE>=500 needed for SA_RESETHAND, though if the system really
 *  doesn't have the latter, the program will silently compile fine without
 *  its functionality
 ** _POSIX_C_SOURCE>=2 for getopt(3) and iirc gettimeofday(2) and select(2)
 ** _BSD_SOURCE is so we can use gettimeofday(2) for compatibility with old
 *  systems, cause adoption of clock_gettime(2) was a bit of a minefield. It
 *  was POSIXed in 1993, but the BSDs didn't implement it until the late 90s
 *  and Linux didn't implement it for the better part of a decade, and then
 *  when it did, it wanted LDFLAGS+=-lrt for a while, until it didn't. Now
 *  everyone's caught up, POSIX is very eager to `obsolete' gettimeofday(2)
 *  but considering the speed of response to the introduction of
 *  clock_gettime(2), I'm not holding my breath
 *
 ** snprintf(3) is widely available and may be enabled by _BSD_SOURCE,
 *  _XOPEN_SOURCE>=500, or just ISO C99
 ** <err.h> is a bugger -- consistently the biggest bugger in any of my
 *  programs. It isn't specified in any standard, but is fairly ubiquitous
 *  on BSD-like systems, and inherited by GNU, going back to the 80s. Any
 *  modern Unix will be either GNU (GNU+Linux), BSD-alike (*BSD obviously,
 *  and macOS), or some alternative userland on Linux, which typically will
 *  have either musl or uClibc, both of which provide <err.h>. So it might
 *  as well be standardised, and is much too useful to give up. Nevertheless,
 *  HP-UX and IBM AIX users beware! (maybe)
 ** If available (on GNU or POSIX-2008-conformant systems), strsignal(3) is
 *  used. Otherwise, sys_siglist is used, inkeeping with the ancient BSD
 *  feel
 *
 * If bloody glibc wouldn't be such a bitch about _BSD_SOURCE basically
 * spoiling all your macros, this would make life a hell of a lot easier
 *
 * But *other* than all that, the code is fine with just _POSIX_C_SOURCE or
 * even _POSIX_SOURCE (though we don't define that cause glibc gripes about
 * that too) */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 500
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 2
#endif

/* Semi-standard, BSD-origin (sys_siglist[], and gettimeofday(2) is
 * ex-POSIX, the others are POSIX)
 *
 * Yeah I know I'm not supposed to fuck with the macros like this. To
 * mitigate some of the damage, the headers for which the macros are
 * fucked with are the first included, so if they need to include any
 * headers of their own with those same macros defined, that's certain to
 * happen here */
#ifndef _BSD_SOURCE
#define _BSD_SOURCE
#endif
#include <sys/time.h>	/* gettimeofday(2); select(2) on old systems */
#include <signal.h>	/* sigaction(2), kill(2), sys_siglist[] if we can't
			 * get strsignal(3) */

#ifdef DEBUG_MACROS
/* provide a consistent lowest-common-denominator environment */
# undef _BSD_SOURCE
# undef _SVID_SOURCE /* justin case */
# undef _DEFAULT_SOURCE
# undef  _GNU_SOURCE
# undef  _XOPEN_SOURCE
# define _XOPEN_SOURCE 500
# undef  _POSIX_C_SOURCE
# define _POSIX_C_SOURCE 2
#endif

/* STDC */
#include <errno.h>
#include <locale.h>	/* setlocale(3) */
#include <stdio.h>
#include <stdlib.h>	/* exit(3), atexit(3) */
#include <string.h>	/* memcpy(3), memchr(3); strlen(3) and strcmp(3) in
			 * process_cmdline(); strsignal(3) if available */

/* POSIX */
#include <err.h>	/* Not actually POSIX but should be */
#include <fcntl.h>	/* Actually fcntl(2), funnily enough */
#include <sys/select.h> /* old systems may need to delete this (the old
			 * necessary header files are included anyway) */
#include <sys/types.h>	/* Big fat misc: ssize_t, wait(2), write(2), general
			 * good practice, one change fewer for select(2) to
			 * work on old systems... */
#include <sys/wait.h>	/* wait(2), dumbass */
#include <time.h>	/* localtime(3), strftime(3) */
#include <unistd.h>	/* pipe(2), dup2(2), fork(2), execvp(3), getopt(3),
			 * isatty(3), write(2), read(2) */

#if defined(_GNU_SOURCE) || _POSIX_C_SOURCE >= 200809L
# define STRSIGNAL(sig) strsignal(sig)
#else
# define STRSIGNAL(sig) sys_siglist[sig]
#endif /* GNU or POSIX-2008 */

#ifdef WITH_CURSES

/* sidestep some -pedantic compiler warnings when -ansi */
# ifdef __STRICT_ANSI__
#  define NCURSES_ENABLE_STDBOOL_H 0
# endif

# ifdef WITH_CURSES_WIDE
#  include <wchar.h>	/* mbsrtowcs(3), wmemchr(3) */
#  define NCURSES_WIDECHAR 1
#  define _XOPEN_SOURCE_EXTENDED
#  define WADDNSTR waddnwstr
typedef wchar_t curs_char_T;
# else
#  define WADDNSTR waddnstr
typedef char curs_char_T;
# endif /* with wide curses */

# include <curses.h>
# include <term.h> /* putp(3X) and *_ca_mode used in set_up_curses */

/* Just make sure that true and false are definitely defined, since curses
 * only guarantees us TRUE and FALSE, but may also provide true and false
 * from stdbool.h */
# ifndef  true
#  define true 1
# endif
# ifndef  false
#  define false 0
# endif

#else /* without curses */

/* If we have no curses, we need to find a bool of our own */
# if __STDC_VERSION__ >= 199900L
#  include <stdbool.h>
# elif defined(__GNUC__) && !defined(__STRICT_ANSI__)
#  define bool _Bool
#  define true 1
#  define false 0
# else /* Neither -std=c99 nor -std=gnu89 */
typedef enum { false = 0, true = 1 } bool;
# endif /* -std=c99 or -std=gnu89 */

#endif /* with curses */

#include "compat__attribute__.h" /* This must always be the last #include */

/* In case some macros aren't defined in those headers, on very old systems */
#ifndef SA_RESETHAND
#define SA_RESETHAND 0 /* ): */
#endif

#ifndef STDOUT_FILENO
#define STDOUT_FILENO 1
#endif
#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

/* Take advantage of features where available */


#if __STDC_VERSION__ >= 199901L
# define INLINE inline
# define RESTRICT restrict
#elif (defined(__GNUC__) && !defined(__STRICT_ANSI__))
# define INLINE __inline__
# define RESTRICT __restrict__
#else /* Neither -std=c99 nor -std=gnu89 */
# define INLINE
# define RESTRICT
#endif /* -std=c99 or -std=gnu89 */

#if __STDC_VERSION__ >= 201100L
# include <stdnoreturn.h>
#else
# define noreturn __attribute__((__noreturn__))
/* Beware using noreturn in functions with other __attribute__s */
#endif

/* An oversight in POSIX: if an IO function fails due to O_NONBLOCK, errno
 * could be either EAGAIN or EWOULDBLOCK, and they may be different values.
 * In practice, this is rarely if ever the case, but truly POSIXLY_CORRECT
 * programs should test both. This tests if this actually necessary */
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
# define WOULD_BLOCK(e) (e == EWOULDBLOCK | e == EAGAIN)
/* Bitwise OR is here used in place of its^boolean cousin because its
 * operands are true booleans. The two options are optimised to the same
 * assembly output by any modern optimising compiler, but on older
 * compilers this produces very marginally better assembly, so */
#else
# define WOULD_BLOCK(e) (e == EAGAIN)
#endif

/* Flag constants -- used to be macros, but it's useful to have them typed
 * just in case */
#if __STDC_VERSION__ >= 202300L
enum : unsigned char {
#else
const unsigned char
#endif /* C23 */
	FLAG_ALLINONE	= 1 << 0,
	FLAG_TIMESTAMPS	= 1 << 1,
	FLAG_COLOUR	= 1 << 2,
	FLAG_PREFIX	= 1 << 3,
	FLAG_VERBOSE	= 1 << 4,
	FLAG_QUIET	= 1 << 5
#ifdef WITH_CURSES
	,FLAG_CURSES	= 1 << 6
#endif /* with curses */
#if __STDC_VERSION__ >= 202300L
}
#endif /* C23 */
	;

#define TIMESTAMP_SIZE (sizeof "[00:00:00.000000] ")

static void __attribute__((nonnull))
sprint_time(char *RESTRICT buf)
/* buf must be TIMESTAMP_SIZE */
{
	struct timeval t;
	gettimeofday(&t, NULL);
	strftime(buf, TIMESTAMP_SIZE, "[%H:%M:%S", localtime(&t.tv_sec));
	snprintf(buf + sizeof "[00:00:00" - 1, sizeof ".000000] ",
		/* -1 is to overwrite NUL ^^^ */
		".%06ld] ", t.tv_usec);
}

static INLINE void __attribute__((nonnull))
mkprefix(const unsigned char flags, const int fd, char *RESTRICT prefixbuf)
/* Based on flags and fd, writes a prefix to prefixbuf that should prefix
 * each buffalo in buffalo, eg. `[21:34:56.135429]&1 ' */
{
	*prefixbuf = '\0'; /* guarantee NUL termination just in case */

	if (flags & FLAG_TIMESTAMPS) {
		sprint_time(prefixbuf);
		prefixbuf += TIMESTAMP_SIZE - 2;
		/* -2 so a subsequent write ^^^ will overwrite the trailing
		 * space and NUL */
	}

	/* TODO: a small thing, but this doesn't need to keep being
	 * recalculated */
	if (flags & FLAG_PREFIX)
		*prefixbuf++ = '&', *prefixbuf++ = fd + '0', /* Assumes that
		* fd < 10; if it isn't, then we're ^^^^^^^^ into punctuation */
		*prefixbuf++ = ' ', *prefixbuf = '\0';
}

/* TODO: the two prepend_lines functions need to be merged properly */

static INLINE void __attribute__((nonnull))
prepend_lines(FILE *RESTRICT outstream, const char *RESTRICT prefixstr,
		const char * unprinted __attribute__((nonstring)),
		/* can't be^RESTRICTed because it's aliased in the function
		 * body by newline_ptr */
		size_t n_unprinted)
{
	const char * newline_ptr = unprinted;

	/* Look for a newline anywhere but the last char */
	while ((newline_ptr = memchr(unprinted, '\n', n_unprinted - 1))) {
		fputs(prefixstr, outstream);
		newline_ptr++;
		n_unprinted -= fwrite(unprinted,
				1, newline_ptr - unprinted, outstream);
		unprinted = newline_ptr;
	}

	/* Once any embedded newlines have been exhausted, print the rest */
	fputs(prefixstr, outstream);
	fwrite(unprinted, 1, n_unprinted, outstream);
}

#ifdef WITH_CURSES
static int /* int for waddnstr(3X) compatibility */ __attribute__((nonnull))
prepend_lines_curses(WINDOW *RESTRICT w, const curs_char_T * unprinted,
		int /*waddnstr(3X) again*/ n_unprinted)
{
	const curs_char_T * newline_ptr = unprinted;
	char prefixstr[TIMESTAMP_SIZE];

	sprint_time(prefixstr);

	/* Look for a newline anywhere but the last char */
# ifdef WITH_CURSES_WIDE
	while ((newline_ptr = wmemchr(unprinted, L'\n', n_unprinted - 1))) {
# else
	while ((newline_ptr = memchr(unprinted, '\n', n_unprinted - 1))) {
# endif
		/* I want to use lots of sizeof here but apparently C's
		 * type system handles that just fine */
		size_t n_printed = ++newline_ptr - unprinted;
		/* ++preincrement  ^^ significant here */
		waddstr(w, prefixstr);
		n_unprinted -= n_printed; /* Bit optimistic */
		WADDNSTR(w, unprinted, n_printed);
		unprinted = newline_ptr; /* Order is important here */
	}

	/* Once any embedded newlines have been exhausted, print the rest */
	waddstr(w, prefixstr);
	WADDNSTR(w, unprinted, n_unprinted);

	return 0;
}
#endif /* with curses */

#define CAT_IN_TECHNICOLOUR(a)\
	bool a(const int ifd, void * output_target, const unsigned char flags)
/* Returns whether ifd is worth listening to anymore (ie. hasn't hit EOF).
 * Also, a whole C++ compiler just for type polymorphism? Bitch */

static __attribute__((nonnull))
CAT_IN_TECHNICOLOUR(cat_in_technicolour_timestamps)
/* This one outputs to FILE* streams */
{
	ssize_t nread;
	char prefixstr[TIMESTAMP_SIZE + 3];
	char buf[BUFSIZ] __attribute__((nonstring));
	FILE *RESTRICT outstream;
	const int fd = *(int*)output_target;
	bool ret = true;

	/* While this does mean that flags must be rechecked every time this
	 * is called, I tried the other way and believe it or not it was
	 * even worse. Also, need colour in here to move towards merging with
	 * the curses version
	 *
	 * Also, writers of code readability guidelines are fannies */

	const char *RESTRICT colour =
		(flags & FLAG_COLOUR)
			? (fd == STDOUT_FILENO ? "\033[32m" : "\033[31m")
			: "";

	if (flags & FLAG_ALLINONE)
		outstream = stdout;
	else switch (fd) {
		case STDOUT_FILENO: outstream = stdout; break;
		case STDERR_FILENO: outstream = stderr; break;
		default: errno = EBADF; err(-1, "internal error");
	}

	mkprefix(flags, fd, prefixstr);

	do {
		nread = read(ifd, buf, BUFSIZ);
		switch (nread) {
			case -1:
				if (WOULD_BLOCK(errno))
					goto end;
				else
					err(-1, "read(2)");
			case  0: close(ifd); ret = false; goto end;
			default:
			fputs(colour, outstream);
			prepend_lines(outstream, prefixstr, buf, nread);
		}
	} while (nread == BUFSIZ);

end:
	fflush(outstream);
	return ret;
}

static __attribute__((nonnull))
CAT_IN_TECHNICOLOUR(cat_in_technicolour) /* buffalo buffalo */
/* This one outputs to unix file descriptors */
{
	/* const everything */
	const int fd = *(int*)output_target;
	const int ofd = (flags & FLAG_ALLINONE) ? STDOUT_FILENO : fd;
	const char *RESTRICT colour =
		(flags & FLAG_COLOUR)
			? (fd == STDOUT_FILENO ? "\033[32m" : "\033[31m")
			: "";

	/* actual variables we'll be operating on, we need for io */
	char buf[BUFSIZ] __attribute__((nonstring));
	ssize_t nread;

	if (*colour) {
		const size_t prefixn = 5; /* strlen(colour) */
		memcpy(buf, colour, prefixn);
		/* First read will be a special case, reading into buf
		 * starting after the prefix */
		nread = read(ifd, buf + prefixn, BUFSIZ - prefixn);
		if (nread > 0) nread += prefixn;
		goto test_nread; /* Jump into the loop after the read,
		* having done the special-case first read; subsequent
		* iterations of the loop will do the regular first read */
	}

	do {
		nread = read(ifd, buf, BUFSIZ);
test_nread:	switch (nread) {
			case -1:
			if (WOULD_BLOCK(errno))
				return true;
			else
				err(-1, "read(2)");

			case  0: close(ifd); return false;
			default: write(ofd, buf, nread);
		}
	} while (nread == BUFSIZ);

	return true;
}

#ifdef WITH_CURSES
static __attribute__((nonnull))
CAT_IN_TECHNICOLOUR(curse_in_technicolour)
/* This one outputs to WINDOW* objects */
{
	WINDOW * w = (WINDOW*)output_target;
	ssize_t nread;
	int (*out_)(WINDOW*, const curs_char_T*, int) =
		(flags & FLAG_TIMESTAMPS) ? prepend_lines_curses : WADDNSTR;

# ifndef WITH_CURSES_WIDE
	/* If we're WIDE then buf is passed to mbsrtowcs(3), so must be
	 * ASCIZ; else goes straight out_() */
	__attribute__((nonstring))
# endif
	char buf[BUFSIZ];

	do {
		nread = read(ifd, buf, BUFSIZ
# ifdef WITH_CURSES_WIDE
			- 1 /* for NUL */
# endif
		);
		switch (nread) {
			case -1:
			if (WOULD_BLOCK(errno))
				return true;
			else
				err(-1, "read(2)");
			case  0: close(ifd); return false;
			default:
# ifdef WITH_CURSES_WIDE
			buf[nread] = '\0'; /* Hence BUFSIZ - 1 above */
			{
				const char * ptr = buf;
				wchar_t wbuf[BUFSIZ];
				const size_t fet =
					mbsrtowcs(wbuf, &ptr, BUFSIZ, NULL);
				if (fet == (size_t)-1) err(-1, "input");
				out_(w, wbuf,fet/* wobuffet! */);
			}
# else
			out_(w, buf, nread);
# endif
			wnoutrefresh(w);
		}
	} while (nread == BUFSIZ);

	return true;
}

static void __attribute__((nonnull))
set_up_curses_window(WINDOW *RESTRICT w, const short colour_pair)
{
	idlok(w, true);
	scrollok(w, true);

	/* 0 as a colour pair is special to curses and would be a noop
	 * here, so it's used to signify colour unavailability */
	if (colour_pair)
		wcolor_set(w, colour_pair, NULL);

	/* wmove(3X) the cursor to the bottom of the window, to write lines
	 * from the bottom up */
	wmove(w, LINES - 1, 0);
	leaveok(w, true); /* No more need for the cursor */
}

static INLINE void __attribute__((nonnull))
set_up_curses(WINDOW *RESTRICT *RESTRICT window)
/* window must be an array of exactly two WINDOW pointers */
{
	/* just a cast to shut up the compiler */
	atexit((void (*)(void))endwin);

	initscr();
	/* Try and keep the output on the terminal (not use the alternate
	 * screen). This hack is probably copyright Thomas E. Dickey and
	 * the FSF, although I take full responsibility for omitting what
	 * looked like quite important defensive code cause I cba. I'm sure
	 * in time I'll be even sorrier for that */
	refresh();
	putp(exit_ca_mode);
	putp("\n"); /* FIXME: If PROG has no output, we print an empty line */
	fflush(stdout);
	enter_ca_mode = NULL, exit_ca_mode = NULL;

	/* No need for cbreak(3) or noecho(3), this should basically be a
	 * normal terminal showing some weird output */
	start_color();

	/* divide screen in half vertically */
	window[0] = newwin(0, COLS / 2, 0, 0),
	window[1] = newwin(0, 0, 0, COLS / 2);
	if (has_colors()) {
		init_pair(1, COLOR_GREEN, 0);
		init_pair(2, COLOR_RED, 0);
		set_up_curses_window(window[0], 1);
		set_up_curses_window(window[1], 2);
	} else {
		set_up_curses_window(window[0], 0);
		set_up_curses_window(window[1], 0);
	}
}
#endif /* with curses */

static INLINE void
parent_listen(const int child_out, const int child_err,
		const unsigned char flags)
{
	/* If -t|-p, points to a more compicated function that uses stdio;
	 * else points to a slimmer one using only unix io
	 *
	 * C noobs BTFO as ever */
	CAT_IN_TECHNICOLOUR((*cat_in_technicolour_)) =
#ifdef WITH_CURSES
		(flags & FLAG_CURSES)
			? curse_in_technicolour
			:
#endif
			(flags & (FLAG_TIMESTAMPS | FLAG_PREFIX))
				? cat_in_technicolour_timestamps
				: cat_in_technicolour;

	/* whether the respective stream is still worth watching -- a bit
	 * array of the file descriptors OR'd together. Clever, hey? No */
	unsigned char watch = STDOUT_FILENO | STDERR_FILENO;

	/* ugly stuff for C ```polymorphism''' */
	void * out_target, * err_target;
	/* These could and probably should be const, but passing a void* to
	 * them makes that feel kinda dishonest, and also the compiler gets
	 * up my ass for it, so have a static to make up for it */
	static int out_fd = STDOUT_FILENO, err_fd = STDERR_FILENO;

	/* Beware: cute preprocessor shit */
#ifdef WITH_CURSES
	WINDOW *RESTRICT w[2];
	if (flags & FLAG_CURSES) {
		set_up_curses(w);
		out_target = w[0], err_target = w[1];
	} else
#endif
		out_target = &out_fd, err_target = &err_fd;

	/* set pipes to nonblocking so that if we get more than BUFSIZ
	 * bytes at once we can use read(2) to check if the pipe is empty
	 * or not, in *cat_in_technicolour_ */
	fcntl(child_out, F_SETFL, O_NONBLOCK);
	fcntl(child_err, F_SETFL, O_NONBLOCK);

	do {
		fd_set fds;
		int fdsn = 1; /* get the increment over with */

		FD_ZERO(&fds);
		if (watch & STDOUT_FILENO) {
			fdsn += child_out;
			FD_SET(child_out, &fds);
		}
		if (watch & STDERR_FILENO) {
			fdsn += child_err;
			FD_SET(child_err, &fds);
		}
#ifdef WITH_CURSES
		if (flags & FLAG_CURSES) doupdate();
#endif
		if (fdsn == 1) break;
		switch (select(fdsn, &fds, NULL, NULL, NULL)) {
			case -1: err(-1, "select(2)");
			case  0: return;
			default:
			/* Read from stderr first, that's probably more
			 * pressing */
			if (FD_ISSET(child_err, &fds))
				if (!cat_in_technicolour_(child_err, err_target, flags))
					watch &= ~(unsigned char)STDERR_FILENO;
			if (FD_ISSET(child_out, &fds))
				if (!cat_in_technicolour_(child_out, out_target, flags))
					watch &= ~(unsigned char)STDOUT_FILENO;
		}
	} while (watch);
}

static INLINE int __attribute__((nonnull))
parent_wait_for_child(const char *RESTRICT child, const unsigned char flags)
/* Clean up after child (common parenting experience). Returns $? */
{
	int child_ret;
	char timebuf[TIMESTAMP_SIZE] = ""; /* zero-init */

	wait(&child_ret);

	if (flags & FLAG_TIMESTAMPS && !(flags & FLAG_QUIET))
		sprint_time(timebuf);

	if (WIFEXITED(child_ret)) {
		const int ret = WEXITSTATUS(child_ret);
		if (flags & FLAG_VERBOSE || (!(flags & FLAG_QUIET)
                                             && ret != EXIT_SUCCESS)) {
			if (*timebuf) fputs(timebuf, stderr);
			warnx("%s exited with status %d", child, ret);
		}
		return ret;
	} else {
		if (!(flags & FLAG_QUIET)) {
			if (*timebuf) fputs(timebuf, stderr);
			if (WIFSIGNALED(child_ret)) {
				const int sig = WTERMSIG(child_ret);
				warnx("%s killed by signal %d: %s",
					child, sig, STRSIGNAL(sig));
			} else
				warn("%s wait(2) status unexpected: %d. errno says",
					child, child_ret);
		}
		return child_ret; /* *Not* sig -- more conventional */
	}
}

noreturn static void
usage(const char *RESTRICT progname)
{
	static const char help[] = "\
Usage: %s [OPT(s)] PROG [PROGARG(s)]\n\
Runs PROG with PROGARG(s) if any, and marks which of the output is stdout\n\
and which is stderr. Returns PROG's exit status\n\
Options:\n\
	-1	Output everything to one stream, stdout. Equivalent of piping\n\
		through |& in bash\n\
	-2	Output PROG's stdout->stdout and stderr->stderr (default)\n\
	-A OPTS	Set any applicable option characters in OPTS (/(?i)[cp]/) to\n\
		auto-detect their values (ie. default settings)\n\
	-c	Colour output (default: if output isatty(3))\n\
	-C	Turn off -c\n\
	-p	Prefix lines with the fd whence they came (default: if\n\
		output isn't coloured)\n\
	-P	Turn off -p\n"
#ifdef WITH_CURSES
"	-S	Print streams side-by-side, using curses (bit of a WIP). Note\n\
		also that -[12ACcPp] are (mostly) silently ignored if this\n\
		flag is passed\n"
#endif
"	-t	Add timestamps\n\
	-q	Quiet -- don't print anything of our own, just get busy\n\
		transforming the output of PROG\n\
	-v	Verbose -- print more\n\
	--help, -h	Print this help\n\
	--version, -V	Print version information\n";

	printf(help, progname);
	exit(EXIT_SUCCESS);
}

noreturn static void
version(void)
{
	char verspiel[] = "ssss version 0.1, built " __DATE__
#ifdef WITH_CURSES
		", with the -S extension with"
# ifndef WITH_CURSES_WIDE
		"out"
# endif
		" UTF-8 support"
#endif
		"\n" SPIEL;

#if 1 /* The artist formerly known as #ifdef DEBUG_MACROS */
	printf("%s\n\n_POSIX_C_SOURCE=%ld\n_XOPEN_SOURCE=%d\n_GNU_SOURCE "
# ifndef _GNU_SOURCE
		"un"
# endif
		"defined\n", verspiel, (long)_POSIX_C_SOURCE, _XOPEN_SOURCE);
#else
	puts(verspiel);
#endif

	exit(EXIT_SUCCESS);
}

static unsigned char
process_cmdline(const int argc, char *const argv[])
/* Returns flags */
{
	static const char optstr[] = "+12A:CPVchpqtv"
	/* The + at the beginning is  ^ for GNU getopt(3), to let us pass
	 * options to PROG (else it permutes them away to us)
	 *
	 * Note also lack of semicolon, that's to faciliate this: */
#ifdef WITH_CURSES
		"S"
#endif
	; /* And *now*, the semicolon */

	unsigned char flags = 0;
	enum { ON, OFF, AUTO } colour = AUTO, prefix = AUTO;

	/* hacky support for --help and --version */
	if (argv[1] && argv[1][0] == '-') {
		const char * longname = argv[1] + 1 + !!(argv[1][1] == '-');
		if (strcmp(longname, "help") == 0)
			usage(argv[0]);
		if (strcmp(longname, "version") == 0)
			version();
	}

	for (;;) {
		const int o = getopt(argc, argv, optstr);
		if (o == -1) break;
		switch (o) {
		case '1':	flags |=  FLAG_ALLINONE; break;
		case '2':	flags &= ~FLAG_ALLINONE; break;
		case 'A':
			for (; *optarg; optarg++)
				switch (*optarg) {
				case 'C': case 'c': colour = AUTO; break;
				case 'P': case 'p': prefix = AUTO; break;
				default: errx(-1, "invalid: -A %c", *optarg);
				}
			break;
		case 'C':	colour = OFF; break;
		case 'P':	prefix = OFF; break;
#ifdef WITH_CURSES
		case 'S':	flags |= FLAG_CURSES; break;
#endif
		case 'V':	version();
		case 'c':	colour = ON;  break;
		case 'h':	usage(argv[0]);
		case 'p':	prefix = ON;  break;
		case 'q':	flags |= FLAG_QUIET; break;
		case 't': 	flags |= FLAG_TIMESTAMPS; break;
		case 'v':	flags |= FLAG_VERBOSE; break;

		case '+':
			/* Provide an error message for -+ on non-GNU
			 * systems. It'll fail notwithstanding, just
			 * don't want it to fail silently */
			warnx("invalid option -- +");
			/* fallthrough */
		default:	exit(-1);
		}
	}

	if (argc - optind == 0)
		errx(-1, "not enough arguments");

	if ((flags & FLAG_QUIET) && (flags & FLAG_VERBOSE))
		errx(-1, "Can't specify both -q and -v"); /* Because I say so */

#ifdef WITH_CURSES
	if (flags & FLAG_CURSES) {
		/* quarter-arsed attempt to warn the user for options that
		 * conflict with -S */
		const char optwarning[] = "-%c is ignored when -S is specified";
		if (flags & FLAG_ALLINONE)
			warnx(optwarning, '1');
		switch (prefix) {
			case AUTO: break; /* no worries */
			case OFF:  warnx(optwarning, 'P'); break;
			case ON:   warnx(optwarning, 'p'); break;
		}
		switch (colour) {
			case AUTO: break; /* no worries */
			case OFF:  warnx(optwarning, 'C'); break;
			case ON:   warnx(optwarning, 'c'); break;
		}

		return flags; /* No need to work on -p or -c */
	}
#endif /* with curses */

	/* Hey man, come take a look at this. You here about the compiler
	 * warnings? Yeah, don't bother yourself with them, buncha squares. I
	 * just tune em out. Anyway, come get a load of this shit, it'll
	 * blow your fuckin mind, man
	 *
	 * I'm so fucking far ahead, man, I'm pretty sure the compiler
	 * literally cannot understand this, it needs at least a null
	 * statement just to begin to understand this kind of shit but
	 * *there is no null statement*, that's the fucking point! I'm going
	 * places the compiler can't follow! */
	switch (colour) {
	case AUTO:
		if (flags & FLAG_ALLINONE ? isatty(1) : isatty(1) && isatty(2))
	case ON:	flags |= FLAG_COLOUR;
		else
	case OFF:	flags &= ~FLAG_COLOUR;
	}
	switch (prefix) {
	case AUTO:
		if (flags & FLAG_COLOUR)
	case OFF:	flags &= ~FLAG_PREFIX;
		else
	case ON:	flags |= FLAG_PREFIX;
	}

	return flags; /* told you so */
}

static void
clean_up_colour()
/* May be called with either (void) by atexit(3) or (int) by sigaction(2).
 * Warning: this function has state (static) */
{
	static bool already_done = false;
	if (!already_done) {
		fputs("\033[m", stdout);
		fflush(stdout);
		already_done = true;
	}
}

static void
handle_bad_prog(int sig __attribute__((unused)))
/* signal handler, set up by setup_handle_bad_prog, to see if the child
 * process fails to exec and signals this back to us -- if it does, it will
 * exit with the value of errno after execvp(3) failed */
{
	int child_ret;
	const pid_t piddle = wait(&child_ret);
	if (WIFEXITED(child_ret)) {
		errno = WEXITSTATUS(child_ret);
		err(-1, "can't exec command");
	} else
		err(-1, "unexpected error in child process, pid %d",
			piddle);
}

static INLINE void
setup_handle_bad_prog(void)
{
	struct sigaction handle_bad_prog_onsig;
	handle_bad_prog_onsig.sa_handler = handle_bad_prog;
	handle_bad_prog_onsig.sa_flags = SA_RESETHAND;
	sigemptyset(&handle_bad_prog_onsig.sa_mask);
	if (sigaction(SIGUSR1, &handle_bad_prog_onsig, NULL))
		warn("sigaction(2)");
}

static INLINE void __attribute__((nonnull))
child_prepare(const char *RESTRICT cmd, const unsigned char flags,
		int *RESTRICT child_stdout, int *RESTRICT child_stderr)
/* int pointers are to arrays of exactly 2 ints */
{
	/* Close read ends, we're reading from the child so the
	 * child is not to read from the parent */
	close(child_stdout[0]);
	close(child_stderr[0]);

	/* Send the std streams into the write ends of our pipes */
	dup2(child_stdout[1], STDOUT_FILENO);

	/* Last chance for the child to talk to the stderr before it
	 * gets duped */
	if (flags & FLAG_VERBOSE) {
		if (flags & FLAG_TIMESTAMPS) {
			char buf[TIMESTAMP_SIZE];
			sprint_time(buf);
			fputs(buf, stderr);
		}
		warnx("starting %s", cmd);
		fflush(stderr);
	}

	dup2(child_stderr[1], STDERR_FILENO);
}

static INLINE void __attribute__((nonnull))
parent_prepare(const unsigned char flags, int *RESTRICT child_stdout,
		int *RESTRICT child_stderr)
/* int pointers are to arrays of exactly 2 ints */
{
	/* Inverse of the child process' close(2) calls */
	close(child_stdout[1]);
	close(child_stderr[1]);

	/* Last point before colour may be output; take the opportunity to
	 * register clean_up_colour if necessary */
	if (flags & FLAG_COLOUR) {
		struct sigaction clean_up_colour_onsig;
		clean_up_colour_onsig.sa_handler = clean_up_colour,
		clean_up_colour_onsig.sa_flags = SA_RESETHAND;
		sigemptyset(&clean_up_colour_onsig.sa_mask);
		if (sigaction(SIGINT, &clean_up_colour_onsig, NULL))
			warn("sigaction(2)");

		atexit(clean_up_colour);
	}

	/* If we're going to be using cat_in_technicolour_timestamps,
	 * change stdio buffer settings; see the top of this file for
	 * details
	 *
	 * Also, beware: more preprocessor sophistry */
	if (
#ifdef WITH_CURSES
		!(flags & FLAG_CURSES) &&
#endif
		(flags & (FLAG_TIMESTAMPS | FLAG_PREFIX)))
	{
		setvbuf(stdout, NULL, _IOFBF, 0);
		if (!(flags & FLAG_ALLINONE))
			setvbuf(stderr, NULL, _IOFBF, 0);
	}
}

int
main(const int argc, char *const argv[])
{
	const unsigned char flags = process_cmdline(argc, argv);
	int child_stdout[2], child_stderr[2];
	pipe(child_stdout);
	pipe(child_stderr);

	setlocale(LC_ALL, "");
	/* curses in particular wants this, but also just good practice */

	setup_handle_bad_prog(); /* i.e. handle SIGUSR1. Best do this
	* before we fork(2), in case of the unlikely event that the child
	* process gets all the way to sending SIGUSR1 before we're even
	* prepared */

	switch (fork()) {
	case -1:	err(-1, NULL);

	/* Child process */
	case 0:
		child_prepare(argv[optind], flags, child_stdout, child_stderr);
		execvp(argv[optind], argv + optind);
		/* If we're here, then exec(3) failed */
		{
			const int er = errno;
			kill(getppid(), SIGUSR1); /* run to parent and tell */
			return er/*? I 'ardly know 'er! Sorry */;
		}

	default:
		parent_prepare(flags, child_stdout, child_stderr);
		parent_listen(child_stdout[0], child_stderr[0], flags);

		/* cleanup and finishing off */
		if (flags & FLAG_COLOUR)
			clean_up_colour();
#ifdef WITH_CURSES
		else if (flags & FLAG_CURSES)
			endwin();
#endif
		/* else no cleanup needed */

		return parent_wait_for_child(argv[optind], flags);
	}
}
