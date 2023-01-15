// from /webkitgtk-2.38.1/Tools/MiniBrowser/gtk/main.c to hack in
// wyeb:data wyeb:itp info pages. Also hooks in

/*
 * Copyright (C) 2006, 2007 Apple Inc.
 * Copyright (C) 2007 Alp Toker <alp@atoker.com>
 * Copyright (C) 2011 Igalia S.L.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


typedef struct {
    WebKitURISchemeRequest *request;
    GList *dataList;
    GHashTable *dataMap;
} AboutDataRequest;

static GHashTable *aboutDataRequestMap;

static void aboutDataRequestFree(AboutDataRequest *request)
{
    if (!request)
        return;

    g_list_free_full(request->dataList, g_object_unref);

    if (request->request)
        g_object_unref(request->request);
    if (request->dataMap)
        g_hash_table_destroy(request->dataMap);

    g_free(request);
}

static AboutDataRequest* aboutDataRequestNew(WebKitURISchemeRequest *uriRequest)
{
    if (!aboutDataRequestMap)
        aboutDataRequestMap = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)aboutDataRequestFree);

    AboutDataRequest *request = g_new0(AboutDataRequest, 1);
    request->request = g_object_ref(uriRequest);
    g_hash_table_insert(aboutDataRequestMap, GUINT_TO_POINTER(webkit_web_view_get_page_id(webkit_uri_scheme_request_get_web_view(request->request))), request);

    return request;
}

static AboutDataRequest *aboutDataRequestForView(guint64 pageID)
{
    return aboutDataRequestMap ? g_hash_table_lookup(aboutDataRequestMap, GUINT_TO_POINTER(pageID)) : NULL;
}

static void websiteDataRemovedCallback(WebKitWebsiteDataManager *manager, GAsyncResult *result, AboutDataRequest *dataRequest)
{
    if (webkit_website_data_manager_remove_finish(manager, result, NULL))
        webkit_web_view_reload(webkit_uri_scheme_request_get_web_view(dataRequest->request));
}

static void websiteDataClearedCallback(WebKitWebsiteDataManager *manager, GAsyncResult *result, AboutDataRequest *dataRequest)
{
    if (webkit_website_data_manager_clear_finish(manager, result, NULL))
        webkit_web_view_reload(webkit_uri_scheme_request_get_web_view(dataRequest->request));
}

static void aboutDataScriptMessageReceivedCallback(WebKitUserContentManager *userContentManager, WebKitJavascriptResult *message, WebKitWebContext *webContext)
{
    char *messageString = jsc_value_to_string(webkit_javascript_result_get_js_value(message));
    char **tokens = g_strsplit(messageString, ":", 3);
    g_free(messageString);

    unsigned tokenCount = g_strv_length(tokens);
    if (!tokens || tokenCount < 2) {
        g_strfreev(tokens);
        return;
    }

    guint64 pageID = g_ascii_strtoull(tokens[0], NULL, 10);
    AboutDataRequest *dataRequest = aboutDataRequestForView(pageID);
    if (!dataRequest) {
        g_strfreev(tokens);
        return;
    }

    WebKitWebsiteDataManager *manager = webkit_web_context_get_website_data_manager(webContext);
    guint64 types = g_ascii_strtoull(tokens[1], NULL, 10);
    if (tokenCount == 2)
        webkit_website_data_manager_clear(manager, types, 0, NULL, (GAsyncReadyCallback)websiteDataClearedCallback, dataRequest);
    else {
        guint64 domainID = g_ascii_strtoull(tokens[2], NULL, 10);
        GList *dataList = g_hash_table_lookup(dataRequest->dataMap, GUINT_TO_POINTER(types));
        WebKitWebsiteData *data = g_list_nth_data(dataList, domainID);
        if (data) {
            GList dataList = { data, NULL, NULL };
            webkit_website_data_manager_remove(manager, types, &dataList, NULL, (GAsyncReadyCallback)websiteDataRemovedCallback, dataRequest);
        }
    }
    g_strfreev(tokens);
}

static void domainListFree(GList *domains)
{
    g_list_free_full(domains, (GDestroyNotify)webkit_website_data_unref);
}

static void aboutDataFillTable(GString *result, AboutDataRequest *dataRequest, GList* dataList, const char *title, WebKitWebsiteDataTypes types, const char *dataPath, guint64 pageID)
{
    guint64 totalDataSize = 0;
    GList *domains = NULL;
    GList *l;
    for (l = dataList; l; l = g_list_next(l)) {
        WebKitWebsiteData *data = (WebKitWebsiteData *)l->data;

        if (webkit_website_data_get_types(data) & types) {
            domains = g_list_prepend(domains, webkit_website_data_ref(data));
            totalDataSize += webkit_website_data_get_size(data, types);
        }
    }
    if (!domains)
        return;

    if (!dataRequest->dataMap)
        dataRequest->dataMap = g_hash_table_new_full(g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)domainListFree);
    g_hash_table_insert(dataRequest->dataMap, GUINT_TO_POINTER(types), domains);

    if (totalDataSize) {
        char *totalDataSizeStr = g_format_size(totalDataSize);
        g_string_append_printf(result, "<h1>%s (%s)</h1>\n<table>\n", title, totalDataSizeStr);
        g_free(totalDataSizeStr);
    } else
        g_string_append_printf(result, "<h1>%s</h1>\n<table>\n", title);
    if (dataPath)
        g_string_append_printf(result, "<tr><td colspan=\"2\">Path: %s</td></tr>\n", dataPath);

    unsigned index;
    for (l = domains, index = 0; l; l = g_list_next(l), index++) {
        WebKitWebsiteData *data = (WebKitWebsiteData *)l->data;
        const char *displayName = webkit_website_data_get_name(data);
        guint64 dataSize = webkit_website_data_get_size(data, types);
        if (dataSize) {
            char *dataSizeStr = g_format_size(dataSize);
            g_string_append_printf(result, "<tr><td>%s (%s)</td>", displayName, dataSizeStr);
            g_free(dataSizeStr);
        } else
            g_string_append_printf(result, "<tr><td>%s</td>", displayName);
        g_string_append_printf(result, "<td><input type=\"button\" value=\"Remove\" onclick=\"removeData('%"G_GUINT64_FORMAT":%u:%u');\"></td></tr>\n",
            pageID, types, index);
    }
    g_string_append_printf(result, "<tr><td><input type=\"button\" value=\"Clear all\" onclick=\"clearData('%"G_GUINT64_FORMAT":%u');\"></td></tr></table>\n",
        pageID, types);
}

static void gotWebsiteDataCallback(WebKitWebsiteDataManager *manager, GAsyncResult *asyncResult, AboutDataRequest *dataRequest)
{
    GList *dataList = webkit_website_data_manager_fetch_finish(manager, asyncResult, NULL);

    GString *result = g_string_new(
        "<html><head>"
        "<script>"
        "  function removeData(domain) {"
        "    window.webkit.messageHandlers.aboutData.postMessage(domain);"
        "  }"
        "  function clearData(dataType) {"
        "    window.webkit.messageHandlers.aboutData.postMessage(dataType);"
        "  }"
        "</script></head><body>\n");

    guint64 pageID = webkit_web_view_get_page_id(webkit_uri_scheme_request_get_web_view(dataRequest->request));
    aboutDataFillTable(result, dataRequest, dataList, "Cookies", WEBKIT_WEBSITE_DATA_COOKIES, NULL, pageID);
    aboutDataFillTable(result, dataRequest, dataList, "Device Id Hash Salt", WEBKIT_WEBSITE_DATA_DEVICE_ID_HASH_SALT, NULL, pageID);
    aboutDataFillTable(result, dataRequest, dataList, "Memory Cache", WEBKIT_WEBSITE_DATA_MEMORY_CACHE, NULL, pageID);
    aboutDataFillTable(result, dataRequest, dataList, "Disk Cache", WEBKIT_WEBSITE_DATA_DISK_CACHE, webkit_website_data_manager_get_disk_cache_directory(manager), pageID);
    aboutDataFillTable(result, dataRequest, dataList, "Session Storage", WEBKIT_WEBSITE_DATA_SESSION_STORAGE, NULL, pageID);
    aboutDataFillTable(result, dataRequest, dataList, "Local Storage", WEBKIT_WEBSITE_DATA_LOCAL_STORAGE, webkit_website_data_manager_get_local_storage_directory(manager), pageID);
    aboutDataFillTable(result, dataRequest, dataList, "IndexedDB Databases", WEBKIT_WEBSITE_DATA_INDEXEDDB_DATABASES, webkit_website_data_manager_get_indexeddb_directory(manager), pageID);
    aboutDataFillTable(result, dataRequest, dataList, "Plugins Data", WEBKIT_WEBSITE_DATA_PLUGIN_DATA, NULL, pageID);
    aboutDataFillTable(result, dataRequest, dataList, "Offline Web Applications Cache", WEBKIT_WEBSITE_DATA_OFFLINE_APPLICATION_CACHE, webkit_website_data_manager_get_offline_application_cache_directory(manager), pageID);
    aboutDataFillTable(result, dataRequest, dataList, "HSTS Cache", WEBKIT_WEBSITE_DATA_HSTS_CACHE, webkit_website_data_manager_get_hsts_cache_directory(manager), pageID);
    aboutDataFillTable(result, dataRequest, dataList, "ITP data", WEBKIT_WEBSITE_DATA_ITP, webkit_website_data_manager_get_itp_directory(manager), pageID);
    aboutDataFillTable(result, dataRequest, dataList, "Service Worker Registratations", WEBKIT_WEBSITE_DATA_SERVICE_WORKER_REGISTRATIONS,
        webkit_website_data_manager_get_service_worker_registrations_directory(manager), pageID);
    aboutDataFillTable(result, dataRequest, dataList, "DOM Cache", WEBKIT_WEBSITE_DATA_DOM_CACHE, webkit_website_data_manager_get_dom_cache_directory(manager), pageID);

    result = g_string_append(result, "</body></html>");
    gsize streamLength = result->len;
    GInputStream *stream = g_memory_input_stream_new_from_data(g_string_free(result, FALSE), streamLength, g_free);
    webkit_uri_scheme_request_finish(dataRequest->request, stream, streamLength, "text/html");
    g_list_free_full(dataList, (GDestroyNotify)webkit_website_data_unref);
}

// EXPORT
static void aboutDataHandleRequest(WebKitURISchemeRequest *request, WebKitWebContext *webContext)
{
    AboutDataRequest *dataRequest = aboutDataRequestNew(request);
    WebKitWebsiteDataManager *manager = webkit_web_context_get_website_data_manager(webContext);
    webkit_website_data_manager_fetch(manager, WEBKIT_WEBSITE_DATA_ALL, NULL, (GAsyncReadyCallback)gotWebsiteDataCallback, dataRequest);
}

typedef struct {
    WebKitURISchemeRequest *request;
    GList *thirdPartyList;
} AboutITPRequest;

static AboutITPRequest *aboutITPRequestNew(WebKitURISchemeRequest *request)
{
    AboutITPRequest *itpRequest = g_new0(AboutITPRequest, 1);
    itpRequest->request = g_object_ref(request);
    return itpRequest;
}

static void aboutITPRequestFree(AboutITPRequest *itpRequest)
{
    g_clear_object(&itpRequest->request);
    g_list_free_full(itpRequest->thirdPartyList, (GDestroyNotify)webkit_itp_third_party_unref);
    g_free(itpRequest);
}

static void gotITPSummaryCallback(WebKitWebsiteDataManager *manager, GAsyncResult *asyncResult, AboutITPRequest *itpRequest)
{
    GList *thirdPartyList = webkit_website_data_manager_get_itp_summary_finish(manager, asyncResult, NULL);
    GString *result = g_string_new("<html><body>\n<h1>Trackers</h1>\n");
    GList *l;
    for (l = thirdPartyList; l && l->data; l = g_list_next(l)) {
        WebKitITPThirdParty *thirdParty = (WebKitITPThirdParty *)l->data;
        result = g_string_append(result, "<details>\n");
        g_string_append_printf(result, "<summary>%s</summary>\n", webkit_itp_third_party_get_domain(thirdParty));
        result = g_string_append(result, "<table border='1'><tr><th>First Party</th><th>Website data access granted</th><th>Last updated</th></tr>\n");
        GList *firstPartyList = webkit_itp_third_party_get_first_parties(thirdParty);
        GList *ll;
        for (ll = firstPartyList; ll && ll->data; ll = g_list_next(ll)) {
            WebKitITPFirstParty *firstParty = (WebKitITPFirstParty *)ll->data;
            char *updatedTime = g_date_time_format(webkit_itp_first_party_get_last_update_time(firstParty), "%Y-%m-%d %H:%M:%S");
            g_string_append_printf(result, "<tr><td>%s</td><td>%s</td><td>%s</td></tr>\n", webkit_itp_first_party_get_domain(firstParty),
                webkit_itp_first_party_get_website_data_access_allowed(firstParty) ? "yes" : "no", updatedTime);
            g_free(updatedTime);
        }
        result = g_string_append(result, "</table></details>\n");
    }

    result = g_string_append(result, "</body></html>");
    gsize streamLength = result->len;
    GInputStream *stream = g_memory_input_stream_new_from_data(g_string_free(result, FALSE), streamLength, g_free);
    webkit_uri_scheme_request_finish(itpRequest->request, stream, streamLength, "text/html");
    aboutITPRequestFree(itpRequest);
}

// EXPORT
static void aboutITPHandleRequest(WebKitURISchemeRequest *request, WebKitWebContext *webContext)
{
    AboutITPRequest *itpRequest = aboutITPRequestNew(request);
    WebKitWebsiteDataManager *manager = webkit_web_context_get_website_data_manager(webContext);
    webkit_website_data_manager_get_itp_summary(manager, NULL, (GAsyncReadyCallback)gotITPSummaryCallback, itpRequest);
}


// ----------------------------------------------------------------------
//
// ;madhu 221109, leech the automation stuff from MiniBrowser
//
static Win *newwin(const char *uri, Win *cbwin, Win *caller, int back);
static GtkWidget *createWebViewForAutomationInWindowCallback(WebKitAutomationSession* session, Win *win)
{
  g_message("createWebViewForAutomationInWindowCallback");
  if (win && win->kitw) return GTK_WIDGET(win->kitw);
  Win *new_win = newwin("about:blank", AUTOMATION_CBWIN, win, 0);
  return GTK_WIDGET(new_win->kitw);
}

static void automationStartedCallback(WebKitWebContext *webContext, WebKitAutomationSession *session, Win *win)
{
    WebKitApplicationInfo *info = webkit_application_info_new();
    webkit_application_info_set_name (info, "Wyeb");
    webkit_application_info_set_version(info, WEBKIT_MAJOR_VERSION, WEBKIT_MINOR_VERSION, WEBKIT_MICRO_VERSION);
    webkit_automation_session_set_application_info(session, info);
    webkit_application_info_unref(info);
    g_signal_connect(session, "create-web-view", G_CALLBACK(createWebViewForAutomationInWindowCallback), win);
}


// ----------------------------------------------------------------------
//
//
//

typedef enum {
	CONTENT_FILTER_STORE_LOAD,
	CONTENT_FILTER_STORE_SAVE,
	CONTENT_FILTER_STORE_REMOVE
} ContentFilterStoreOp;


typedef struct {
    ContentFilterStoreOp op;
    GMainLoop *mainLoop;
    WebKitUserContentFilter *filter;
    GError *error;
    gboolean ret;
} FilterStoreData;

static void filterCallback(WebKitUserContentFilterStore *store, GAsyncResult *result, FilterStoreData *data)
{
	switch(data->op) {
	case CONTENT_FILTER_STORE_SAVE:
		data->filter = webkit_user_content_filter_store_save_finish(store, result, &data->error);
		break;
	case CONTENT_FILTER_STORE_LOAD:
		data->filter = webkit_user_content_filter_store_load_finish(store, result, &data->error);
		break;
	case CONTENT_FILTER_STORE_REMOVE:
		data->ret = webkit_user_content_filter_store_remove_finish(store, result, &data->error);
		break;
	default:
		g_assert_not_reached();
	}
	g_main_loop_quit(data->mainLoop);
}

// EXPORT
static gboolean opContentFilter (ContentFilterStoreOp op, const char *identifier, const char *contentFilter, WebKitUserContentFilter **cfret)
{
	if (identifier == NULL || g_strcmp0("", identifier) == 0) identifier = APP "Filter";

	if (op == CONTENT_FILTER_STORE_SAVE) {
		if (!contentFilter || g_strcmp0(contentFilter, "") == 0) {
			g_printerr("Cannot save a content filter without a path to a blocker json file");
			return FALSE;
		}
	}

	gboolean ret = FALSE;
	gchar *filtersPath = g_build_filename(g_get_user_cache_dir(), fullname, "filters", NULL);
        WebKitUserContentFilterStore *store = webkit_user_content_filter_store_new(filtersPath);
        g_free(filtersPath);

	FilterStoreData saveData = { op, NULL, NULL, NULL, FALSE };
	GFile *contentFilterFile = NULL;

	switch (op) {
	case CONTENT_FILTER_STORE_SAVE:
		contentFilterFile = g_file_new_for_commandline_arg(contentFilter);
		webkit_user_content_filter_store_save_from_file(store, identifier, contentFilterFile, NULL, (GAsyncReadyCallback)filterCallback, &saveData);
		break;
	case CONTENT_FILTER_STORE_LOAD:
		webkit_user_content_filter_store_load(store, identifier, NULL, (GAsyncReadyCallback)filterCallback, &saveData);
		break;
	case CONTENT_FILTER_STORE_REMOVE:
		webkit_user_content_filter_store_remove(store, identifier, NULL, (GAsyncReadyCallback)filterCallback, &saveData);
		break;
	default:
		g_assert_not_reached();
	}

        saveData.mainLoop = g_main_loop_new(NULL, FALSE);
        g_main_loop_run(saveData.mainLoop);
        g_object_unref(store);

	switch (op) {
	case CONTENT_FILTER_STORE_SAVE:
		if (saveData.filter) {
			g_message("wyeb: saved content filter at %s as '%s'", contentFilter, identifier);
			if (cfret) *cfret = webkit_user_content_filter_ref(saveData.filter);
			ret = TRUE;
		}  else
			g_printerr("Cannot save filter '%s' as '%s': %s\n", contentFilter, identifier, saveData.error->message);
		break;
	case CONTENT_FILTER_STORE_LOAD:
		if (saveData.filter) {
			g_message("wyeb: loaded content filter named %s", identifier);
			if (cfret) *cfret = webkit_user_content_filter_ref(saveData.filter);
			ret = TRUE;
		} else
			g_printerr("Cannot load content filter named '%s': %s\n", identifier, saveData.error->message);
		break;
	case CONTENT_FILTER_STORE_REMOVE:
		if (saveData.ret) {
			g_message("wyeb: removed content filter named %s", identifier);
			ret = TRUE;
		} else
			g_printerr("Cannot remove content filter named '%s': %s\n", identifier, saveData.error->message);
		break;
	default:
		g_assert_not_reached();
	}

        g_clear_pointer(&saveData.error, g_error_free);
        g_clear_pointer(&saveData.filter, webkit_user_content_filter_unref);
        g_main_loop_unref(saveData.mainLoop);
        if (contentFilterFile) g_object_unref(contentFilterFile);
	return ret;
}

// EXPORT.
static void connect_ucm_aboutdata_script_callback(WebKitUserContentManager *userContentManager)
{
	webkit_user_content_manager_register_script_message_handler(userContentManager, "aboutData");
	g_signal_connect(userContentManager, "script-message-received::aboutData", G_CALLBACK(aboutDataScriptMessageReceivedCallback), ctx);

}


// ----------------------------------------------------------------------
//
//  ;madhu 230115 extra stylesheets handling
//
// load files of the glob pattern "[0-9]*.css" found in
// ~/.config/devhelp/styles/ (overridden by environment variable
// STYLESDIR) sorted in g_utf8_collate_key_for_filename order
// into webkit
//
#include <fnmatch.h>

GList * listDirectory (const char *path, const char *filter);
int natcmp0(gconstpointer a, gconstpointer b, gpointer user_data);
void maybe_load_styles_from_stylesdir (WebKitWebView* view);

int
natcmp0(gconstpointer a, gconstpointer b, gpointer user_data)
{
  return g_strcmp0 (g_hash_table_lookup (user_data, a),
		    g_hash_table_lookup (user_data, b));
}

GList *
listDirectory (const char *path, const char *filter)
{
  GList *entries = NULL;
  GError *err = NULL;
  GDir *dir = NULL;
  const char *name = NULL;
  char *entry = NULL, *value = NULL;
  static GHashTable *table = NULL;

  dir = g_dir_open (path, 0, &err);
  if (!dir) {
    g_error ("listDirectory: failed to g_dir_open %s: %s\n", path,
	     (err && err->message) ? err->message : "");
    return entries;
  }

  table = g_hash_table_new_full (g_str_hash, g_str_equal,
				 (GDestroyNotify) NULL,
				 (GDestroyNotify) g_free);

  while ((name = g_dir_read_name (dir))) {
    if (fnmatch (filter, name, 0) != 0)
	continue;
    entry = g_build_filename (path, name, NULL);
    value = g_utf8_collate_key_for_filename (entry, -1);
    g_hash_table_insert (table, entry, value);
    entries = g_list_prepend (entries, entry);
  }

  entries = g_list_sort_with_data (entries, natcmp0, table);
  g_hash_table_destroy (table);
  g_dir_close (dir);

  return entries;
}

void
maybe_load_styles_from_stylesdir (WebKitWebView* view)
{
  const char *dir;
  GList *l, *entries;
  char *stylesdir = NULL;

  dir = g_getenv ("STYLESDIR");
  if (!dir) {
    // use path2conf
          stylesdir = g_build_filename (g_get_user_config_dir (),
                                        "devhelp", "styles", NULL);
          dir = stylesdir;
  }

  entries = listDirectory (dir, "[0-9]*.css");
  for (l = entries; l != NULL; l = l->next) {
    char *file = l->data;
    char *style = NULL;
    if (!g_file_get_contents (file, &style, NULL, NULL)) {
      g_warning ("Could not read style file: %s\n", file);
      continue;
    }
    webkit_user_content_manager_add_style_sheet
      (webkit_web_view_get_user_content_manager(view),
       webkit_user_style_sheet_new(style,
				   WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
				   WEBKIT_USER_STYLE_LEVEL_USER,
				   NULL, NULL));
    g_message("added style from %s", file);
    g_free(style);
  }
  g_free (stylesdir);
}
