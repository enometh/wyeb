#include <glib.h>

#if SOUP_MAJOR_VERSION == 3
#error needed only for libsoup2
#endif

typedef enum {
	SOUP_URI_NONE,
	SOUP_URI_SCHEME,
	SOUP_URI_USER,
	SOUP_URI_PASSWORD,
	SOUP_URI_AUTH_PARAMS,
	SOUP_URI_HOST,
	SOUP_URI_PORT,
	SOUP_URI_PATH,
	SOUP_URI_QUERY,
	SOUP_URI_FRAGMENT
} SoupURIComponent;


/**
 * soup_uri_copy: (skip)
 * @uri: the #GUri to copy
 * @first_component: first #SoupURIComponent to update
 * @...: value of @first_component  followed by additional
 *    components and values, terminated by %SOUP_URI_NONE
 *
 * Return a copy of @uri with the given components updated.
 *
 * Returns: (transfer full): a new #GUri
 */
GUri *
soup_uri_copy3 (GUri            *uri,
               SoupURIComponent first_component,
               ...)
{
        va_list args;
        SoupURIComponent component = first_component;
        gpointer values[SOUP_URI_FRAGMENT + 1];
        gboolean values_to_set[SOUP_URI_FRAGMENT + 1];
        GUriFlags flags = g_uri_get_flags (uri);

        g_return_val_if_fail (uri != NULL, NULL);

        memset (&values_to_set, 0, sizeof (values_to_set));

        va_start (args, first_component);
        while (component != SOUP_URI_NONE) {
                if (component == SOUP_URI_PORT)
                        values[component] = GINT_TO_POINTER (va_arg (args, glong));
                else
                        values[component] = va_arg (args, gpointer);
                values_to_set[component] = TRUE;
                component = va_arg (args, SoupURIComponent);
        }
        va_end (args);

        if (values_to_set[SOUP_URI_PASSWORD])
                flags |= G_URI_FLAGS_HAS_PASSWORD;
        if (values_to_set[SOUP_URI_AUTH_PARAMS])
                flags |= G_URI_FLAGS_HAS_AUTH_PARAMS;
        if (values_to_set[SOUP_URI_PATH])
                flags |= G_URI_FLAGS_ENCODED_PATH;
        if (values_to_set[SOUP_URI_QUERY])
                flags |= G_URI_FLAGS_ENCODED_QUERY;
        if (values_to_set[SOUP_URI_FRAGMENT])
                flags |= G_URI_FLAGS_ENCODED_FRAGMENT;
        return g_uri_build_with_user (
                flags,
                values_to_set[SOUP_URI_SCHEME] ? values[SOUP_URI_SCHEME] : g_uri_get_scheme (uri),
                values_to_set[SOUP_URI_USER] ? values[SOUP_URI_USER] : g_uri_get_user (uri),
                values_to_set[SOUP_URI_PASSWORD] ? values[SOUP_URI_PASSWORD] : g_uri_get_password (uri),
                values_to_set[SOUP_URI_AUTH_PARAMS] ? values[SOUP_URI_AUTH_PARAMS] : g_uri_get_auth_params (uri),
                values_to_set[SOUP_URI_HOST] ? values[SOUP_URI_HOST] : g_uri_get_host (uri),
                values_to_set[SOUP_URI_PORT] ? GPOINTER_TO_INT (values[SOUP_URI_PORT]) : g_uri_get_port (uri),
                values_to_set[SOUP_URI_PATH] ? values[SOUP_URI_PATH] : g_uri_get_path (uri),
                values_to_set[SOUP_URI_QUERY] ? values[SOUP_URI_QUERY] : g_uri_get_query (uri),
                values_to_set[SOUP_URI_FRAGMENT] ? values[SOUP_URI_FRAGMENT] : g_uri_get_fragment (uri)
        );
}

static inline gboolean
parts_equal (const char *one, const char *two, gboolean insensitive)
{
	if (!one && !two)
		return TRUE;
	if (!one || !two)
		return FALSE;
	return insensitive ? !g_ascii_strcasecmp (one, two) : !strcmp (one, two);
}

static inline gboolean
path_equal (const char *one, const char *two)
{
        if (one[0] == '\0')
                one = "/";
        if (two[0] == '\0')
                two = "/";

	return !strcmp (one, two);
}

static gboolean
flags_equal (GUriFlags flags1, GUriFlags flags2)
{
        /* We only care about flags that affect the contents which these do */
        static const GUriFlags normalization_flags = (G_URI_FLAGS_ENCODED | G_URI_FLAGS_ENCODED_FRAGMENT |
                                                      G_URI_FLAGS_ENCODED_PATH | G_URI_FLAGS_ENCODED_QUERY |
                                                      G_URI_FLAGS_SCHEME_NORMALIZE);

        return (flags1 & normalization_flags) == (flags2 & normalization_flags);
}


/**
 * soup_uri_equal:
 * @uri1: a #GUri
 * @uri2: another #GUri
 *
 * Tests whether or not @uri1 and @uri2 are equal in all parts.
 *
 * Returns: %TRUE if equal otherwise %FALSE
 **/
gboolean
soup_uri_equal3 (GUri *uri1, GUri *uri2)
{
        g_returan_val_if_fail (uri1 != NULL, FALSE);
        g_return_val_if_fail (uri2 != NULL, FALSE);

                if (!flags_equal (g_uri_get_flags (uri1), g_uri_get_flags (uri2))                  ||
            g_strcmp0 (g_uri_get_scheme (uri1), g_uri_get_scheme (uri2))                   ||
            g_uri_get_port (uri1) != g_uri_get_port (uri2)                                 ||
            !parts_equal (g_uri_get_user (uri1), g_uri_get_user (uri2), FALSE)             ||
            !parts_equal (g_uri_get_password (uri1), g_uri_get_password (uri2), FALSE)     ||
            !parts_equal (g_uri_get_host (uri1), g_uri_get_host (uri2), TRUE)              ||
            !path_equal (g_uri_get_path (uri1), g_uri_get_path (uri2))                     ||
            !parts_equal (g_uri_get_query (uri1), g_uri_get_query (uri2), FALSE)           ||
            !parts_equal (g_uri_get_fragment (uri1), g_uri_get_fragment (uri2), FALSE)) {
                return FALSE;
            }

        return TRUE;
}
