/*
  gcc soup-uri-normalize.c $(pkg-config --cflags glib-2.0) -c
*/
#include <glib.h>

#if SOUP_MAJOR_VERSION == 2
#error needed only for libsoup3
#endif

#define XDIGIT(c) ((c) <= '9' ? (c) - '0' : ((c) & 0x4F) - 'A' + 10)
#define HEXCHAR(s) ((XDIGIT (s[1]) << 4) + XDIGIT (s[2]))

const char soup_char_attributes[];
#define SOUP_CHAR_URI_PERCENT_ENCODED 0x01
#define SOUP_CHAR_URI_GEN_DELIMS      0x02
#define SOUP_CHAR_URI_SUB_DELIMS      0x04
#define SOUP_CHAR_HTTP_SEPARATOR      0x08
#define SOUP_CHAR_HTTP_CTL            0x10

#define soup_char_is_uri_percent_encoded(ch) (soup_char_attributes[(guchar)ch] & SOUP_CHAR_URI_PERCENT_ENCODED)
#define soup_char_is_uri_gen_delims(ch)      (soup_char_attributes[(guchar)ch] & SOUP_CHAR_URI_GEN_DELIMS)
#define soup_char_is_uri_sub_delims(ch)      (soup_char_attributes[(guchar)ch] & SOUP_CHAR_URI_SUB_DELIMS)
#define soup_char_is_uri_unreserved(ch)      (!(soup_char_attributes[(guchar)ch] & (SOUP_CHAR_URI_PERCENT_ENCODED | SOUP_CHAR_URI_GEN_DELIMS | SOUP_CHAR_URI_SUB_DELIMS)))
#define soup_char_is_token(ch)               (!(soup_char_attributes[(guchar)ch] & (SOUP_CHAR_HTTP_SEPARATOR | SOUP_CHAR_HTTP_CTL)))

/* 00 URI_UNRESERVED
 * 01 URI_PCT_ENCODED
 * 02 URI_GEN_DELIMS
 * 04 URI_SUB_DELIMS
 * 08 HTTP_SEPARATOR
 * 10 HTTP_CTL
 */
const char soup_char_attributes[] = {
	/* 0x00 - 0x07 */
	0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
	/* 0x08 - 0x0f */
	0x11, 0x19, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
	/* 0x10 - 0x17 */
	0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
	/* 0x18 - 0x1f */
	0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11,
	/*  !"#$%&' */
	0x09, 0x04, 0x09, 0x02, 0x04, 0x01, 0x04, 0x04,
	/* ()*+,-./ */
	0x0c, 0x0c, 0x04, 0x04, 0x0c, 0x00, 0x00, 0x0a,
	/* 01234567 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* 89:;<=>? */
	0x00, 0x00, 0x0a, 0x0c, 0x09, 0x0a, 0x09, 0x0a,
	/* @ABCDEFG */
	0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* HIJKLMNO */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* PQRSTUVW */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* XYZ[\]^_ */
	0x00, 0x00, 0x00, 0x0a, 0x09, 0x0a, 0x01, 0x00,
	/* `abcdefg */
	0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* hijklmno */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* pqrstuvw */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* xyz{|}~  */
	0x00, 0x00, 0x00, 0x09, 0x01, 0x09, 0x00, 0x11,
	/* 0x80 - 0xFF */
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
	0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01
};


/* length must be set (e.g. from strchr()) such that [part, part + length]
 * contains no nul bytes */
static char *
uri_normalized_copy (const char *part, int length,
		     const char *unescape_extra)
{
	unsigned char *s, *d, c;
	char *normalized = g_strndup (part, length);
	gboolean need_fixup = FALSE;

	if (!unescape_extra)
		unescape_extra = "";

	s = d = (unsigned char *)normalized;
	while (*s) {
		if (*s == '%') {
			if (s[1] == '\0' ||
			    s[2] == '\0' ||
			    !g_ascii_isxdigit (s[1]) ||
			    !g_ascii_isxdigit (s[2])) {
				*d++ = *s++;
				continue;
			}

			c = HEXCHAR (s);
			if (soup_char_is_uri_unreserved (c) ||
			    (c && strchr (unescape_extra, c))) {
				*d++ = c;
				s += 3;
			} else {
				/* We leave it unchanged. We used to uppercase percent-encoded
				 * triplets but we do not do it any more as RFC3986 Section 6.2.2.1
				 * says that they only SHOULD be case normalized.
				 */
				*d++ = *s++;
				*d++ = *s++;
				*d++ = *s++;
			}
		} else {
			if (!g_ascii_isgraph (*s) &&
			    !strchr (unescape_extra, *s))
				need_fixup = TRUE;
			*d++ = *s++;
		}
	}
	*d = '\0';

	if (need_fixup) {
		GString *fixed;

		fixed = g_string_new (NULL);
		s = (guchar *)normalized;
		while (*s) {
			if (g_ascii_isgraph (*s) ||
			    strchr (unescape_extra, *s))
				g_string_append_c (fixed, *s);
			else
				g_string_append_printf (fixed, "%%%02X", (int)*s);
			s++;
		}
		g_free (normalized);
		normalized = g_string_free (fixed, FALSE);
	}

	return normalized;
}

/**
 * soup_uri_normalize:
 * @part: a URI part
 * @unescape_extra: (allow-none): reserved characters to unescape (or %NULL)
 *
 * %<!-- -->-decodes any "unreserved" characters (or characters in
 * @unescape_extra) in @part, and %<!-- -->-encodes any non-ASCII
 * characters, spaces, and non-printing characters in @part.
 *
 * "Unreserved" characters are those that are not allowed to be used
 * for punctuation according to the URI spec. For example, letters are
 * unreserved, so soup_uri_normalize() will turn
 * <literal>http://example.com/foo/b%<!-- -->61r</literal> into
 * <literal>http://example.com/foo/bar</literal>, which is guaranteed
 * to mean the same thing. However, "/" is "reserved", so
 * <literal>http://example.com/foo%<!-- -->2Fbar</literal> would not
 * be changed, because it might mean something different to the
 * server.
 *
 * In the past, this would return %NULL if @part contained invalid
 * percent-encoding, but now it just ignores the problem (as
 * soup_uri_new() already did).
 *
 * Return value: the normalized URI part
 */
char *
soup_uri_normalize (const char *part, const char *unescape_extra)
{
	g_return_val_if_fail (part != NULL, NULL);

	return uri_normalized_copy (part, strlen (part), unescape_extra);
}

