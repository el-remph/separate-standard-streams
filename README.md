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


Building
--------

To build, run `make`. If applicable, this will automatically run
configure.sh to generate config.h (don't worry, it's not an autoconf
script). `ssss` depends only on ANSI C and a POSIX-conformant standard C
library; it should build straight away on any modern unix-like system.

`make all` or `make doc` builds a manual page -- this requires
[GNU help2man](https://www.gnu.org/s/help2man), a perl script, available at
<https://ftpmirror.gnu.org/help2man> and on most package managers.

The `-S` switch requires wide-character support from C95. There is currently
no way to disable this, as I've personally never even seen a pre-C95
toolchain, but it should be fairly trivial to implement if for some reason
it becomes necessary (famous last words).

For usage, run it with `-h` or `--help`, or see the generated ssss.1
manpage if available. For more information about system compatibility and
dependencies, see comments in the source (near the top) and `configure.sh`,
and if you really need that then godspeed to you.


Copyright
---------

Copyright &copy; 2023-2024 The Remph <lhr@disroot.org>. This is free
software: file-specific licence information is contained in SPDX headers
at the top of each file, and the full GPL text is in the file [GPL](GPL),
or at <https://gnu.org/licenses/gpl.txt>.


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
