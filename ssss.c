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
 ** The curses thing doesn't use the scrollback buffer. I keep thinking
 *  that something like filter(3X) is the solution, then backing out. Pads
 *  are probably the way to go, but they'll need to be manually wrapped and
 *  grown. How does `git diff' do it?
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
 ** _POSIX_C_SOURCE>=2 for getopt(3)
 ** _POSIX_C_SOURCE>=199309L for sigqueue(3) (optional, replaced with
 *  kill(2) if SA_SIGINFO is undefined, but you're pretty likely to have it)
 ** Defined if applicable in config.h:
 *** _POSIX_C_SOURCE=200809L for strsignal(3), or _BSD_SOURCE and
 *   _DEFAULT_SOURCE for sys_siglist[]
 *** _BSD_SOURCE and possibly _GNU_SOURCE for unlocked_stdio(3)
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
 *
 * But *other* than all that, the code is fine with just _POSIX_C_SOURCE or
 * even _POSIX_SOURCE (though we don't define that cause glibc gripes about
 * that too) */
#include "config.h" /* Must be before any other includes or test macros */

#if _POSIX_C_SOURCE < 199309L
#undef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 199309L
#endif
#if _XOPEN_SOURCE < 500
#undef _XOPEN_SOURCE
#define _XOPEN_SOURCE 500
#endif
#ifdef WITH_CURSES_WIDE
#define _XOPEN_SOURCE_EXTENDED
#endif

/* STDC */
#include <errno.h>
#include <locale.h>	/* setlocale(3) */
#include <stdio.h>
#include <stdlib.h>	/* exit(3), atexit(3) */
#include <string.h>	/* memcpy(3), memchr(3); strcmp(3) in process_cmdline();
			 * strsignal(3) */
/* POSIX */
#include <err.h>	/* Not actually POSIX but should be */
#include <fcntl.h>	/* Actually fcntl(2), funnily enough */
#include <signal.h>	/* sigaction(2), kill(2) */
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>	/* old systems require different includes, which
			 * here are included anyway */
#endif
#include <sys/time.h>	/* gettimeofday(2); select(2) on old systems */
#include <sys/types.h>	/* Big fat misc: ssize_t, wait(2), write(2), general
			 * good practice, one change fewer for select(2) to
			 * work on old systems... */
#include <sys/wait.h>	/* wait(2), dumbass */
#include <time.h>	/* localtime(3), strftime(3) */
#include <unistd.h>	/* pipe(2), dup2(2), fork(2), execvp(3), getopt(3),
			 * isatty(3), write(2), read(2) */


#ifdef WITH_CURSES

/* sidestep some -pedantic compiler warnings when -ansi */
# ifdef __STRICT_ANSI__
#  define NCURSES_ENABLE_STDBOOL_H 0
# endif

# ifdef WITH_CURSES_WIDE
#  include <wchar.h>	/* mbsrtowcs(3), wmemchr(3) */
#  define WADDNSTR waddnwstr
#  define MEMCHR wmemchr
typedef wchar_t curs_char_T;
# else
#  define WADDNSTR waddnstr
#  define MEMCHR memchr
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

#ifndef O_NONBLOCK
# ifdef O_NDELAY
#  define O_NONBLOCK O_NDELAY
# else
#  error
# endif
#endif

/* Take advantage of features where available */
#ifndef __GNUC__
# if __STDC_VERSION__ >= 199901L
#  define __inline__ inline
#  define __restrict__ restrict
# else
#  define __inline__
#  define __restrict__
# endif
#endif

#if __STDC_VERSION__ >= 201100L
# include <stdnoreturn.h>
#elif defined(__DMC__) || defined(__WATCOMC__) /* My condolences */
# define noreturn __declspec(noreturn)
#else
# define noreturn __attribute__((__noreturn__))
/* Beware using noreturn in functions with other __attribute__s */
#endif

union target {
	int fd;
	FILE * fp;
#ifdef WITH_CURSES
	WINDOW * w;
#endif
};

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
sprint_time(char *__restrict__ buf)
/* buf must be TIMESTAMP_SIZE
 *
 * This uses gettimeofday(2) for compatibility with old systems, cause
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

static __inline__ int __attribute__((nonnull))
mkprefix(const unsigned char flags, const int fd, char *__restrict__ prefixbuf)
/* Based on flags and fd, writes a prefix to prefixbuf that should prefix
 * each buffalo in buffalo, eg. `[21:34:56.135429]&1 '. Returns the length
 * of the string written to prefixbuf, not including any terminating NUL if
 * there is one, WHICH THERE MAY NOT BE. Do NOT rely on the string written
 * to prefixbuf being NUL-terminated!
 *
 * Fair warning: this function is hyper-optimised
 *
 * If you're wondering about the gratituous use of fwrite(3) where fputs(3)
 * might have been clearer, that's cause fputs_unlocked(3) is not so widely
 * available (see configure.sh)
 *
 * The goto is to ensure that (flags & FLAG_PREFIX) is only tested once
 * Think of it like
 *	if (flags & FLAG_TIMESTAMPS && flags & FLAG_PREFIX)
 *		...
 *	else if (flags & FLAG_TIMESTAMPS)
 *		...
 *	else if (flags & FLAG_PREFIX)
 *		...
 * but marginally less mank
 *
 * TODO: a small thing, but we don't need to keep rechecking flags */
{
	int i = 0;

	if (flags & FLAG_TIMESTAMPS) {
		sprint_time(prefixbuf);
		if (flags & FLAG_PREFIX) {
			i += TIMESTAMP_SIZE - 2;
			/* Overwrite trailing ^ space and NUL */
			goto prefix;
		} else
			/* Don't include trailing NUL in return value */
			return TIMESTAMP_SIZE - 1;
	}

	if (flags & FLAG_PREFIX)
prefix:		prefixbuf[i++] = '&', prefixbuf[i++] = fd + '0', /* Assumes that
		* fd < 10; if it isn't, then we're ^^^^^^^^ into punctuation */
		prefixbuf[i] = ' ';

	return i;
}

