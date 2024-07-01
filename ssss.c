/* SPDX-FileCopyrightText:  2023-2024 The Remph <lhr@disroot.org>
 * SPDX-License-Identifier: GPL-3.0-or-later
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
 ** The curses thing doesn't use the scrollback buffer
 *** I keep thinking that something like filter(3X) is the solution, then
 *   backing out
 *** Pads may be the way to go, but they'll need to be manually wrapped and
 *   grown
 *** GNU `less' and Linux `more' both do it by outputting the actual data
 *   to stdout (occasionally stderr) and separately sending formatting
 *   codes to the terminal with terminfo; this makes more sense when you're
 *   transparently piping contiguous data to the terminal, as from a file
 *   or pipeline; for formatted data as our split columns, we'll have to
 *   keep a close eye on screen size and basically reimplement half of
 *   curses
 *** One way or another, I think this must need the cursor to backtrack
 *   more than one line
 **** Does this imply that in order to work with the scrollback, the cursor
 *    will have to backtrack into the scrollback and edit there? (obviously
 *    this is impossible)
 ** waddnstr(3X) can't deal with embedded NUL chars, but waddch(3X) syncs so
 *  we can't just call that iteratively. waddnstr(3X) also doesn't tell us
 *  where it left off (such as eg. strchrnul(3)) so we'd have to make a
 *  second pass over the string with memchr(3) to find NUL, and restart
 *  from there, until memchr(3) returned NULL; or, make one big pass over
 *  the string with memchr(3) iteratively, replacing NULs (with what?),
 *  then call waddnstr(3X). That minimises calls to waddnstr(3X) but it's
 *  still one pass too many. Could make an option like -0 to `not break
 *  when input containing embedded NULs is passed,' but what the fuck is
 *  the use case for that?
 *** Looks like this is provisionally fixed for widechar curses, but not
 *   for pure ASCII curses. This may end up being a wontfix */

#if 0
__attribute__((__noreturn__, nonnull, __access__(write_only, 1, 2)))
static __inline__ void
foo(const size_t n,
	const char *__restrict__ const bar __attribute__((nonstring)));
/* ^ might have gone a bit heavy on that stuff, is what I'm saying */
#endif

/* feature_test_macros(7):
 ** _XOPEN_SOURCE>=500 needed for SA_NODEFER, though if the system really
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
#include <assert.h>
#include <errno.h>
#include <locale.h>	/* setlocale(3) */
#include <stdio.h>
#include <stdlib.h>	/* atexit(3) */
#include <string.h>	/* memcpy(3), memchr(3), strsignal(3) */

/* POSIX */
#include <err.h>	/* Not actually POSIX but should be */
#include <fcntl.h>	/* Actually fcntl(2), funnily enough */
#include <signal.h>	/* sigaction(2), kill(2) */
#include <sys/time.h>	/* gettimeofday(2); select(2) on old systems */
#include <sys/types.h>	/* ssize_t, wait(2), write(2), select(2)... */
#include <sys/wait.h>	/* wait(2), dumbass */
#include <time.h>	/* localtime(3), strftime(3) */
#include <unistd.h>	/* pipe(2), dup2(2), fork(2), execvp(3), write(2),
			 * read(2) */

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>	/* old systems require different includes, which
			 * here are included anyway */
#endif

#ifdef WITH_CURSES

/* sidestep some -pedantic compiler warnings when -ansi */
# if __STDC_VERSION__ < 199900L && defined __STRICT_ANSI__
#  define NCURSES_ENABLE_STDBOOL_H 0
# endif

# include <curses.h>
# include <term.h> /* putp(3X) and *_ca_mode used in set_up_curses */

# ifdef WITH_CURSES_WIDE
#  include <wchar.h>	/* wmemchr(3) */
#  include "wread.h"
#  define WADDNSTR waddnwstr
#  define MEMCHR wmemchr
#  define READ(fd, buf) wread(fd, buf)
typedef wchar_t curs_char_T;
# else
#  define WADDNSTR waddnstr
#  define MEMCHR memchr
#  define READ(fd, buf) read(fd, buf, BUFSIZ)
typedef char curs_char_T;
# endif /* with wide curses */

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
# include "compat/bool.h"

#endif /* with curses */

#include "process_cmdline.h"
/* These must always be the last <#include>s */
#include "compat/inline-restrict.h"
#include "compat/__attribute__.h"

/* In case some macros aren't defined in those headers, on very old systems */
#ifndef SA_NODEFER
#define SA_NODEFER 0 /* ): */
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

union target {
	int fd;
	FILE * fp;
#ifdef WITH_CURSES
	WINDOW * w;
#endif
};

#define TIMESTAMP_SIZE (sizeof "[00:00:00.000000] ")

static void __attribute__((nonnull, __access__(write_only, 1)))
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

