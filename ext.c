/*
Copyright 2017-2020 jun7@hush.com

This file is part of wyeb.

wyeb is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

wyeb is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with wyeb.  If not, see <http://www.gnu.org/licenses/>.
*/


//Make sure JSC is 4 times slower and lacks features we using
//So even JSC is true, there are the DOM funcs left and slow

#if ! JSC + 0
#undef JSC
#define JSC 0
#endif

#if JSC
#define let JSCValue *
#else
#define let void *
#endif

#include <assert.h>

#include <ctype.h>
#include <webkit2/webkit-web-extension.h>

typedef enum {
	W3MMODE_USECONF, W3MMODE_OFF, W3MMODE_ONE, W3MMODE_SAME_HOST
} w3mmode_status_enum;

typedef struct _WP {
	WebKitWebPage *kit;
#if JSC
	WebKitFrame   *mf;
#endif
	bool           hint;
	char          *apkeys;
	guint          hintcb;
	char           lasttype;
	char          *lasthintkeys;
	char          *rangestart;
	bool           script;
	GSList        *black;
	GSList        *white;
	GSList        *emitters;

	int            pagereq;
	bool           redirected;
	char         **refreq;

	//conf
	GObject       *seto;
	char          *lasturiconf;
	char          *lastreset;
	char          *overset;
	bool           setagent;
	bool           setagentprev;
	bool setaccept;
	bool setacceptprev;
	GMainLoop     *sync;

	//overrides conf value for w3mmode setting if set to something
	//other than W3MMODE_USECONF
	w3mmode_status_enum w3mmode_status;
} Page;

#include "general.c"
static void loadconf()
{
	if (!confpath)
		confpath = path2conf("main.conf");

	GKeyFile *new = g_key_file_new();
	g_key_file_load_from_file(new, confpath,G_KEY_FILE_NONE, NULL);
	initconf(new);
}
static void resetconf(Page *page, const char *uri, bool force)
{
	page->setagentprev = page->setagent && !force;
	page->setagent = false;

	page->setacceptprev = page->setaccept && !force;
	page->setaccept = false;

	_resetconf(page, uri, force);

	g_object_set_data(G_OBJECT(page->kit), "adblock",
			GINT_TO_POINTER(getsetbool(page, "adblock") ? 'y' : 'n'));
}

#ifndef MKCLPLUG
static
#endif
	GPtrArray *pages;

static void freepage(Page *page)
{
	g_free(page->apkeys);
	if (page->hintcb)
		g_source_remove(page->hintcb);
	g_free(page->lasthintkeys);
	g_free(page->rangestart);

	g_slist_free_full(page->black, g_free);
	g_slist_free_full(page->white, g_free);
	g_slist_free_full(page->emitters, g_object_unref);
	g_strfreev(page->refreq);

	g_object_unref(page->seto);
	g_free(page->lasturiconf);
	g_free(page->lastreset);
	g_free(page->overset);

	g_ptr_array_remove(pages, page);
	g_free(page);
}

typedef struct {
	bool ok;
	bool insight;
	let  elm;
	double fx;
	double fy;
	double x;
	double y;
	double w;
	double h;
	double zi;
} Elm;

static const char *clicktags[] = {
	"INPUT", "TEXTAREA", "SELECT", "BUTTON", "A", "AREA", NULL};
static const char *linktags[] = {
	"A", "AREA", NULL};
static const char *uritags[] = {
	"A", "AREA", "IMG", "VIDEO", "AUDIO", NULL};

static const char *texttags[] = {
	"INPUT", "TEXTAREA", NULL};

static const char *inputtags[] = {
	"INPUT", "TEXTAREA", "SELECT", NULL};

// input types
/*
static const char *itext[] = { //no name is too
	"search", "text", "url", "email",  "password", "tel", NULL
};
static const char *ilimitedtext[] = {
	"month",  "number", "time", "week", "date", "datetime-local", NULL
};
*/
static const char *inottext[] = {
	"color", "file", "radio", "range", "checkbox", "button", "reset", "submit",

	// unknown
	"image", //to be submit
	// not focus
	"hidden",
	NULL
};


#if JSC
static void __attribute__((constructor)) ext22()
{ DD("this is ext22\n"); }


static JSCValue *pagejsv(Page *page, char *name)
{
	return jsc_context_get_value(webkit_frame_get_js_context(page->mf), name);
}
static JSCValue *sdoc(Page *page)
{
	static JSCValue *s;
	if (s) g_object_unref(s);
	return s = pagejsv(page, "document");
}

#define invoker(...) jsc_value_object_invoke_method(__VA_ARGS__, G_TYPE_NONE)
#define invoke(...) g_object_unref(invoker(__VA_ARGS__))
#define isdef(v) (!jsc_value_is_undefined(v) && !jsc_value_is_null(v))

#define aB(s) G_TYPE_BOOLEAN, s
#define aL(s) G_TYPE_LONG, s
#define aD(s) G_TYPE_DOUBLE, s
#define aS(s) G_TYPE_STRING, s
#define aJ(s) JSC_TYPE_VALUE, s

static let prop(let v, char *name)
{
	let retv = jsc_value_object_get_property(v, name);
	if (isdef(retv)) return retv;
	g_object_unref(retv);
	return NULL;
}
static double propd(let v, char *name)
{
	let retv = jsc_value_object_get_property(v, name);
	double ret = jsc_value_to_double(retv);
	g_object_unref(retv);
	return ret;
}
static char *props(let v, char *name)
{
	let retv = jsc_value_object_get_property(v, name);
	char *ret = isdef(retv) ? jsc_value_to_string(retv) : NULL;
	g_object_unref(retv);
	return ret;
}
static void setprop_s(let v, char *name, char *data)
{
	let dv = jsc_value_new_string(jsc_value_get_context(v), data);
	jsc_value_object_set_property(v, name, dv);
	g_object_unref(dv);
}
static char *attr(let v, char *name)
{
	let retv = invoker(v, "getAttribute", aS(name));
	char *ret = isdef(retv) ? jsc_value_to_string(retv) : NULL;
	g_object_unref(retv);
	return ret;
}


static void __attribute__((unused)) proplist(JSCValue *v)
{
	if (jsc_value_is_undefined(v))
	{
		DD(undefined value)
		return;
	}

	char **ps = jsc_value_object_enumerate_properties(v);
	if (ps)
		for (char **pr = ps; *pr; pr++)
			D(p %s, *pr)
	else
		DD(no props)
	g_strfreev(ps);
}

#define jscunref(v) if (v) g_object_unref(v)
#define docelm(v) prop(v, "documentElement")
#define focuselm(t) invoke(t, "focus")
#define getelms(doc, name) invoker(doc, "getElementsByTagName", aS(name))
#define defaultview(doc) prop(doc, "defaultView")


//JSC
#else
//DOM


#define defaultview(doc) webkit_dom_document_get_default_view(doc)
#define getelms(doc, name) \
	webkit_dom_document_get_elements_by_tag_name_as_html_collection(doc, name)
#define focuselm(t) webkit_dom_element_focus(t)
#define docelm(v) webkit_dom_document_get_document_element(v)
#define jscunref(v) ;

#define attr webkit_dom_element_get_attribute
#define sdoc(v) webkit_web_page_get_dom_document(v->kit)

#endif

static void clearelm(Elm *elm)
{
	if (elm->elm) g_object_unref(elm->elm);
}

static char *stag(let elm)
{
	if (!elm) return NULL;
	static char *name;
	g_free(name);
#if JSC
	name = props(elm, "tagName");
#else
	name = webkit_dom_element_get_tag_name(elm);
#endif

	//normally name is uppercase letters but occasionally lower
	if (name) for (char *c = name; *c; c++)
		*c = g_ascii_toupper(*c);

	return name;
}
static bool attrb(let v, char *name)
{
	return !g_strcmp0(sfree(attr(v, name)), "true");
}
static let idx(let cl, int i)
{
#if JSC
	char buf[9];
	snprintf(buf, 9, "%d", i);
	return prop(cl, buf);
#else
	return webkit_dom_html_collection_item(cl, i);
#endif

//	let retv = jsc_value_object_get_property_at_index(cl, i);
//	if (isdef(retv))
//		return retv;
//	g_object_unref(retv);
//	return NULL;
}

static let activeelm(let doc)
{
#if JSC
	let te = prop(doc, "activeElement");
#else
	let te = webkit_dom_document_get_active_element(doc);
#endif

	if (te && (!g_strcmp0(stag(te), "IFRAME") || !g_strcmp0(stag(te), "BODY")))
	{
		jscunref(te);
		return NULL;
	}
	return te;
}

static void recttovals(let rect, double *x, double *y, double *w, double *h)
{
#if JSC
	*x = propd(rect, "left");
	*y = propd(rect, "top");
	*w = propd(rect, "width");
	*h = propd(rect, "height");
#else
	*x = webkit_dom_client_rect_get_left(rect);
	*y = webkit_dom_client_rect_get_top(rect);
	*w = webkit_dom_client_rect_get_width(rect);
	*h = webkit_dom_client_rect_get_height(rect);
#endif
}



