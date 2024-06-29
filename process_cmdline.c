#include "config.h" /* Must be before any other includes or test macros */

#include  <stdio.h> /* puts(3), printf(3), fprintf(3) */
#include <stdlib.h> /* exit(3) */
#include <string.h> /* strcmp(3) */
#include <unistd.h> /* isatty(3), getopt(3) */

#include "process_cmdline.h"
#include "compat/bool.h"
#include "compat/inline-restrict.h"
#include "compat/__attribute__.h"

#if __STDC_VERSION__ >= 202000L
/* C23 deprecates stdnoreturn.h and _Noreturn after just two revisions in
 * favour of newly introduced [[attributes]]. Also introduces the deprecated
 * [[_Noreturn]] -- yes, introduced deprecated: born dead */
# define noreturn [[__noreturn__]]
#elif __STDC_VERSION__ >= 201100L
# define noreturn _Noreturn
#elif defined(__DMC__) || defined(__WATCOMC__) || defined(__MSC_VER__)
# define noreturn __declspec(noreturn) /* My condolences */
#else
# define noreturn __attribute__((__noreturn__))	/* compat/__attribute__.h
						   already included */
#endif

noreturn static void __attribute__((cold))
usage(const char *__restrict__ const progname)
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

noreturn static void __attribute__((cold))
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
		"\n\
ssss -- split standard streams: highlight the stdout and stderr of a process\n\
Copyright 2023 the Remph\n\n\
This is free software; permission is given to copy and/or distribute it,\n\
with or without modification, under the terms of the GNU General Public\n\
Licence, version 3 or later. For more information, see the GNU GPL, found\n\
distributed with this in the file `GPL', and at https://gnu.org/licenses/gpl");

	exit(EXIT_SUCCESS);
}

static void __attribute__((cold))
longopt_help_version(char *const *const argv)
{
	const char *const longname = argv[1] + 1 + !!(argv[1][1] == '-');
	if (strcmp(longname, "help") == 0)
		usage(argv[0]);
	if (strcmp(longname, "version") == 0)
		version();
}

extern char ** environ;

static bool
do_colour(const unsigned char flags)
{
	size_t i = 0;

	for (; environ[i]; i++) {
		/* this tests for /^NO_COLOU?R=?$/ */
		if (strncmp(environ[i], "NO_COLO", 7) == 0) {
			const char * e = environ[i];

			if (*e == 'U')
				e++;

			if (*e == 'R')
				e++;
			else
				continue;

			switch (*e) {
			case '=':	return !*++e;
			case '\0':	return false;
			}
		}
	}

	return	isatty(STDOUT_FILENO)
		? flags & FLAG_ALLINONE
			? true
			: isatty(STDERR_FILENO)
		: false;
}

extern unsigned char
process_cmdline(const int argc, char *const *const argv)
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
	if (argv[1] && argv[1][0] == '-')
		longopt_help_version(argv);

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
				default:
					fprintf(stderr, "%s: invalid: -A %c", *argv, *optarg);
					exit(-1);
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

#ifndef __GLIBC__
		case '+':
			/* Provide an error message for -+ on non-GNU
			 * systems. It'll fail notwithstanding, just
			 * don't want it to fail silently */
			fprintf(stderr, "%s: invalid option -- +\n", argv[0]);
			/*@fallthrough@*/
#endif
		default:	exit(-1);
		}
	}

	if (argc - optind == 0) {
		fprintf(stderr, "%s: not enough arguments\n", argv[0]);
		exit(-1);
	}

	if ((flags & FLAG_QUIET) && (flags & FLAG_VERBOSE)) {
		fprintf(stderr, "%s: Can't specify both -q and -v\n",
			argv[0]); /* ^ Because I say so */
		exit(-1);
	}

#ifdef WITH_CURSES
	if (flags & FLAG_CURSES) {
		/* quarter-arsed attempt to warn the user for options that
		 * conflict with -S */
		static const char optwarning[] =
			"%s: -%c is ignored when -S is specified\n";

		if (flags & FLAG_ALLINONE)
			fprintf(stderr, optwarning, *argv, '1');

		switch (prefix) {
		case AUTO:	break; /* no worries */
		case OFF:	fprintf(stderr, optwarning, *argv, 'P'); break;
		case ON:	fprintf(stderr, optwarning, *argv, 'p'); break;
		}

		switch (colour) {
		case AUTO:	break; /* no worries */
		case OFF:	fprintf(stderr, optwarning, *argv, 'C'); break;
		case ON:	fprintf(stderr, optwarning, *argv, 'c'); break;
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
		if (do_colour(flags))
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

	return flags;
}