/* TODO: the two prepend_lines functions need to be merged properly */

static __inline__ void __attribute__((nonnull))
prepend_lines(	FILE *__restrict__ outstream,
		const char *__restrict__ prefixstr,
		const int prefixn,
		const char * unprinted __attribute__((nonstring)),
		/* can't be^__restrict__ed because it's aliased in the function
		 * body by newline_ptr */
		size_t n_unprinted)
{
	const char * newline_ptr = unprinted;

	/* Look for a newline anywhere but the last char */
	while ((newline_ptr = memchr(unprinted, '\n', n_unprinted - 1))) {
		fwrite(prefixstr, 1, prefixn, outstream);
		newline_ptr++;
		n_unprinted -= fwrite(unprinted,
				1, newline_ptr - unprinted, outstream);
		unprinted = newline_ptr;
	}

	/* Once any embedded newlines have been exhausted, print the rest */
	fwrite(prefixstr, 1, prefixn, outstream);
	fwrite(unprinted, 1, n_unprinted, outstream);
}

#ifdef WITH_CURSES
static int /* int for waddnstr(3X) compatibility */ __attribute__((nonnull))
prepend_lines_curses(WINDOW *__restrict__ w, const curs_char_T * unprinted,
		int /*waddnstr(3X) again*/ n_unprinted)
{
	const curs_char_T * newline_ptr = unprinted;
	char prefixstr[TIMESTAMP_SIZE];

	sprint_time(prefixstr);

	/* Look for a newline anywhere but the last char */
	while ((newline_ptr = MEMCHR(unprinted, '\n', n_unprinted - 1))) {
		/* I want to use lots of sizeof here but apparently C's
		 * type system handles that just fine */
		const size_t n_printed = ++newline_ptr - unprinted;
		/* note ++preincrement   ^^ here */
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
	bool a(const int ifd, const union target output_target, const unsigned char flags)
/* Returns whether ifd is worth listening to anymore (ie. hasn't hit EOF).
 * Also, a whole C++ compiler just for type polymorphism? Bitch */

static __inline__
CAT_IN_TECHNICOLOUR(cat_in_technicolour_timestamps)
/* This one outputs to FILE* streams
 * FIXME: even with unlocked_stdio(3) this is a touch too slow
 * Replaced stdio with straight kernel calls, which allowed replacing
 * `goto's with `return's and avoiding fflush(3): I had one good moment but
 * looks like that was a fluke, so that can't be the
 * problem. curse_in_technicolour doesn't seem to be as bad as this one. So
 * what's the difference? */
{
	ssize_t nread;
	char prefixstr[TIMESTAMP_SIZE + 3] __attribute__((nonstring)),
		buf[BUFSIZ] __attribute__((nonstring));
	bool ret = true;

	/* While this does mean that flags must be rechecked every time this
	 * is called, I tried the other way and believe it or not it was
	 * even worse. Also, for compatibility with curse_in_technicolour */
	const char *__restrict__ colour =
		(flags & FLAG_COLOUR)
			? (output_target.fp == stdout ? "\033[32m" : "\033[31m")
			: NULL;
	FILE * outstream = (flags & FLAG_ALLINONE) ? stdout : output_target.fp;
	const int prefixn = mkprefix(flags, fileno(output_target.fp), prefixstr);

	do {
		nread = read(ifd, buf, BUFSIZ);
		switch (nread) {
		case -1:
			if (errno == EAGAIN)
				goto end;
			else
				err(-1, "read(2)");

		case 0:	close(ifd); ret = false; goto end;

		default:
			if (colour) {
				fwrite(colour, 1, 5, outstream);
				colour = NULL; /* No need to keep writing colour */
			}
			prepend_lines(outstream, prefixstr, prefixn, buf, nread);
		}
	} while (nread == BUFSIZ);

end:	fflush(outstream);
	return ret;
}

static __inline__
CAT_IN_TECHNICOLOUR(cat_in_technicolour) /* buffalo buffalo */
/* This one outputs to unix file descriptors */
{
	/* const everything */
	const int ofd = (flags & FLAG_ALLINONE) ? STDOUT_FILENO : output_target.fd;
	const char *__restrict__ colour =
		(flags & FLAG_COLOUR)
			? (output_target.fd == STDOUT_FILENO ? "\033[32m" : "\033[31m")
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
		nread += prefixn * (nread > 0);
		goto test_nread; /* Jump into the loop after the read,
		* having done the special-case first read; subsequent
		* iterations of the loop will do the regular first read */
	}

	do {
		nread = read(ifd, buf, BUFSIZ);
test_nread:	switch (nread) {
		case -1:
			if (errno == EAGAIN)
				return true;
			else
				err(-1, "read(2)");

		case 0:	close(ifd); return false;
		default:write(ofd, buf, nread);
		}
	} while (nread == BUFSIZ);

	return true;
}

#ifdef WITH_CURSES
static __inline__
CAT_IN_TECHNICOLOUR(curse_in_technicolour)
/* This one outputs to WINDOW* objects */
{
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
		nread = read(ifd, buf, sizeof buf
# ifdef WITH_CURSES_WIDE
			- 1 /* for NUL */
# endif
		);
		switch (nread) {
		case -1:
			if (errno == EAGAIN)
				return true;
			else
				err(-1, "read(2)");
		case 0:	close(ifd); return false;
		default:
# ifdef WITH_CURSES_WIDE
			buf[nread] = '\0'; /* Hence BUFSIZ - 1 above */
			{
				const char * ptr = buf;
				wchar_t wbuf[BUFSIZ];
				const size_t fet =
					mbsrtowcs(wbuf, &ptr, BUFSIZ, NULL);
				if (fet == (size_t)-1) err(-1, "input");
				out_(output_target.w, wbuf,fet/* wobuffet! */);
			}
# else
			out_(output_target.w, buf, nread);
# endif
			wnoutrefresh(output_target.w);
		}
	} while (nread == BUFSIZ);

	return true;
}