//@misc
static bool send(Page *page, char *action, const char *arg)
{
	//D(send to main %s, ss)
	return ipcsend("main", sfree(g_strdup_printf(
		"%"G_GUINT64_FORMAT":%s:%s",
		webkit_web_page_get_id(page->kit), action, arg ?: "")));
	fprintf(stderr,"ext send: page=%p action=%s, arg=%s\n", page, action, arg);
}
static bool isins(const char **ary, char *val)
{
	if (!val) return false;
	for (;*ary; ary++)
		if (!strcmp(val, *ary)) return true;
	return false;
}
static bool isinput(let te)
{
	char *tag = stag(te);
	if (isins(inputtags, tag))
	{
		if (strcmp(tag, "INPUT"))
			return true;
		else if (!isins(inottext, sfree(attr(te, "TYPE"))))
			return true;
	}
	return false;
}
static char *tofull(let te, char *uri)
{
	if (!te || !uri) return NULL;
#if JSC
	char *base = props(te, "baseURI");
#else
	char *base = webkit_dom_node_get_base_uri((WebKitDOMNode *)te);
#endif

	char *ret = g_uri_resolve_relative(base, uri, SOUP_HTTP_URI_FLAGS, NULL);

	g_free(base);
	return ret;
}

//func is void *(*func)(let doc, Page *page)
static void *_eachframes(let doc, Page *page, void *func)
{
	void *ret;
	if ((ret = ((void *(*)(let doc, Page *page))func)(doc, page))) return ret;

	let cl = getelms(doc, "IFRAME");
	let te;
	for (int i = 0; (te = idx(cl, i)); i++)
	{
#if JSC
		WebKitDOMHTMLIFrameElement *tfe = (void *)webkit_dom_node_for_js_value(te);
		let fdoc = webkit_frame_get_js_value_for_dom_object(page->mf,
			(void *)webkit_dom_html_iframe_element_get_content_document(tfe));
#else
		let fdoc = webkit_dom_html_iframe_element_get_content_document(te);
#endif

		ret = _eachframes(fdoc, page, func);

		jscunref(fdoc);
		jscunref(te);
		if (ret) break;
	}
	g_object_unref(cl);

	return ret;
}
static void *eachframes(Page *page, void *func)
{
	return _eachframes(sdoc(page), page, func);
}



//@whiteblack
typedef struct {
	int white;
	regex_t reg;
} Wb;
static void clearwb(Wb *wb)
{
	regfree(&wb->reg);
	g_free(wb);
}
typedef struct {
	GSList *wblist;
	char   *wbpath;
}  wbstruct;

static wbstruct global_wb;

static void setwblist(wbstruct * wbstruct, bool reload)
{
	if (wbstruct == NULL) wbstruct = &global_wb;

	if (wbstruct->wblist)
		g_slist_free_full(wbstruct->wblist, (GDestroyNotify)clearwb);
	wbstruct->wblist = NULL;

	if (!g_file_test(wbstruct->wbpath, G_FILE_TEST_EXISTS)) return;

	GIOChannel *io = g_io_channel_new_file(wbstruct->wbpath, "r", NULL);
	char *line;
	while (g_io_channel_read_line(io, &line, NULL, NULL, NULL)
			== G_IO_STATUS_NORMAL)
	{
		if (*line == 'w' || *line =='b')
		{
			g_strchomp(line);
			Wb *wb = g_new0(Wb, 1);
			if (regcomp(&wb->reg, line + 1, REG_EXTENDED | REG_NOSUB))
			{
				g_free(line);
				g_free(wb);
				continue;
			}
			wb->white = *line == 'w' ? 1 : 0;
			wbstruct->wblist = g_slist_prepend(wbstruct->wblist, wb);
		}
		g_free(line);
	}
	g_io_channel_unref(io);

	if (reload)
		send(*pages->pdata, "_reloadlast", NULL);
}
static int checkwb(wbstruct *wbstruct, const char *uri) // -1 no result, 0 black, 1 white;
{
	if (wbstruct == NULL) wbstruct = &global_wb;
	if (!wbstruct->wblist) return -1;

	for (GSList *next = wbstruct->wblist; next; next = next->next)
	{
		Wb *wb = next->data;
		if (regexec(&wb->reg, uri, 0, NULL, 0) == 0)
			return wb->white;
	}

	return -1;
}
static void addwhite(Page *page, const char *uri)
{
	//D(blocked %s, uri)
	if (getsetbool(page, "showblocked"))
		send(page, "_blocked", uri);
	page->white = g_slist_prepend(page->white, g_strdup(uri));
}
static void addblack(Page *page, const char *uri)
{
	page->black = g_slist_prepend(page->black, g_strdup(uri));
}
static void showwhite(wbstruct *wbstruct, Page *page, bool white)
{
	if (wbstruct == NULL) wbstruct = &global_wb;

	GSList *list = white ? page->white : page->black;
	if (!list)
	{
		send(page, "showmsg", "No List");
		return;
	}

	FILE *f = fopen(wbstruct->wbpath, "a");
	if (!f) return;

	if (white)
		send(page, "wbnoreload", NULL);

	char pre = white ? 'w' : 'b';
	fprintf(f, "\n# %s in %s\n",
			white ? "blocked" : "loaded",
			webkit_web_page_get_uri(page->kit));

	list = g_slist_reverse(g_slist_copy(list));
	for (GSList *next = list; next; next = next->next)
		fputs(sfree(g_strdup_printf(
			"%c^%s\n", pre, sfree(regesc(next->data)))), f);

	g_slist_free(list);

	fclose(f);

	send(page, "openeditor", wbstruct->wbpath);
}


//@textlink
static let tldoc;
#if JSC
static let tlelm;
#else
static WebKitDOMHTMLTextAreaElement *tlelm;
static WebKitDOMHTMLInputElement *tlielm;
#endif
static void textlinkset(Page *page, char *path)
{
	let doc = sdoc(page);
	let cdoc = docelm(doc);
	jscunref(cdoc);
	if (tldoc != cdoc) return;

	GIOChannel *io = g_io_channel_new_file(path, "r", NULL);
	char *text;
	g_io_channel_read_to_end(io, &text, NULL, NULL);
	g_io_channel_unref(io);

#if JSC
	setprop_s(tlelm, "value", text);
#else
	if (tlelm)
		webkit_dom_html_text_area_element_set_value(tlelm, text);
	else
		webkit_dom_html_input_element_set_value(tlielm, text);
#endif
	g_free(text);
}
static void textlinkget(Page *page, char *path)
{
	let te = eachframes(page, activeelm);
	if (!te) return;

#if JSC
	if (tlelm) g_object_unref(tlelm);
	if (tldoc) g_object_unref(tldoc);
	tlelm = NULL;
	tldoc = NULL;

	if (isinput(te))
		tlelm = te;
#else

	tlelm = NULL;
	tlielm = NULL;

	if (!strcmp(stag(te), "TEXTAREA"))
		tlelm = (WebKitDOMHTMLTextAreaElement *)te;
	else if (isinput(te))
		tlielm = (WebKitDOMHTMLInputElement *)te;
#endif
	else
	{
		send(page, "showmsg", "Not a text");
		return;
	}

	let doc = sdoc(page);
	tldoc = docelm(doc);
	char *text =
#if JSC
		props(tlelm, "value");
#else
		tlelm ?
		webkit_dom_html_text_area_element_get_value(tlelm) :
		webkit_dom_html_input_element_get_value(tlielm);
#endif

	GIOChannel *io = g_io_channel_new_file(path, "w", NULL);
	g_io_channel_write_chars(io, text ?: "", -1, NULL, NULL);
	g_io_channel_unref(io);
	g_free(text);

	send(page, "_textlinkon", NULL);
}


//@hinting
#if JSC
static char *getstyleval(let style, char *name)
{
	char *ret = NULL;
	let retv = invoker(style, "getPropertyValue", aS(name));
	ret = jsc_value_to_string(retv);
	g_object_unref(retv);
	return ret;
}
#else
#define getstyleval webkit_dom_css_style_declaration_get_property_value
#endif
static bool styleis(let dec, char* name, char *pval)
{
	char *val = getstyleval(dec, name);
	bool ret = (val && !strcmp(pval, val));
	g_free(val);

	return ret;
}

static Elm getrect(let te)
{
	Elm elm = {0};

#if JSC
	let rect = invoker(te, "getBoundingClientRect");
#else
	WebKitDOMClientRect *rect =
		webkit_dom_element_get_bounding_client_rect(te);
#endif
	recttovals(rect, &elm.x, &elm.y, &elm.w, &elm.h);
	g_object_unref(rect);

	return elm;
}

static void _trim(double *tx, double *tw, double *px, double *pw)
{
	double right = *tx + *tw;
	double pr    = *px + *pw;
	if (pr < right)
		*tw -= right - pr;

	if (*px > *tx)
	{
		*tw -= *px - *tx;
		*tx = *px;
	}
}

