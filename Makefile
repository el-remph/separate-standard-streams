# make WITH_CURSES=1 to enable -S (bit shit, you have been warned)
#
# Configurable options: WITH_CURSES WITHOUT_UNICODE DEBUG OPTIMISATION
# CSTANDARD CWARNINGS, as well as the usual CC CPPFLAGS CFLAGS LDFLAGS. So:
# 	$ make OPTIMISATION='-Ofast -march=native -mtune=native' CSTANDARD=-std=gnu89 CWARNINGS=-War
#	cc -pipe -fwhole-program -Ofast -march=native -mtune=native -std=gnu89 -War ssss.c -o ssss
# 	$ make CFLAGS='-Ofast -march=native -mtune=native -std=gnu89 -War'
#	cc -Ofast -march=native -mtune=native -std=gnu89 -War ssss.c -o ssss
#
# This compiles with pcc and tcc, but seems like they haven't got the hang
# of ld(1). Them and me both lol. gcc and clang can both link the object
# code produced by pcc, although that rather misses the point:
#
#	pcc -O1 -c ssss.c
#	gcc -s ssss.o -o ssss
#	strip ssss
#
# Or for a debug build:
#
#	tcc -DWITH_CURSES -g -c ssss.c
#	gcc ssss.o -lcursesw -ltinfo -o ssss
#
# Sometimes pcc needs GNU cpp also. Don't forget to pass whatever -std you're
# using to both:
#	# Preprocess (.c -> .i):
#	cpp -std=c99 -DWITH_CURSES ssss.c > ssss.i
#	# Compile and assemble (.i -> .s -> .o)
#	pcc -std=c99 -O1 -c ssss.i
#	# Link
#	gcc -lcurses -s ssss.o -o ssss


CWARNINGS ?=-Wall -Wextra -Wno-implicit-fallthrough -Wno-overlength-strings -Winline
# I would really rather not have to use -Wno-implicit-fallthrough here, but
# I can't get -Wimplicit-fallthrough=n to work

OBJS = ssss.o process_cmdline.o

ifdef WITH_CURSES
        CPPFLAGS += -DWITH_CURSES
    ifdef WITHOUT_UNICODE
        WREAD_H =
        LDLIBS ?=-lcurses -ltinfo
    else
        WREAD_H = wread.h
        OBJS += wread.o
        CPPFLAGS += -DWITH_CURSES_WIDE
        LDLIBS ?=-lcursesw -ltinfo
    endif
endif

ifdef DEBUG
    # a dev build
    CSTANDARD    ?=-ansi -pedantic
    OPTIMISATION ?=-Og -ggdb3 -fstrict-aliasing -Wstrict-aliasing=1
    LDFLAGS      ?= -flto
else
    # Proper build -- don't define CSTANDARD (unless the user does on
    # the command line), let the compiler use everything it's got
    OPTIMISATION ?=-O2 -march=native -mtune=native
    CPPFLAGS     ?= -DNDEBUG
    LDFLAGS      ?= -flto -s
endif

CFLAGS?=-pipe $(OPTIMISATION) $(CSTANDARD) $(CWARNINGS)


.PHONY = all clean

all: ssss
ssss: $(OBJS)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@
ifndef DEBUG
	strip $@
endif

ssss.o: ssss.c compat/__attribute__.h compat/bool.h config.h $(WREAD_H)
process_cmdline.o: process_cmdline.c process_cmdline.h compat/__attribute__.h compat/bool.h config.h
wread.o: wread.c wread.h compat/__attribute__.h config.h

config.h: configure.sh
	./$<>$@ || { ret=$$?; rm -f $@; exit $$ret; }
# ^ This removes config.h if configuration fails, to avoid a false positive
# with a subsequent invocation of make, which could continue with a malformed
# config.h

clean:
	@rm -fv ssss *.o config.h