static void __attribute__((nonnull))
set_up_curses_window(WINDOW *__restrict__ w, const short colour_pair)
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

static __inline__ void __attribute__((nonnull))
set_up_curses(WINDOW *__restrict__ *__restrict__ window)
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

static __inline__ void
parent_listen(const int child_out, const int child_err,
		const unsigned char flags)
{
	/* If -t|-p, points to a more compicated function that uses stdio;
	 * else points to a slimmer one using only unix io */
	CAT_IN_TECHNICOLOUR((*cat_in_technicolour_));

	/* whether the respective stream is still worth watching -- a bit
	 * array of the file descriptors OR'd together. Clever, hey? No */
	unsigned char watch = STDOUT_FILENO | STDERR_FILENO;

	/* ugly stuff for C ```polymorphism''' */
	union target out_target, err_target;

	/* Beware: cute preprocessor shit */
#ifdef WITH_CURSES
	WINDOW *__restrict__ w[2];
	if (flags & FLAG_CURSES) {
		set_up_curses(w);
		cat_in_technicolour_ = curse_in_technicolour,
		out_target.w = w[0], err_target.w = w[1];
	} else
#endif
	if (flags & (FLAG_TIMESTAMPS | FLAG_PREFIX))
		cat_in_technicolour_ = cat_in_technicolour_timestamps,
		out_target.fp = stdout, err_target.fp = stderr;
	else
		cat_in_technicolour_ = cat_in_technicolour,
		out_target.fd = STDOUT_FILENO, err_target.fd = STDERR_FILENO;

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

static __inline__ int __attribute__((nonnull))
parent_wait_for_child(const char *__restrict__ child, const unsigned char flags)
/* Clean up after child (common parenting experience). Returns $? */
{
	int child_ret;
	char timebuf[TIMESTAMP_SIZE] = ""; /* zero-init */

	wait(&child_ret);

	if (flags & FLAG_TIMESTAMPS && !(flags & FLAG_QUIET))
		sprint_time(timebuf);

	if (WIFEXITED(child_ret)) {
		const int ret = WEXITSTATUS(child_ret);
		if (flags & FLAG_VERBOSE
		    || (!(flags & FLAG_QUIET) && ret != EXIT_SUCCESS))
		{
			if (*timebuf) fputs(timebuf, stderr);
			warnx("%s exited with status %d", child, ret);
		}
		return ret;
	} else {
		if (!(flags & FLAG_QUIET)) {
			if (*timebuf) fputs(timebuf, stderr);
			if (WIFSIGNALED(child_ret)) {
				const int sig = WTERMSIG(child_ret);
#ifdef HAVE_STRSIGNAL
				warnx("%s killed by signal %d: %s",
					child, sig, strsignal(sig));
#else
				warnx("%s killed by signal %d", child, sig);
#endif
			} else
				warn("%s wait(2) status unexpected: %d. errno says",
					child, child_ret);
		}
		return child_ret; /* *Not* sig -- more conventional */
	}
}

noreturn static void
usage(const char *__restrict__ progname)
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
	puts("ssss version 0.2, built " __DATE__
#ifdef WITH_CURSES
		", with the -S extension with"
# ifndef WITH_CURSES_WIDE
		"out"
# endif
		" UTF-8 support"
#endif
		"\n" SPIEL);
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
handle_bad_prog(int sig __attribute__((unused))
#ifdef SA_SIGINFO
		, siginfo_t * si, void * ucontext __attribute__((unused))
#endif
		)
/* signal handler, set up by setup_handle_bad_prog, to see if the child
 * process fails to exec and signals this back to us -- if it does, it will
 * exit with the value of errno after execvp(3) failed, and if possible,
 * queues a pointer to the name of the program */
{
	int child_ret;
	const pid_t piddle = wait(&child_ret);
	if (WIFEXITED(child_ret)) {
		errno = WEXITSTATUS(child_ret);
#ifdef SA_SIGINFO
		err(-1, "%s", (char*)si->si_ptr);
#else
		err(-1, "can't exec command");
#endif
	} else
		err(-1, "unexpected error in child process, pid %d",
			piddle);
}

static __inline__ void
setup_handle_bad_prog(void)
{
	struct sigaction handle_bad_prog_onsig;
#ifdef SA_SIGINFO
	handle_bad_prog_onsig.sa_sigaction = handle_bad_prog,
	handle_bad_prog_onsig.sa_flags = SA_RESETHAND | SA_SIGINFO;
#else
	handle_bad_prog_onsig.sa_handler = handle_bad_prog,
	handle_bad_prog_onsig.sa_flags = SA_RESETHAND;
#endif
	sigemptyset(&handle_bad_prog_onsig.sa_mask);
	if (sigaction(SIGUSR1, &handle_bad_prog_onsig, NULL))
		warn("sigaction(2)");
}

static __inline__ void __attribute__((nonnull))
child_prepare(const char *__restrict__ cmd, const unsigned char flags,
		int *__restrict__ child_stdout,
		int *__restrict__ child_stderr)
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
		warnx("starting %s", cmd); /* TODO: ripoffline(3X)? */
		fflush(stderr);
	}

	dup2(child_stderr[1], STDERR_FILENO);
}