static __inline__ size_t __attribute__((nonnull, __access__(write_only, 3)))
mkprefix(const unsigned char flags, const int fd, char prefixbuf[TIMESTAMP_SIZE + 3])
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
	size_t i = 0;

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
prefix:		prefixbuf[i++] = '&', prefixbuf[i++] = fd + '0', /* Assumes
		* that fd < 10; if it isn't, then we're into punctuation */
		prefixbuf[i++] = ' ';

	return i;
}

/* TODO: the two prepend_lines functions need to be merged properly */

static __inline__ void __attribute__((nonnull, __access__(read_only, 2, 3)))
prepend_lines (
	FILE *__restrict__ const outstream,
	const char *__restrict__ unprinted __attribute__((nonstring)),
	/* I think  ^ this is probably maybe quite possibly OK */
	size_t n_unprinted,
	const unsigned char flags
) {
	const char *__restrict__ newline_ptr __attribute__((nonstring));
	/* This one ^ also */
	char prefixstr[TIMESTAMP_SIZE + 3] __attribute__((nonstring));
	const size_t prefixn = mkprefix(flags, fileno(outstream), prefixstr);
	/* Calls gettimeofday(2), ^ so must be called *after* read(2),
	 * else it delays read(2) too long and fucks up the timing */

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
/* int for waddnstr(3X) compatibility */
static int __attribute__((nonnull, __access__(read_only, 2, 3)))
prepend_lines_curses(WINDOW *__restrict__ const w, const curs_char_T * unprinted,
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

static
CAT_IN_TECHNICOLOUR(cat_in_technicolour_timestamps)
/* This one outputs to FILE* streams */
{
	ssize_t nread;
	char buf[BUFSIZ] __attribute__((nonstring));
	bool ret = true;

	/* While this does mean that flags must be rechecked every time this
	 * is called, I tried the other way and believe it or not it was
	 * even worse. Also, for compatibility with curse_in_technicolour */
	const char *__restrict__ colour =
		(flags & FLAG_COLOUR)
			? (output_target.fp == stdout ? "\033[32m" : "\033[31m")
			: NULL;
	FILE *const outstream = (flags & FLAG_ALLINONE) ? stdout : output_target.fp;

	assert(output_target.fp == stdout || output_target.fp == stderr);

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
			prepend_lines(outstream, buf, nread, flags);
		}
	} while (nread == BUFSIZ);

end:	fflush(outstream);
	return ret;
}

