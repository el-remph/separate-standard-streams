<!-- -*- fill-column: 75; tab-width: 8; -*- -->

ssss (*s*eparate *s*tandard *s*tream*s*)
========================================

This is to visually separate stdout and stderr by printing each with its own
colour, or if colour isn't possible, then by prefixing each line with the
file descriptor whence it came (1 for stdout, 2 for stderr). Running

	$ ssss PROG

is a bit like

	$ PROG > >(sed $'s/^/\e[32m/') 2> >(sed $'s/^/\e[31m/')

in GNU bash, and likewise

	$ ssss -Cp PROG

is like

	$ PROG > >(sed 's/^/\&1 /') 2> >(sed 's/^/\&2 /')

but hopefully less mank.

It sticks to low-level Unix functions, and is written in ANSI C; mostly
requires only POSIX 1993 (with a couple widely available exceptions
described in the source) and could hypothetically probably maybe compile on
4.3BSD, although I haven't tested that and wouldn't want to. It should
almost certainly compile and run fine on any Unix-like system (except maybe
some pure SysV derivatives like HP-UX or IBM AIX, if that's still around).
The build `WITH_CURSES` is tested on GNU ncurses and netbsd-curses, although
it's pretty sketchy on the latter


Building
--------

To build, run `make`. If applicable, this will automatically run
configure.sh to generate config.h (don't worry, it's not an autoconf
script). `ssss` depends only on ANSI C and a POSIX-conformant standard C
library; it should build straight away on any modern unix-like system

There is an extra, WIP feature, printing stdout and stderr side-by-side,
which requires curses, and can be enabled with `make WITH_CURSES=1`. This
also requires libcursesw (or just libcurses if you're using netbsd-curses)
and C95 for Unicode support; in the unlikely event that you are missing
either of those, Unicode support can be disabled with
`make WITH_CURSES=1 WITHOUT_UNICODE=1`.

For usage, run it with `-h`. For more information about system
compatibility and dependencies, see comments in the source (near the top)
and `configure.sh`, and if you really need that then godspeed to you.

Copyright
---------

Copyright &copy; 2023-2024 The Remph. This is free software: file-specific
licence information is contained in SPDX headers at the top of each file,
and the full GPL text is in the file [GPL](./GPL), or at
<https://gnu.org/licenses/gpl.txt>.

BUGS
----

-	Small caveat to ANSI C (ie. C89) conformance: the help message is a
	bit longer than the minimum required string length of an ANSI C
	compiler (509 bytes), but if you're using a very strictly ANSI
	C-compliant compiler with a hard 509-byte string cutoff, you probably
	have bigger problems
-	If the child process (PROG in -h output) exits quickly, its output
	may be flushed to us all at once, which means the output of each
	stream at the time of flushing will be printed each in one block
	per stream rather than in the order it was written. For example
	(with possible *not certain* outputs; can be influenced by timing
	and kernel caching for instance):

		$ ssss -p sh -c 'echo foo; echo bar >&2; echo yeedleyeedleyee'
		&2 bar
		&1 foo
		&1 yeedleyeedleyee
		$ rlwrap -AS $'\e[36m> ' ssss -p sh
		> echo foo; echo bar >&2; echo yeedleyeedleyee
		&1 foo
		&2 bar
		&1 yeedleyeedleyee

	As of commit `f33db16e` this behaviour is fairly sporadic.

-	The curses thing is a WIP:
	-	doesn't use scrollback buffer, so if output goes off screen
		it's gone
	-	on netbsd-curses it still murderises output when the program
		ends
	-	The non-Unicode version can't handle input with embedded NUL
		chars. This is a pretty minor issue

TODO
----

Give up the ghost on curses scrollback (intractable) and make it print in
simple columns instead, with gaps in each column where only the other has
output. This could quite possibly be implemented without curses, using
COLUMNS and LINES in the input where available, though this would be unable
to handle SIGWINCH -- maybe we could make the column-printing thing
configurable at compile-time whether or not it would use curses