static __inline__ void __attribute__((nonnull))
parent_prepare(const unsigned char flags,
		int *__restrict__ child_stdout,
		int *__restrict__ child_stderr)
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
	int child_stdout[2], child_stderr[2];
	const unsigned char flags = process_cmdline(argc, argv);

	setlocale(LC_ALL, "");
	/* curses in particular wants this, but also just good practice
	 * FIXME: should come before the call to process_cmdline */

	pipe(child_stdout);
	pipe(child_stderr);

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
		/* If we're here, exec(3) failed; run to parent and tell */
		{
			const int er = errno;
#ifdef SA_SIGINFO
			union sigval progname;
			progname.sival_ptr = argv[optind];
			sigqueue(getppid(), SIGUSR1, progname);
#else
			kill(getppid(), SIGUSR1);
#endif
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

/*
 * WIP WIP WIP, that's some Work In Progress
 * Local Variables:
 * indent-tabs-mode: t
 * c-basic-offset: 8
 * c-offsets-alist: ((label . --) (case-label . 0) (statement-case-intro . +) (substatement . +) (statement-block-intro . +) (statement-cont +) (defun-block-intro . +) (arglist-cont-nonempty . ++) (c . 1) (cpp-macro . --))
 * c-block-comment-prefix: "*"
 * End:
 */
