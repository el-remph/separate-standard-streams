# make WITH_CURSES=1 to enable -S (bit shit, you have been warned)
#
# Configurable options: WITH_CURSES WITHOUT_UNICODE DEBUG ASM OPTIMISATION
# CSTANDARD CWARNINGS, as well as the usual CC CPPFLAGS CFLAGS LDFLAGS. So:
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
# Sometimes it needs GNU cpp also. Don't forget to pass whatever -std you're
# using to both:
#	# Preprocess (.c -> .i):
#	cpp -std=c99 -DWITH_CURSES -DDEBUG_MACROS ssss.c > ssss.i
#	# Compile and assemble (.i -> .s -> .o)
#	pcc -std=c99 -O1 -c ssss.i
#	# Link
#	gcc -lcurses -s ssss.o -o ssss


CWARNINGS ?=-Wall -Wextra -Wno-implicit-fallthrough -Wno-overlength-strings -Wno-cpp
# I would really rather not have to use -Wno-implicit-fallthrough here, but
# I can't get -Wimplicit-fallthrough=n to work
#
# -Wno-cpp is for glibc's features.h bloody whining about bloody _BSD_SOURCE

ifdef WITH_CURSES
	CPPFLAGS += -DWITH_CURSES
    ifdef WITHOUT_UNICODE
	LDFLAGS  += -lcurses
    else
	CSTANDARD?=-std=c99
	CPPFLAGS += -DWITH_CURSES_WIDE
	LDFLAGS  += -lcursesw
    endif
endif

ifdef DEBUG
	# a dev build
	CPPFLAGS     += -DDEBUG_MACROS
	CWARNINGS    += -Winline
	CSTANDARD    ?=-ansi -pedantic
	OPTIMISATION ?=-Og -ggdb3
else
	# Proper build -- don't define CSTANDARD (unless the user does on
	# the command line), let the compiler use everything it's got
	OPTIMISATION ?=-O2 -march=native -mtune=native
	LDFLAGS      += -s
endif

CFLAGS?=-pipe $(OPTIMISATION) $(CSTANDARD) $(CWARNINGS)


.PHONY = all clean

all: ssss

# `make ASM=1' leaves assembly droppings, so you can diff ssss.s against
# its younger counterpart and see if it's lost any weight. Also cause I like
# the filename ssss.s

ssss: ssss.c compat__attribute__.h
ifdef ASM
	$(CC) $(CPPFLAGS) $(CFLAGS) -S $<
	$(CC) $(LDFLAGS) $@.s -o $@
else
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $< -o $@
endif
ifndef DEBUG
	strip $@
endif

clean:
	@rm -fv ssss ssss.s
