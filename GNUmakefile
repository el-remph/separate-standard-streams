# Configurable options: DEBUG OPTIMISATION CSTANDARD CWARNINGS, as well as
# the usual CC CPPFLAGS CFLAGS LDFLAGS. So:
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
#	tcc -g -c ssss.c
#	gcc ssss.o -o ssss
#
# Sometimes pcc needs GNU cpp also. Don't forget to pass whatever -std you're
# using to both:
#	# Preprocess (.c -> .i):
#	cpp -std=c99 ssss.c > ssss.i
#	# Compile and assemble (.i -> .s -> .o)
#	pcc -std=c99 -O1 -c ssss.i
#	# Link
#	gcc -s ssss.o -o ssss
#	# Or mayhaps:
#	cpp -std=c99 ssss.c | pcc -std=c99 -O1 -c -o - - | gcc -s -x c -o ssss -


CWARNINGS ?=-Wall -Wextra -Wno-implicit-fallthrough -Wno-overlength-strings -Winline
# I would really rather not have to use -Wno-implicit-fallthrough here, but
# I can't get -Wimplicit-fallthrough=n to work

OBJS = ssss.o process_cmdline.o timestamp.o column-in-technicolour.o

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

# The former by design, the latter by coincidence
$(OBJS): config.h compat/__attribute__.h

# Basically every object except ssss.o
column-in-technicolour.o process_cmdline.o timestamp.o: %.o: %.h

ssss.o process_cmdline.o: compat/bool.h
ssss.o column-in-technicolour.o process_cmdline.o: compat/inline-restrict.h compat/unlocked-stdio.h
column-in-technicolour.o: compat/ckdint.h

config.h compat/unlocked-stdio.h &: configure.sh
	./$<

clean:
	@rm -fv ssss $(OBJS) config.h compat/unlocked-stdio.h ssss.1
