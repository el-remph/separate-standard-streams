#ifndef PROCESS_CMDLINE_H
#define PROCESS_CMDLINE_H

#include "compat/__attribute__.h"

/* Flag constants -- used to be macros, but it's useful to have them typed
 * just in case */
#if __STDC_VERSION__ >= 202300L
enum : unsigned char {
#else
static const unsigned char
#endif /* C23 */
	FLAG_ALLINONE   = 1 << 0,
	FLAG_TIMESTAMPS = 1 << 1,
	FLAG_COLOUR     = 1 << 2,
	FLAG_PREFIX     = 1 << 3,
	FLAG_VERBOSE    = 1 << 4,
	FLAG_QUIET      = 1 << 5
#ifdef WITH_CURSES
	,FLAG_CURSES    = 1 << 6
#endif /* with curses */
#if __STDC_VERSION__ >= 202300L
}
#endif /* C23 */
       ;

extern unsigned char process_cmdline(const int argc, char *const * argv) __attribute__((leaf));

#endif /* process_cmdline.h */