static char *makehintelm(Page *page, Elm *elm,
		const char* text, int len, double pagex, double pagey)
{
	char *tag = stag(elm->elm);
	bool center = isins(uritags, tag) && !isins(linktags, tag);

//	char *uri =
//		attr(elm->elm, "ONCLICK") ?:
//		attr(elm->elm, "HREF") ?:
//		attr(elm->elm, "SRC");

	GString *str = g_string_new(NULL);

#if JSC
	let rects = invoker(elm->elm, "getClientRects");
	let rect;
	for (int i = 0; (rect = idx(rects, i)); i++)
	{
#else
	WebKitDOMClientRectList *rects = webkit_dom_element_get_client_rects(elm->elm);
	gulong l = webkit_dom_client_rect_list_get_length(rects);
	for (gulong i = 0; i < l; i++)
	{
		WebKitDOMClientRect *rect =
			webkit_dom_client_rect_list_item(rects, i);
#endif
		double x, y, w, h;
		recttovals(rect, &x, &y, &w, &h);
		jscunref(rect);

		_trim(&x, &w, &elm->x, &elm->w);
		_trim(&y, &h, &elm->y, &elm->h);

		g_string_append_printf(str, "%d%6.0lf*%6.0lf*%6.0lf*%6.0lf*%3d*%d%s;",
				center,
				x + elm->fx,
				y + elm->fy,
				w,
				h,
				len, i == 0, text);
	}
	g_object_unref(rects);

	char *ret = g_string_free(str, false);

//	g_free(uri);

	return ret;
}


static int getdigit(int len, int num)
{
	int tmp = num - 1;
	int digit = 1;
	while ((tmp = tmp / len)) digit++;
	return digit;
}

static char *makekey(char *keys, int len, int max, int tnum, int digit)
{
	char ret[digit + 1];
	ret[digit] = '\0';

	int llen = len;
	while (llen--)
		if (pow(llen, digit) < max) break;

	llen++;

	int tmp = tnum;
	for (int i = digit - 1; i >= 0; i--)
	{
		ret[i] = toupper(keys[tmp % llen]);
		tmp = tmp / llen;
	}

	return g_strdup(ret);
}

static void trim(Elm *te, Elm *prect)
{
	_trim(&te->x, &te->w, &prect->x, &prect->w);
	_trim(&te->y, &te->h, &prect->y, &prect->h);
}
static Elm checkelm(let win, Elm *frect, Elm *prect, let te,
		bool js, bool notttag)
{
	let dec = NULL;
	Elm ret = getrect(te);

	double bottom = ret.y + ret.h;
	double right  = ret.x + ret.w;
	if (
		(ret.y < 0        && bottom < 0       ) ||
		(ret.y > frect->h && bottom > frect->h) ||
		(ret.x < 0        && right  < 0       ) ||
		(ret.x > frect->w && right  > frect->w)
	)
		goto retfalse;

	ret.insight = true;

	//elms visibility hidden have size also opacity
#if JSC
	dec = invoker(win, "getComputedStyle", aJ(te));
#else
	dec = webkit_dom_dom_window_get_computed_style(win, te, NULL);
#endif

	static char *check[][2] = {
		{"visibility", "hidden"},
		{"opacity"   , "0"},
		{"display"   , "none"},
	};
	for (int k = 0; k < sizeof(check) / sizeof(*check); k++)
		if (styleis(dec, check[k][0], check[k][1]))
			goto retfalse;


	ret.zi = atoi(sfree(getstyleval(dec, "z-index")));

	if (ret.zi > prect->zi || styleis(dec, "position", "absolute"))
		trim(&ret, frect);
	else
		trim(&ret, prect);

	if (js && (ret.h == 0 || ret.w == 0))
		goto retfalse;

	if (js && notttag && !styleis(dec, "cursor", "pointer"))
		goto retfalse;

	g_object_unref(dec);

	ret.elm = g_object_ref(te);
	ret.fx = frect->fx;
	ret.fy = frect->fy;
	ret.ok = true;

	return ret;

retfalse:
	clearelm(&ret);
	if (dec) g_object_unref(dec);
	return ret;
}

static bool addelm(Elm *pelm, GSList **elms)
{
	if (!pelm->ok) return false;
	Elm *elm = g_new(Elm, 1);
	*elm = *pelm;
	if (*elms) for (GSList *next = *elms; next; next = next->next)
	{
		if (elm->zi >= ((Elm *)next->data)->zi)
		{
			*elms = g_slist_insert_before(*elms, next, elm);
			break;
		}

		if (!next->next)
		{
			*elms = g_slist_append(*elms, elm);
			break;
		}
	}
	else
		*elms = g_slist_append(*elms, elm);

	return true;
}

static bool eachclick(let win, let cl,
		Coms type, GSList **elms, Elm *frect, Elm *prect)
{
	bool ret = false;

	let te;
	for (int j = 0; (te = idx(cl, j)); j++)
	{
		bool div = false;
		char *tag = stag(te);
		if (isins(clicktags, tag))
		{
			Elm elm = checkelm(win, frect, prect, te, true, false);
			if (elm.ok)
				addelm(&elm, elms);

			jscunref(te);
			continue;
		} else if (!strcmp(tag, "DIV"))
			div = true; //div is random

		Elm elm = checkelm(win, frect, prect, te, true, true);
		if (!elm.insight && !div && elm.h > 0)
		{
			jscunref(te);
			continue;
		}

		Elm *crect = prect;
#if JSC
		let ccl = prop(te, "children");
		let dec = invoker(win, "getComputedStyle", aJ(te));
#else
		WebKitDOMHTMLCollection *ccl = webkit_dom_element_get_children(te);
		WebKitDOMCSSStyleDeclaration *dec =
			webkit_dom_dom_window_get_computed_style(win, te, NULL);
#endif
		jscunref(te);
		if (
				styleis(dec, "overflow", "hidden") ||
				styleis(dec, "overflow", "scroll") ||
				styleis(dec, "overflow", "auto")
		)
			crect = &elm;

		g_object_unref(dec);

		if (eachclick(win, ccl, type, elms, frect, crect))
		{
			ret = true;
			g_object_unref(ccl);
			clearelm(&elm);
			continue;
		}
		g_object_unref(ccl);

		if (elm.ok)
		{
			ret = true;
			addelm(&elm, elms);
		}
	}
	return ret;
}
static char *ctexttext;
static GSList *_makelist(Page *page, let doc, let win,
		Coms type, GSList *elms, Elm *frect, Elm *prect)
{
	const char **taglist = clicktags; //Cclick
	if (type == Clink ) taglist = linktags;
	if (type == Curi  ) taglist = uritags;
	if (type == Cspawn) taglist = uritags;
	if (type == Crange) taglist = uritags;
	if (type == Cimage) taglist = uritags;
	if (type == Ctext ) taglist = texttags;

	if (type == Cclick && page->script)
	{
#if JSC
		let body = prop(doc , "body");
		let cl = prop(body, "children");
		g_object_unref(body);
#else
		WebKitDOMHTMLCollection *cl = webkit_dom_element_get_children(
				(WebKitDOMElement *)webkit_dom_document_get_body(doc));
#endif
		eachclick(win, cl, type, &elms, frect, prect);
		g_object_unref(cl);
	}
	else for (const char **tag = taglist; *tag; tag++)
	{
		let cl = getelms(doc, *tag);
		let te;
		for (int j = 0; (te = idx(cl, j)); j++)
		{
			Elm elm = checkelm(win, frect, prect, te, false, false);
			jscunref(te);
			if (elm.ok)
			{
				if (type == Ctext)
				{
					if (!isinput(elm.elm))
					{
						clearelm(&elm);
						continue;
					}

					if (ctexttext)
#if JSC
						setprop_s(elm.elm, "value", ctexttext);
#else
						webkit_dom_html_input_element_set_value(elm.elm, ctexttext);
#endif
					focuselm(elm.elm);
					clearelm(&elm);

					g_object_unref(cl);
					return NULL;
				}

				addelm(&elm, &elms);
			}

		}
		g_object_unref(cl);
	}

	return elms;
}

static GSList *makelist(Page *page, let doc, let win,
		Coms type, Elm *frect, GSList *elms)
{
	Elm frectr = {0};
	if (!frect)
	{
#if JSC
		frectr.w = propd(win, "innerWidth");
		frectr.h = propd(win, "innerHeight");
#else
		frectr.w = webkit_dom_dom_window_get_inner_width(win);
		frectr.h = webkit_dom_dom_window_get_inner_height(win);
#endif
		frect = &frectr;
	}
	Elm prect = *frect;
	prect.x = prect.y = 0;

	//D(rect %d %d %d %d, rect.y, rect.x, rect.h, rect.w)
	elms = _makelist(page, doc, win, type, elms, frect, &prect);

	let cl = getelms(doc, "IFRAME");
	let te;
	for (int j = 0; (te = idx(cl, j)); j++)
	{
		Elm cfrect = checkelm(win, frect, &prect, te, false, false);
		if (cfrect.ok)
		{
#if JSC
			double cx = propd(te, "clientLeft");
			double cy = propd(te, "clientTop");
			double cw = propd(te, "clientWidth");
			double ch = propd(te, "clientHeight");
#else
			double cx = webkit_dom_element_get_client_left(te);
			double cy = webkit_dom_element_get_client_top(te);
			double cw = webkit_dom_element_get_client_width(te);
			double ch = webkit_dom_element_get_client_height(te);
#endif

			cfrect.w = MIN(cfrect.w - cx, cw);
			cfrect.h = MIN(cfrect.h - cy, ch);

			cfrect.fx += cfrect.x + cx;
			cfrect.fy += cfrect.y + cy;
			cfrect.x = cfrect.y = 0;

#if JSC
			//some times can't get content
			//let fdoc = prop(te, "contentDocument");
			//if (!fdoc) continue;
			WebKitDOMHTMLIFrameElement *tfe =
				(void *)webkit_dom_node_for_js_value(te);
			let fdoc = webkit_frame_get_js_value_for_dom_object(page->mf,
				(void *)webkit_dom_html_iframe_element_get_content_document(tfe));

			//fwin can't get style vals
			//let fwin = prop(fdoc, "defaultView");
			//et fwin = prop(te, "contentWindow");
			let fwin = g_object_ref(win);
#else
			WebKitDOMDocument *fdoc =
				webkit_dom_html_iframe_element_get_content_document(te);
			WebKitDOMDOMWindow *fwin = defaultview(fdoc);
#endif

			elms = makelist(page, fdoc, win, type, &cfrect, elms);
			g_object_unref(fwin);
			jscunref(fdoc);
		}

		jscunref(te);
		clearelm(&cfrect);
	}
	g_object_unref(cl);

	return elms;
}

static char *hinturi(Coms type, let te, char *uritype)
{
	char *uri = NULL;
	if (type == Curi || type == Cspawn || type == Crange || type == Cimage)
	{
		uri = attr(te, "SRC");

		if (!uri)
		{
#if JSC
			let cl = prop(te, "children");
#else
			WebKitDOMHTMLCollection *cl = webkit_dom_element_get_children(te);
#endif
			let le;
			for (int j = 0; (le = idx(cl, j)); j++)
			{
				if (!g_strcmp0(stag(le), "SOURCE"))
					uri = attr(le, "SRC");

				jscunref(le);
				if (uri) break;
			}

			g_object_unref(cl);
		}

		if (uri && (type == Cspawn || type == Crange || type == Cimage))
		{
			if (!strcmp(stag(te), "IMG"))
				*uritype = 'i';
			else
				*uritype = 'm';
		}
	}

	if (!uri)
		uri = attr(te, "HREF");

	return uri;
}
static void hintret(Page *page, Coms type, let te, bool hasnext)
{
	char uritype = 'l';
	char *uri = hinturi(type, te, &uritype) ?: g_strdup("about:blank");
	char *label =
#if JSC
		props(te, "innerText") ?:
#else
		webkit_dom_html_element_get_inner_text((WebKitDOMHTMLElement *)te) ?:
#endif
		attr(te, "ALT") ?:
		attr(te, "TITLE");

#if JSC
	let odoc = prop(te, "ownerDocument");
	char *ouri = props(odoc, "documentURI");
	g_object_unref(odoc);
#else
	WebKitDOMDocument *odoc = webkit_dom_node_get_owner_document((void *)te);
	char *ouri = webkit_dom_document_get_document_uri(odoc);
#endif

	char *suri = tofull(te, uri);
	char *retstr = g_strdup_printf("%c%d%s %s %s", uritype, hasnext, ouri, suri, label);
	send(page, "_hintret", retstr);

	g_free(uri);
	g_free(label);
	g_free(ouri);
	g_free(suri);
	g_free(retstr);
}

static gboolean revealfu = false;

static bool makehint(Page *page, Coms type, char *hintkeys, char *ipkeys)
{
	let doc = sdoc(page);

	if (!revealfu) page->lasttype = type;

	if (type != Cclick)
	{
#if JSC
		let dtype = prop(doc, "doctype");
		char *name = NULL;
		if (dtype)
		{
			name = props(dtype, "name");
			g_object_unref(dtype);
		}
		if (name && strcmp("html", name))
		{
			g_free(name);
#else
		WebKitDOMDocumentType *dtype = webkit_dom_document_get_doctype(doc);
		if (dtype && strcmp("html", webkit_dom_document_type_get_name(dtype)))
		{
#endif
			//no elms may be;P
			send(page, "_hintret", sfree(g_strdup_printf(
							"l0%s ", webkit_web_page_get_uri(page->kit))));

			g_free(ipkeys);
			return false;
		}
#if JSC
		g_free(name);
#endif
	}

	if (!revealfu)
	if (hintkeys)
	{
		g_free(page->lasthintkeys);
		hintkeys = page->lasthintkeys = g_strdup(hintkeys);
	}
	else
		hintkeys = page->lasthintkeys;
	if (strlen(hintkeys ?: "") < 3) hintkeys = HINTKEYS;

	if (!revealfu) GFA(page->apkeys, ipkeys)

	let win = defaultview(doc);
#if JSC
	double pagex = propd(win, "scrollX");
	double pagey = propd(win, "scrollY");
#else
	double pagex = webkit_dom_dom_window_get_scroll_x(win);
	double pagey = webkit_dom_dom_window_get_scroll_y(win);
#endif
	GSList *elms = makelist(page, doc, win, type, NULL, NULL);
	g_object_unref(win);

	guint tnum = g_slist_length(elms);

	GString *hintstr = g_string_new(NULL);

	int  keylen = strlen(hintkeys);
	int  iplen = ipkeys ? strlen(ipkeys) : 0;
	int  digit = getdigit(keylen, tnum);
	bool last = iplen == digit;
	elms = g_slist_reverse(elms);
	int  i = -1;
	bool ret = false;

	bool rangein = false;
	int  rangeleft = getsetint(page, "hintrangemax");
	let  rangeend = NULL;

	//tab key
	bool focused = false;
	bool dofocus = ipkeys && ipkeys[strlen(ipkeys) - 1] == 9;
	if (dofocus)
		ipkeys[strlen(ipkeys) - 1] = '\0';

	char enterkey[2] = {0};
	*enterkey = (char)GDK_KEY_Return;
	bool enter = page->rangestart && !g_strcmp0(enterkey, ipkeys);
	if (type == Crange && (last || enter))
		for (GSList *next = elms; next; next = next->next)
	{
		Elm *elm = (Elm *)next->data;
		let te = elm->elm;
		i++;
		char *key = sfree(makekey(hintkeys, keylen, tnum, i, digit));
		rangein |= !g_strcmp0(key, page->rangestart);

		if (page->rangestart && !rangein)
			continue;

		if (enter)
		{
			rangeend = te;
			if (--rangeleft < 0) break;
			continue;
		}

		if (!revealfu)
		if (!strcmp(key, ipkeys))
		{
			ipkeys = NULL;
			iplen = 0;
			g_free(page->apkeys);
			page->apkeys = NULL;

			if (!page->rangestart)
				page->rangestart = g_strdup(key);
			else
				rangeend = te;

			break;
		}
	}

	GSList *rangeelms = NULL;
	i = -1;
	rangein = false;
	rangeleft = getsetint(page, "hintrangemax");
	for (GSList *next = elms; next; next = next->next)
	{
		Elm *elm = (Elm *)next->data;
		let te = elm->elm;
		i++;
		char *key = makekey(hintkeys, keylen, tnum, i, digit);
		rangein |= !g_strcmp0(key, page->rangestart);

		if (dofocus)
		{
			if (!focused &&
					(type != Crange || !page->rangestart || rangein) &&
					g_str_has_prefix(key, ipkeys ?: ""))
			{
				focuselm(te);
				focused = true;
			}
		}
		else if (last && type != Crange)
		{
			if (!ret && !strcmp(key, ipkeys))
			{
				ret = true;
				focuselm(te);
				if (type == Cclick && ! revealfu)
				{
					bool isi = isinput(te);
					if (page->script && !isi)
					{
#if JSC
						let rects = invoker(elm->elm, "getClientRects");
						let rect = idx(rects, 0);
#else
						WebKitDOMClientRectList *rects =
							webkit_dom_element_get_client_rects(elm->elm);
						WebKitDOMClientRect *rect =
							webkit_dom_client_rect_list_item(rects, 0);
#endif
						double x, y, w, h;
						recttovals(rect, &x, &y, &w, &h);
						jscunref(rect);
						g_object_unref(rects);

						char *arg = g_strdup_printf("%f:%f",
							x + elm->fx + w / 2.0 + 1.0,
							y + elm->fy + h / 2.0 + 1.0
						);
						send(page, "click", arg);
						g_free(arg);
					}
					else
					{
						if (!getsetbool(page,
									"javascript-can-open-windows-automatically")
								&& sfree(attr(te, "TARGET")))
							send(page, "showmsg", "The element has target, may have to type the enter key.");

#if JSC
						let ce = invoker(doc, "createEvent", aS("MouseEvent"));
						invoke(ce, "initEvent", aB(true), aB(true));
						invoke(te, "dispatchEvent", aJ(ce));
#else
						WebKitDOMEvent *ce =
							webkit_dom_document_create_event(doc, "MouseEvent", NULL);
						webkit_dom_event_init_event(ce, "click", true, true);
						webkit_dom_event_target_dispatch_event(
							(WebKitDOMEventTarget *)te, ce, NULL);
#endif
						g_object_unref(ce);
					}

					if (isi)
						send(page, "toinsert", NULL);
					else
						send(page, "tonormal", NULL);

				}
				else
				{
					if (revealfu) {
						char *uri =
							attr(elm->elm, "ONCLICK") ?:
							attr(elm->elm, "HREF") ?:
							attr(elm->elm, "SRC");
						send(page, "showmsg", uri);
					} else {
						hintret(page, type, te, false);
						send(page, "tonormal", NULL);
					}
				}
			}
		}
		else if (rangeend && rangein)
		{
			rangeelms = g_slist_prepend(rangeelms, g_object_ref(te));
		}
		else if (!page->rangestart || (rangein && !rangeend))
		{
			bool has = g_str_has_prefix(key, ipkeys ?: "");
			ret |= has;
			if (has)
				g_string_append(hintstr, sfree(makehintelm(page,
						elm, key, iplen, pagex, pagey)));
		}

		g_free(key);
		clearelm(elm);
		g_free(elm);

		if (rangein)
			rangein = --rangeleft > 0 && rangeend != te;
	}

	if (!revealfu)
	send(page, "_hintdata", hintstr->str);
	g_string_free(hintstr, true);

	for (GSList *next = rangeelms; next; next = next->next)
	{
		hintret(page, type, next->data, next->next);
		g_usleep(getsetint(page, "rangeloopusec"));
	}
	g_slist_free_full(rangeelms, g_object_unref);

	g_slist_free(elms);

	return ret;
}


//@context

static void domfocusincb(let w, let e, Page *page)
{
	let te = eachframes(page, activeelm);
	send(page, "_focusuri",
			sfree(tofull(te, te ? sfree(attr(te, "HREF")) : NULL)));
	jscunref(te);
}
static void domfocusoutcb(let w, let e, Page *page)
{ send(page, "_focusuri", NULL); }
//static void domactivatecb(WebKitDOMDOMWindow *w, WebKitDOMEvent *ev, Page *page)
//{ DD(domactivate!) }
static void rmtags(let doc, char *name)
{
	let cl = getelms(doc, name);

	GSList *rms = NULL;
	let te;
	for (int i = 0; (te = idx(cl, i)); i++)
		rms = g_slist_prepend(rms, te);

	for (GSList *next = rms; next; next = next->next)
	{
#if JSC
		let pn = prop(next->data, "parentNode");
		invoke(pn, "removeChiled", aJ(next->data));
		g_object_unref(pn);
		g_object_unref(next->data);
#else
		webkit_dom_node_remove_child(
			webkit_dom_node_get_parent_node(next->data), next->data, NULL);
#endif
	}

	g_slist_free(rms);
	g_object_unref(cl);
}
static void domloadcb(let w, let e, let doc)
{
	rmtags(doc, "NOSCRIPT");
}
static gboolean _hintcb(Page *page)
{
	if (page->hint)
		makehint(page, page->lasttype, NULL, NULL);
	if (page->hintcb)
		g_source_remove(page->hintcb);
	page->hintcb = 0;
	return false;
}
static void hintcb(let w, let e, Page *page)
{ _hintcb(page); }
static void dhintcb(let w, let e, Page *page)
{
	if (!page->hintcb)
		page->hintcb = g_timeout_add(400, (GSourceFunc)_hintcb, page);
}
static void unloadcb(let w, let e, Page *page)
{
	GFA(page->apkeys, NULL)
}
static void pagestart(Page *page)
{
	g_slist_free_full(page->black, g_free);
	g_slist_free_full(page->white, g_free);
	page->black = NULL;
	page->white = NULL;
}

static void addlistener(void *emt, char *name, void *func, void *data)
{
#if JSC
//this is disabled when javascript disabled
//also data is only sent once
//	let f = jsc_value_new_function(jsc_value_get_context(emt), NULL,
//			func, data, NULL,
//			G_TYPE_NONE, 1, JSC_TYPE_VALUE, JSC_TYPE_VALUE);
//	invoke(emt, "addEventListener", aS(name), aJ(f));
//	g_object_unref(f);

//	webkit_dom_event_target_add_event_listener(
//		(void *)webkit_dom_node_for_js_value(emt), name, func, false, data);
#else
#endif
	webkit_dom_event_target_add_event_listener(emt, name, func, false, data);
}

static void *frameon(let doc, Page *page)
{
	if (!doc) return NULL;
	void *emt =
		//have to be a view not a doc for beforeunload
#if JSC
		//Somehow JSC's defaultView(conveted to the DOM) is not a event target
		//Even it is, JSC's beforeunload may be killed
		webkit_dom_document_get_default_view(
			(void *)webkit_dom_node_for_js_value(g_object_ref(doc)));
#else
		defaultview(doc);
#endif

	page->emitters = g_slist_prepend(page->emitters, emt);

	if (getsetbool(page, "rmnoscripttag"))
	{
		rmtags(doc, "NOSCRIPT");
		//have to monitor DOMNodeInserted or?
		addlistener(emt, "DOMContentLoaded", domloadcb, doc);
	}

	addlistener(emt, "DOMFocusIn"  , domfocusincb , page);
	addlistener(emt, "DOMFocusOut" , domfocusoutcb, page);
	//addlistener(emt, "DOMActivate" , domactivatecb, page);

	//for hint
	addlistener(emt, "resize"      , hintcb  , page);
	addlistener(emt, "scroll"      , hintcb  , page);
	addlistener(emt, "beforeunload", unloadcb, page);
	addlistener(emt, "load"                 , dhintcb, page);
	addlistener(emt, "DOMContentLoaded"     , dhintcb, page);
	addlistener(emt, "DOMFrameContentLoaded", dhintcb, page);
	addlistener(emt, "DOMSubtreeModified"   , dhintcb, page);

	return NULL;
}


static void pageon(Page *page, bool finished)
{
	g_slist_free_full(page->emitters, g_object_unref);
	page->emitters = NULL;

	eachframes(page, frameon);

	if (!finished
		|| !g_str_has_prefix(webkit_web_page_get_uri(page->kit), APP":main")
		|| !g_key_file_get_boolean(conf, "boot", "enablefavicon", NULL)
	)
		return;

	let doc = sdoc(page);

	let cl = getelms(doc, "IMG");
	let te;
	for (int j = 0; (te = idx(cl, j)); j++)
	{
		if (!g_strcmp0(sfree(attr(te, "SRC")), APP":F"))
		{
#if JSC
			let pe = prop(te, "parentElement");
#else
			WebKitDOMElement *pe =
				webkit_dom_node_get_parent_element((WebKitDOMNode *)te);
#endif
			char *f = g_strdup_printf(
					APP":f/%s", sfree(
						g_uri_escape_string(
							sfree(attr(pe, "HREF")) ?: "", NULL, true)));
#if JSC
			invoke(te, "setAttribute", aS("SRC"), aS(f));
#else
			webkit_dom_element_set_attribute(te, "SRC", f, NULL);
#endif
			g_free(f);
			jscunref(pe);
		}
		jscunref(te);
	}

	g_object_unref(cl);
}


//@misc com funcs
static void mode(Page *page)
{
	let te = eachframes(page, activeelm);

	if (te && (isinput(te) || attrb(te, "contenteditable")))
		send(page, "toinsert", NULL);
	else
		send(page, "tonormal", NULL);

	jscunref(te);
}

static void *focusselection(let doc)
{
	void *ret = NULL;
	let win = defaultview(doc);

#if JSC
	let selection = invoker(win, "getSelection");

	let an =
		   prop(selection, "anchorNode")
		?: prop(selection, "focusNode" )
		?: prop(selection, "baseNode"  )
		?: prop(selection, "extentNode");

	if (an) do
	{
		let pe = prop(an, "parentElement");
		if (pe && isins(clicktags, stag(pe)))
		{
			focuselm(pe);
			g_object_unref(pe);
			ret = pe;
			pe = NULL;
		}
		g_object_unref(an);
		an = pe;
	} while (an);

#else
	WebKitDOMDOMSelection *selection =
		webkit_dom_dom_window_get_selection(win);

	WebKitDOMNode *an = NULL;

	an = webkit_dom_dom_selection_get_anchor_node(selection)
	  ?: webkit_dom_dom_selection_get_focus_node(selection)
	  ?: webkit_dom_dom_selection_get_base_node(selection)
	  ?: webkit_dom_dom_selection_get_extent_node(selection);

	if (an) do
	{
		WebKitDOMElement *pe = webkit_dom_node_get_parent_element(an);
		if (pe && isins(clicktags , stag(pe)))
		{
			focuselm(pe);
			ret = pe;
			break;
		}

	} while ((an = webkit_dom_node_get_parent_node(an)));

#endif

	g_object_unref(selection);
	g_object_unref(win);
	return ret;
}


static void blur(let doc)
{
	let te = activeelm(doc);
	if (te)
	{
#if JSC
		invoke(te, "blur");
		g_object_unref(te);
#else
		webkit_dom_element_blur(te);
#endif
	}

	//clear selection

	let win = defaultview(doc);
#if JSC
	let selection = invoker(win, "getSelection");
	invoke(selection, "empty");
#else
	WebKitDOMDOMSelection *selection =
		webkit_dom_dom_window_get_selection(win);
	webkit_dom_dom_selection_empty(selection);
#endif
	g_object_unref(win);
	g_object_unref(selection);
}

static void
scrollposition(Page *page, char *arg)
{
	WebKitDOMDocument *doc = webkit_web_page_get_dom_document(page->kit);
	WebKitDOMDOMWindow *win = webkit_dom_document_get_default_view(doc);
	if ((strcmp(arg, "") == 0) // return scroll position
	    || strcmp(arg, "-") == 0) // non-empty action argument
	{
		//fprintf(stderr, "ext handling getscrollposition %s\n", arg);
		long x = webkit_dom_dom_window_get_scroll_x(win);
		long y = webkit_dom_dom_window_get_scroll_y(win);
		char *pstr = g_strdup_printf("%lu %lu", x, y);
		send(page, "scrollposition", pstr);
		g_free(pstr);
		return;
	}
	fprintf(stderr, "ext handling setscrollposition %s\n", arg);
	double x = 0, y = 0;
	int n = sscanf(arg, "%lg %lg", &x, &y);
	if (n == 2) {
		//fprintf(stderr, "ext - setting scroll to %lg %lg\n", x, y);
		webkit_dom_dom_window_scroll_to(win, x, y);
	} else {
		fprintf(stderr, "ext: scrollposition: did not parse %s\n", arg);
	}
}

static void halfscroll(Page *page, bool d)
{
	let win = defaultview(sdoc(page));

#if JSC
	double h = propd(win, "innerHeight");
	invoke(win, "scrollTo",
			aD(propd(win, "scrollX")),
			aD(propd(win, "scrollY") + (d ? h/2 : - h/2)));
#else
	double h = webkit_dom_dom_window_get_inner_height(win);
	double y = webkit_dom_dom_window_get_scroll_y(win);
	double x = webkit_dom_dom_window_get_scroll_x(win);
	webkit_dom_dom_window_scroll_to(win, x, y + (d ? h/2 : - h/2));
#endif

	g_object_unref(win);
}

static gboolean offline = false;

static void w3mmode_setstatus(Page *page, const char *arg, int resetp)
{
	w3mmode_status_enum old = page->w3mmode_status;
	if (strcmp(arg, "one") == 0)
		page->w3mmode_status = W3MMODE_ONE;
	else if (strcmp(arg, "same_host") == 0)
		page->w3mmode_status = W3MMODE_SAME_HOST;
	else if (strcmp(arg, "off") == 0)
		page->w3mmode_status = W3MMODE_OFF;
	else if (strcmp(arg, "use_conf") == 0)
		page->w3mmode_status = W3MMODE_USECONF;
	else if (strcmp(arg, "status") == 0) {// noop
	} else {
		g_print("ext: invalid value for w3mmode_setstatus: %s. reset to W3MMODE_ONE\n", arg);
		page->w3mmode_status = W3MMODE_ONE;
	}
	char *useconf = NULL;
	if (!resetp || (page->w3mmode_status != old))
		send(page, "w3mmode_status", page->w3mmode_status == W3MMODE_ONE ? "ONE" : (page->w3mmode_status == W3MMODE_SAME_HOST) ? "SAME_HOST" : (page->w3mmode_status == W3MMODE_OFF) ? "OFF" : (page->w3mmode_status == W3MMODE_USECONF) ? (useconf = g_strdup_printf("USECONF: %s", getset(page,"w3mmode"))) : "INVALID!");
	if(useconf) g_free(useconf);
}


//@ipccb
void ipccb(const char *line)
{
	char **args = g_strsplit(line, ":", 3);
	fprintf(stderr, "ext ipccb: args[0]=%s arg[1]=%s args[2]=%s\n",
		args[0], args[1], args[2]);

	Page *page = NULL;
	long lid = atol(args[0]);
	for (int i = 0; i < pages->len; i++)
		if (webkit_web_page_get_id(((Page *)pages->pdata[i])->kit) == lid)
		{
			page = pages->pdata[i];

			//workaround
			//we can't detect suspended webprocess
			let win = defaultview(sdoc(page));
			if (win)
			{
				g_object_unref(win);
				break;
			}
			else
				page = NULL;
		}

	if (!page) return;
//	fprintf(stderr, "ipccb: set revealfu %d=>false page->lasttype=%c hint=%d apkeys=%s lasthintkeys=%s rangestart=%s\n", revealfu, page->lasttype, page->hint, page->apkeys, page->lasthintkeys, page->rangestart);
	revealfu = false;

	Coms type = *args[1];
	char *arg = args[2];

	char *ipkeys = NULL;
	switch (type) {
	case Cload:
		loadconf();
		break;
	case Coverset:
		GFA(page->overset, g_strdup(*arg ? arg : NULL))
		resetconf(page, webkit_web_page_get_uri(page->kit), true);
		if (page->hint)
			makehint(page, page->lasttype, NULL, g_strdup(page->apkeys));
		break;
	case Cstart:
		pagestart(page);
		break;
	case Con:
		g_strfreev(page->refreq);
		page->refreq = NULL;

		pageon(page, *arg == 'f');
		break;

	case Creveal:
		revealfu = true;
		ipkeys = strdup(arg);

	case Ckey:
	{
		if (page->hintcb)
		{
			g_source_remove(page->hintcb);
			page->hintcb = g_timeout_add(400, (GSourceFunc)_hintcb, page);
		}
		if (!revealfu)
		{
		char key[2] = {0};
		key[0] = toupper(arg[0]);
		ipkeys = page->apkeys ?
			g_strconcat(page->apkeys, key, NULL) : g_strdup(key);
		}

		type = page->lasttype;
		arg = NULL;
	}
	case Cclick:
	case Clink:
	case Curi:
	case Cspawn:
	case Cimage:
	case Crange:
		if (arg)
		{
			g_free(page->rangestart);
			page->rangestart = NULL;
			page->script = *arg == 'y';
		}
//gint64 start = g_get_monotonic_time();
		if (!(page->hint = makehint(page, type, getset(page, "hintkeys"), ipkeys)))
		{
			send(page, "showmsg", "No hint");
			if (!revealfu) send(page, "tonormal", NULL);
		}
//D(time %f, (g_get_monotonic_time() - start) / 1000000.0)
		break;
	case Ctext:
	{
		GFA(ctexttext, *arg ? g_strdup(arg) : NULL);
		let doc = sdoc(page);
		let win = defaultview(doc);
		makelist(page, doc, win, Ctext, NULL, NULL);
		g_object_unref(win);
		break;
	}
	case Crm:
		page->hint = false;
		GFA(page->apkeys, NULL)
		break;

	case Cmode:
		mode(page);
		break;

	case Cfocus:
		eachframes(page, focusselection);
		break;

	case Cblur:
		eachframes(page, blur);
		break;

	case Cwhite:
		if (*arg == 'r') setwblist(NULL, true);
		if (*arg == 'n') setwblist(NULL, false);
		if (*arg == 'w') showwhite(NULL, page, true);
		if (*arg == 'b') showwhite(NULL, page, false);
		break;

	case Ctlget:
		textlinkget(page, arg);
		break;
	case Ctlset:
		textlinkset(page, arg);
		break;

	case Cwithref:
		g_strfreev(page->refreq);
		page->refreq = g_strsplit(arg, " ", 2);
		break;

	case Cscroll:
		halfscroll(page, *arg == 'd');
		break;

	case Cfree:
		freepage(page);
		page = NULL;
		break;

	case Cw3mmode:
		w3mmode_setstatus(page, arg, 0);
		break;

	case Coffline:
		if (strcmp(arg, "true") == 0)
			offline = true;
		else if (strcmp(arg, "false") == 0)
			offline = false;
		send(page, "offline_status", offline ? "true" : "false");
		break;

	case Cscrollposition:
		scrollposition(page, arg);
	}

	g_strfreev(args);

	if (page && page->sync)
		g_main_loop_quit(page->sync);
}


//@page cbs
static void headerout(const char *name, const char *value, gpointer p)
{
	g_print("%s : %s\n", name, value);
}


#if JSC
void init_ephy0(WebKitFrame *mf)
{
	g_assert(WEBKIT_IS_FRAME(mf));
	JSCContext *js_context = webkit_frame_get_js_context(mf);
	g_assert(JSC_IS_CONTEXT(js_context));
	char *data="var Ephy1 = {};"
"Ephy1.icon_url_p = function(str)"
"{"
"    let links = document.getElementsByTagName('link');"
"    for (let i = 0; i < links.length; i++) {"
"        let link = links[i];"
"        if (link.rel == 'icon' || link.rel == 'shortcut icon' || link.rel == 'icon shortcut' || link.rel == 'shortcut-icon' || link.rel == 'apple-touch-icon') {"
"\n		console.debug('link=' + link + ': href=' + link.href + ': str=' + str + ':');\n"
"	    if (str.indexOf(link.href) != -1) {"
"		return true;"
"	    }"
"	}"
"    }"
"    return false;"
"};";
	JSCValue *result1 = jsc_context_evaluate_with_source_uri(js_context, data, -1, "resource:///usr/local/wyeb/ephy1.js", 1);
	g_assert(JSC_IS_VALUE(result1));
	g_object_unref(result1);
}

void init_ephy1(Page *page) {
	init_ephy0(page->mf);
}

static gboolean icon_url_p_ephy1(const char *reqstr, Page *page) {
	JSCContext *js_context = webkit_frame_get_js_context(page->mf);
	g_assert(JSC_IS_CONTEXT(js_context));
	JSCValue *js_ephy = jsc_context_get_value(js_context, "Ephy1");
	g_assert(JSC_IS_VALUE(js_ephy));

/*;madhu 190506
resource:///usr/local/wyeb/ephy1.js:1:21: JS ERROR TypeError: undefined is not an object (evaluating 'Ephy1')
**
ERROR:../ext.c:1981:icon_url_p_ephy1: assertion failed: (!jsc_value_is_undefined(js_ephy))

*/

	if(jsc_value_is_undefined(js_ephy)) {
		fprintf(stderr, "INITIALIZING EPHY1\n");
		init_ephy1(page);
		js_context = webkit_frame_get_js_context(page->mf);
		js_ephy = jsc_context_get_value(js_context, "Ephy1");
	}
	if (jsc_value_is_undefined(js_ephy)) {
	  fprintf(stderr, "FAIL FAIL FAIL: icon_url_p_ephy1(%s)\n", reqstr);
	  return false;
	}
	g_assert(!jsc_value_is_undefined(js_ephy));
	JSCValue *result = jsc_value_object_invoke_method(js_ephy, "icon_url_p", G_TYPE_STRING, reqstr, G_TYPE_NONE);
	g_assert(JSC_IS_VALUE(result));
	g_assert(jsc_value_is_boolean(result));
	gboolean allow_favicon = jsc_value_to_boolean(result);
	g_object_unref(result);
	g_object_unref(js_ephy);
	g_object_unref(js_context);
	return allow_favicon;
}

gboolean
icon_url_p_jsc_dom(const char *str, Page *page)
{
	let doc = sdoc(page);
	let links = invoker(doc, "getElementsByTagName", aS("link"));
	int i;
	//let lengthv = prop(links, "length");
	//int length = toi(lengthv); g_object_unref(lengthv);
	let link;
	for (i = 0; (link = idx(links, i)); i++) {
		//let link = idx(links, i);
		char * rel = props(link, "rel");
		//fprintf(stderr, "rel=%s\n", rel);
		if (rel != NULL &&
		    (strcmp(rel, "icon") == 0 ||
		     strcmp(rel, "shortcut icon") == 0 ||
		     strcmp(rel, "icon shortcut") == 0 ||
		     strcmp(rel, "apple-touch-icon") == 0)) {
			char *image = props(link, "href");
			//fprintf(stderr, "image=%s\n", image);
			if (image && strstr(str, image)) {
				g_free(image);
				g_free(rel);
				g_object_unref(links);
				return true;
			}
			g_free(image);
		}
		g_free(rel);
	}
	g_object_unref(links);
	return false;
}
#endif

gboolean
icon_url_p_dom(const char *str, Page *page)
{
	WebKitDOMDocument *document = webkit_web_page_get_dom_document(page->kit);
	WebKitDOMHTMLCollection *links = webkit_dom_document_get_elements_by_tag_name_as_html_collection(document, "link");
	int length = webkit_dom_html_collection_get_length(links);
	int i;
	for (i = 0; i < length; i++) {
		WebKitDOMNode *node = webkit_dom_html_collection_item(links, i);
		char *rel = webkit_dom_html_link_element_get_rel(WEBKIT_DOM_HTML_LINK_ELEMENT(node));
		if (rel != NULL && (
			    g_ascii_strcasecmp (rel, "icon") == 0 ||
			    g_ascii_strcasecmp (rel, "shortcut icon") == 0 ||
			    g_ascii_strcasecmp (rel, "icon shortcut") == 0 ||
			    g_ascii_strcasecmp (rel, "shortcut-icon") == 0 ||
			    g_ascii_strcasecmp (rel, "apple-touch-icon") == 0)) {
			g_free(rel);
			char *image = webkit_dom_html_link_element_get_href (WEBKIT_DOM_HTML_LINK_ELEMENT (node));
			if (image && strstr(str, image)) {
				g_free(image);
				return true;
			}
			g_free(image);
		}
	}
	return false;
}

int
uri_scheme_http_p(const char *uri)
{
  int i; const char *p;
  for (i = 0, p = uri; *p; p++, i++)
    switch(i) {
    case 0: if (*p != 'h') return 0; break;
    case 1: if (*p != 't') return 0; break;
    case 2: if (*p != 't') return 0; break;
    case 3: if (*p != 'p') return 0; break;
    case 4: if (*p != 's') return 0; break;
    default: return 0;
    }
  return 1;
}

#if SOUP_MAJOR_VERSION == 2
#undef SOUP_URI_VALID_FOR_HTTP
#endif
#define SOUP_URI_VALID_FOR_HTTP(uri) ((uri) && uri_scheme_http_p(g_uri_get_scheme(uri)) && g_uri_get_host(uri) && g_uri_get_path(uri))

static gboolean reqcb(
		WebKitWebPage *p,
		WebKitURIRequest *req,
		WebKitURIResponse *res,
		Page *page)
{
	bool ret = false;
	char *reason="";

	page->pagereq++;
	const char *reqstr = webkit_uri_request_get_uri(req);
	if (offline) return true;
	if (g_str_has_prefix(reqstr, APP":"))
	{
		fprintf(stderr, "REQCB: ACCEPT: %s: scheme\n", reqstr);
		return false;
	}

	const char *pagestr = webkit_web_page_get_uri(page->kit);
	SoupMessageHeaders *head = webkit_uri_request_get_http_headers(req);

	// special case data to avoid long data urls in output
	if (!head && g_str_has_prefix(reqstr, "data:")) {
		static char buf[126]; int i;
		for (i = 0; i < (sizeof(buf) - 3) && reqstr[i]; i++)
			buf[i] = reqstr[i];
		if (i == (sizeof(buf) - 3)) {
			buf[i++] = '.';
			buf[i++] = '.';
			buf[i++] = '\0';
		}
		fprintf(stderr, "REQCB: ACCEPT: %s: scheme\n", buf);
		return false;
	}

	GUri *puri = NULL;
	GUri *ruri = NULL;

	// the ui process sets page->w3mmode_status. If it is set by
	// the ui process, use it. Otherwise use what is set by the
	// conf system
	w3mmode_status_enum w3mmode_status = page->w3mmode_status;
	g_assert(w3mmode_status == W3MMODE_ONE ||
		 w3mmode_status == W3MMODE_SAME_HOST ||
		 w3mmode_status == W3MMODE_OFF ||
		 w3mmode_status == W3MMODE_USECONF);
	if (w3mmode_status == W3MMODE_USECONF) {
		char *w3mmode =  getset(page, "w3mmode");
		if (w3mmode) {
			if (strcmp(w3mmode, "one") == 0)
				w3mmode_status = W3MMODE_ONE;
			else if (strcmp(w3mmode, "same_host") == 0)
				w3mmode_status = W3MMODE_SAME_HOST;
			else if (strcmp(w3mmode, "off") == 0)
				w3mmode_status = W3MMODE_OFF;
		}
		// check if it was set correctly
		if (w3mmode_status == W3MMODE_USECONF) {
			fprintf(stderr, "invalid conf setting for w3mmode_status: %s. Using W3MMODE_ONE\n", w3mmode);
			w3mmode_status = W3MMODE_ONE;
		}
	}

	if (g_key_file_get_boolean(conf, "boot", "enablefavicon", NULL)) {
		gboolean allow_favicon, ret;

#if JSC
		gint64 start1, stop1, start2, stop2, start3, stop3;

		start1 = g_get_monotonic_time();
#endif
		ret = icon_url_p_dom(reqstr, page);
		allow_favicon = ret;
#if JSC
		stop1 = g_get_monotonic_time();

		start2 = g_get_monotonic_time();
		ret = icon_url_p_ephy1(reqstr, page);
		stop2 = g_get_monotonic_time();
//		g_assert(ret == allow_favicon);
		if (! (ret == allow_favicon))
			fprintf(stderr, "ASSERT FAIL FAIL FAIL Ephy: icon_url_p_dom allow_favicon=%d but icon_url_p_ephy1=%d\n", allow_favicon, ret);

		start3 = g_get_monotonic_time();
		ret = icon_url_p_jsc_dom(reqstr, page);
		stop3 = g_get_monotonic_time();
//		g_assert(ret == allow_favicon);
		if (! (ret == allow_favicon))
			fprintf(stderr, "ASSERT FAIL FAIL FAIL JSCDOM: icon_url_p_dom allow_favicon=%d but icon_url_p_jsc_dom=%d\n", allow_favicon, ret);


		fprintf(stderr, "elapsed_dom1 %"G_GINT64_FORMAT
			" = %"G_GINT64_FORMAT " - %"G_GINT64_FORMAT"\n",
			stop1 - start1, stop1, start1);
		fprintf(stderr, "elapsed_ephy1 %"G_GINT64_FORMAT
			" = %"G_GINT64_FORMAT " - %"G_GINT64_FORMAT"\n",
			stop2 - start2, stop2, start2);
		fprintf(stderr, "elapsed_jsc1 %"G_GINT64_FORMAT
			" = %"G_GINT64_FORMAT " - %"G_GINT64_FORMAT"\n",
			stop3 - start3, stop3, start3);
#endif
		if (allow_favicon) {
			reason = "allow favicon";
			goto out;
		}
	}

	if (w3mmode_status == W3MMODE_ONE)  {
		const char *respuri = webkit_web_page_get_uri(p);
		assert(strcmp(respuri, pagestr) == 0);
		if (reqstr && respuri) {
			const char *g = reqstr;
			const char *h = respuri;
			if ((*g++ == *h && *h++ == 'h') &&
			    (*g++ == *h && *h++ == 't') &&
			    (*g++ == *h && *h++ == 't') &&
			    (*g++ == *h && *h++ == 'p') &&
			    ((*g && *h && *g == *h) ||
			     (*g == ':' && *h == 's' && *++h) ||
			     (*g == 's' && *h == ':' && *++g)) &&
			    strcmp(g, h) == 0) {
				//ok
			} else {
				int glen = strlen(g);
				int hlen = strlen(h);
				if ((glen == hlen + 1 && g[glen - 1] == '/') ||
				    (hlen == glen + 1 && h[hlen - 1] == '/')) {
					//ok
				} else if (hlen == glen && strcmp(g, h) == 0) {
					//ok
				} else if (res) {
				  fprintf(stderr, "???ALLOWING REDIRECT???\npagestr=%s\nrequri=%s\nrespuri=%s\n",
					  respuri, reqstr, webkit_uri_response_get_uri(res));
				  //ok
				} else {
					reason = "w3mone";
					ret = true; goto end;
				}
			}
		}
	} else if (w3mmode_status == W3MMODE_OFF) {
	} else if (w3mmode_status == W3MMODE_SAME_HOST) {
		ruri = ruri ?: g_uri_parse(reqstr, SOUP_HTTP_URI_FLAGS, NULL);
		puri = puri ?: g_uri_parse(pagestr, SOUP_HTTP_URI_FLAGS, NULL);
		if (SOUP_URI_VALID_FOR_HTTP(ruri)) {
			if (!SOUP_URI_VALID_FOR_HTTP(puri) ||
			    strcmp(g_uri_get_host(puri),
				   g_uri_get_host(ruri))) {
				g_uri_unref(puri);
				g_uri_unref(ruri);
				reason = "w3msamehost";
				ret = true; goto end;
			}
		}
	}

	int check = checkwb(NULL, reqstr);
	if (check == 0) {
		reason = "checkwb";
		ret = true;
	}
	else if (check == -1 && getsetbool(page, "adblock"))
	{
		bool (*checkf)(const char *, const char *) =
			g_object_get_data(G_OBJECT(page->kit), APP"check");
		if (checkf) {
			reason = "adblock";
			ret = !checkf(reqstr, pagestr);
		}
	}
	if (ret) goto out;

	if (res && page->pagereq == 2)
	{//redirect. pagereq == 2 means it is a top level request
		//in redirection we don't get yet current uri
		resetconf(page, reqstr, false);
		page->pagereq = 1;
		reason = "pagereqredirect";
		page->redirected = true;
		goto out;
	}

	if (check == 1 //white
		|| page->pagereq == 1 //open page request
		|| !head
		|| !getsetbool(page, "reldomaindataonly")
		|| !soup_message_headers_get_list(head, "Referer")
	) {
		reason = "check-noreldomain-norefererer";
		goto out;
	}

	//reldomainonly
	puri = puri ?: g_uri_parse(pagestr, SOUP_HTTP_URI_FLAGS, NULL);
	const char *phost = g_uri_get_host(puri);

	//g_assert(strcmp(reason,"")==0);
	if (strcmp(reason,"") != 0) {
		fprintf(stderr, "---------->ASSERTFAIL: reason=%s\n", reason);
	}

	if (phost)
	{
		char **cuts = g_strsplit(
				getset(page, "reldomaincutheads") ?: "", ";", -1);
		for (char **cut = cuts; *cut; cut++)
			if (g_str_has_prefix(phost, *cut))
			{
				phost += strlen(*cut);
				break;
			}
		g_strfreev(cuts);

		ruri = ruri ?: g_uri_parse(reqstr, SOUP_HTTP_URI_FLAGS, NULL);
		const char *rhost = g_uri_get_host(ruri);

		reason = "reldomain" ;
		ret = rhost && !g_str_has_suffix(rhost, phost);
	}
	g_uri_unref(ruri);
	g_uri_unref(puri);

out:
	if (ret)
		addwhite(page, reqstr);
	else
		addblack(page, reqstr);

	if (!ret && head)
	{
		soup_message_headers_remove(head, "Upgrade-Insecure-Requests");
		soup_message_headers_remove(head, "Referer");

		if (/*page->pagereq == 1 &&*/(page->setagent || page->setagentprev))
			soup_message_headers_replace(head, "User-Agent",
					getset(page, "user-agent") ?: "");
		if (/*page->pagereq == 1 &&*/(page->setaccept || page->setacceptprev))
			soup_message_headers_replace(head, "Accept",
					getset(page, "accept") ?: "");

		char *rmhdrs = getset(page, "removeheaders");
		if (rmhdrs)
		{
			char **rms = g_strsplit(rmhdrs, ";", -1);
			for (char **rm = rms; *rm; rm++)
				soup_message_headers_remove(head, *rm);
			g_strfreev(rms);
		}
	}

	if (page->refreq && !g_strcmp0(page->refreq[1], reqstr))
	{
		if (!ret && head)
			soup_message_headers_append(head, "Referer", page->refreq[0]);
		g_strfreev(page->refreq);
		page->refreq = NULL;
	}

	if (!ret && getsetbool(page, "stdoutheaders"))
	{
		if (res)
		{
			g_print("RESPONSE: %s\n", webkit_uri_response_get_uri(res));
			soup_message_headers_foreach(
					webkit_uri_response_get_http_headers(res), headerout, NULL);
			g_print("\n");
		}
		g_print("REQUEST: %s\n", reqstr);
		if (head)
			soup_message_headers_foreach(head, headerout, NULL);
		g_print("\n");
	}

end:
	fprintf(stderr, "REQCB %s: %s: %s\n",
		(ret == true) ? "REJECT" : "ACCEPT",
		reqstr, reason);
	return ret;
}
//static void formcb(WebKitWebPage *page, GPtrArray *elms, gpointer p) {}
//static void loadcb(WebKitWebPage *kp, Page *page) {}
static void uricb(Page* page)
{
	//workaround: when in redirect change uri delays
	if (page->redirected)
		page->pagereq = 1;
	else
	{
		page->pagereq = 0;
		resetconf(page, webkit_web_page_get_uri(page->kit), false);
	}
	page->redirected = false;
}


//static gboolean printopt(const char *option, JSCOptionType type,
//		const char *description, gpointer user_data)
//{
//	D(option %s --- %s, option, description);
//	return false;
//}
static gboolean inittimeoutcb(gpointer roop)
{
	g_main_loop_quit(roop);
	return false;
}
static void initpage(WebKitWebExtension *ex, WebKitWebPage *kp)
{
//	jsc_options_foreach(printopt, NULL);

	Page *page = g_new0(Page, 1);
	g_object_weak_ref(G_OBJECT(kp), (GWeakNotify)freepage, page);
	page->kit = kp;
#if JSC
	page->mf = webkit_web_page_get_main_frame(kp);
#endif
	page->seto = g_object_new(G_TYPE_OBJECT, NULL);
	page->w3mmode_status = W3MMODE_USECONF;
	g_ptr_array_add(pages, page);

	wbstruct *wbstruct = &global_wb;
	wbstruct->wbpath = path2conf("whiteblack.conf");
	setwblist(NULL, false);

	static char *ipcid;
	if (!ipcid)
	{
		ipcid = g_strdup_printf("%d", getpid());
		ipcwatch(ipcid, g_main_context_default());
	}

	loadconf();
	//workaround this timing the view can not get page id when page is recreated happening on some pages. thus we send it

	g_message("ext.c:initpage: sending pageinit %d\n", getpid());
#if 0
	{
	  WebKitUserMessage *msg;
	  msg = webkit_user_message_new("pageinit",
		g_variant_new( "s",
			       sfree(g_strdup_printf("%s:%lu",
						     ipcid, webkit_web_page_get_id(kp)))));
	  webkit_web_extension_send_message_to_context(ex, msg, NULL, NULL, NULL);
	}
#else
	if (send(page, "_pageinit", sfree(g_strdup_printf("%s:%lu",
						ipcid, webkit_web_page_get_id(kp)))))
	{
		GMainContext *ctx = g_main_context_new();
		page->sync = g_main_loop_new(ctx, true);
		ipcwatch(ipcid, ctx);

		GSource *src = g_timeout_source_new_seconds(1);
		g_source_set_callback(src, inittimeoutcb, page->sync, NULL);
		g_source_attach(src, ctx);
		g_source_unref(src);

		g_main_loop_run(page->sync);

		g_main_context_unref(ctx);
		g_main_loop_unref(page->sync);
	}
	page->sync = NULL;
#endif

//	SIG( page->kit, "context-menu"            , contextcb, NULL);
	SIG( page->kit, "send-request"            , reqcb    , page);
//	SIG( page->kit, "document-loaded"         , loadcb   , page);
	SIGW(page->kit, "notify::uri"             , uricb    , page);
//	SIG( page->kit, "form-controls-associated", formcb   , NULL);

	g_message("ext.c:exiting initpage %d\n", getpid());
}

G_MODULE_EXPORT void webkit_web_extension_initialize_with_user_data(
		WebKitWebExtension *ex, const GVariant *v)
{
	const char *str = g_variant_get_string((GVariant *)v, NULL);
	fullname = g_strdup(g_strrstr(str, ";") + 1);
	pages = g_ptr_array_new();
	SIG(ex, "page-created", initpage, NULL);

#if JSC
	void window_object_cleared_cb(WebKitScriptWorld *world, WebKitWebPage *page, WebKitFrame *frame, WebKitWebExtension *extension);
	WebKitScriptWorld *script_world = webkit_script_world_new_with_name ("wyebguid");
	SIG(script_world, "window-object-cleared", window_object_cleared_cb, ex);
#endif
}

#if JSC
void
window_object_cleared_cb(WebKitScriptWorld *world, WebKitWebPage *page, WebKitFrame *frame, WebKitWebExtension *extension)
{
	JSCContext *js_context = webkit_frame_get_js_context_for_script_world(frame, world);
	WebKitFrame *frame1 = webkit_web_page_get_main_frame(page);
	fprintf(stderr, "ext: window_object_cleared_cb: frame_for_script_world = %p frame = %p \n", frame, frame1);
	fprintf(stderr, "INITIALIZING EPHY1 in window-object-cleared\n");
	init_ephy0(frame1);
}
#endif
