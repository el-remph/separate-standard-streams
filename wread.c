/* Damn, this really is what wide-oriented stdio is for */

#include "config.h"

#include <assert.h>
#include <limits.h>	/* MB_LEN_MAX */
#include <locale.h>
#include <stdio.h>	/* BUFSIZ */
#include <stdlib.h>	/* abort(3); MB_CUR_MAX ifndef NDEBUG */
#include <string.h>	/* memcpy(3), memset(3), strncmp(3) */
#include <sys/param.h>	/* BSD */
#include <unistd.h>	/* read(2), write(2) */
#include <wchar.h>	/* mbrtowc(3), wmemset(3) */

#include "wread.h"
#include "compat/inline-restrict.h"

/* total size on 64-bit GNU: 8 + 16 + (4 + 4) = 32 bytes */
struct fd_state {
	size_t trailing_bad_chars_len;
	char trailing_bad_chars[MB_LEN_MAX] __attribute__((nonstring));
	mbstate_t mbstate;
};
#define FD_STATE_INIT { 0, "", { 0 } }

static __inline__ struct fd_state * __attribute__((returns_nonnull))
assign_fd_state(const int fd)
/* !!static state!! !!static state!!
 * stdio uses (on GNU) an eight-kilobyte (two-page) heap buffer per stream.
 * Since I know I'm only going to need two buffers of MB_LEN_MAX bytes
 * each, I can ```optimise''' a bit. The result is unfortunately quite
 * ugly, so I've sequestered most of the ugliness away into this function
 * to prevent it causing panic should it be exposed in the wider world,
 * like Quasimodo */
{
	static struct fd_state state_a = FD_STATE_INIT, state_b = FD_STATE_INIT;
	/* file descriptor associated with buffer, kept in here to keep it
	 * private from other functions (we're also reimplementing C++
	 * here as well as wide stdio) and to make fd_state look nice and
	 * pretty and 64-bit aligned for absolutely no reason */
	static int fd_a = -1, fd_b = -1;

	if (fd == fd_a)
		return &state_a;

	if (fd == fd_b)
		return &state_b;

	if (fd_a == -1) {
		fd_a = fd;
		return &state_a;
	}

	if (fd_b == -1) {
		fd_b = fd;
		return &state_b;
	}

	/* else dump core */
	abort();
}

static __inline__ ssize_t __attribute__((__access__(write_only, 2)))
unget_badchars_then_read (
	const int fd,
	char dest[BUFSIZ] __attribute__((nonstring)),
	struct fd_state *__restrict__ const state
) {
	ssize_t ret;
	size_t nchars_todo = BUFSIZ;

	if (state->trailing_bad_chars_len) {
		memcpy(dest, state->trailing_bad_chars, state->trailing_bad_chars_len);
		dest += state->trailing_bad_chars_len,
		nchars_todo -= state->trailing_bad_chars_len;
	}

	ret = read(fd, dest, nchars_todo);

	if (state->trailing_bad_chars_len) {
		assert(state->trailing_bad_chars_len <= MB_CUR_MAX);

		if (ret == -1)
			/* if EOF but we have bytes yet to process, don't
			 * subtract 1 from those bytes */
			ret = state->trailing_bad_chars_len;
		else
			ret += state->trailing_bad_chars_len;

		/* The trailings have been used and should not be reused */
		state->trailing_bad_chars_len = 0;
	}

	return ret;
}

#if __STDC_VERSION__ >= 199900L
#define UNICODE_UNKNOWN L'\ufffd' /* more technically correct and robust */
#else
#define UNICODE_UNKNOWN L'\xfffd' /* works for UTF-16 and UTF-32, which in practice
			is everything. Pretty sure it accounts for endianness */
