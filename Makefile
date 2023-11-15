# make WITH_CURSES=1 to enable -S (bit shit, you have been warned)
#
# Configurable options: WITH_CURSES DEBUG OPTIMISATION CSTANDARD CWARNINGS,
# as well as the usual CC CPPFLAGS CFLAGS LDFLAGS. So:
# 	$ make OPTIMISATION='-Ofast -march=native -mtune=native' CSTANDARD=-std=gnu89 CWARNINGS=-War
#	cc -pipe -fwhole-program -Ofast -march=native -mtune=native -std=gnu89 -War ssss.c -o ssss
# 	$ make CFLAGS='-Ofast -march=native -mtune=native -std=gnu89 -War'
#	cc -Ofast -march=native -mtune=native -std=gnu89 -War ssss.c -o ssss
#
# This compiles with pcc, but I haven't been able to link it with pcc on
# GNU. gcc and clang can both link the object code produced by pcc, although
# that rather misses the point:
#
#	pcc -O1 -c ssss.c
#	gcc -s ssss.o -o ssss
#	strip ssss
#
# Or for a debug build:
#
#	pcc -O0 -g -c ssss.c
#	gcc ssss.o -o ssss
#
# Sometimes it needs GNU cpp also, with -ansi, at least with glibc, which
# seems to disagree with pcc's idea of C99. Don't forget to pass -ansi to
# cpp also:
#	# Preprocess (.c -> .i):
#	cpp -ansi -DWITH_CURSES ssss.c > ssss.i
#	# Compile and assemble (.i -> .s -> .o)
#	pcc -ansi -O1 -c ssss.i
#	# Link
#	gcc -lcurses -s ssss.o -o ssss


CWARNINGS ?=-Wall -Wextra -Wno-implicit-fallthrough -Wno-overlength-strings
# I would really rather not have to use -Wno-implicit-fallthrough here, but
# I can't get -Wimplicit-fallthrough=n to work

ifdef DEBUG
	# a dev build
	CPPFLAGS     ?=-DDEBUG_MACROS
	CWARNINGS    += -Winline
	CSTANDARD    ?=-ansi -pedantic
	OPTIMISATION ?=-Og -ggdb3
else
	# Proper build -- don't define CSTANDARD (unless the user does on
	# the command line), let the compiler use everything it's got
	OPTIMISATION?=-O2 -march=native -mtune=native
	LDFLAGS?=-s
endif

ifdef WITH_CURSES
	CPPFLAGS += -DWITH_CURSES
	LDFLAGS  += -lcurses
endif

CFLAGS?=-pipe -fwhole-program $(OPTIMISATION) $(CSTANDARD) $(CWARNINGS)


.PHONY = all clean

all: ssss

# `make DEBUG=1' leaves assembly droppings, so you can diff ssss.s against
# its younger counterpart and see if it's lost any weight. Also cause I like
# the filename ssss.s

ssss: ssss.c
ifdef DEBUG
	$(CC) $(CPPFLAGS) $(CFLAGS) -S ssss.c
	$(CC) $(LDFLAGS) ssss.s -o ssss
else
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) ssss.c -o ssss
	strip ssss
endif

clean:
	@rm -fv ssss ssss.s
