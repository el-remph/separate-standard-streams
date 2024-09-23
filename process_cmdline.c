/* SPDX-FileCopyrightText:  2023-2024 The Remph <lhr@disroot.org>
   SPDX-License-Identifier: GPL-3.0-or-later */
#include "config.h" /* Must be before any other includes or test macros */

#include <assert.h>
#include  <stdio.h> /* puts(3), printf(3), fprintf(3) */
#include <stdlib.h> /* exit(3) */
#include <string.h> /* strcmp(3) */
#include <unistd.h> /* isatty(3), optind */

#include "compat/bool.h"
#include "compat/inline-restrict.h"

/* horrible horrible hackery */
#define restrict __restrict__ /* __restrict__ already defined (maybe) by compat/inline-restrict.h */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wlong-long"
#include "dryopt/dryopt.h"
#pragma GCC diagnostic pop

#include "process_cmdline.h"
#include "compat/unlocked-stdio.h"
#include "compat/__attribute__.h"

static size_t __attribute__((cold))
version(struct dryopt const * opt __attribute__((unused)), char const * arg __attribute__((unused)))
{
	puts("ssss version " SSSS_VERSION ", DRYopt branch\n\n\
Copyright 2023-2024 the Remph\n\
This is free software; permission is given to copy and/or distribute it,\n\
with or without modification, under the terms of the GNU General Public\n\
Licence, version 3 or later. For more information, see the GNU GPL, found\n\
distributed with this in the file `GPL', and at <https://gnu.org/licenses/gpl>");
	exit(EXIT_SUCCESS);
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

enum threeway_switch { OFF = 0, ON = 1, AUTO };

struct optA_callback_data {
	char const *__restrict__ progname;
	enum threeway_switch *__restrict__ colour, *__restrict__ prefix;
};

static size_t
optA_callback(struct dryopt const *__restrict__ const opt, char const *__restrict__ const arg)
{
	struct optA_callback_data const *const dat = opt->assign_val.p;
	size_t i;
	for (i = 0; arg[i]; i++)
		switch (arg[i]) {
		case 'C': case 'c': *dat->colour = AUTO; break;
		case 'P': case 'p': *dat->prefix = AUTO; break;
		default: return i;
		}
	return i;
}

#define ARGPTR(x) sizeof(x), &(x)

extern unsigned char
process_cmdline(const int argc, char *const *const argv)
{
	/* Static memory here is really just to facilitate initialising opt[],
	   since these variables' addresses will now be known ahead of time */
	static unsigned char flags = 0;
	static enum threeway_switch colour = AUTO, prefix = AUTO;

	struct dryopt opt[] = {
		{ L'1', "allinone", "Output everything to one stream, stdout. Equivalent of piping through |& in bash",
			UNSIGNED, NO_ARG, DRYARG_OR, ARGPTR(flags), {{FLAG_ALLINONE}} },
		{ L'2', NULL, "Output PROG's stdout->stdout and stderr->stderr (default; aka --no-allinone)",
			UNSIGNED, NO_ARG, DRYARG_AND, ARGPTR(flags), {{(unsigned char)~FLAG_ALLINONE}} },
			/* {{FLAG_ALLINONE ^ UCHAR_MAX}} would also work */
		{ L'A', "auto", "Set any applicable option characters in ARG (/(?i)[cp]/) to auto-detect their values (ie. default settings)",
			CALLBACK, REQ_ARG, 0, 0, optA_callback, {{0}} /*init below*/ },
		{ L'c', "colour", "Colour output (default: if output isatty(3))",
			UNSIGNED, NO_ARG, 0, ARGPTR(colour), {{ON}} },
		{ L'C', NULL, "Turn off -c (aka --no-colour)",
			UNSIGNED, NO_ARG, 0, ARGPTR(colour), {{OFF}} },
		{ L'p', "prefix", "Prefix lines with the fd whence they came (default: if output isn't coloured)",
			UNSIGNED, NO_ARG, 0, ARGPTR(prefix), {{ON}} },
		{ L'P', NULL, "Turn off -p (aka --no-prefix)",
			UNSIGNED, NO_ARG, 0, ARGPTR(prefix), {{OFF}} },
		{ L'S', "columns", "Print streams side-by-side, (bit of a WIP). Note that -[12Pp] are (mostly) silently ignored if this flag is passed. Note also that $COLUMNS is respected if ssss can't get window size from the terminal",
			UNSIGNED, NO_ARG, DRYARG_OR, ARGPTR(flags), {{FLAG_COLUMNS}} },
		{ L't', "timestamp",	NULL, UNSIGNED, 0, DRYARG_OR, ARGPTR(flags), {{FLAG_TIMESTAMPS}} },
		{ L'q', "quiet",	NULL, UNSIGNED, 0, DRYARG_OR, ARGPTR(flags), {{FLAG_QUIET}} },
		{ L'v', "verbose",	NULL, UNSIGNED, 0, DRYARG_OR, ARGPTR(flags), {{FLAG_VERBOSE}} },
		{ L'V', "version",	"Print version info and exit", CALLBACK, 0,0,0, version, {{0}} }
	};

	/* No question of static memory here though :( */
	struct optA_callback_data datA = { NULL, &colour, &prefix };
	datA.progname = *argv;
	opt[2].assign_val.p = &datA;
	assert(opt[2].shortopt == L'A');
	assert(!flags);

	DRYopt_help_args = "PROG [PROGARGS]",
	DRYopt_help_extra = "\
Runs PROG with PROGARG(s) if any, and marks which of the output is stdout\n\
and which is stderr. Returns PROG's exit status\n\n\
Options:";

	optind = DRYOPT_PARSE(argv, opt);
	if (argc - optind == 0) {
		fprintf(stderr, "%s: not enough arguments\n", argv[0]);
		exit(-1);
	}

	if ((flags & FLAG_QUIET) && (flags & FLAG_VERBOSE)) {
		fprintf(stderr, "%s: Can't specify both -q and -v\n",
			argv[0]); /* ^ Because I say so */
		exit(-1);
	}

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

	if (flags & FLAG_COLUMNS) {
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
	} else {
		switch (prefix) {
		case AUTO:
			if (flags & FLAG_COLOUR)
		case OFF:	flags &= ~FLAG_PREFIX;
			else
		case ON:	flags |= FLAG_PREFIX;
		}
	}

	return flags;
}