static
CAT_IN_TECHNICOLOUR(cat_in_technicolour) /* buffalo buffalo */
/* This one outputs to unix file descriptors */
{
	/* const everything */
	const int ofd = (flags & FLAG_ALLINONE) ? STDOUT_FILENO : output_target.fd;
	const char *__restrict__ const colour =
		(flags & FLAG_COLOUR)
			? (output_target.fd == STDOUT_FILENO ? "\033[32m" : "\033[31m")
			: "";

	/* actual variables we'll be operating on, we need for io */
	char buf[BUFSIZ] __attribute__((nonstring));
	ssize_t nread;

	assert(output_target.fd == STDOUT_FILENO || output_target.fd == STDERR_FILENO);

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
static
CAT_IN_TECHNICOLOUR(curse_in_technicolour)
/* This one outputs to WINDOW* objects */
{
	ssize_t nread;
	int (*const out_)(WINDOW*, const curs_char_T*, int) =
		(flags & FLAG_TIMESTAMPS) ? prepend_lines_curses : WADDNSTR;

	/* The following is to shutup the gcc */
# ifndef WITH_CURSES_WIDE
	__attribute__((nonstring))
# endif
	curs_char_T buf[BUFSIZ];

	do {
		nread = READ(ifd, buf);
		switch (nread) {
		case -1:
			if (errno == EAGAIN)
				return true;
			else
				err(-1, "read(2)");
		case 0:
			close(ifd);
			return false;
		default:
			out_(output_target.w, buf, nread);
			wnoutrefresh(output_target.w);
		}
	} while (nread == BUFSIZ);

	return true;
}

static void __attribute__((nonnull))
set_up_curses_window(WINDOW *__restrict__ const w, const short colour_pair)
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
set_up_curses(WINDOW *__restrict__ window[2])
{
	/* just a cast to shut up the compiler */
	atexit((void (*)(void))endwin);

	initscr();
	/* Try and keep the output on the terminal (not use the alternate
	 * screen). This hack is probably copyright Thomas E. Dickey and
	 * the FSF, although I take full responsibility for omitting what
	 * looked like quite important defensive code cause I cba. I'm sure
	 * in time I'll be even sorrier for that */
	refresh();	/* is this really necessary? I think Dickey might
			 * have done it to flush things like cbreak(3X) */
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
	/* whether the respective stream is still worth watching -- a bit
	 * array of the file descriptors OR'd together. Clever, hey? No */
	int watch = STDOUT_FILENO | STDERR_FILENO;

	/* ugly stuff for C ```polymorphism''' */
	union target out_target, err_target;

	/* If -t|-p, points to a more compicated function that uses stdio;
	 * else points to a slimmer one using only unix io */
	CAT_IN_TECHNICOLOUR((*const cat_in_technicolour_)) =
#ifdef WITH_CURSES
		flags & FLAG_CURSES ? curse_in_technicolour :
#endif
		flags & (FLAG_TIMESTAMPS | FLAG_PREFIX)
			? cat_in_technicolour_timestamps
			: cat_in_technicolour;

	/* Beware: cute preprocessor shit. Also, these tests are the same
	 * as those above but for C variable declaration reasons they are
	 * tricky to merge, so: Mr. Compiler, optimise these please and
	 * thank you */
#ifdef WITH_CURSES
	WINDOW *__restrict__ w[2];
	if (cat_in_technicolour_ == curse_in_technicolour) {
		set_up_curses(w);
		out_target.w = w[0], err_target.w = w[1];
	} else
#endif
	if (cat_in_technicolour_ == cat_in_technicolour_timestamps)
		out_target.fp = stdout, err_target.fp = stderr;
	else {
		assert(cat_in_technicolour_ == cat_in_technicolour);
		out_target.fd = STDOUT_FILENO, err_target.fd = STDERR_FILENO;
	}

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
parent_wait_for_child(const char *__restrict__ const child, const unsigned char flags)
/* Clean up after child (common parenting experience). Returns $? */
{
	int child_ret;
	char timebuf[TIMESTAMP_SIZE] = ""; /* zero-init */

	wait(&child_ret);

	if (flags & FLAG_TIMESTAMPS && ~flags & FLAG_QUIET)
		sprint_time(timebuf);

	if (WIFEXITED(child_ret)) {
		const int ret = WEXITSTATUS(child_ret);
		if (flags & FLAG_VERBOSE
		    || (~flags & FLAG_QUIET && ret != EXIT_SUCCESS))
		{
			if (*timebuf)
				fputs(timebuf, stderr);
			warnx("%s exited with status %d", child, ret);
		}
		return ret;
	} else {
		if (~flags & FLAG_QUIET) {
			if (*timebuf)
				fputs(timebuf, stderr);

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

static void
clean_up_colour()
/* May be called with either (void) by atexit(3) or (int) by sigaction(2).
 * Warning: this function has state (static) */
{
	static bool already_done = false;
	if (!already_done) {
		write(STDOUT_FILENO, "\033[m", 3);
		already_done = true;
	}
}

static void __attribute__((cold))
handle_bad_prog(int sig __attribute__((unused))
#ifdef SA_SIGINFO
		, siginfo_t *const si, void * ucontext __attribute__((unused))
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
	handle_bad_prog_onsig.sa_flags = SA_NODEFER | SA_SIGINFO;
#else
	handle_bad_prog_onsig.sa_handler = handle_bad_prog,
	handle_bad_prog_onsig.sa_flags = SA_NODEFER;
#endif
	sigemptyset(&handle_bad_prog_onsig.sa_mask);
	if (sigaction(SIGUSR1, &handle_bad_prog_onsig, NULL))
		warn("sigaction(2)");
}

static __inline__ void __attribute__((nonnull))
child_prepare(const char *__restrict__ const cmd, const unsigned char flags,
		const int child_stdout[2], const int child_stderr[2])
/* tried
	const int (*__restrict__ const child_stdout)[2]
 * and all that, to get those pointers restricted, but it violates ANSI/ISO
 * and more importantly doesn't seem to make any difference to the assembly
 * even with -Og -fstrict-aliasing */
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
			warnx("%sstarting %s", buf, cmd);
		} else
			warnx("starting %s", cmd); /* TODO: ripoffline(3X)? */
		fflush(stderr);
	}

	dup2(child_stderr[1], STDERR_FILENO);
}

static __inline__ void __attribute__((nonnull))
parent_prepare(const unsigned char flags,
		const int child_stdout[2], const int child_stderr[2])
{
	/* Inverse of the child process' close(2) calls */
	close(child_stdout[1]);
	close(child_stderr[1]);

	/* set pipes to nonblocking so that if we get more than BUFSIZ
	 * bytes at once we can use read(2) to check if the pipe is empty
	 * or not, in cat_in_technicolour etc. */
	fcntl(child_stdout[0], F_SETFL, O_NONBLOCK);
	fcntl(child_stderr[0], F_SETFL, O_NONBLOCK);

	/* Last point before colour may be output; take the opportunity to
	 * register clean_up_colour if necessary */
	if (flags & FLAG_COLOUR) {
		struct sigaction clean_up_colour_onsig;
		clean_up_colour_onsig.sa_handler = (void (*)(int))clean_up_colour,
		clean_up_colour_onsig.sa_flags = SA_NODEFER;
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
		~flags & FLAG_CURSES &&
#endif
		(flags & (FLAG_TIMESTAMPS | FLAG_PREFIX)))
	{
		setvbuf(stdout, NULL, _IOFBF, 0);
		if (~flags & FLAG_ALLINONE)
			setvbuf(stderr, NULL, _IOFBF, 0);
	}
}

int
main(const int argc, char *const *const argv)
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
