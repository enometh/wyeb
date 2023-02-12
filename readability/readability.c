#include "readability.h"
// copied from /7/gtk/epiphany/embed/ephy-reader-handler.c
/* -*- Mode: C; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 *  Copyright © Red Hat Inc.
 *
 *  Epiphany is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Epiphany is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Epiphany.  If not, see <http://www.gnu.org/licenses/>.
 */

static char *
ephy_encode_for_html_entity (const char *input)
{
  GString *str = g_string_new (input);

  g_string_replace (str, "&", "&amp;", 0);
  g_string_replace (str, "<", "&lt;", 0);
  g_string_replace (str, ">", "&gt;", 0);
  g_string_replace (str, "\"", "&quot;", 0);
  g_string_replace (str, "'", "&#x27;", 0);
  g_string_replace (str, "/", "&#x2F;", 0);

  return g_string_free (str, FALSE);
}

static char *
readability_get_property_string (WebKitJavascriptResult *js_result,
                                 char                   *property)
{
  JSCValue *jsc_value;
  char *result = NULL;

  jsc_value = webkit_javascript_result_get_js_value (js_result);

  if (!jsc_value_is_object (jsc_value))
    return NULL;

  if (jsc_value_object_has_property (jsc_value, property)) {
    g_autoptr (JSCValue) jsc_content = jsc_value_object_get_property (jsc_value, property);

    result = jsc_value_to_string (jsc_content);

    if (result && strcmp (result, "null") == 0)
      g_clear_pointer (&result, g_free);
  }

  return result;
}

static void
readability_js_finish_cb (GObject      *object,
                          GAsyncResult *result,
                          gpointer      user_data)
{
  WebKitWebView *web_view = WEBKIT_WEB_VIEW (object);
  Win *win = user_data;
  g_autoptr (WebKitJavascriptResult) js_result = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree gchar *byline = NULL;
  g_autofree gchar *encoded_byline = NULL;
  g_autofree gchar *content = NULL;
  g_autofree gchar *encoded_title = NULL;
  g_autoptr (GString) html = NULL;
  const gchar *title;
  const gchar *font_style;

  js_result = webkit_web_view_run_javascript_finish (web_view, result, &error);
  if (!js_result) {
    if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
      g_warning ("Error running javascript: %s", error->message);
    return;
  }

  byline = readability_get_property_string (js_result, "byline");
  content = readability_get_property_string (js_result, "content");
  title = webkit_web_view_get_title (web_view);

  encoded_byline = byline ? ephy_encode_for_html_entity (byline) : g_strdup ("");
  encoded_title = ephy_encode_for_html_entity (title);

  html = g_string_new (NULL);
  g_string_append_printf (html, "<style>%s</style>"
                          "<title>%s</title>"
                          "<meta http-equiv='Content-Type' content='text/html;' charset='UTF-8'>" \
                          "<meta http-equiv='Content-Security-Policy' content=\"script-src 'none'\">" \
                          "<body>"
                          "<article>"
                          "<h2>"
                          "%s"
                          "</h2>"
                          "<i>"
                          "%s"
                          "</i>"
                          "<hr>",
			  CSS_READER,
                          encoded_title,
                          encoded_title,
                          encoded_byline);

  /* We cannot encode the page content because it contains HTML tags inserted by
   * Readability.js. Upstream recommends that we use an XSS sanitizer like
   * DOMPurify plus Content-Security-Policy, but I'm not keen on adding more
   * bundled JS dependencies, and we have an advantage over Firefox in that we
   * don't need scripts to work at this point. So instead the above CSP
   * completely blocks all scripts, which should hopefully obviate the need for
   * a DOM purifier.
   *
   * Note the encoding for page title and byline is still required, as they're
   * not supposed to contain markup, and Readability.js unescapes them before
   * returning them to us.
   */
  g_string_append (html, content);
  g_string_append (html, "</article>");
  g_string_append (html, "</body>");

  webkit_web_view_load_html(win->kit, html->str, URI(win));
}


void
readability_mode(Win *win)
{
  WebKitSettings *s = webkit_web_view_get_settings(win->kit);
  gboolean orig = webkit_settings_get_enable_javascript(s);
  if (!orig)
    webkit_settings_set_enable_javascript(s, TRUE);
  webkit_web_view_run_javascript (win->kit,  JS_READABILITY, NULL, readability_js_finish_cb, win);
  if (!orig)
    webkit_settings_set_enable_javascript(s, FALSE);
}