#endif
/* FIXME: the above depends on wchar_t being Unicode, which it actually may
 * not be on FreeBSD (or MacOS, which copies FreeBSD's homework). On those,
 * it should be UTF-32 if the locale is Unicode-related, but (for example)
 * if the locale is Big5 then wchar_t is just the 8-bit characters one after
 * the other (I guess Big5 must have a maximum 4-byte variable-width limit,
 * like UTF-8 for the foreseeable future). Since wchar_t is locale-dependent
 * there, should UNKNOWN be not a macro, but a variable initialised at
 * runtime? Maybe static to wread()
 *
 * To continue with Big5, it has no U+FFFD equivalent, and iconv(1)
 * transliterates it to plain ASCII '?' */

#if !defined(__STDC_ISO_10646__)
	&& ((defined(BSD) && !defined(__OpenBSD__)) /* I think OpenBSD uses UTF-32? */
	    || defined(__sun))
# define WCHAR_UNICODE 0
#else
# define WCHAR_UNICODE 1
#endif

/*
#undef WCHAR_UNICODE
#define WCHAR_UNICODE 0
*/

#if !WCHAR_UNICODE
static __inline__ unsigned int __attribute__((pure))
locale_is_unicode(void)
/* setlocale(3) return value is system-defined -- how many systems use the
 * iconv(1) strings? */
{
	const char * locale = setlocale(LC_ALL, NULL);
	{
		const char *const dot = strchr(locale, '.');
		if (dot)
			locale = dot + 1;
	}

	if (*locale++ != 'U')
		return 0;

	switch (*locale++) {
	case 'C':	return *locale == 'S';
	case 'T':	return *locale == 'F';
	case 'N':	return strncmp(locale, "ICODE", 5) == 0;
	default:	return 0;
	}
}

static __inline__ wchar_t
unknown_marker(void)
{
	static wchar_t ret = L'\0';
	if (!ret)
		ret = locale_is_unicode() ? UNICODE_UNKNOWN : L'?';
	return ret;
}
#endif

extern ssize_t /* attributes specified in header */
wread(const int fd, wchar_t wptr[BUFSIZ])
{
	char buf[BUFSIZ] __attribute__((nonstring));
	const wchar_t *__restrict__ const og_wptr = wptr;
	/* Alright, this ^ is fine because no writes to wptr will be read
	 * from og_wptr. Actually, nothing at all will be read from og_wptr,
	 * it's never dereferenced -- kinda wish there was an attribute for
	 * that, like GCC 11's functions' __attribute__((__access__(none))) */

	const wchar_t unknown =
#if WCHAR_UNICODE
		UNICODE_UNKNOWN;
#else
		unknown_marker();
#endif

	struct fd_state *__restrict__ const state = assign_fd_state(fd);
	const ssize_t nread = unget_badchars_then_read(fd, buf, state);
	const char * ptr = buf, *const endptr = buf + nread;

	if (nread <= 0)
		return nread;

	assert(ptr < endptr);
	do {
		const size_t nchars = mbrtowc(wptr, ptr, endptr - ptr, &state->mbstate);

		switch (nchars) {
		case (size_t)-2:
			{
				const size_t n = endptr - ptr;
				assert(n + state->trailing_bad_chars_len <= MB_CUR_MAX);
				memcpy(state->trailing_bad_chars + state->trailing_bad_chars_len,
					ptr, n);
				state->trailing_bad_chars_len += n;
			}
			goto end;	/* mbrtowc(3) has access to all of buf
					 * remaining, so if it is still
					 * indecisive, it must have processed
					 * all of buf */

		case (size_t)-1: case 0:
			*wptr++ = unknown, ptr++;
			break;
		default:
			wptr++, ptr += nchars;
		}
	} while (ptr < endptr);

	if (/*nchars != (size_t)-2 &&*/ state->trailing_bad_chars_len) {
		/* A decisively valid or invalid char confirms any pending bad
		 * chars as definitely invalid */
		wmemset(wptr, unknown, state->trailing_bad_chars_len);
		wptr += state->trailing_bad_chars_len;
		state->trailing_bad_chars_len = 0;
	}

end:	return wptr - og_wptr;
}
