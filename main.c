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

#include <webkit2/webkit2.h>

//for window list
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif

#include <gcr/gcr.h>

//flock
#include <sys/file.h>

#define LASTWIN (wins && wins->len ? (Win *)*wins->pdata : NULL)
#define URI(win) (webkit_web_view_get_uri(win->kit) ?: "")

#define gdkw(wd) gtk_widget_get_window(wd)

typedef enum {
	Mnormal    = 0,
	Minsert    = 1,
	Mhint      = 2,
	Mhintrange = 2 + 4,
	Mfind      = 512,
	Mopen      = 1024,
	Mopennew   = 2048,
	Mlist      = 4096,
	Mpointer   = 8192,
} Modes;

typedef enum {
	VSH_HTML,
	VSH_SOURCE,
	VSH_HEADERS
} viewsourceorheaders_mode;

typedef struct {
	long h;
	long v;
} Adj;

// kludge to get scroll position from webprocess
static Adj *scroll_adj = NULL;

typedef struct {
	char * uri;
	char *mimetype;
	viewsourceorheaders_mode mode;
	GBytes *source;
	GBytes *headers;
	Adj source_adj, headers_adj, page_adj;
} viewsourceorheader_info;

typedef struct _Spawn Spawn;

typedef struct _WP {
	union {
		GtkWindow *win;
		GtkWidget *winw;
		GObject   *wino;
	};
	union {
		WebKitWebView *kit;
		GtkWidget     *kitw;
		GObject       *kito;
	};
	union {
		WebKitSettings *set;
		GObject        *seto;
	};
	union {
		GtkLabel  *lbl;
		GtkWidget *lblw;
	};
	union {
		GtkEntry  *ent;
		GtkWidget *entw;
		GObject   *ento;
	};
	GtkWidget *canvas;
	char   *winid;
	GSList *ipcids;
	WebKitFindController *findct;

	//mode
	Modes   lastmode;
	Modes   mode;
	bool    crashed;
	bool    userreq; //not used

	//conf
	char   *lasturiconf;
	char   *lastreset;
	char   *overset;

	//draw
	double  lastx;
	double  lasty;
	char   *msg;
	double  prog;
	gint64  prog_start1, prog_end1;
	double  progd;
	GdkRectangle progrect;
	guint   drawprogcb;
	GdkRGBA rgba;

	//hittestresult
	char   *link;
	char   *focusuri;
	bool    usefocus;
	char   *linklabel;
	char   *image;
	char   *media;
	bool    oneditable;

	//pointer
	double  lastdelta;
	guint   lastkey;
	double  px;
	double  py;
	guint   pbtn;
	guint   ppress;

	//entry
	GSList *undo;
	GSList *redo;
	char   *lastsearch;
	bool    infind;

	//winlist
	int     cursorx;
	int     cursory;
	bool    scrlf;
	int     scrlcur;
	double  scrlx;
	double  scrly;

	//history
	char   *histstr;
	guint   histcb;

	//hint
	char   *hintdata;
	char    com; //Coms
	//hint and spawn
	Spawn  *spawn;

	//misc
	bool    scheme;
	GTlsCertificateFlags tlserr;
	GTlsCertificate *cert, *failedcert;
	int errorpage;

	char   *fordl;
	guint   msgfunc;
	bool    maychanged;

	bool cancelcontext;
	bool cancelbtn1r;
	bool cancelmdlr;

	Window xid;
	char sxid[64];
	Display *dpy;

	viewsourceorheader_info v;
	Adj * page_adj;
} Win;

struct _Spawn {
	Win  *win;
	char *action;
	char *cmd;
	char *path;
	bool once;
};

//@global
#ifdef MKCLPLUG
#define static __attribute__((visibility("default"))) 
#endif

static char      *suffix = "";
static GPtrArray *wins;
static GPtrArray *dlwins;
static GQueue    *histimgs;
typedef struct {
	char   *buf;
	gsize   size;
	guint64 id;
} Img;
static char *lastmsg;
static char *lastkeyaction;

static char *mdpath;
static char *accelp;

static char *hists[]  = {"h1", "h2", "h3", "h4", "h5", "h6", "h7", "h8", "h9", NULL};
static int   histfnum = sizeof(hists) / sizeof(*hists) - 1;
static char *histdir;

static GtkAccelGroup *accelg;
static WebKitWebContext *ctx;
static bool ephemeral;

//for xembed
#include <gtk/gtkx.h>
static long plugto;

#ifdef MKCLPLUG
#define static static
#endif

#ifdef MKCLPLUG
#define STATIC __attribute__((visibility("default")))
#else
#define STATIC static
#endif

//shared code
static void _kitprops(bool set, GObject *obj, GKeyFile *kf, char *group);
#define MAINC
#include "general.c"
#if V24
#define FORDISP(s) sfree(webkit_uri_for_display(s))
//#define FORDISP(s) (webkit_uri_for_display(s))
#else
#define FORDISP(s) s
#endif

#include "surfprop.h"

static char *usage =
	"usage: "APP" [[[suffix] action|\"\"] uri|arg|\"\"]\n"
	"\n"
	"  "APP" www.gnu.org\n"
	"  "APP" new www.gnu.org\n"
	"  "APP" / new www.gnu.org\n"
	"\n"
	"  suffix: Process ID.\n"
	"    It is added to all directories conf, cache and etc.\n"
	"    '/' is default. '//' means $SUFFIX.\n"
	"  action: Such as new(default), open, pagedown ...\n"
	"    Except 'new' and some, without a set of $SUFFIX and $WINID,\n"
	"    actions are sent to the window last focused\n"
	;

static char *mainmdstr =
"<!-- this is text/markdown -->\n"
"<meta charset=utf8>\n"
"<style>\n"
"body{overflow-y:scroll} /*workaround for the delaying of the context-menu*/\n"
"a{background:linear-gradient(to right top, #ddf, white, white, white);\n"
" color:#109; margin:1px; padding:2px; text-decoration:none; display:inline-block}\n"
"a:hover{text-decoration:underline}\n"
"img{height:1em; width:1em; margin:-.1em}\n"
"strong > code{font-size:1.4em}\n"
"</style>\n\n"
"###Specific Keys:\n"
"- **`e`** : Edit this page\n"
"- **`E`** : Edit main config file\n"
"- **`c`** : Open config directory\n"
"- **`m`** : Show this page\n"
"- **`M`** : Show **[history]("APP":history)**\n"
"- **`b`** : Add title and URI of a page opened to this page\n"
"\n"
"If **e,E,c** don't work, edit values '`"MIMEOPEN"`' of ~/.config/"DIRNAME"/main.conf<br>\n"
"or change mimeopen's database by running "
"'<code>mimeopen <i>file/directory</i></code>' in terminals.\n"
"\n"
"For other keys, see **[help]("APP":help)** assigned '**`:`**'.\n"
"Since "APP" is inspired by **[dwb](https://wiki.archlinux.org/index.php/dwb)**\n"
"and luakit, usage is similar to them.\n"
"\n---\n<!--\n"
"wyeb:i/iconname returns an icon image of current icon theme of gtk.\n"
"wyeb:f/uri returns a favicon of the uri loaded before.\n"
"wyeb:F converted to the wyeb:f with a parent tag's href.\n"
"-->\n"
"[![]("APP":i/"APP") "APP"](https://github.com/jun7/"APP")\n"
"[Wiki](https://github.com/jun7/"APP"/wiki)\n"
"[![]("APP":F) Adblock](https://github.com/jun7/"APP"adblock)\n"
"[![]("APP":f/"DISTROURI") "DISTRONAME"]("DISTROURI")\n"
;

static	WebKitNetworkProxyMode proxy_mode = WEBKIT_NETWORK_PROXY_MODE_DEFAULT;
static	WebKitNetworkProxySettings *proxy_settings = NULL;

static void proxy_settings_from_conf()
{
	char *arg = g_strdup(confcstr("proxymode"));
	WebKitNetworkProxyMode mode = WEBKIT_NETWORK_PROXY_MODE_DEFAULT;

	if (arg)
		if (strcmp(arg, "no_proxy") == 0)
			mode = WEBKIT_NETWORK_PROXY_MODE_NO_PROXY;
		else if (strcmp(arg, "default") == 0)
			mode = WEBKIT_NETWORK_PROXY_MODE_DEFAULT;
		else if (strcmp(arg, "custom") == 0)
			mode = WEBKIT_NETWORK_PROXY_MODE_CUSTOM;
		else {
			fprintf(stderr, "checkconf: unknown proxymode: %s ( no_proxy|default|custom) using default\n",
				arg);
		}

	char *default_proxy_uri = g_strdup(confcstr("customproxy"));
	fprintf(stderr, "checkconf: default proxy = %s\n", default_proxy_uri);

	char *ignore_hosts_str = confcstr("customproxyignorehosts");
	char **ignore_hosts = NULL;
	char * const *p;
	if (ignore_hosts_str) {
		ignore_hosts = g_strsplit(ignore_hosts_str, ",", -1);
		for (p = ignore_hosts; *p; p++)
			fprintf(stderr, "checkonf: customproxyignorehosts: %s\n", *p);
	}

	char *schemes[10], *proxies[10];
	int nlines = 0;
	char *schemeproxiesstring = confcstr("customproxiesforschemes");
	if (schemeproxiesstring) {
		char **scheme_proxies = g_strsplit(schemeproxiesstring, ",", -1);
		if (scheme_proxies) {
			for (p = scheme_proxies; *p && nlines < 10-1; p++) {
				char **scheme_proxy_pair= g_strsplit(*p, "=", -1);
				char **q = scheme_proxy_pair;
				char *scheme, *proxy;
				int valid = 0;
				if (*q) {
					scheme = *q;
					q++;
					if (*q) {
						proxy = *q;
						q++;
						valid = 1;
					}
					if (*q) {
						fprintf(stderr, "checkconf: customproxiesforschemes: ignoring: ");
						do { fprintf(stderr, "%s", *q++); } while (*q);
						fprintf(stderr, "\n");
					}
				}
				if (!valid) {
					fprintf(stderr, "checkconf: customproxyschemes: failed to parse scheme=proxy_url line:\n");
				} else {
					fprintf(stderr, "checkconf: customproxyschemes: adding %s: %s\n", scheme, proxy);
					schemes[nlines] = strdup(scheme);
					proxies[nlines] = strdup(proxy);
					nlines++;
				}
				g_strfreev(scheme_proxy_pair);
			}
		}
		g_strfreev(scheme_proxies);
	}

	if (proxy_settings) {
		webkit_network_proxy_settings_free(proxy_settings);
		proxy_settings = NULL;
	}

	// set proxy_settings from "customproxy" and "customproxyignorehosts"
	if (default_proxy_uri) {
		fprintf(stderr, "checkconf: creating proxy settings with default proxy = %s\n", default_proxy_uri);
		proxy_settings = webkit_network_proxy_settings_new
			(default_proxy_uri, (const char * const *) ignore_hosts);
		g_strfreev(ignore_hosts);
		int i;
		for (i = 0; i < nlines; i++) {
			webkit_network_proxy_settings_add_proxy_for_scheme
				(proxy_settings, schemes[i], proxies[i]);
			g_free(schemes[i]); g_free(proxies[i]);
		}
	}

	if (arg && ctx)       // proxymode was supplied. we set mode from it.
		if (mode == WEBKIT_NETWORK_PROXY_MODE_NO_PROXY ||
		    mode == WEBKIT_NETWORK_PROXY_MODE_DEFAULT)  {
			// we do not use proxy_settings.
			fprintf(stderr, "setting proxymode = %s\n",
				(mode == WEBKIT_NETWORK_PROXY_MODE_NO_PROXY) ? "None" : "System");
			webkit_web_context_set_network_proxy_settings(ctx, mode, NULL);
			proxy_mode = mode;
		}
		else if (mode == WEBKIT_NETWORK_PROXY_MODE_CUSTOM)
			if (proxy_settings) {
				webkit_web_context_set_network_proxy_settings
					(ctx, mode, proxy_settings);
				fprintf(stderr, "setting customproxy: %s\n", default_proxy_uri);
				proxy_mode = mode;
			} else {
				fprintf(stderr, "checkconf: CUSTOM: but no customproxy setting. setting default\n");
				proxy_mode = WEBKIT_NETWORK_PROXY_MODE_DEFAULT;
				webkit_web_context_set_network_proxy_settings(ctx, proxy_mode, NULL);
			}
	g_free(arg);
	g_free(default_proxy_uri);
}

//@misc
//util (indeipendent
static void addhash(char *str, guint *hash)
{
	if (*hash == 0) *hash = 5381;
	do *hash = *hash * 33 + *str;
	while (*++str);
}

//core
static bool isin(GPtrArray *ary, void *v)
{
	if (ary && v) for (int i = 0; i < ary->len; i++)
		if (v == ary->pdata[i]) return true;
	return false;
}

char* GET_WINID(Win *win)
{
  GFA(win->winid, g_strdup_printf("%"G_GUINT64_FORMAT, webkit_web_view_get_page_id(win->kit)));
  return win->winid;
}

STATIC Win *winbyid(const char *pageid)
{
	guint64 intid = atol(pageid);
	Win *maychanged = NULL;
	for (int i = 0; i < wins->len; i++)
	{
		Win *win = wins->pdata[i];
		if (intid == webkit_web_view_get_page_id(win->kit)
				|| !g_strcmp0(pageid, GET_WINID(win) /*win->winid*/))
			return win;

		if (win->maychanged)
			maychanged = win;
	}

	//workaround: _pageinit are sent to unknown when page is recreated
	return maychanged;
}
static void quitif()
{
	if (!wins->len && !dlwins->len && !confbool("keepproc"))
	{
		//workaround: gtk_main_quit doesn't shutdown io when webproc is freezed
		remove(ipcpath("main"));
		gtk_main_quit();
	}
}
static void reloadlast()
{
	if (!LASTWIN) return;
	static gint64 last = 0;
	gint64 now = g_get_monotonic_time();
	if (now - last < 300000) return;
	last = now;
	webkit_web_view_reload(LASTWIN->kit);
}
static void alert(char *msg)
{
	GtkWidget *dialog = gtk_message_dialog_new(
			LASTWIN ? LASTWIN->win : NULL,
			GTK_DIALOG_DESTROY_WITH_PARENT,
			GTK_MESSAGE_ERROR,
			GTK_BUTTONS_CLOSE,
			"%s", msg);
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);
}

//history
static void append(char *path, const char *str)
{
	FILE *f = fopen(path, "a");
	if (f)
	{
		fprintf(f, "%s\n", str ?: "");
		fclose(f);
	}
	else
		alert(sfree(g_strdup_printf("fopen %s failed", path)));
}
static void freeimg(Img *img)
{
	g_free(img ? img->buf : NULL);
	g_free(img);
}
static void pushimg(Win *win, bool swap)
{
	int maxi = MAX(confint("histimgs"), 0);

	while (histimgs->length > 0 && histimgs->length >= maxi)
		freeimg(g_queue_pop_tail(histimgs));

	if (!maxi) return;

	if (swap)
		freeimg(g_queue_pop_head(histimgs));

	double ww = gtk_widget_get_allocated_width(win->kitw);
	double wh = gtk_widget_get_allocated_height(win->kitw);
	double scale = confint("histimgsize") / MAX(1, MAX(ww, wh));
	if (!(
		gtk_widget_get_visible(win->kitw) &&
		gtk_widget_is_drawable(win->kitw) &&
		ww * scale >= 1 &&
		wh * scale >= 1
	)) {
		g_queue_push_head(histimgs, NULL);
		return;
	}

	Img *img = g_new(Img, 1);
	static guint64 unique;
	img->id = ++unique;

	cairo_surface_t *suf = cairo_image_surface_create(
			CAIRO_FORMAT_ARGB32, ww * scale, wh * scale);

	cairo_t *cr = cairo_create(suf);
	cairo_scale(cr, scale, scale);
	gtk_widget_draw(win->kitw, cr);
	cairo_destroy(cr);

	GdkPixbuf *scaled = gdk_pixbuf_get_from_surface(suf, 0, 0,
				cairo_image_surface_get_width(suf),
				cairo_image_surface_get_height(suf));
	cairo_surface_destroy(suf);

	gdk_pixbuf_save_to_buffer(scaled,
			&img->buf, &img->size,
			"jpeg", NULL, "quality", "77", NULL);

	g_object_unref(scaled);

	g_queue_push_head(histimgs, img);
}
static char *histfile;
static char *lasthist;

static int archivehistory(const char *current)
{
	struct stat info;
	int ret = stat(current, &info);
	if (ret < 0) {
		fprintf(stderr, "archivehistory: couldn't stat %s: ", current);
		perror(""); return ret;
	}
	char subdir[25];
	struct tm tm;
	localtime_r(&(info.st_mtime), &tm);
	ret = strftime(subdir, sizeof(subdir), "archive-%Y-%m", &tm);
	if (ret == 0) return -1;
	char *archivedir = g_build_filename(histdir, subdir, NULL);
	char *archivepath = NULL;
	for (int i = 1; i < 100; i++) {
		char filename[25];
		snprintf(filename, sizeof(filename), "h%d.txt", i);
		GFA(archivepath, g_build_filename(archivedir, filename, NULL));
		if (!g_file_test(archivepath, G_FILE_TEST_EXISTS)) break;
	}
	g_assert(!g_file_test(archivepath, G_FILE_TEST_EXISTS));
	ret = g_mkdir_with_parents(archivedir, 0700);
	if (ret < 0) {
		fprintf(stderr, "archivehistory: error creating dir %s:\n",
			archivedir);
		perror("");
	} else {
		ret = rename(current, archivepath);
		if (ret < 0) {
			fprintf(stderr, "archivehistory: error renaming %s to %s\n", current, archivepath);
			perror("");
		}
	}
	g_free(archivepath);
	g_free(archivedir);
	return ret;
}

static gboolean histuniquep(WebKitWebView *view, const char *str)
{
	WebKitBackForwardList *bfl = webkit_web_view_get_back_forward_list(view);
	WebKitBackForwardListItem *item;
	int i; int f = 0;

	for (i = 1; (item = webkit_back_forward_list_get_nth_item(bfl, i)) != NULL; i++) {
		f++;
		const char *url = webkit_back_forward_list_item_get_uri(item);
		int pos = strchrnul(str+19, ' ') - (str+19);
		if (strncmp(str + 19, url, pos) == 0) {
			return false;
		}
	}
	if (f != 0) {
		return false;
	}
	for (i = -1; (item = webkit_back_forward_list_get_nth_item(bfl, i)) != NULL; i--) {
		const char *url = webkit_back_forward_list_item_get_uri(item);
		int pos = strchrnul(str+19, ' ') - (str+19);
		if (strncmp(str + 19, url, pos) == 0) {
			return false;
		}
	}
	return true;
}

static int historyenabled = 1;
static gboolean histcb(Win *win)
{
	if (!isin(wins, win)) return false;
	win->histcb = 0;

	if (!win) return false;
	if (!historyenabled) return false;

	if (strcmp(URI(win), getatom(win, atomGo)))
		surfaddhist(URI(win));

#define MAXSIZE 22222
	static int ci;
	static int csize;
	if (!histfile || !g_file_test(histdir, G_FILE_TEST_EXISTS))
	{
		_mkdirif(histdir, false);

		csize = 0;
		for (ci = 0; ci < histfnum; ci++)
		{
			GFA(histfile, g_build_filename(histdir, hists[ci], NULL))
			struct stat info;
			if (stat(histfile, &info) == -1) {
				fprintf(stderr, "stat %s failed\n", histfile);
				csize = 0;
				break; //first time. errno == ENOENT
			} else {
				csize = info.st_size;
				if (csize < MAXSIZE)
					break;
			}
		}
	}

	char *str = win->histstr;
	win->histstr = NULL;

	if (!histuniquep(win->kit, str)) {
		g_free(str);
		return false;
 	}

	if (lasthist && !strcmp(str + 18, lasthist + 18))
	{
		g_free(str);
		pushimg(win, true);
		return false;
	}
	pushimg(win, false);

	append(histfile, str);
	GFA(lasthist, str)

	csize += strlen(str) + 1;
	if (csize > MAXSIZE)
	{
		if (++ci >= histfnum) ci = 0;

		GFA(histfile, g_build_filename(histdir, hists[ci], NULL))
		archivehistory(histfile);
		FILE *f = fopen(histfile, "w");
		fclose(f);
		csize = 0;
	}

	return false;
}
static bool updatehist(Win *win)
{
	const char *uri;
	if (ephemeral
	|| !*(uri = URI(win))
	|| g_str_has_prefix(uri, APP":")
	|| g_str_has_prefix(uri, "data:")
	|| g_str_has_prefix(uri, "about:")) return false;

	char tstr[99];
	time_t t = time(NULL);
	strftime(tstr, sizeof(tstr), "%T/%d/%b/%y", localtime(&t));

	GFA(win->histstr, g_strdup_printf("%s %s %s", tstr, uri,
			webkit_web_view_get_title(win->kit) ?: ""))

	return true;
}
static void histperiod(Win *win)
{
	if (win->histstr)
	{
		if (win->histcb)
			g_source_remove(win->histcb);

		//if not cancel updated by load finish(fixhist)
		histcb(win);
	}
}
static void fixhist(Win *win)
{
	if (webkit_web_view_is_loading(win->kit) ||
			!updatehist(win)) return;

	if (win->histcb)
		g_source_remove(win->histcb);

	//drawing delays so for ss have to swap non finished draw
	win->histcb = g_timeout_add(200, (GSourceFunc)histcb, win);
}

static void removehistory()
{
	for (char **file = hists; *file; file++)
		remove(sfree(g_build_filename(histdir, *file, NULL)));

	GFA(histfile, NULL)
	GFA(lasthist, NULL)
}

//msg
static gboolean clearmsgcb(Win *win)
{
	if (!isin(wins, win)) return false;

	GFA(lastmsg, win->msg)
	win->msg = NULL;
	gtk_widget_queue_draw(win->canvas);
	win->msgfunc = 0;
	return false;
}
static void _showmsg(Win *win, char *msg)
{
	if (win->msgfunc) g_source_remove(win->msgfunc);
	fprintf(stderr, "_SHOWMSG: %s\n", msg);
	GFA(win->msg, msg)
	win->msgfunc = !msg ? 0 :
		g_timeout_add(getsetint(win, "msgmsec"), (GSourceFunc)clearmsgcb, win);
	gtk_widget_queue_draw(win->canvas);
}
static void showmsg(Win *win, const char *msg)
{ _showmsg(win, g_strdup(msg)); }

//com
static void _send(Win *win, Coms type, const char *args, guint64 pageid)
{
	char *arg = sfree(g_strdup_printf("%"G_GUINT64_FORMAT":%c:%s",
				pageid, type, args ?: ""));

	static bool alerted;
	GSList *nextnext;

	for (GSList *next = win->ipcids; next; next = next ? next->next : nextnext) {
		fprintf(stderr, "main send: calling ipcsend(%s, %s)\n",
			(char *) next->data, arg);
		if (!ipcsend(next->data, arg))
		{
			g_free(next->data);
			nextnext = next->next;
			win->ipcids = g_slist_delete_link(win->ipcids, next);
			next = NULL;

			if (!win->ipcids && !win->crashed && !alerted && type == Cstart)
			{
				alerted = true;
				alert("Failed to communicate with the Web Extension.\n"
						"Make sure ext.so is in "EXTENSION_DIR".");
			}
		}
	}
}
static void send(Win *win, Coms type, const char *args)
{
	_send(win, type, args, webkit_web_view_get_page_id(win->kit));
}
//;madhu 240531 send has to be static because libdbus calls send(2)
//and picks up this version
void send_wrapper(Win *win, Coms type, const char *args) { send(win,type,args); }


static void sendeach(Coms type, char *args)
{
	char *sent = NULL;
	for (int i = 0; i < wins->len; i++)
	{
		Win *lw = wins->pdata[i];
		if (!lw->ipcids || (sent && !strcmp(sent, lw->ipcids->data))) continue;
		sent = lw->ipcids->data;
		send(lw, type, args);
	}
}
typedef struct {
	Win  *win;
	Coms  type;
	char *args;
} Send;
static gboolean senddelaycb(Send *s)
{
	fprintf(stderr,"senddelaycb s=%p=(%p,%u,%s)\n", s, s->win, s->type, s->args);
	if (isin(wins, s->win))
		send(s->win, s->type, s->args);
	g_free(s->args);
	g_free(s);
	return false;
}
static void senddelay(Win *win, Coms type, char *args) //args is eaten
{
	Send s = {win, type, args};
	fprintf(stderr,"senddelay: win->maychanged=%d type=%d args=%p Send=%p\n",
		win->maychanged, type, args, &s);

	g_timeout_add(40, (GSourceFunc)senddelaycb, g_memdup(&s, sizeof(Send)));
}

//event
static GdkDevice *pointer()
{ return gdk_seat_get_pointer(
		gdk_display_get_default_seat(gdk_display_get_default())); }
static GdkDevice *keyboard()
{ return gdk_seat_get_keyboard(
		gdk_display_get_default_seat(gdk_display_get_default())); }

static void *kitevent(Win *win, bool ispointer, GdkEventType type)
{
	GdkEvent    *e  = gdk_event_new(type);
	GdkEventAny *ea = (void *)e;

	ea->window = gdkw(win->kitw);
	g_object_ref(ea->window);
	gdk_event_set_device(e, ispointer ? pointer() : keyboard());
	return e;
}
static void putevent(void *e)
{
	gdk_event_put(e);
	gdk_event_free(e);
}
static void _putbtn(Win* win, GdkEventType type, guint btn, double x, double y)
{
	GdkEventButton *eb = kitevent(win, true, type);

	eb->send_event = false; //true destroys the mdl btn hack
	if (btn > 10)
	{
		btn -= 10;
		eb->state = GDK_BUTTON1_MASK;
	}
	eb->button = btn;
	eb->type   = type;
	eb->x      = x;
	eb->y      = y;

	putevent(eb);
}
static void putbtn(Win* win, GdkEventType type, guint btn)
{ _putbtn(win, type, btn, win->px, win->py); }

static gboolean delaymdlrcb(Win *win)
{
	if (isin(wins, win))
		putbtn(win, GDK_BUTTON_RELEASE, 2);
	return false;
}
static void makeclick(Win *win, guint btn)
{
	putbtn(win, GDK_BUTTON_PRESS, btn);
	if (btn == 2)
		g_timeout_add(40, (GSourceFunc)delaymdlrcb, win);
	else
		putbtn(win, GDK_BUTTON_RELEASE, btn);
}
static void motion(Win *win, double x, double y)
{
	GdkEventMotion *em = kitevent(win, true, GDK_MOTION_NOTIFY);
	em->x = x;
	em->y = y;
	if (win->ppress)
		em->state = win->pbtn == 3 ? GDK_BUTTON3_MASK :
		            win->pbtn == 2 ? GDK_BUTTON2_MASK : GDK_BUTTON1_MASK;

	gtk_widget_event(win->kitw, (void *)em);
	gdk_event_free((void *)em);
}

//shared
static void setresult(Win *win, WebKitHitTestResult *htr)
{
	g_free(win->image);
	g_free(win->media);
	g_free(win->link);
	g_free(win->linklabel);

	win->image = win->media = win->link = win->linklabel = NULL;
	win->usefocus = false;

	if (!htr) return;

	win->image = webkit_hit_test_result_context_is_image(htr) ?
		g_strdup(webkit_hit_test_result_get_image_uri(htr)) : NULL;
	win->media = webkit_hit_test_result_context_is_media(htr) ?
		g_strdup(webkit_hit_test_result_get_media_uri(htr)) : NULL;
	win->link = webkit_hit_test_result_context_is_link(htr) ?
		g_strdup(webkit_hit_test_result_get_link_uri(htr)) : NULL;

	win->linklabel = g_strdup(webkit_hit_test_result_get_link_label(htr) ?:
			webkit_hit_test_result_get_link_title(htr));

	win->oneditable = webkit_hit_test_result_context_is_editable(htr);
}
static void undo(Win *win, GSList **undo, GSList **redo)
{
	if (!*undo && redo != undo) return;
	const char *text = gtk_entry_get_text(win->ent);

	if (*text && (!*redo || strcmp((*redo)->data, text)))
		*redo = g_slist_prepend(*redo, g_strdup(text));

	if (redo == undo) return;

	if (!strcmp((*undo)->data, text))
	{
		g_free((*undo)->data);
		*undo = g_slist_delete_link(*undo, *undo);
		if (!*undo) return;
	}

	gtk_entry_set_text(win->ent, (*undo)->data);
	gtk_editable_set_position((void *)win->ent, -1);

	if (*undo == win->redo) //redo
	{
		if (!strcmp((*undo)->data, (*redo)->data))
			g_free((*undo)->data);
		else
			*redo = g_slist_prepend(*redo, (*undo)->data);
		*undo = g_slist_delete_link(*undo, *undo);
	}
}
#define getent(win) gtk_entry_get_text(win->ent)
static void setent(Win *win, const char *str)
{
	undo(win, &win->undo, &win->undo);
	gtk_entry_set_text(win->ent, str);
}

//@@conf
static int thresholdp(Win *win)
{
	int ret = 8;
	g_object_get(gtk_widget_get_settings(win->winw),
			"gtk-dnd-drag-threshold", &ret, NULL);
	return ret * ret;
}
static char *dldir(Win *win)
{
	return g_build_filename(
			g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD) ?:
				g_get_home_dir(),
			getset(win, "dlsubdir"),
			NULL);
}
static void colorf(Win *win, cairo_t *cr, double alpha)
{
	cairo_set_source_rgba(cr,
			win->rgba.red, win->rgba.green, win->rgba.blue, alpha);
}
static void colorb(Win *win, cairo_t *cr, double alpha)
{
	if (win->rgba.red + win->rgba.green + win->rgba.blue < 1.5)
		cairo_set_source_rgba(cr, 1, 1, 1, alpha);
	else
		cairo_set_source_rgba(cr, 0, 0, 0, alpha - .04);
}
//monitor
static GHashTable *monitored = NULL;
static void monitorcb(GFileMonitor *m, GFile *f, GFile *o, GFileMonitorEvent e,
		void (*func)(const char *))
{
	if (e != G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT &&
			e != G_FILE_MONITOR_EVENT_DELETED) return;

	//delete event's path is old and chenge event's path is new,
	//when renamed out new is useless, renamed in old is useless.
	char *path = g_file_get_path(f);
	if (g_hash_table_lookup(monitored, path))
		func(path);
	g_free(path);
}
static bool monitor(char *path, void (*func)(const char *))
{
	if (!monitored) monitored = g_hash_table_new(g_str_hash, g_str_equal);

	if (g_hash_table_lookup(monitored, path)) return false;
	g_hash_table_add(monitored, g_strdup(path));

	if (g_file_test(path, G_FILE_TEST_IS_SYMLINK))
	{ //this only works boot time though
		char buf[PATH_MAX + 1];
		char *rpath = realpath(path, buf);
		if (rpath)
			monitor(rpath, func);
	}

	GFile *gf = g_file_new_for_path(path);
	GFileMonitor *gm = g_file_monitor_file(
			gf, G_FILE_MONITOR_NONE, NULL, NULL);
	SIG(gm, "changed", monitorcb, func);

	g_object_unref(gf);
	return true;
}

void _kitprops(bool set, GObject *obj, GKeyFile *kf, char *group)
{
	//properties
	guint len;
	GParamSpec **list = g_object_class_list_properties(
			G_OBJECT_GET_CLASS(obj), &len);

	for (int i = 0; i < len; i++) {
		GParamSpec *s = list[i];
		const char *key = s->name;
		if (!(s->flags & G_PARAM_WRITABLE)) continue;
		if (set != g_key_file_has_key(kf, group, key, NULL)) continue;

		GValue gv = {0};
		g_value_init(&gv, s->value_type);

		g_object_get_property(obj, key, &gv);

		switch (s->value_type) {
		case G_TYPE_BOOLEAN:
			if (set) {
				bool v = g_key_file_get_boolean(kf, group, key, NULL);
				if (g_value_get_boolean(&gv) == v) continue;
				g_value_set_boolean(&gv, v);
			}
			else
				g_key_file_set_boolean(kf, group, key, g_value_get_boolean(&gv));
			break;
		case G_TYPE_UINT:
			if (set) {
				int v = g_key_file_get_integer(kf, group, key, NULL);
				if (g_value_get_uint(&gv) == v) continue;
				g_value_set_uint(&gv, v);
			}
			else
				g_key_file_set_integer(kf, group, key, g_value_get_uint(&gv));
			break;
		case G_TYPE_STRING:
			if (set) {
				char *v = sfree(g_key_file_get_string(kf, group, key, NULL));
				if (!strcmp((g_value_get_string(&gv))?:"", v)) continue;;
				g_value_set_string(&gv, v);
			} else
				g_key_file_set_string(kf, group, key, g_value_get_string(&gv) ?: "");
			break;
		default:
			if (!strcmp(key, "hardware-acceleration-policy")) {
				if (set) {
					char *str = g_key_file_get_string(kf, group, key, NULL);

					WebKitHardwareAccelerationPolicy v;
					if (!strcmp(str, "ALWAYS"))
						v = WEBKIT_HARDWARE_ACCELERATION_POLICY_ALWAYS;
					else if (!strcmp(str, "NEVER"))
						v = WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER;
					else //ON_DEMAND
						v = WEBKIT_HARDWARE_ACCELERATION_POLICY_ON_DEMAND;

					g_free(str);
					if (v == g_value_get_enum(&gv)) continue;

					g_value_set_enum(&gv, v);
				} else {
					switch (g_value_get_enum(&gv)) {
					case WEBKIT_HARDWARE_ACCELERATION_POLICY_ON_DEMAND:
						g_key_file_set_string(kf, group, key, "ON_DEMAND");
						break;
					case WEBKIT_HARDWARE_ACCELERATION_POLICY_ALWAYS:
						g_key_file_set_string(kf, group, key, "ALWAYS");
						break;
					case WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER:
						g_key_file_set_string(kf, group, key, "NEVER");
						break;
					}
				}
			}
			else
				continue;
		}
		if (set)
		{
			//D(change value %s, key)
			g_object_set_property(obj, key, &gv);
		}
		g_value_unset(&gv);
	}
}

STATIC void setcss(Win *win, char *namesstr); //declaration
STATIC void setscripts(Win *win, char *namesstr); //declaration
STATIC void resetconf(Win *win, const char *uri, int type)
{ //type: 0: uri, 1:force, 2:overset, 3:file
//	"reldomaindataonly", "removeheaders"
	char *checks[] = {"reldomaincutheads", "rmnoscripttag", NULL};
	guint hash = 0;
	char *lastcss = g_strdup(getset(win, "usercss"));
	char *lastscripts = g_strdup(getset(win, "userscripts"));

	if (type && LASTWIN == win)
		for (char **check = checks; *check; check++)
			addhash(getset(win, *check) ?: "", &hash);

	_resetconf(win, uri ?: URI(win), type);
	if (type == 3)
		send(win, Cload, NULL);
	if (type >= 2)
		send(win, Coverset, win->overset);

	if (type && LASTWIN == win)
	{
		guint last = hash;
		hash = 0;
		for (char **check = checks; *check; check++)
			addhash(getset(win, *check) ?: "", &hash);
		if (last != hash)
			reloadlast();
	}

	if (getsetbool(win, "addressbar"))
		gtk_widget_show(win->lblw);
	else
		gtk_widget_hide(win->lblw);

	char *newcss = getset(win, "usercss");
	if (g_strcmp0(lastcss, newcss))
		setcss(win, newcss);
	char *newscripts = getset(win, "userscripts");
	if (g_strcmp0(lastscripts, newscripts))
		setscripts(win, newscripts);

	gdk_rgba_parse(&win->rgba, getset(win, "msgcolor") ?: "");

	g_free(lastcss);
	g_free(lastscripts);
}

static void checkmd(const char *mp)
{
	if (mdpath && wins->len && g_file_test(mdpath, G_FILE_TEST_EXISTS))
		for (int i = 0; i < wins->len; i++)
	{
		Win *win = wins->pdata[i];
		if (g_str_has_prefix(URI(win), APP":main"))
			webkit_web_view_reload(win->kit);
	}
}
static void prepareif(
		char **path,
		char *name, char *initstr, void (*monitorcb)(const char *))
{
	bool first = !*path;
	if (first) *path = path2conf(name);

	if (g_file_test(*path, G_FILE_TEST_EXISTS))
		goto out;

	GFile *gf = g_file_new_for_path(*path);

	GFileOutputStream *o = g_file_create(
			gf, G_FILE_CREATE_PRIVATE, NULL, NULL);
	g_output_stream_write((GOutputStream *)o,
			initstr, strlen(initstr), NULL, NULL);
	g_object_unref(o);

	g_object_unref(gf);

out:
	if (first)
		monitor(*path, monitorcb);
}
static void preparemd()
{
	prepareif(&mdpath, "mainpage.md", mainmdstr, checkmd);
}

static bool wbreload = true;
static void checkwb(const char *mp)
{
	sendeach(Cwhite, wbreload ? "r" : "n");
	wbreload = true;
}
static void preparewb()
{
	static char *wbpath;
	prepareif(&wbpath, "whiteblack.conf",
			"# First char is 'w':white list or 'b':black list.\n"
			"# Second and following chars are regular expressions.\n"
			"# Preferential order: bottom > top\n"
			"# Keys 'a' and 'A' on "APP" add blocked or loaded list to this file.\n"
			"\n"
			"w^https?://([a-z0-9]+\\.)*githubusercontent\\.com/\n"
			"\n"
			, checkwb);
}

static void clearaccels(gpointer p,
		const char *path, guint key, GdkModifierType mods, gboolean changed)
{
	gtk_accel_map_change_entry(path, 0, 0, true);
}
static bool cancelaccels = false;
static void checkaccels(const char *mp)
{
	if (!cancelaccels && accelp)
	{
		gtk_accel_map_foreach(NULL, clearaccels);
		if (g_file_test(accelp, G_FILE_TEST_EXISTS))
			gtk_accel_map_load(accelp);
	}
	cancelaccels = false;
}

static void checkset(const char *mp, char *set, void (*setfunc)(Win *, char *))
{
	if (!wins) return;
	for (int i = 0; i < wins->len; i++)
	{
		Win *lw = wins->pdata[i];
		char *us = getset(lw, set);
		if (!us) continue;

		bool changed = false;
		char **names = g_strsplit(us, ";", -1);
		for (char **name = names; *name; name++)
			if ((changed = !strcmp(mp, sfree(path2conf(*name))))) break;
		g_strfreev(names);

		if (changed)
			setfunc(lw, us);
	}
}
static void checkcss    (const char *mp) { checkset(mp, "usercss"    , setcss    ); }
static void checkscripts(const char *mp) { checkset(mp, "userscripts", setscripts); }

void setcontent(Win *win, char *namesstr, bool css)
{
	char **names = g_strsplit(namesstr ?: "", ";", -1);

	WebKitUserContentManager *cmgr =
		webkit_web_view_get_user_content_manager(win->kit);

	if (css)
		webkit_user_content_manager_remove_all_style_sheets(cmgr);
	else
		webkit_user_content_manager_remove_all_scripts(cmgr);

	for (char **name = names; *name; name++)
	{
		char *path = path2conf(*name);
		monitor(path, css ? checkcss : checkscripts); //even not exists

		char *str;
		if (g_file_test(path, G_FILE_TEST_EXISTS)
				&& g_file_get_contents(path, &str, NULL, NULL))
		{
			if (css)
				webkit_user_content_manager_add_style_sheet(cmgr,
					webkit_user_style_sheet_new(str,
							WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
							WEBKIT_USER_STYLE_LEVEL_USER,
							NULL, NULL));
			else
				webkit_user_content_manager_add_script(cmgr,
					webkit_user_script_new(str,
							WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
							WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
							NULL, NULL));

			g_free(str);
		}
		g_free(path);
	}
	g_strfreev(names);
}
void setcss    (Win *win, char *names) { setcontent(win, names, true ); }
void setscripts(Win *win, char *names) { setcontent(win, names, false); }

static void checkconf(const char *mp)
{
	if (!confpath)
	{
		confpath = path2conf("main.conf");
		monitor(confpath, checkconf);
	}

	if (!g_file_test(confpath, G_FILE_TEST_EXISTS))
	{
		if (mp) return;
		if (!conf)
			initconf(NULL);

		mkdirif(confpath);
		g_key_file_save_to_file(conf, confpath, NULL);
		return;
	}
	else if (!mp && conf) return; //from focuscb

	GKeyFile *new = g_key_file_new();
	GError *err = NULL;
	g_key_file_load_from_file(new, confpath, G_KEY_FILE_KEEP_COMMENTS, &err);
	if (err)
	{
		alert(err->message);
		g_error_free(err);
		if (!conf)
			initconf(NULL);
		return;
	}

	initconf(new);

	if (ctx)
	{
		webkit_web_context_set_tls_errors_policy(ctx,
				confbool("ignoretlserr") ?
				WEBKIT_TLS_ERRORS_POLICY_IGNORE :
				WEBKIT_TLS_ERRORS_POLICY_FAIL);
#if WEBKIT_MAJOR_VERSION > 2 || WEBKIT_MINOR_VERSION > 28
		webkit_website_data_manager_set_itp_enabled(
			webkit_web_context_get_website_data_manager(ctx), confbool("itp"));
#endif
	}

	if (ctx) proxy_settings_from_conf();

	if (!wins) return;

	sendeach(Cload, NULL);
	for (int i = 0; i < wins->len; i++)
		resetconf(wins->pdata[i], NULL, 1);
}


//@context
static void settitle(Win *win, const char *pstr)
{
	if (!pstr && win->crashed)
		pstr = "!! Web Process Crashed !!";

	bool bar = getsetbool(win, "addressbar");

	if (bar)
		gtk_label_set_text(win->lbl, pstr ?: URI(win));

	const char *wtitle = webkit_web_view_get_title(win->kit) ?: "";
	const char *title = pstr && !bar ? pstr : sfree(g_strconcat(
                webkit_web_view_is_controlled_by_automation(win->kit) ? "[Auto]" : "",
		win->tlserr ? "!TLS " : "",
		suffix            , *suffix      ? "| " : "",
		win->overset ?: "", win->overset ? "| " : "",
		wtitle, bar ? "" : " - ", bar && *wtitle ? NULL : FORDISP(URI(win)), NULL));

	gtk_window_set_title(win->win, title);
}
static void enticon(Win *win, const char *name); //declaration
static void pmove(Win *win, guint key); //declaration
static bool winlist(Win *win, guint type, cairo_t *cr); //declaration
static void _modechanged(Win *win)
{
	Modes last = win->lastmode;
	win->lastmode = win->mode;

	switch (last) {
	case Minsert:
		break;

	case Mfind:
	case Mopen:
	case Mopennew:
		gtk_widget_hide(win->entw);
		gtk_widget_grab_focus(win->kitw);
		break;

	case Mlist:
		if (win->lastx || win->lasty)
			motion(win, win->lastx, win->lasty);
		win->lastx = win->lasty = 0;

		gtk_widget_queue_draw(win->canvas);
		gdk_window_set_cursor(gdkw(win->winw), NULL);
		break;

	case Mpointer:
		if (win->mode != Mhint) win->pbtn = 0;
		gtk_widget_queue_draw(win->canvas);
		break;

	case Mhint:
		if (win->mode != Mpointer) win->pbtn = 0;
	case Mhintrange:
		GFA(win->hintdata, NULL);
		gtk_widget_queue_draw(win->canvas);
		send(win, Crm, NULL);
		break;

	case Mnormal:
		gtk_window_remove_accel_group(win->win, accelg);
		break;
	}

	//into
	switch (win->mode) {
	case Minsert:
		break;

	case Mfind:
		win->infind = false;
		if (win->crashed)
		{
			win->mode = Mnormal;
			break;
		}

	case Mopen:
	case Mopennew:
		enticon(win, NULL);

		if (win->mode != Mfind)
		{
			if (g_strcmp0(getset(NULL, "search"), getset(win, "search")))
				enticon(win, "system-search");
			else if (!getset(NULL, "search"))
				showmsg(win, "No search settings");
		}

		gtk_widget_show(win->entw);
		gtk_widget_grab_focus(win->entw);
		undo(win, &win->undo, &win->undo);
		break;

	case Mlist:
		winlist(win, 2, NULL);
		gtk_widget_queue_draw(win->canvas);
		break;

	case Mpointer:
		pmove(win, 0);
		break;

	case Mhint:
	case Mhintrange:
		if (win->crashed)
			win->mode = Mnormal;
		else
			send(win, win->com, sfree(g_strdup_printf("%c",
					win->pbtn > 1 || getsetbool(win, "hackedhint4js") ?
					'y' : 'n')));
		break;

	case Mnormal:
		gtk_window_add_accel_group(win->win, accelg);
		break;
	}
}
static void update(Win *win)
{
	if (win->lastmode != win->mode) _modechanged(win);

	switch (win->mode) {
	case Mnormal: break;
	case Minsert:
		settitle(win, "-- INSERT MODE --");
		break;

	case Mlist:
		settitle(win, "-- LIST MODE --");
		break;

	case Mpointer:
		if (win->link) goto normal;
		settitle(win, sfree(g_strdup_printf("-- POINTER MODE %d --", win->pbtn)));
		break;

	case Mhintrange:
		settitle(win, "-- RANGE MODE --");
		break;

	case Mhint:
	case Mopen:
	case Mopennew:
	case Mfind:
		settitle(win, NULL);
		break;
	}

	//normal mode
	if (win->mode != Mnormal) return;
normal:

	if (win->focusuri || win->link)
	{
		bool f = (win->usefocus && win->focusuri) || !win->link;
		char *title=(g_strconcat(f ? "Focus" : "Link",
				": ", FORDISP(f ? win->focusuri : win->link),
					NULL));
		settitle(win, title);
		g_free(title);
	}
	else
		settitle(win, NULL);
}
STATIC void tonormal(Win *win)
{
	win->mode = Mnormal;
	update(win);
}

STATIC void eval_javascript(Win *win, const char *script);//declaration

//@funcs for actions
STATIC bool run(Win *win, char* action, const char *arg); //declaration

static int formaturi(char **uri, char *key, const char *arg, char *spare)
{
	int checklen = 1;
	char *format;

	if      ((format = g_key_file_get_string(conf, "template", key, NULL))) ;
	else if ((format = g_key_file_get_string(conf, "raw"     , key, NULL))) ; //backward
	else if ((format = g_key_file_get_string(conf, "search"  , key, NULL) ?:
				g_strdup(spare)))
	{
		checklen = strlen(arg) ?: 1; //only search else are 1 even ""
		arg = sfree(g_uri_escape_string(arg, NULL, false));
	}
	else return 0;

	*uri = g_strdup_printf(format, arg);
	g_free(format);
	return checklen;
}

// when file:///tmp/foo%20bar refers to a file "/tmp/foo%20bar",
// webkit_web_view_load_uri tries to load "/tmp/foo bar" which fails.
// resolved_path is expected to be a malloc'ed string with prefix
// "file://". It may be freed and a new string may be
// malloc'ed. Returns the new string if escpaed..
static char *percent_escape_percent_file_url(char *resolved_path)
{
    g_autoptr(GUri) suri = g_uri_parse(resolved_path, SOUP_HTTP_URI_FLAGS, NULL);
    const char *fragment = suri ? g_uri_get_fragment(suri) : 0;

    GFile *gfile = g_file_new_for_commandline_arg(resolved_path);
    gchar *fileURL = g_file_get_uri(gfile);
    g_object_unref(gfile);
    g_free(resolved_path);

    if (fragment) {
      char *b = g_strdup_printf("%s#%s", fileURL, fragment);
      g_free(fileURL);
      fileURL = b;
    }
    return fileURL;

  GError *err = NULL;
  char *ret = g_filename_to_uri(resolved_path, NULL, &err);
  if (err)  {
    fprintf_gerror(stderr, err, "g_filename_to_uri(%s) failed\n", resolved_path);
    return resolved_path;
  }
  fprintf(stderr, "resolved %s =>\n%s\n", resolved_path, ret);
  g_free(resolved_path);
  return ret;

	if (resolved_path && g_strrstr(resolved_path, "%")) {
		while (1) {
			static GRegex *percent;
			GError *err;
			if (percent == NULL) {
				err = NULL;
				percent = g_regex_new("%", 0, 0, &err);
				if (percent == NULL) {
					fprintf_gerror(stderr, err, "g_regex_new failed\n");
					break;
				}
			}
			err = NULL;
			char *tmp= g_regex_replace_literal(percent, resolved_path, -1, 0, "%25", 0, &err);
			if (err) {
				fprintf_gerror(stderr, err, "failed to perform regexp replace\n");
				break;
			}
			g_free(resolved_path);
			resolved_path = tmp;
			break;
		}
	}
	return resolved_path;
}


//fwd
static gboolean maybe_reuse(Win *curwin, const char *uri, gboolean new_if_reuse_fails);
static Win *maybe_newwin(const char *uri, Win *cbwin, Win *caller, int back);
static void reusemode(Win *win, const char *arg);

static void _openuri(Win *win, const char *str, Win *caller)
{
	win->userreq = true;
	if (!str || !*str) str = APP":blank";

	if (
		g_str_has_prefix(str, "http:") ||
		g_str_has_prefix(str, "https:") ||
		g_str_has_prefix(str, APP":") ||
//		g_str_has_prefix(str, "file:") ||
		g_str_has_prefix(str, "data:") ||
		g_str_has_prefix(str, "blob:") ||
		g_str_has_prefix(str, "about:") ||
		g_str_has_prefix(str, "webkit:") ||
		g_str_has_prefix(str, "inspector:")
	) {
		if (maybe_reuse(win, str, false)) return;
		setatom(win, atomUri, URI(win));
		webkit_web_view_load_uri(win->kit, str);
		return;
	}

	if (str != getent(win))
		setent(win, str ?: "");

	if (g_str_has_prefix(str, "javascript:")) {
		eval_javascript(win, str+11);
		return;
	}

	char *uri = NULL;
	int checklen = 0;
	char **stra = g_strsplit(str, " ", 2);

	if (*stra && stra[1] &&
			(checklen = formaturi(&uri, stra[0], stra[1], NULL)))
	{
		GFA(win->lastsearch, g_strdup(stra[1]))
		goto out;
	}

	if (g_str_has_prefix(str, "file://")) {
		uri = g_strdup(str);
		uri = percent_escape_percent_file_url(uri);
		goto out;
	}

	if (g_str_has_prefix(str, "/")) {
		uri = g_strconcat("file://", str, NULL);
		uri = percent_escape_percent_file_url(uri);
		goto out;
	}

	static regex_t *ipadr = NULL;
	if (!ipadr) {
		ipadr= g_new(regex_t, 1);
		int err = regcomp(ipadr, "^(([01]?[0-9]?[0-9]|2([0-4][0-9]|5[0-5]))\\.){3}([01]?[0-9]?[0-9]|2([0-4][0-9]|5[0-5]))(:[0-9]+)?(/.*)?",
			REG_EXTENDED | REG_NOSUB);
		if (err != 0) {
			fprintf(stderr, "Could not compile regex %s\n", "");
			exit(1);
		}
	}
	if ((g_ascii_strncasecmp(str, "localhost", 8) == 0) ||
	    (regexec(ipadr, str, 0, NULL, 0) == 0)) {
		uri = g_strconcat("http://", str, NULL);
		goto out;
	}

	static regex_t *url;
	if (!url)
	{
		url = g_new(regex_t, 1);
		regcomp(url,
				"^([a-zA-Z0-9-]{1,63}\\.)+[a-z]{2,6}(/.*)?$",
				REG_EXTENDED | REG_NOSUB);
	}

	char *dsearch;
	if (regexec(url, str, 0, NULL, 0) == 0)
		uri = g_strdup_printf("http://%s", str);
	else if ((dsearch = getset(caller ?: win, "search")) && *dsearch)
	{
		checklen = formaturi(&uri, dsearch, str, dsearch);
		GFA(win->lastsearch, g_strdup(str))
	} else {
		showmsg(win, "Invalid URI");
		if (uri) g_free(uri);
		g_strfreev(stra);
		return;
	}

	if (!uri) uri = g_strdup(str);
out:
	g_strfreev(stra);

	int max;
	if (checklen > 1 && (max = getsetint(win, "searchstrmax")) && checklen > max)
		_showmsg(win, g_strdup_printf("Input Len(%d) > searchstrmax=%d",
					checklen, max));
	else
	{
		if (!maybe_reuse(win, uri, false))
		webkit_web_view_load_uri(win->kit, uri);

		GUri *suri = g_uri_parse(uri, SOUP_HTTP_URI_FLAGS, NULL);
		if (suri)
			g_uri_unref(suri);
		else
			_showmsg(win, g_strdup_printf("Invalid URI: %s", uri));
	}

	g_free(uri);
}
static void openuri(Win *win, const char *str)
{ _openuri(win, str, NULL); }

static Spawn *spawnp(Win *win,
		const char *action, const char *cmd, const char *path, bool once)
{
	Spawn ret = {win, g_strdup(action), g_strdup(cmd), g_strdup(path), once};
	return g_memdup(&ret, sizeof(Spawn));
}
static void spawnfree(Spawn* s, bool force)
{
	if (!s || (!s->once && !force)) return;
	g_free(s->action);
	g_free(s->cmd);
	g_free(s->path);
	g_free(s);
}
static void envspawn(Spawn *p,
		bool iscallback, char *result, char *piped, gsize plen)
{
	Win *win = p->win;
	if (!isin(wins, win)) goto out;

	char **argv;
	if (p->cmd)
	{
		GError *err = NULL;
		if (*p->action == 's' && 'h' == p->action[1])
		{
			argv = g_new0(char*, 4);
			argv[0] = g_strdup("sh");
			argv[1] = g_strdup("-c");
			argv[2] = g_strdup(p->cmd);
		}
		else if (!g_shell_parse_argv(p->cmd, NULL, &argv, &err))
		{
			showmsg(win, err->message);
			g_error_free(err);
			goto out;
		}
	} else {
		argv = g_new0(char*, 2);
		argv[0] = g_strdup(p->path);
	}

	if (getsetbool(win, "spawnmsg"))
		_showmsg(win, g_strdup_printf("spawn: %s", p->cmd ?: p->path));

	char *dir = p->cmd ?
		(p->path ? g_strdup(p->path) : path2conf("menu"))
		: g_path_get_dirname(p->path);

	char **envp = g_get_environ();
	envp = g_environ_setenv(envp, "ISCALLBACK",
			iscallback ? "1" : "0", true);
	envp = g_environ_setenv(envp, "RESULT", result ?: "", true);
	//for backward compatibility
	envp = g_environ_setenv(envp, "JSRESULT", result ?: "", true);

	char buf[9];
	snprintf(buf, 9, "%d", wins->len);
	envp = g_environ_setenv(envp, "WINSLEN", buf, true);
	envp = g_environ_setenv(envp, "SUFFIX" , *suffix ? suffix : "/", true);

	if (!win->winid)
		win->winid = g_strdup_printf("%"G_GUINT64_FORMAT,
				webkit_web_view_get_page_id(win->kit));
	envp = g_environ_setenv(envp, "WINID"  , GET_WINID(win) /*win->winid*/, true);

	envp = g_environ_setenv(envp, "CURRENTSET", win->overset ?: "", true);
	envp = g_environ_setenv(envp, "URI"    , URI(win), true);
	envp = g_environ_setenv(envp, "LINK_OR_URI", URI(win), true);
	envp = g_environ_setenv(envp, "DLDIR"  , sfree(dldir(win)), true);
	envp = g_environ_setenv(envp, "CONFDIR", sfree(path2conf(NULL)), true);
	envp = g_environ_setenv(envp, "CANBACK",
			webkit_web_view_can_go_back(   win->kit) ? "1" : "0", true);
	envp = g_environ_setenv(envp, "CANFORWARD",
			webkit_web_view_can_go_forward(win->kit) ? "1" : "0", true);
	int WINX, WINY, WIDTH, HEIGHT;
	gtk_window_get_position(win->win, &WINX, &WINY);
	gtk_window_get_size(win->win, &WIDTH, &HEIGHT);
#define Z(x) \
	snprintf(buf, 9, "%d", x); \
	envp = g_environ_setenv(envp, #x, buf, true);

	Z(WINX) Z(WINY) Z(WIDTH) Z(HEIGHT)
#undef Z

	const char *title = webkit_web_view_get_title(win->kit);
	if (!title) title = URI(win);
	envp = g_environ_setenv(envp, "TITLE" , title, true);
	envp = g_environ_setenv(envp, "LABEL_OR_TITLE" , title, true);

	envp = g_environ_setenv(envp, "FOCUSURI", win->focusuri ?: "", true);

	envp = g_environ_setenv(envp, "LINK", win->link ?: "", true);
	if (win->link)
	{
		envp = g_environ_setenv(envp, "LINK_OR_URI", win->link, true);
		envp = g_environ_setenv(envp, "LABEL_OR_TITLE" , win->link, true);
	}
	envp = g_environ_setenv(envp, "LINKLABEL", win->linklabel ?: "", true);
	if (win->linklabel)
	{
		envp = g_environ_setenv(envp, "LABEL_OR_TITLE" , win->linklabel, true);
	}

	envp = g_environ_setenv(envp, "MEDIA", win->media ?: "", true);
	envp = g_environ_setenv(envp, "IMAGE", win->image ?: "", true);

	envp = g_environ_setenv(envp, "MEDIA_IMAGE_LINK",
			win->media ?: win->image ?: win->link ?: "", true);

	char *cbtext;
	cbtext = gtk_clipboard_wait_for_text(
			gtk_clipboard_get(GDK_SELECTION_PRIMARY));
	envp = g_environ_setenv(envp, "PRIMARY"  , cbtext ?: "", true);
	envp = g_environ_setenv(envp, "SELECTION", cbtext ?: "", true);
	g_free(cbtext);
	cbtext = gtk_clipboard_wait_for_text(
			gtk_clipboard_get(GDK_SELECTION_SECONDARY));
	envp = g_environ_setenv(envp, "SECONDARY", cbtext ?: "", true);
	g_free(cbtext);
	cbtext = gtk_clipboard_wait_for_text(
			gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
	envp = g_environ_setenv(envp, "CLIPBOARD", cbtext ?: "", true);
	g_free(cbtext);

	int input;
	GPid child_pid;
	GError *err = NULL;
	if (piped ?
			!g_spawn_async_with_pipes(
				dir, argv, envp,
				G_SPAWN_SEARCH_PATH,
				NULL,
				NULL,
				&child_pid,
				&input,
				NULL,
				NULL,
				&err)
			:
			!g_spawn_async(
				dir, argv, envp,
				p->cmd ? G_SPAWN_SEARCH_PATH : G_SPAWN_DEFAULT,
				NULL, NULL, &child_pid, &err))
	{
		showmsg(win, err->message);
		g_error_free(err);
	}
	else if (piped)
	{
		GIOChannel *io = g_io_channel_unix_new(input);
		g_io_channel_set_encoding(io, NULL, NULL);

		if (G_IO_STATUS_NORMAL !=
				g_io_channel_write_chars(
					io, piped, plen, NULL, &err))
		{
			showmsg(win, err->message);
			g_error_free(err);
		}
		g_io_channel_unref(io);
		close(input);
	}

	g_spawn_close_pid(child_pid);

	g_strfreev(envp);
	g_strfreev(argv);
	g_free(dir);

out:
	spawnfree(p, false);
}

static void scroll(Win *win, int x, int y)
{
	GdkEventScroll *es = kitevent(win, false, GDK_SCROLL);

	es->send_event = false; //for multiplescroll
	//es->time   = GDK_CURRENT_TIME;
	es->direction =
		x < 0 ? GDK_SCROLL_LEFT :
		x > 0 ? GDK_SCROLL_RIGHT :
		y < 0 ? GDK_SCROLL_UP :
		        GDK_SCROLL_DOWN;

	es->delta_x = x;
	es->delta_y = y;

	es->x = win->px;
	es->y = win->py;
	if (!es->x && !es->y)
	{
		es->x = gtk_widget_get_allocated_width(win->kitw) / 2;
		es->y = gtk_widget_get_allocated_height(win->kitw) / 2;
	}

	putevent(es);
}
void pmove(Win *win, guint key)
{
	//GDK_KEY_Down
	double ww = gtk_widget_get_allocated_width(win->kitw);
	double wh = gtk_widget_get_allocated_height(win->kitw);
	if (!win->px && !win->py)
	{
		win->px = ww * 3 / 7;
		win->py = wh * 1 / 3;
	}
	if (key == 0)
		win->lastdelta = MIN(ww, wh) / 7;

	guint lkey = win->lastkey;
	if (
		(key  == GDK_KEY_Up   && lkey == GDK_KEY_Down) ||
		(lkey == GDK_KEY_Up   && key  == GDK_KEY_Down) ||
		(key  == GDK_KEY_Left && lkey == GDK_KEY_Right) ||
		(lkey == GDK_KEY_Left && key  == GDK_KEY_Right)
	)
		win->lastdelta /= 2;

	guint32 unit = MAX(10, webkit_settings_get_default_font_size(win->set)) / 3;
	if (win->lastdelta < unit) win->lastdelta = unit;
	double d = win->lastdelta;
	if (key == GDK_KEY_Up   ) win->py -= d;
	if (key == GDK_KEY_Down ) win->py += d;
	if (key == GDK_KEY_Left ) win->px -= d;
	if (key == GDK_KEY_Right) win->px += d;

	win->px = CLAMP(win->px, 0, ww);
	win->py = CLAMP(win->py, 0, wh);

	win->lastdelta *= .9;
	win->lastkey = key;

	motion(win, win->px, win->py);

	gtk_widget_queue_draw(win->canvas);
}
static void altcur(Win *win, double x, double y)
{
	static GdkCursor *cur;
	if (!cur) cur = gdk_cursor_new_for_display(
			gdk_display_get_default(), GDK_CENTER_PTR);
	if (x + y == 0)
		gdk_window_set_cursor(gdkw(win->kitw), cur);
	else if (gdk_window_get_cursor(gdkw(win->kitw)) == cur)
		motion(win, x, y); //clear
}
static void putkey(Win *win, guint key)
{
	GdkEventKey *ek = kitevent(win, false, GDK_KEY_PRESS);
	ek->send_event = true;
//	ek->time   = GDK_CURRENT_TIME;
	ek->keyval = key;
//	ek->state  = ek->state & ~GDK_MODIFIER_MASK;
	putevent(ek);
}

static void command(Win *win, const char *cmd, const char *arg)
{
	cmd = sfree(g_strdup_printf(cmd, arg));
	_showmsg(win, g_strdup_printf("Run '%s'", cmd));
	GError *err = NULL;
	if (!g_spawn_command_line_async(cmd, &err))
		alert(err->message), g_error_free(err);
}

static void openeditor(Win *win, const char *path, char *editor)
{
	command(win, editor ?: getset(win, "editor") ?: MIMEOPEN, path);
}
static void openconf(Win *win, bool shift)
{
	char *path;
	char *editor = NULL;

	const char *uri = URI(win);
	if (g_str_has_prefix(uri, APP":main"))
	{
		if (shift)
			path = confpath;
		else {
			path = mdpath;
			editor = getset(win, "mdeditor");
		}
	}
	else if (!shift && g_str_has_prefix(uri, APP":"))
	{
		showmsg(win, "No config");
		return;
	} else {
		path = confpath;
		if (!shift)
		{
			char *name = sfree(g_strdup_printf("uri:^%s", sfree(regesc(uri))));
			if (!g_key_file_has_group(conf, name))
				append(path, sfree(g_strdup_printf("\n[%s]", name)));
		}
	}

	openeditor(win, path, editor);
}

static void present(Win *win)
{
	gtk_window_present(win->win);
	if (confbool("pointerwarp") &&
			keyboard() == gtk_get_current_event_device())
	{
		int px, py;
		gdk_device_get_position(pointer(), NULL, &px, &py);
		GdkRectangle rect;
		gdk_window_get_frame_extents(gdkw(win->winw), &rect);

		gdk_device_warp(pointer(),
				gdk_display_get_default_screen(
					gdk_window_get_display(gdkw(win->winw))),
				CLAMP(px, rect.x, rect.x + rect.width  - 1),
				CLAMP(py, rect.y, rect.y + rect.height - 1));
	}
}
static int inwins(Win *win, GSList **list, bool onlylen)
{
	guint len = 0;
	GdkWindow  *dw = gdkw(win->winw);
	GdkDisplay *dd = gdk_window_get_display(dw);
	for (int i = 0; i < wins->len; i++)
	{
		Win *lw = wins->pdata[i];
		if (lw == win) continue;

		GdkWindow *ldw = gdkw(lw->winw);

		if (gdk_window_get_state(ldw) & GDK_WINDOW_STATE_ICONIFIED)
			continue;

#ifdef GDK_WINDOWING_X11
		if (GDK_IS_X11_DISPLAY(dd) &&
			(gdk_x11_window_get_desktop(dw) !=
					gdk_x11_window_get_desktop(ldw)))
			continue;
#endif

		len++;
		if (!onlylen)
			*list = g_slist_append(*list, lw);
	}
	return len;
}
static void nextwin(Win *win, bool next)
{
	guint index;
	gboolean found = g_ptr_array_find(wins, win, &index);
	if (found && wins->len > 1) {
		int newindex;
		if (next)
			newindex = index == wins->len - 1 ?  0 : index + 1;
		else
			newindex = index == 0 ? wins->len - 1 : index - 1;
		g_assert(0 <= newindex < wins->len);
		Win *newwin = g_ptr_array_index(wins, newindex);
		present(newwin); //present first to keep focus on xfce
		//if (!plugto) gdk_window_lower(gdkw(win->winw));
		newwin->lastx = win->lastx;
		newwin->lasty = win->lasty;
 	}
}
static bool quitnext(Win *win, bool next)
{
	if (inwins(win, NULL, true) < 1)
	{
		if (win->crashed || !strcmp(APP":main", URI(win)))
			return run(win, "quit", NULL);

		run(win, "showmainpage", NULL);
		showmsg(win, "Last Window");
		return false;
	}
	if (next)
		run(win, "nextwin", NULL);
	else
		run(win, "prevwin", NULL);
	return run(win, "quit", NULL);
}
static void arcrect(cairo_t *cr, double r,
		double rx, double ry, double rr,  double rb)
{
	cairo_new_sub_path(cr);
	cairo_arc(cr, rr - r, ry + r, r, M_PI / -2, 0         );
	cairo_arc(cr, rr - r, rb - r, r, 0        , M_PI / 2  );
	cairo_arc(cr, rx + r, rb - r, r, M_PI / 2 , M_PI      );
	cairo_arc(cr, rx + r, ry + r, r, M_PI     , M_PI * 1.5);
	cairo_close_path(cr);
}
bool winlist(Win *win, guint type, cairo_t *cr)
//type: 0: none 1:present 2:setcursor 3:close, and GDK_KEY_Down ... GDK_KEY_Right
{
	//for window select mode
	if (win->mode != Mlist) return false;
	GSList *actvs = NULL;
	guint len = inwins(win, &actvs, false);
	if (len < 1)
		return false;

	double w = gtk_widget_get_allocated_width(win->kitw);
	double h = gtk_widget_get_allocated_height(win->kitw);

	double yrate = h / w;

	double yunitd = sqrt(len * yrate);
	double xunitd = yunitd / yrate;
	int yunit = yunitd;
	int xunit = xunitd;

	if (yunit * xunit >= len)
		; //do nothing
	else if ((yunit + 1) * xunit >= len && (xunit + 1) * yunit >= len)
	{
		if (yunit > xunit)
			xunit++;
		else
			yunit++;
	}
	else if ((yunit + 1) * xunit >= len)
		yunit++;
	else if ((xunit + 1) * yunit >= len)
		xunit++;
	else {
		xunit++;
		yunit++;
	}

	switch (type) {
	case GDK_KEY_Up:
	case GDK_KEY_Down:
	case GDK_KEY_Left:
	case GDK_KEY_Right:
		if (win->scrlf)
			type = 0;
		win->scrlf = false;
		if (win->cursorx > 0 && win->cursory > 0)
			break;
	case 2:
		win->scrlf = false;
		win->cursorx = xunit / 2.0 - .5;
		win->cursory = yunit / 2.0 - .5;
		win->cursorx++;
		win->cursory++;
		if (type == 2)
			return true;
	}

	switch (type) {
	case GDK_KEY_Page_Up:
		if ((win->scrlf || win->scrlcur == 0) &&
				--win->scrlcur < 1) win->scrlcur = len;
		win->scrlf = true;
		return true;
	case GDK_KEY_Page_Down:
		if ((win->scrlf || win->scrlcur == 0) &&
				++win->scrlcur > len) win->scrlcur = 1;
		win->scrlf = true;
		return true;

	case GDK_KEY_Up:
		if (--win->cursory < 1) win->cursory = yunit;
		return true;
	case GDK_KEY_Down:
		if (++win->cursory > yunit) win->cursory = 1;
		return true;
	case GDK_KEY_Left:
		if (--win->cursorx < 1) win->cursorx = xunit;
		return true;
	case GDK_KEY_Right:
		if (++win->cursorx > xunit) win->cursorx = 1;
		return true;
	}

	double uw = w / xunit;
	double uh = h / yunit;

	if (cr)
	{
		cairo_set_source_rgba(cr, .4, .4, .4, win->scrlf ? 1 : .6);
		cairo_paint(cr);
	}

	double px, py;
	gdk_window_get_device_position_double(
			gdkw(win->kitw), pointer(), &px, &py, NULL);

	int count = 0;
	bool ret = false;
	GSList *crnt = actvs;
	for (int yi = 0; yi < yunit; yi++) for (int xi = 0; xi < xunit; xi++)
	{
		if (!crnt) break;
		Win *lw = crnt->data;
		crnt = crnt->next;
		count++;
		if (win->scrlf)
		{
			if (count != win->scrlcur) continue;
			win->cursorx = xi + 1;
			win->cursory = yi + 1;
		}

		double lww = gtk_widget_get_allocated_width(lw->kitw);
		double lwh = gtk_widget_get_allocated_height(lw->kitw);

		if (lww == 0 || lwh == 0) lww = lwh = 9;

		double scale = MIN(uw / lww, uh / lwh) * (1.0 - 1.0/(pow(MAX(yunit, xunit), 2) + 1));
		double tw = lww * scale;
		double th = lwh * scale;
		//pos is double makes blur
		int tx = xi * uw + (uw - tw) / 2;
		int ty = yi * uh + (uh - th) / 2;
		double tr = tx + tw;
		double tb = ty + th;

		if (win->scrlf)
		{
			scale = 1;
			tx = ty = 2;
			tr = w - 2;
			tb = h - 2;
		}

		bool pin = win->cursorx + win->cursory == 0 ?
			px > tx && px < tr && py > ty && py < tb :
			xi + 1 == win->cursorx && yi + 1 == win->cursory;
		ret = ret || pin;

		if (!pin)
		{
			if (!cr) continue;
		}
		else if (type == 1) //present
			present(lw);
		else if (type == 3) //close
		{
			run(lw, "quit", NULL);
			if (len > 1)
			{
				if (count == len)
					win->scrlcur = len - 1;
				gtk_widget_queue_draw(win->canvas);
			}
			else
				tonormal(win);
		} else {
			settitle(win, sfree(g_strdup_printf("LIST| %s",
					webkit_web_view_get_title(lw->kit))));

			win->cursorx = xi + 1;
			win->cursory = yi + 1;
			win->scrlcur = count;
		}

		if (!cr) goto out;

		cairo_reset_clip(cr);
		arcrect(cr, 4 + th / 66.0, tx, ty, tr, tb);
		if (pin)
		{
			colorf(lw, cr, 1);
			cairo_set_line_width(cr, 6.0);
			cairo_stroke_preserve(cr);
		}
		cairo_clip(cr);

		if (/*!plugto
				&& */gtk_widget_get_visible(lw->kitw)
				&& gtk_widget_is_drawable(lw->kitw)
		) {
			if (win->scrlf)
			{
				tx = MAX(tx, (w - lww) / 2);
				ty = MAX(ty, (h - lwh) / 2);
			}
			cairo_translate(cr, tx, ty);
			cairo_scale(cr, scale, scale);
			gtk_widget_draw(lw->kitw, cr);
			cairo_scale(cr, 1 / scale, 1 / scale);
			cairo_translate(cr, -tx, -ty);
		}
		else
		{
			cairo_set_source_rgba(cr, .1, .0, .2, .4);
			cairo_paint(cr);
		}
	}
out:

	g_slist_free(actvs);

	if (!ret)
		update(win);

	return ret;
}

static void addlink(Win *win, const char *title, const char *uri)
{
	char *str = NULL;
	preparemd();
	if (uri)
	{
		char *escttl;
		if (title && *title)
			escttl = _escape(sfree(g_markup_escape_text(title, -1)), "[]");
		else
			escttl = g_strdup(uri);

		char *fav = g_strdup_printf(APP":f/%s",
				sfree(g_uri_escape_string(uri, "/:=&", true)));

		char *items = getset(win, "linkdata") ?: "tu";
		int i = 0;
		const char *as[9] = {""};
		for (char *c = items; *c && i < 9; c++)
			as[i++] =
				*c == 't' ? escttl:
				*c == 'u' ? uri:
				*c == 'f' ? fav:
				"";
		str = g_strdup_printf(getset(win, "linkformat"),
				as[0], as[1], as[2], as[3], as[4], as[5], as[6], as[7], as[8]);

		if (!g_utf8_validate(str, -1, NULL))
			GFA(str, g_utf8_make_valid(str, -1))

		g_free(fav);
		g_free(escttl);
	}
	append(mdpath, str);
	g_free(str);

	showmsg(win, "Added");
}

#define findtxt(win) webkit_find_controller_get_search_text(win->findct)
static void find(Win *win, const char *arg, bool next, bool insensitive)
{
	const char *u = insensitive ? "" : arg;
	do if (g_ascii_isupper(*u)) break; while (*++u);
	webkit_find_controller_search(win->findct, arg
		, (*u   ? 0 : WEBKIT_FIND_OPTIONS_CASE_INSENSITIVE)
		| (next ? 0 : WEBKIT_FIND_OPTIONS_BACKWARDS)
		| WEBKIT_FIND_OPTIONS_WRAP_AROUND
		, G_MAXUINT);
	win->infind = true;
	GFA(win->lastsearch, NULL)
}
static void findnext(Win *win, bool next)
{
	if (findtxt(win))
	{
		if (next)
			webkit_find_controller_search_next(win->findct);
		else
			webkit_find_controller_search_previous(win->findct);
	}
	else if (win->lastsearch)
	{
		setent(win, win->lastsearch);
		find(win, win->lastsearch, next, true);
	}
	else
	{
		showmsg(win, "No search words");
		return;
	}
	senddelay(win, Cfocus, NULL);
}

static void jscb(GObject *po, GAsyncResult *pres, gpointer p)
{
	GError *err = NULL;
	WebKitJavascriptResult *res =
		webkit_web_view_run_javascript_finish(WEBKIT_WEB_VIEW(po), pres, &err);

	char *resstr = NULL;
	if (res)
	{
#if V22
		resstr = jsc_value_to_string(
				webkit_javascript_result_get_js_value(res));
#else
		JSValueRef jv = webkit_javascript_result_get_value(res);
		JSGlobalContextRef jctx =
			webkit_javascript_result_get_global_context(res);

		if (JSValueIsString(jctx, jv))
		{
			JSStringRef jstr = JSValueToStringCopy(jctx, jv, NULL);
			gsize len = JSStringGetMaximumUTF8CStringSize(jstr);
			resstr = g_malloc(len);
			JSStringGetUTF8CString(jstr, resstr, len);
			JSStringRelease(jstr);
		}
		else
			resstr = g_strdup("unsupported return value");
#endif
		webkit_javascript_result_unref(res);
	}
	else
	{
		resstr = g_strdup(err->message);
		g_error_free(err);
	}

	envspawn(p, true, resstr, NULL, 0);
	g_free(resstr);
}
static void resourcecb(GObject *srco, GAsyncResult *res, gpointer p)
{
	gsize len;
	guchar *data = webkit_web_resource_get_data_finish(
			(WebKitWebResource *)srco, res, &len, NULL);

	envspawn(p, true, NULL, (char *)data, len);
	g_free(data);
}
#if WEBKIT_CHECK_VERSION(2, 20, 0)
static void cookiescb(GObject *cm, GAsyncResult *res, gpointer p)
{
	char *header = NULL;
	GList *gl = webkit_cookie_manager_get_cookies_finish(
				(WebKitCookieManager *)cm, res, NULL);
	if (gl)
	{
		header = soup_cookies_to_cookie_header((GSList *)gl);
		g_list_free_full(gl, (GDestroyNotify)soup_cookie_free);
	}

	envspawn(p, true, header ?: "", NULL, 0);
	g_free(header);
}
#endif

//textlink
static char *tlpath;
static Win  *tlwin;
static void textlinkcheck(const char *mp)
{
	if (!isin(wins, tlwin)) return;
	send(tlwin, Ctlset, tlpath);
}
static void textlinkon(Win *win)
{
	run(win, "openeditor", tlpath);
	tlwin = win;
}
static void textlinktry(Win *win)
{
	tlwin = NULL;
	if (!tlpath)
	{
		tlpath = g_build_filename(
			g_get_user_data_dir(), fullname, "textlink.txt", NULL);
		monitor(tlpath, textlinkcheck);
	}
	send(win, Ctlget, tlpath);
}

const char *cookiepolicystring(WebKitCookieAcceptPolicy pol)
{
	switch(pol) {
	case WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS: return "ALWAYS";
	case WEBKIT_COOKIE_POLICY_ACCEPT_NEVER: return "NEVER";
	case WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY:
		return "NO_THIRD_PARTY";
	default: return "UNKNOWN";
	}
}

enum cookie_policy_action {
	COOKIES_ON, COOKIES_OFF, COOKIES_STATUS, COOKIES_CYCLE
};

struct cookie_policy_action_data {
	Win *win;
	enum cookie_policy_action action;
};

void
togglecookiepolicycb(GObject *mgr, GAsyncResult *res, gpointer _data)
{
	int no_set = 0;
	struct cookie_policy_action_data * data = (struct cookie_policy_action_data *) _data;
	enum cookie_policy_action action = data->action;
	Win *win = data->win;
	WebKitCookieAcceptPolicy new, current =
		webkit_cookie_manager_get_accept_policy_finish(WEBKIT_COOKIE_MANAGER(mgr), res, NULL);
	switch(action) {
	case COOKIES_STATUS:
		no_set = 1;
		break;
	case COOKIES_ON:
		new = WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS;
		no_set = (current == WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS);
		break;
	case COOKIES_OFF:
		new = WEBKIT_COOKIE_POLICY_ACCEPT_NEVER;
		no_set = (current == WEBKIT_COOKIE_POLICY_ACCEPT_NEVER);
		break;
	case COOKIES_CYCLE:
		switch(current) {
		// TODO: must clear out cookie from being sent
		case WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS: /* 0 */
			new = WEBKIT_COOKIE_POLICY_ACCEPT_NEVER;
			break;
		case WEBKIT_COOKIE_POLICY_ACCEPT_NEVER: /* 1 */
			new = WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY;
			break;
		case WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY: /* 2 */
			new = WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS;
			break;
		default:
			new = WEBKIT_COOKIE_POLICY_ACCEPT_NEVER;
			fprintf(stderr,
				"unknown current cookie accept policy %d\n",
				current);
		}
		break;
	default:
		fprintf(stderr, "ignoring unknown cookies action %d\n",
			action);
		return;
	}
	char buf[128];
	if (action == COOKIES_STATUS)
		snprintf(buf, sizeof(buf), "Cookie policy set to %s",
			 cookiepolicystring(current));
	else if (no_set)
		snprintf(buf, sizeof(buf), "Cookie policy set to %s (Unchanged)",
			 cookiepolicystring(current));
	else
		snprintf(buf, sizeof(buf), "Cookie policy changed from  %s to %s",
			 cookiepolicystring(current),
			 cookiepolicystring(new));
	showmsg((Win *)win, buf);
	fprintf(stderr, "%s\n", buf);
	if (!no_set)
		webkit_cookie_manager_set_accept_policy(WEBKIT_COOKIE_MANAGER(mgr), new);
}

#include <utime.h>
static void dltimestamp(const char *path, WebKitURIResponse *res, WebKitURIRequest *req)
{
	const char *lmtimestamp = NULL;
	char *header_dump = NULL;
	if (req || res) {
		header_dump = g_strconcat(path, ".headers", NULL);
		FILE *stream = fopen(header_dump, "w");
		if (stream) {
		if (req)
			fprintf(stream, "--> dlfin %s\n\n", webkit_uri_request_get_uri(req));
		else
			fprintf(stream, "--> dlfin %s\n\n", webkit_uri_response_get_uri(res));
		if (req) {
			print_headers(webkit_uri_request_get_http_headers(req), stream, "---> dlfin request\n\n");
			fprintf(stream, "\n");
		}
		if (res) {
			print_headers(webkit_uri_response_get_http_headers(res), stream, "---> dlfin response\n\n");
		}
		fclose(stream);
		fprintf(stderr, "dlfin: wrote headers to %s\n", header_dump);
		} else {
		  int saved_errno = errno;
		  fprintf(stderr, "dltimestamp: failed: %s: %s\n", header_dump, g_strerror(saved_errno));
		}
		g_free(header_dump);
	}

	if (res) {
		lmtimestamp = soup_message_headers_get_one(webkit_uri_response_get_http_headers(res), "Last-Modified");
	}
	if (lmtimestamp) {
		fprintf(stderr, "timestamp %s to %s\n", path, lmtimestamp);
		struct tm tmbuf;
		memset(&tmbuf, 0, sizeof(tmbuf));
		if (((char *) strptime(lmtimestamp,
				       "%A, %d %B %Y %H:%M:%S %Z",
				       &tmbuf)) == NULL) {
			fprintf(stderr, "strptime failed on %s.\n",
				lmtimestamp);
		} else {
			time_t t = mktime(&tmbuf);
			if (t == -1) {
				perror("mktime failed");
				abort();
			}
			const struct utimbuf buf = { t , t };
			struct stat info;
			stat(path, &info);
			if (t < info.st_mtime)
				g_utime(path, &buf);
		}
	}
}

// Return a string denoting the uri to a destination file to which the
// contents of the given URI should be saved.  This string must be
// freed by the caller.  If the URI ends with a slash, append an
// "index.html" basename.  Create all leading directories.  If the
// immediate parent directory of the target happens to be a file,
// rename that file out of the way by appending a ".html" to its name
// before creating the directory. Return NULL on any failure. Does not
// check if the destination file exists.
char *
make_savepath(const char *uri, const char *basedir)
{
	GUri *souprequri = g_uri_parse(uri, SOUP_HTTP_URI_FLAGS, NULL);
	if (!souprequri) {
		fprintf(stderr, "make_savepath: bad uri: %s\n", uri);
		return NULL;
	}
	if (strcmp(g_uri_get_scheme(souprequri), "file") == 0 ||
	    strcmp(g_uri_get_scheme(souprequri), "about") == 0 ||
	    strcmp(g_uri_get_scheme(souprequri), "wyeb") == 0)
	  {
		fprintf(stderr, "make_savepath: refusing to make a path for a file url %s\n", uri);
		//g_uri_unref(souprequri);
		return NULL;
	}

	const char *requripath = g_uri_get_path(souprequri);
	const char *requrihost = g_uri_get_host(souprequri);
	char *requriquery = (char *)g_uri_get_query(souprequri);
	char *requrifragment = (char *)g_uri_get_fragment(souprequri);
	if (requriquery) requriquery = g_strconcat("?", requriquery, NULL);
	if (requrifragment) requrifragment = g_strconcat("#", requrifragment, NULL);
	char *subpath = g_strconcat(requripath ?: "",
				    requriquery ?: "",
				    requrifragment ?: "",
				    NULL);

	char *path = g_build_filename(basedir ?: "",
				      requrihost ?: "",
				      subpath ?: "",
				      NULL);
	g_free(subpath);
	g_free(requriquery);
	g_free(requrifragment);
	if (!path) {
		fprintf(stderr, "make_savepath: %s: failed\n", uri);
		//g_uri_unref(souprequri);
		return NULL;
	}
	if (g_str_has_suffix(path, "/")) {
		char *tmp = g_build_filename(path, "index.html", NULL);
		GFA(path, tmp);
	}

	gboolean failed = false;
	char *dir = g_path_get_dirname(path);
	if (!g_file_test(dir, G_FILE_TEST_EXISTS)) {
		fprintf(stderr, "make_savepath: MKDIR %s\n", dir);
		int ret = g_mkdir_with_parents(dir, 0755);
		if (ret < 0) {
			fprintf(stderr, "make_save_path: failed to create directory");
			perror("");
			failed=true;
		}
	}
	if (!g_file_test(dir, G_FILE_TEST_IS_DIR)) {
//		fprintf(stderr, "make_savepath: not a directory: %s\n", dir);
		char *new_file = g_strconcat(dir, ".html", NULL);
		if (!g_file_test(new_file, G_FILE_TEST_EXISTS)) {
			fprintf(stderr, "make_save_path: attempting to rename file %s to %s\n", dir, new_file);
			int ret = rename(dir, new_file);
			if (ret < 0) {
				fprintf(stderr, "make_savepath: RENAME failed");
				perror("");
				failed=true;
			} else {
				fprintf(stderr, "make_savepath: MKDIR %s\n", dir);
				if (mkdir(dir, 0755) < 0) {
					fprintf(stderr, "make_savepath: MKDIR failed to create directory: %s\n", dir);
					perror("");
					failed=true;
				}
			}
		} else {
			fprintf(stderr, "make_savepath: fallback rename target %s exists\n", new_file);
			failed=true;
		}
		g_free(new_file);
	}
	//g_uri_unref(souprequri);
	g_free(dir);
	if (failed) {
		g_free(path);
		path = NULL;
	}
	fprintf(stderr, "make_savepath(%s): => returning %s\n", uri, path);
	return path;
}

void
savemhtmlcb(GObject *res, GAsyncResult *result, gpointer user_data)
{
	GError *gerror = NULL;
	WebKitWebView *web_view = (WebKitWebView *)res;
	GFile *gfile = (GFile *) user_data;
	gboolean ret = webkit_web_view_save_to_file_finish(web_view, result, &gerror);
	char *dest = g_file_get_path(gfile);
	if (ret)
		fprintf(stderr, "savemhtmlcb: saved mhtml to %s\n", dest);
	else
		fprintf_gerror(stderr, gerror, "savemhtmlcb: could not save mhtml to %s\n", dest);
	g_free(dest);
	g_object_unref(gfile);
}

void
savesourcecb(GObject *res, GAsyncResult *result, gpointer user_data)
{
	char *dest = (char *)user_data;
	gsize len; GError *gerror = NULL;
	guchar *data = webkit_web_resource_get_data_finish(
		(WebKitWebResource *)res, result, &len, &gerror);
	if (!data)
		fprintf_gerror(stderr, gerror, "savesourcecb: no data to save to %s\n", dest);
	else {
		GFile *gfile = g_file_new_for_path(dest);
		gerror = NULL;
		gboolean ret = g_file_replace_contents
			(gfile, (char *)data, len, NULL, false, //XXX
			 G_FILE_CREATE_NONE, NULL, NULL, &gerror);
		if (!ret)
			fprintf_gerror(stderr, gerror, "savesourcecb: error saving: %s\n", dest);
		else
			fprintf(stderr, "savesourcecb: saved %s\n", dest);
		g_object_unref(gfile);
		dltimestamp(dest, webkit_web_resource_get_response((WebKitWebResource *)res), NULL);
	}
	g_free(dest);
}

void vsh_clear(Win *win)
{
	g_free(win->v.uri); win->v.uri = NULL;
	g_free(win->v.mimetype); win->v.mimetype = NULL;
	if (win->v.source) g_bytes_unref(win->v.source);
	win->v.source = NULL;
	if (win->v.headers) g_bytes_unref(win->v.headers);
	win->v.headers = NULL;
	win->v.source_adj.h = win->v.source_adj.v =
		win->v.headers_adj.h = win->v.headers_adj.v =
		win->v.page_adj.h = win->v.page_adj.v = 0;
}

// store the current page scroll position in one of
// win->v.{source,headers,page}_adj depending on the vsh mode.  Note:
// The storing is actually done in _run(), which stores the response
// from the webprocess in the global location page_adj.
static void save_adj(Win *win)
{
	switch (win->v.mode) {
	case VSH_SOURCE: scroll_adj = &(win->v.source_adj); break;
	case VSH_HEADERS: scroll_adj = &(win->v.headers_adj); break;
	default:
	case VSH_HTML: scroll_adj = &(win->v.page_adj); break;
	}
	fprintf(stderr, "save_adj: %p\n", scroll_adj);
	// this is an asynchronous to the webprocess to return the
	// scoll position.  response will be saved in scroll_adj.
	send(win, Cscrollposition, "-");
}

//  Note: Restoring a saved position is done in loadcb() which will
//  asynchronously request the webprocess to set the scroll position
//  to the values it finds in the location win->page_adj.
static void restore_adj(Win *win, Adj *adj)
{
	win->page_adj = adj;
}

void
viewsourceorheaderscb(GObject *res, GAsyncResult *result, gpointer dest)
{
	Win *win = (Win *)dest;
	fprintf(stderr, "viewsourceorheaderscb(%d)\n", win->v.mode);
	gsize len; GError *gerror = NULL;
	guchar *data = webkit_web_resource_get_data_finish(
		(WebKitWebResource *)res, result, &len, &gerror);
	if (!data)
		fprintf_gerror(stderr, gerror, "viewsourceorheaderscb: no data to save to %s\n", dest);
	else {
		g_assert(!win->v.source);
		g_assert(!win->v.headers);
		g_assert(!win->v.mimetype);
		g_assert(!win->v.uri);
		win->v.uri = strdup(webkit_web_resource_get_uri((WebKitWebResource *)res));
		// we expect the uri in the WebKitWebView to be the
		// same as the uri in the WebKitWebResource. But if
		// load fails, the web resource only has
		// "about:blank". This happens for example with 999
		// HTTP responses from linkedin which result in a
		// "Message Corrupt" from libsoup.
		if (! (strcmp(win->v.uri, URI(win)) == 0))
			fprintf(stderr, "viewsourceorheaderscb: TODO apparently load failed\n");
		fprintf(stderr, "viewresourceorheaderscb: filling data for uri %s\n", win->v.uri);
		WebKitURIResponse *response = webkit_web_resource_get_response((WebKitWebResource *)res);
		win->v.mimetype = response ? g_strdup((char *) webkit_uri_response_get_mime_type(response)) : NULL; // defaults to text/html
		win->v.source = g_bytes_new_take(data, len);
		if (response) {
			SoupMessageHeaders *head = webkit_uri_response_get_http_headers(response);
			if (head) {
				char *str = NULL, *name, *value;
				SoupMessageHeadersIter iter;
				soup_message_headers_iter_init (&iter, head);
				while (soup_message_headers_iter_next(&iter, (const char **) &name, (const char **) &value) == TRUE) {
					char *tmp = g_strdup_printf("%s%s: %s\n", str ?: "", name, value);
					g_free(str);
					str = tmp;
				}
				if (str)
					win->v.headers = g_bytes_new_take(str, strlen(str));
				else fprintf(stderr, "viewsourceorheaderscb: no headers in SoupMessageHeaders\n");
			} else fprintf(stderr, "viewsourceorheaderscb: no SoupMessageHeaders\n");
		}
		switch(win->v.mode) {
		case VSH_SOURCE:
			webkit_web_view_load_bytes(win->kit, win->v.source, "text/plain", "ISO-8895-1", URI(win));
			restore_adj(win, &(win->v.source_adj));
			break;
		case VSH_HEADERS:
			if (win->v.headers) {
				webkit_web_view_load_bytes(win->kit, win->v.headers, "text/plain", "ISO-8895-1", URI(win));
				restore_adj(win, &(win->v.headers_adj));
			} else
				fprintf(stderr, "viewsourceorheaderscb: load headers failed\n");
			break;
		case VSH_HTML:
		default:
			fprintf(stderr, "viewsourceorheaderscb: unexpected mode: %d\n", win->v.mode);
			webkit_web_view_load_bytes(win->kit, win->v.source, win->v.mimetype, NULL, URI(win));
			restore_adj(win, &(win->v.page_adj));
		}
	}
}

static void viewsourceorheaders(Win *win, viewsourceorheaders_mode flag)
{
	save_adj(win);
	if (win->v.uri) {
		if (strcmp(URI(win), win->v.uri) == 0) {
			fprintf(stderr, "viewresourceorheaders: %d: orig=%d: same uri %s\n", flag, win->v.mode, win->v.uri);
			if (flag == win->v.mode) {
				// revert back to html
				if (win->v.mode != VSH_HTML) {
					fprintf(stderr, "revert to html\n");
					win->v.mode = VSH_HTML;
					webkit_web_view_load_bytes(win->kit, win->v.source, win->v.mimetype, NULL, URI(win));
					restore_adj(win, &(win->v.page_adj));
				} else
					fprintf(stderr, "no change\n");
			} else if (flag == VSH_SOURCE) {
				fprintf(stderr, "view source\n");
				win->v.mode = VSH_SOURCE;
				webkit_web_view_load_bytes(win->kit, win->v.source, "text/plain", "ISO-8895-1", URI(win));
				restore_adj(win, &(win->v.source_adj));
			} else if (flag == VSH_HTML) {
				fprintf(stderr, "view html\n");
				win->v.mode = VSH_HTML;
				webkit_web_view_load_bytes(win->kit, win->v.source, win->v.mimetype, NULL, URI(win));
				restore_adj(win, &(win->v.page_adj));
			} else {
				fprintf(stderr, "view headers\n");
				g_assert(flag == VSH_HEADERS);
				int orig = win->v.mode;
				win->v.mode = VSH_HEADERS;
				if (win->v.headers) {
					webkit_web_view_load_bytes(win->kit, win->v.headers, "text/plain", "ISO-8895-1", URI(win));
					restore_adj(win, &(win->v.headers_adj));
				} else {
					win->v.mode = orig;
					fprintf(stderr, "viewresourceorheaders: no headers on back button: reset mode to: %d\n", orig);
				}
			}
			return;
		}
		fprintf(stderr, "viewresourceorheaders: freeing data for old uri %s\n", win->v.uri);
		vsh_clear(win);
	}
	WebKitWebResource *res = webkit_web_view_get_main_resource(win->kit);
	win->v.mode = flag;
	if (res)
		webkit_web_resource_get_data(res, NULL, viewsourceorheaderscb, win);
	else
		fprintf(stderr, "viewsourceorheaders: no resource\n");
}



static void xhtml_to_html5_resourcecb(GObject *res, GAsyncResult *result, gpointer dest)
{
	Win *win = (Win *)dest;
	gsize sz;
	guchar *str = webkit_web_resource_get_data_finish(
			(WebKitWebResource *)res, result, &sz, NULL);

	if (!str) {
		fprintf(stderr, "nothing to reload\n");
		return;
	}
	static GRegex *xhtml_to_html5;
	GError *err;
	if (!xhtml_to_html5) {
		err = NULL;
		xhtml_to_html5 = g_regex_new
			("<!DOCTYPE html[^>]*>[^<]*<html xmlns[^>]*>",
			 G_REGEX_CASELESS,
			 0,
			 &err);
		if (err) {
			fprintf_gerror(stderr, err, "g_regex_new failed\n");
			return;
		}
	}
	err = NULL;
	gchar *new_str = g_regex_replace_literal
		(xhtml_to_html5, (gchar *) str, sz, 0,
		 "<!DOCTYPE html>\n<html>\n", //G_MATCH_ANCHORED
		 0,
		 &err);
	if (err) {
		fprintf_gerror(stderr, err, "failed to fudge xhtml->html5\n");
		return;
	}

	webkit_web_view_load_html(win->kit, new_str, URI(win));
	g_free(str);
	g_free(new_str);
}


static void
xhtml_to_html5(Win *win)
{
	WebKitWebResource *res = webkit_web_view_get_main_resource(win->kit);
	if (res)
		webkit_web_resource_get_data(res, NULL, xhtml_to_html5_resourcecb, win);
}

void readability_mode(Win *win);
#include "readability/readability.c"

//@actions
typedef struct {
	char *name;
	guint key;
	guint mask;
	char *desc;
} Keybind;
static Keybind dkeys[]= {
//every mode
	{"tonormal"      , GDK_KEY_Escape, 0, "To Normal Mode"},
	{"tonormal"      , '[', GDK_CONTROL_MASK},

//normal
	{"toinsert"      , 'i', 0},
	{"toinsert"	 , GDK_KEY_BackSpace, 0},
	{"toinsertinput" , 'I', 0, "To Insert Mode with focus of first input"},
	{"topointer"     , 'p', 0, "pp resets damping. Esc clears pos. Press enter/space makes btn press"},
	{"topointermdl"  , 'P', 0, "Makes middle click"},
	{"topointerright", 'p', GDK_CONTROL_MASK, "right click"},

	{"tohint"        , 'f', 0, "Follow Mode"},
	{"tohintnew"     , 'F', 0},
	{"tohintback"    , 't', 0},
	{"tohintdl"      , 'd', 0, "dl is Download"},
	{"tohintbookmark", 'T', 0},
	{"tohintrangenew", 'r', GDK_CONTROL_MASK, "Open new windows"},

	{"showdldir"     , 'D', 0},

	{"yankuri"       , 'y', 0, "Clipboard"},
	{"yanktitle"     , 'Y', 0, "Clipboard"},
	{"bookmark"      , 'b', GDK_CONTROL_MASK, "arg: \"\" or \"uri + ' ' + label\""},
	{"bookmarkbreak" , 'B', GDK_CONTROL_MASK, "Add line break to the main page"},

	{"quit"          , 'q', 0},
	{"quitall"       , 'Q', 0},
//	{"quit"          , 'Z', 0},

	{"scrolldown"    , 'j', 0},
	{"scrollup"      , 'k', 0},
	{"scrollleft"    , 'h', 0},
	{"scrollright"   , 'l', 0},

	{"arrowdown"     , 'j', GDK_CONTROL_MASK},
	{"arrowup"       , 'k', GDK_CONTROL_MASK},
	{"arrowleft"     , 'h', GDK_CONTROL_MASK},
	{"arrowright"    , 'l', GDK_CONTROL_MASK},

	{"pagedown"      , 'f', GDK_CONTROL_MASK},
	{"pageup"        , 'b', GDK_CONTROL_MASK},
//	{"halfdown"      , 'd', GDK_CONTROL_MASK},
//	{"halfup"        , 'u', GDK_CONTROL_MASK},

	{"top"           , 'g', 0},
	{"bottom"        , 'G', 0},
	{"zoomin"        , '+', 0},
	{"zoomout"       , '-', 0},
	{"zoomreset"     , '=', 0},

	//tab
	{"nextwin"       , 'J', 0},
	{"prevwin"       , 'K', 0},
//	{"quitnext"      , 'x', 0, "Raise next win and quit current win"},
//	{"quitprev"      , 'X', 0},
	{"winlist"       , 'z', 0},

	{"back"          , 'H', 0},
	{"forward"       , 'L', 0},
	{"stop"          , 's', 0},
	{"reload"        , 'r', 0},
	{"reloadbypass"  , 'R', 0, "Reload bypass cache"},

	{"find"          , '/', 0},
	{"findnext"      , 'n', 0},
	{"findprev"      , 'N', 0},
	{"findselection" , '*', 0},

	{"open"          , 'o', 0},
	{"opennew"       , 'w', 0, "New window"},
	{"edituri"       , 'O', 0, "Edit arg or focused link or current page's URI"},
	{"editurinew"    , 'W', 0},

	{"inspector"	 , 'O', GDK_CONTROL_MASK},
	{"cookiepolicy"  , 'V', GDK_CONTROL_MASK},

	{"surfcmdprompt",  'x', GDK_MOD1_MASK },
	{"surfgo"	 , 'g', GDK_CONTROL_MASK},
	{"surfgo"        , GDK_KEY_F5, },

	{"surffind"	 , '/', GDK_CONTROL_MASK},
	{"savemhtml"	 , 'S', GDK_CONTROL_MASK},
	{"savesource"	 , 'd', GDK_CONTROL_MASK},
	{"viewsource"	 , '\\', 0},
	{"viewheaders"	 , '=', GDK_CONTROL_MASK},

//	{"showsource"    , 'S', 0}, //not good
	{"showhelp"      , ':', 0},
	{"showhistory"   , 'M', 0},
	{"showhistoryall", 'm', GDK_CONTROL_MASK},
	{"showmainpage"  , 'm', 0},


//	{"clearallwebsitedata", 'C', GDK_CONTROL_MASK},
	{"edit"          , 'e', 0, "Edit current uri conf or mainpage"},
	{"editconf"      , 'E', 0},
	{"openconfigdir" , 'e', GDK_CONTROL_MASK},

	{"setv"          , 'v', 0, "Use the 'set:v' group"},
	{"setscript"     , 's', GDK_CONTROL_MASK, "Use the 'set:script' group"},
	{"setimage"      , 'i', GDK_CONTROL_MASK, "set:image"},
	{"unset"         , 'u', 0},

	{"addwhitelist"  , 'a', 0, "Add URIs blocked to whiteblack.conf as white list"},
	{"addblacklist"  , 'A', 0, "URIs loaded"},

//insert
	{"textlink"      , 'e', GDK_CONTROL_MASK, "For text elements in insert mode"},

//nokey
	{"set"           , 0, 0, "Use 'set:' + arg group of main.conf. This toggles"},
	{"set2"          , 0, 0, "Not toggle"},
	{"setstack"      , 0, 0,
		"arg == NULL ? remove last : add set without checking duplicate"},
	{"new"           , 0, 0},
	{"newclipboard"  , 0, 0, "Open [arg + ' ' +] clipboard text in a new window"},
	{"newselection"  , 0, 0, "Open [arg + ' ' +] selection ..."},
	{"newsecondary"  , 0, 0, "Open [arg + ' ' +] secondaly ..."},
	{"findclipboard" , 0, 0},
	{"findsecondary" , 0, 0},

	{"tohintopen"    , 0, 0, "not click but opens uri as opennew/back"},

	{"tohintimageopen", ';', GDK_CONTROL_MASK},
	{"tohintimagenew",  ';', 0},
	{"tohintimageback", ':', GDK_CONTROL_MASK},
	{"tohintimagedl",   'D', GDK_CONTROL_MASK},

	{"copytoclipboard", 0, 0},
	{"copytoprimary",   0, 0},
	{"tohintimagecopyclipboard", 'C', 0},
	{"tohintimagecopyprimary",   'c', 0},


	{"openback"      , 0, 0},
	{"openwithref"   , 0, 0, "Current uri is sent as Referer"},
	{"download"      , 0, 0},
	{"dlwithheaders" , 0, 0, "Current uri is sent as Referer. Also cookies. arg 2 is dir"},
	{"showmsg"       , 0, 0},
	{"raise"         , 0, 0},
	{"winpos"        , 0, 0, "x:y"},
	{"winsize"       , 0, 0, "w:h"},
	{"click"         , 0, 0, "x:y"},
	{"openeditor"    , 0, 0},

	{"spawn"         , 0, 0, "arg is called with environment variables"},

	{"sh"            , 0, 0, "sh -c arg with env vars"},
	{"shjs"          , 0, 0, "sh(arg2) with javascript(arg)'s $RESULT"},
	{"shhint"        , 0, 0, "sh with envs selected by a hint"},
	{"shrange"       , 0, 0, "sh with envs selected by ranged hints"},
	{"shsrc"         , 0, 0, "sh with src of current page via pipe"},
#if WEBKIT_CHECK_VERSION(2, 20, 0)
	{"shcookie"      , 0, 0,
		"` "APP" // shcookie $URI 'echo $RESULT' ` prints cookies."
			"\n  Make sure, the callbacks of "APP" are async."
			"\n  The stdout is not caller's but first process's stdout."},
#endif
	{"surfcharset"	 , 0, 0, "Reload with charset"},
	{"revealhint"    , GDK_KEY_F2, 0, "Reveal hint"},
	{"showcert"	 , 'X', GDK_CONTROL_MASK, "Show tls certificate" },

	{"applystyle"    , 0, 0, "Apply a css stylesheet"},

	{"selectall"     , 'a', GDK_CONTROL_MASK, "SelectAll"},

//todo pagelist
//	{"windowimage"   , 0, 0}, //winid
//	{"windowlist"    , 0, 0}, //=>winid uri title
};
static char *ke2name(Win *win, GdkEventKey *ke)
{
	guint key = ke->keyval;

	char **swaps = getsetsplit(win, "keybindswaps");
	if (swaps)
	{
		for (char **swap = swaps; *swap; swap++)
		{
			if (!**swap || !*(*swap + 1)) continue;
			if (key == **swap)
				key =  *(*swap + 1);
			else
			if (key == *(*swap + 1))
				key =  **swap;
			else
				continue;
			break;
		}
		g_strfreev(swaps);
	}

	guint mask = ke->state & (~GDK_SHIFT_MASK &
			gdk_keymap_get_modifier_mask(
				gdk_keymap_get_for_display(gdk_display_get_default()),
				GDK_MODIFIER_INTENT_DEFAULT_MOD_MASK));
	static int len = sizeof(dkeys) / sizeof(*dkeys);
	for (int i = 0; i < len; i++) {
		Keybind b = dkeys[i];
		if (key == b.key && b.mask == mask)
			return b.name;
	}
	return NULL;
}

char*
parse_hintdata_at(Win *win, int x, int y)
{
	char *ret = 0;
	if (win->hintdata) {
		PangoFontDescription *desc = pango_font_description_copy(
			pango_context_get_font_description(
				gtk_widget_get_pango_context(win->winw)));
		pango_font_description_set_family(desc, "monospace");
		pango_font_description_set_weight(desc, PANGO_WEIGHT_BOLD);

		double z = webkit_web_view_get_zoom_level(win->kit);
		char **hints = g_strsplit(win->hintdata, ";", -1);
		for (char **lh = hints; *lh && **lh; lh++) {
			char *h = *lh;
			//0   123*   141*   190*   164*  0*FF //example
			//0123456789012345678901234567890123456789
			//0---------1---------2---------3---------
			h[7]=h[14]=h[21]=h[28]=h[32] = '\0';
#define Z(i) atoi(h + i) * z
			int bx = Z(1), by=Z(8), bw=Z(15), bh=Z(22);
#undef Z
			char *txt = h+34;
//			int len = atoi(h+29);
//			bool head = h[33] == '1';
			int center = *h == '1';

			PangoLayout *layout = gtk_widget_create_pango_layout(win->winw, txt);
			pango_layout_set_font_description(layout, desc);
			PangoRectangle inkrect, logicrect;
			pango_layout_get_pixel_extents(layout, &inkrect, &logicrect);
			int m = 2;

			int pw = logicrect.width + m*2;
			int ph = inkrect.height + m*2;
			int px = (bx + bx + bw - pw ) / 2;
			int py = MAX(-m, by + (center ? ph/2 : -ph/2 - 1));

			/*
			fprintf(stderr, "in (x,y,r,b) b(%d %d %d %d) p(%d %d %d %d): txt=%s: cen=%d head=%d len=%d\n",
				bx, by, bx + bw, by + bh,
				px, py, px + pw, py + ph,
				txt, center, head,len
				);
			*/
			if ((x >= bx) && (y >= by) && (x <= (bx + bw)) &&
			    (y <= (by + bh))) {
				ret = txt;
			}
			if ((x >= px) && (y >= py) && (x <= (px + pw)) &&
			    (y <= (py + ph))) {
				ret = txt; //break
			}
		}
		g_strfreev(hints);
		pango_font_description_free(desc);
	}
	return ret;
}

#if (SOUP_MAJOR_VERSION == 3)
#include "soup-uri-normalize.c"
#endif

// cookie to pass to newwin cbwin to indicate automation mode
#define AUTOMATION_CBWIN (void *)0x01

#include "extraschemes.c"	/* for setcontentfiler, also see schemecb below */

//declaration
static Win *newwin(const char *uri, Win *cbwin, Win *caller, int back);
static bool _run(Win *win, const char* action, const char *arg, char *cdir, char *exarg)
{
#define Z(str, func) if (!strcmp(action, str)) {func; goto out;}
#define ZZ(t1, t2, f) Z(t1, f) Z(t2, f)
	//D(action %s, action)
	if (action == NULL) return false;
	char **agv = NULL;

	fprintf(stderr, "main run: action=%s\n", action);
	Z("quitall"     , gtk_main_quit())
	Z("new"         , maybe_newwin(arg, NULL, win, 0))
	Z("plugto"      , plugto = atol(exarg ?: arg ?: "0");
			return run(win, "new", exarg ? arg : NULL))
	Z("automation", newwin( !arg || !strcmp(arg,"bogus") ? // python selenium webdriver.WebKitGTKOptions.add_argument('') cannot handle empty argument. use add_argument("bogus") instead
				"about:blank" : arg,
				AUTOMATION_CBWIN, win, 0))

#define CLIP(clip) \
		char *uri = g_strdup_printf(arg ? "%s %s" : "%s%s", arg ?: "", \
			sfree(gtk_clipboard_wait_for_text(gtk_clipboard_get(clip)))); \
		win = newwin(uri, NULL, NULL, 0); \
		g_free(uri)
	Z("newclipboard", CLIP(GDK_SELECTION_CLIPBOARD))
	Z("newselection", CLIP(GDK_SELECTION_PRIMARY))
	Z("newsecondary", CLIP(GDK_SELECTION_SECONDARY))
#undef CLIP

	if (win == NULL && (!arg || strcmp(action, "dlwithheaders"))) return false;

	//internal
	Z("_pageinit"  ,
			agv = g_strsplit(arg, ":", 2);
			win->ipcids = g_slist_prepend(win->ipcids, g_strdup(agv[0]));
			//when page proc recreated on some pages, webkit_web_view_get_page_id delays
			_send(win, Coverset, win->overset, atol(g_strdup(agv[1]))))
	Z("_textlinkon", textlinkon(win))
	Z("_blocked"   ,
			_showmsg(win, g_strdup_printf("Blocked %s", arg));
			return true;)
	Z("_reloadlast", reloadlast())
	Z("_hintdata"  , if (!(win->mode & Mhint)) return false;
			gtk_widget_queue_draw(win->canvas);
			GFA(win->hintdata, g_strdup(arg)))
	Z("_focusuri"  , win->usefocus = true; GFA(win->focusuri, g_strdup(arg)))
	if (!strcmp(action, "_hintret"))
	{
		const char *orgarg = arg;
		char *result = *++arg == '0' ? "0" : "1";
		agv = g_strsplit(++arg, " ", 3);
		arg = agv[1];

		action = win->spawn->action;
		if (!strcmp(action, "bookmark"))
			arg = strchr(orgarg, ' ') + 1;
		else if (!strcmp(action, "spawn") || !strcmp(action, "sh"))
		{
			setresult(win, NULL);
			win->linklabel = g_strdup(agv[2]);

			switch (*orgarg) {
			case 'l':
				win->link  = g_strdup(arg); break;
			case 'i':
				win->image = g_strdup(arg); break;
			case 'm':
				win->media = g_strdup(arg); break;
			}

			envspawn(win->spawn, true, result, NULL, 0);
			goto out;
		}
	}


	// if identifier is not specified uses the default identifier.
	// automatically adds the stored filter into the usercontentmanager
	Z("registercontentfilter", /* arg: "pathname identifier" */
	  WebKitUserContentFilter *cf = NULL;
	  agv = g_strsplit(arg, " ", 2);
	  g_message("agv[1]=%s, *agv=%s", agv[1], *agv);
	  if (opContentFilter(CONTENT_FILTER_STORE_SAVE, agv[1], *agv, &cf)) {
		  webkit_user_content_manager_add_filter(
			  webkit_web_view_get_user_content_manager(win->kit), cf);
		  webkit_user_content_filter_unref(cf);
	  })

	// automatically first removes the filter from the usercontentmanager
	Z("unregistercontentfilterid", /* arg: "identifier" */
	  webkit_user_content_manager_remove_filter_by_id(
		  webkit_web_view_get_user_content_manager(win->kit),
		  !arg || g_strcmp0("", arg) == 0 ? APP "Filter" : arg);
	  opContentFilter(CONTENT_FILTER_STORE_REMOVE, arg, NULL, NULL))

	Z("addcontentfilterid", /* arg: "identifier" */
	  WebKitUserContentFilter *cf = NULL;
	  if (opContentFilter(CONTENT_FILTER_STORE_LOAD, arg, NULL, &cf)) {
		  webkit_user_content_manager_add_filter(
			  webkit_web_view_get_user_content_manager(win->kit), cf);
		  webkit_user_content_filter_unref(cf);
	  })

	Z("removecontentfilterid", /* arg: "identifier" */
	  webkit_user_content_manager_remove_filter_by_id(
		  webkit_web_view_get_user_content_manager(win->kit),
		  !arg || g_strcmp0("", arg) == 0 ? APP "Filter" : arg))

	if (arg != NULL) {
		Z("find"   , find(win, arg, true, false))
		Z("open"   , openuri(win, arg))
		Z("opennew", maybe_newwin(arg, NULL, win, 0))

		Z("bookmark",
				agv = g_strsplit(arg, " ", 2); addlink(win, agv[1], *agv);)

		//nokey
		Z("openback",
			altcur(win, 0,0); showmsg(win, "Opened"); newwin(arg, NULL, win, 1))
		Z("openwithref",
			const char *ref = agv ? *agv : URI(win);
		  /*
			GUri *uri = g_uri_parse(arg, SOUP_HTTP_URI_FLAGS, NULL);
			char *nrml = g_uri_to_string(uri);
			g_uri_unref(uri);
		  */
		  	char *nrml = soup_uri_normalize(arg, NULL);

			if (!g_str_has_prefix(ref, APP":") &&
				!g_str_has_prefix(ref, "file:")
			) send(win, Cwithref, sfree(g_strdup_printf("%s %s", ref, nrml)));

			webkit_web_view_load_uri(win->kit, nrml);
			g_free(nrml);
		)
		Z("download", webkit_web_view_download_uri(win->kit, arg))
		Z("dlwithheaders",
			if (!strcmp(arg, "about:blank"))
			{
				if (win) showmsg(win, arg);
				goto out;
			}
			Win *dlw = newwin(NULL, win, win, 2);
			dlw->fordl = g_strdup(exarg ?: "");

			const char *ref = agv ? *agv : win ? URI(win) : arg;
			WebKitURIRequest *req = webkit_uri_request_new(arg);
			SoupMessageHeaders *hdrs = webkit_uri_request_get_http_headers(req);
			if (hdrs && //scheme APP: returns NULL
				!g_str_has_prefix(ref, APP":") &&
				!g_str_has_prefix(ref, "file:"))
				soup_message_headers_append(hdrs, "Referer", ref);
			//load request lacks cookies except policy download at nav action
			webkit_web_view_load_request(dlw->kit, req);
			g_object_unref(req);
		)

		Z("showmsg" , showmsg(win, arg))
		Z("click",
			agv = g_strsplit(arg ?: "100:100", ":", 2);
			double z = webkit_web_view_get_zoom_level(win->kit);
			win->px = atof(*agv) * z;
			win->py = atof(agv[1] ?: exarg) * z;
			makeclick(win, win->pbtn ?: 1);
		)
		Z("openeditor", openeditor(win, arg, NULL))
		ZZ("sh", "spawn",
				envspawn(spawnp(win, action, arg, cdir, true)
					, true, exarg, NULL, 0))
		ZZ("shjs", "jscallback"/*backward*/,
			webkit_web_view_run_javascript(win->kit, arg, NULL, jscb
				, spawnp(win, action, exarg, cdir, true)))
		ZZ("shsrc", "sourcecallback"/*backward*/,
			WebKitWebResource *res =
				webkit_web_view_get_main_resource(win->kit);
			webkit_web_resource_get_data(res, NULL, resourcecb
				, spawnp(win, action, arg, cdir, true));
			)
#if WEBKIT_CHECK_VERSION(2, 20, 0)
		ZZ("shcookie", "cookies"/*backward*/,
			WebKitCookieManager *cm =
				webkit_web_context_get_cookie_manager(ctx);
			webkit_cookie_manager_get_cookies(cm, arg, NULL, cookiescb
				, spawnp(win, action, exarg, cdir, true)))
#endif

		Z("cookiepolicy",
			static struct cookie_policy_action_data cbdata;
			cbdata.win = win;
			if (strcmp(arg, "on") == 0) {
				cbdata.action = COOKIES_ON;
			} else if (strcmp(arg, "off") == 0) {
				cbdata.action = COOKIES_OFF;
			} else if (strcmp(arg, "status") == 0) {
				cbdata.action = COOKIES_STATUS;
			} else if (strcmp(arg, "cycle") == 0) {
				cbdata.action = COOKIES_CYCLE;
			} else {
				fprintf(stderr, "Unknown arg %s\n", arg);
				cbdata.action = COOKIES_STATUS;
			}
			WebKitCookieManager *mgr = webkit_web_context_get_cookie_manager(webkit_web_view_get_context(win->kit));
			webkit_cookie_manager_get_accept_policy(mgr, NULL, togglecookiepolicycb, &cbdata))
		Z("customcharset", webkit_web_view_set_custom_charset(
			win->kit, (strcmp(arg, "") == 0) ? NULL : arg))

		Z("applystyle",
		  _showmsg(win, g_strdup_printf("applystyle %s", arg));
		  const char *name = arg;
		  WebKitUserContentManager *cmgr =
		  webkit_web_view_get_user_content_manager(win->kit);
		  if (name == NULL || strcmp(name, "") == 0 || strcmp(name, "none") == 0) {
			  webkit_user_content_manager_remove_all_style_sheets(cmgr);
		  } else {
			  char *path = path2conf(name);
			  char *str;
			  if (g_file_test(path, G_FILE_TEST_EXISTS)
			      && g_file_get_contents(path, &str, NULL, NULL)) {
				  WebKitUserStyleSheet *css =
					  webkit_user_style_sheet_new(str,
								      WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
								      WEBKIT_USER_STYLE_LEVEL_USER,
								      NULL, NULL);
				  webkit_user_content_manager_add_style_sheet(cmgr, css);
				  g_free(str);
			  }
			  g_free(path);
		  })

		Z("w3mmode_status",
			_showmsg(win, g_strdup_printf("w3mmode is %s", arg)))
		Z("offline_status",
			_showmsg(win, g_strdup_printf("offline status is %s", arg)))
		Z("w3mmode", send(win, Cw3mmode, (char *)arg))
		Z("offline", send(win, Coffline, (char *)arg))

		// forward scrollposition request to the webprocess
		Z("setscrollposition",
			send(win, Cscrollposition, (char *)arg))
		// save scrollposition response from the webprocess
		Z("scrollposition",
			if (scroll_adj) {
				int n = sscanf(arg, "%lu %lu", &(scroll_adj->h), &(scroll_adj->v));
				if (n != 2) {
					fprintf(stderr, "main: failed to parse scollposition from ext: %s\n", arg);
				} else {
					// fprintf(stderr, "main: scrollposition: storing to %p: %lu %lu\n", scroll_adj, scroll_adj->h, scroll_adj->v);
				}
				scroll_adj = 0;
			}
			_showmsg(win, g_strdup_printf("scroll position is %s", arg)))

		Z("cachemodel",
		  	WebKitWebContext *ctx = webkit_web_view_get_context(win->kit);
			WebKitCacheModel old = webkit_web_context_get_cache_model(ctx);
			int actionp = 0;
			if (strcmp(arg, "on") == 0) {
				actionp = 1;
				webkit_web_context_set_cache_model(ctx, WEBKIT_CACHE_MODEL_WEB_BROWSER);
			} else if (strcmp(arg, "mem") == 0) {
				actionp = 1;
				webkit_web_context_set_cache_model(ctx, WEBKIT_CACHE_MODEL_DOCUMENT_BROWSER);
			} else if (strcmp(arg, "off") == 0) {
				actionp = 1;
				webkit_web_context_set_cache_model(ctx, WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER);
			} else if (arg && strcmp(arg, "status") == 0) { //noop
			} else {
				fprintf(stderr, "cachemodel: unknown arg %s\n", arg);
			}
			char *name = "UNKNOWN";
			WebKitCacheModel model = webkit_web_context_get_cache_model(ctx);
			if (model == WEBKIT_CACHE_MODEL_WEB_BROWSER)
				name = "WEB BROWSER (ON)";
			else if (model == WEBKIT_CACHE_MODEL_DOCUMENT_VIEWER)
				name = "DOCUMENT VIEWER (OFF)";
			else if (model == WEBKIT_CACHE_MODEL_DOCUMENT_BROWSER)
				name = "DOCUMENT BROWSER (MEM)";
			_showmsg(win, g_strdup_printf("cachemodel = CACHE MODEL %s%s\n", name, (actionp ? ((model == old) ? " (unchanged)" : "(changed)") : ""))))

		Z("historymode",
			int actionp = 0;
			int old = historyenabled;
			if (strcmp(arg, "on") == 0) {
			    actionp = 1;
			    historyenabled = 1;
			} else if (strcmp(arg, "off") == 0) {
				actionp = 1;
				historyenabled = 0;
			} else if (arg && strcmp(arg, "status") == 0) { //noop
			} else {
				fprintf(stderr, "historymode: unknown arg %s\n", arg);
			}
			_showmsg(win, g_strdup_printf("historymode = %s%s\n", historyenabled ? "enabled" : "disabled", (actionp ? ((historyenabled == old) ? "(unchanged)" : "(changed)") : "")));)

		Z("proxymode",
			// cannot retrieve earlier settings from webkit.
			WebKitNetworkProxyMode old = proxy_mode;
			WebKitNetworkProxyMode new;
			char *enum_name[3];
			int statusonly = 0;
			enum_name[WEBKIT_NETWORK_PROXY_MODE_DEFAULT] = "System";
			enum_name[WEBKIT_NETWORK_PROXY_MODE_NO_PROXY] = "None";
			enum_name[WEBKIT_NETWORK_PROXY_MODE_CUSTOM] = "Custom";
			if (strcmp(arg, "no_proxy") == 0 ||
			    strcmp(arg, "none") == 0)
				new = WEBKIT_NETWORK_PROXY_MODE_NO_PROXY;
			else if (strcmp(arg, "default") == 0)
				new = WEBKIT_NETWORK_PROXY_MODE_DEFAULT;
			else if (strcmp(arg, "custom") == 0)
				new = WEBKIT_NETWORK_PROXY_MODE_CUSTOM;
			else if (strcmp(arg, "status") == 0) {
				// unreliable
				_showmsg(win, g_strdup_printf("proxymode: last known value %d %s",
							      proxy_mode, enum_name[proxy_mode]));
				statusonly = 1;
			} else {
				fprintf(stderr, "proxymode: Unknown mode %d. using default\n", old);
				new = WEBKIT_NETWORK_PROXY_MODE_DEFAULT;
			}
			if (!statusonly) {
				if (new == WEBKIT_NETWORK_PROXY_MODE_CUSTOM && proxy_settings == NULL) {
					showmsg(win, "proxymode: CUSTOM settings empty. Using default");
					new = WEBKIT_NETWORK_PROXY_MODE_DEFAULT;
				}
				WebKitWebContext *ctx = webkit_web_view_get_context(win->kit);
				if (old != new) {
					if (new == WEBKIT_NETWORK_PROXY_MODE_DEFAULT ||
					    new == WEBKIT_NETWORK_PROXY_MODE_NO_PROXY)
						webkit_web_context_set_network_proxy_settings(ctx, new, NULL);
					else
						// use last known global settings
						webkit_web_context_set_network_proxy_settings
							(ctx, new, proxy_settings);
					proxy_mode = new;
				}
				char *msg = g_strdup_printf("proxymode = %s", enum_name[new]);
				if (old == new) {
					GFA(msg, g_strdup_printf("%s (unchanged)", msg));
				} else {
					GFA(msg, g_strdup_printf("%s (was %s)", msg, enum_name[old]));
				}
				_showmsg(win, msg);
					})

		// XXX code duplicated with yankuri*
		Z("copytoclipboard",
		  gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), arg, -1))
		Z("copytoprimary",
		  gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_PRIMARY), arg, -1))

		Z("reusemode", reusemode(win, arg))
	}

	Z("tonormal"    , win->mode = Mnormal)

	Z("toinsert"    , win->mode = Minsert)
	Z("toinsertinput", win->mode = Minsert; send(win, Ctext, arg))

	if (g_str_has_prefix(action, "topointer"))
	{
		guint prevbtn = win->pbtn;
		if (!strcmp(action, "topointerright"))
			win->pbtn = 3;
		else if (!strcmp(action, "topointermdl"))
			win->pbtn = 2;
		else
			win->pbtn = 1;

		win->mode = win->mode == Mpointer && prevbtn == win->pbtn ?
			Mnormal : Mpointer;
		goto out;
	}

#define H(str, pcom, paction, arg, dir, pmode) Z(str, win->com = pcom; \
		spawnfree(win->spawn, true); \
		win->spawn = spawnp(win, paction, arg, dir, false); \
		win->mode = pmode)
	H("tohint"        , Cclick, ""        , NULL, NULL, Mhint) //click
	H("tohintopen"    , Clink , "open"    , NULL, NULL, Mhint)
	H("tohintnew"     , Clink , "opennew" , NULL, NULL, Mhint)
	H("tohintback"    , Clink , "openback", NULL, NULL, Mhint)
	H("tohintimageopen", Cimage, "open"   , NULL, NULL, Mhint)
	H("tohintimagenew",  Cimage , "opennew" ,  NULL, NULL, Mhint)
	H("tohintimageback", Cimage , "openback",  NULL, NULL, Mhint)
	H("tohintimagedl",   Cimage , "dlwithheaders",  NULL, NULL, Mhint)

	H("tohintimagecopyclipboard", Cimage, "copytoclipboard", NULL, NULL, Mhint)
	H("tohintimagecopyprimary", Cimage, "copytoprimary", NULL, NULL, Mhint)

	H("tohintdl"      , Curi  , getsetbool(win, "dlwithheaders") ?
			"dlwithheaders" : "download"  , NULL, NULL, Mhint)
	H("tohintbookmark", Curi  , "bookmark", NULL, NULL, Mhint)
	H("tohintrangenew", Crange, "sh"      ,
			APP" // opennew $MEDIA_IMAGE_LINK"  , NULL, Mhintrange)

	if (arg != NULL) {
	H("shhint"        , Cspawn, "sh"      , arg , cdir, Mhint)
	H("tohintcallback", Cspawn, "spawn"   , arg , cdir, Mhint) //backward
	H("shrange"       , Crange, "sh"      , arg , cdir, Mhintrange)
	H("tohintrange"   , Crange, "spawn"   , arg , cdir, Mhintrange) //backward
	}
#undef H

	Z("showdldir"   ,
		command(win, getset(win, "diropener") ?: MIMEOPEN, sfree(dldir(win)));
	)

	Z("yankuri"     ,
		gtk_clipboard_set_text(
			gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), URI(win), -1);
		showmsg(win, "URI is yanked to clipboard")
	)
	Z("yanktitle"   ,
		gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),
			webkit_web_view_get_title(win->kit) ?: "", -1);
		showmsg(win, "Title is yanked to clipboard")
	)
	Z("bookmark"    , addlink(win, webkit_web_view_get_title(win->kit), URI(win)))
	Z("bookmarkbreak", addlink(win, NULL, NULL))


	Z("reloadashtml5", xhtml_to_html5(win));
	Z("readermode", readability_mode(win));

	Z("quit"        , gtk_widget_destroy(win->winw); return true)

	if (win->mode == Mpointer)
	{
		Z("scrolldown" , pmove(win, GDK_KEY_Down))
		Z("scrollup"   , pmove(win, GDK_KEY_Up))
		Z("scrollleft" , pmove(win, GDK_KEY_Left))
		Z("scrollright", pmove(win, GDK_KEY_Right))
	}
	bool arrow = getsetbool(win, "hjkl2arrowkeys");
	Z(arrow ? "scrolldown"  : "arrowdown" , putkey(win, GDK_KEY_Down))
	Z(arrow ? "scrollup"    : "arrowup"   , putkey(win, GDK_KEY_Up))
	Z(arrow ? "scrollleft"  : "arrowleft" , putkey(win, GDK_KEY_Left))
	Z(arrow ? "scrollright" : "arrowright", putkey(win, GDK_KEY_Right))
	Z(arrow ? "arrowdown"  : "scrolldown" , scroll(win, 0, 1))
	Z(arrow ? "arrowup"    : "scrollup"   , scroll(win, 0, -1))
	Z(arrow ? "arrowleft"  : "scrollleft" , scroll(win, -1, 0))
	Z(arrow ? "arrowright" : "scrollright", scroll(win, 1, 0))

	Z("pagedown"    , putkey(win, GDK_KEY_Page_Down))
	Z("pageup"      , putkey(win, GDK_KEY_Page_Up))
	Z("halfdown"    , send(win, Cscroll, "d"))
	Z("halfup"      , send(win, Cscroll, "u"))

	Z("top"         , putkey(win, GDK_KEY_Home))
	Z("bottom"      , putkey(win, GDK_KEY_End))
	Z("zoomin"      ,
			double z = webkit_web_view_get_zoom_level(win->kit);
			webkit_web_view_set_zoom_level(win->kit, z * 1.06))
	Z("zoomout"     ,
			double z = webkit_web_view_get_zoom_level(win->kit);
			webkit_web_view_set_zoom_level(win->kit, z / 1.06))
	Z("zoomreset"   ,
			webkit_web_view_set_zoom_level(win->kit, 1.0))

	Z("nextwin"     , nextwin(win, true))
	Z("prevwin"     , nextwin(win, false))
	Z("winlist"     ,
			if (inwins(win, NULL, true) > 0)
				win->mode = Mlist;
			else
				showmsg(win, "No other windows");
	)

	Z("quitnext"    , return quitnext(win, true))
	Z("quitprev"    , return quitnext(win, false))

	Z("back"        ,
			if (webkit_web_view_can_go_back(win->kit))
			webkit_web_view_go_back(win->kit);
			else
			showmsg(win, "No Previous Page")
	)
	Z("forward"     ,
			if (webkit_web_view_can_go_forward(win->kit))
			webkit_web_view_go_forward(win->kit);
			else
			showmsg(win, "No Next Page")
	 )
	Z("stop"        , webkit_web_view_stop_loading(win->kit))
	Z("reload"      , webkit_web_view_reload(win->kit))
	Z("reloadbypass", vsh_clear(win);
			webkit_web_view_reload_bypass_cache(win->kit))

	Z("find"        , win->mode = Mfind)
	Z("findnext"    , findnext(win, true))
	Z("findprev"    , findnext(win, false))

#define CLIP(clip) \
	char *val = gtk_clipboard_wait_for_text(gtk_clipboard_get(clip)); \
	if (val) setent(win, val); \
	run(win, "find", val); \
	g_free(val); \
	senddelay(win, Cfocus, NULL);

	Z("findselection", CLIP(GDK_SELECTION_PRIMARY))
	Z("findclipboard", CLIP(GDK_SELECTION_CLIPBOARD))
	Z("findsecondary", CLIP(GDK_SELECTION_SECONDARY))
#undef CLIP

	Z("open"        , win->mode = Mopen)
	Z("edituri"     ,
			win->mode = Mopen;
			setent(win, arg ?: win->focusuri ?: URI(win)))
	Z("opennew"     , win->mode = Mopennew)
	Z("editurinew"  ,
			win->mode = Mopennew;
			setent(win, arg ?: win->focusuri ?: URI(win)))

//	Z("showsource"  , )
	Z("showhelp"    , openuri(win, APP":help"))
	Z("showhistory" , openuri(win, APP":history"))
	Z("showhistoryall", openuri(win, APP":history/all"))
	Z("showmainpage", openuri(win, APP":main"))

	Z("clearallwebsitedata",
			WebKitWebsiteDataManager *mgr =
				webkit_web_context_get_website_data_manager(ctx);
			webkit_website_data_manager_clear(mgr,
				WEBKIT_WEBSITE_DATA_ALL, 0, NULL, NULL, NULL);

			removehistory();
			if (!getsetbool(win, "keepfavicondb"))
				webkit_favicon_database_clear(
					webkit_web_context_get_favicon_database(ctx));

			showmsg(win, action);
	)
	Z("edit"        , openconf(win, false))
	Z("editconf"    , openconf(win, true))
	Z("openconfigdir",
			command(win, getset(win, "diropener") ?: MIMEOPEN, sfree(path2conf(arg))))

	Z("setv"        , return run(win, "set", "v"))
	Z("setscript"   , return run(win, "set", "script"))
	Z("setimage"    , return run(win, "set", "image"))
	bool unset = false;
	if (!strcmp(action, "unset"))
		action = (unset = arg) ? "set" : "setstack";
	Z("set"         ,
			char **os = &win->overset;
			char **ss = g_strsplit(*os ?: "", "/", -1);
			GFA(*os, NULL)
			if (arg) for (char **s = ss; *s; s++)
			{
				if (g_strcmp0(*s, arg))
					GFA(*os, g_strconcat(*os ?: *s, *os ? "/" : NULL, *s, NULL))
				else
					unset = true;
			}
			if (!unset)
				GFA(*os, g_strconcat(*os ?: arg, *os ? "/" : NULL, arg, NULL))
			g_strfreev(ss);
			resetconf(win, NULL, 2))
	Z("set2"        ,
			GFA(win->overset, g_strdup(arg))
			resetconf(win, NULL, 2))
	Z("setstack"    ,
			char **os = &win->overset;
			if (arg)
				GFA(*os, g_strconcat(*os ?: arg, *os ? "/" : NULL, arg, NULL))
			else if (*os && strrchr(*os, '/'))
				*(strrchr(*os, '/')) = '\0';
			else if (*os)
				GFA(*os, NULL)
			resetconf(win, NULL, 2))

	Z("wbnoreload", wbreload = false) //internal
	Z("addwhitelist", send(win, Cwhite, "white"))
	Z("addblacklist", send(win, Cwhite, "black"))

	Z("inspector",
	  WebKitWebInspector *inspector =
	  webkit_web_view_get_inspector(win->kit);
	  if (webkit_web_inspector_is_attached(inspector))
		  webkit_web_inspector_close(inspector);
	  else
		  webkit_web_inspector_show(inspector);
		)

	xwinid = win->sxid;
	Z("surfcmdprompt", surf_cmdprompt(win))
	Z("surffind", SETPROP("_SURF_FIND", "find", "Find:"))
	Z("surfgo", SETPROP("_SURF_URI", "open", "Go:"))
	Z("surfcharset", SETPROP("_SURF_CHARSET", "customcharset", "Charset:"))
	Z("surfapplystyle", SETPROP("_SURF_STYLE", "applystyle", "Stylesheet:"))

	Z("cookiepolicy",
	  WebKitCookieManager *mgr = webkit_web_context_get_cookie_manager(webkit_web_view_get_context(win->kit));
	  static struct cookie_policy_action_data cbdata;
	  cbdata.win = win;
	  cbdata.action = COOKIES_CYCLE;
	  webkit_cookie_manager_get_accept_policy(mgr, NULL, togglecookiepolicycb, &cbdata))

	Z("savesource",
	  WebKitWebResource *res = webkit_web_view_get_main_resource(win->kit);
	  if (res) {
		  WebKitURIResponse *resp = webkit_web_resource_get_response(res);
		  char *dest = resp ? make_savepath(webkit_uri_response_get_uri(resp), dldir(NULL)) : NULL;
		  if (!dest) fprintf(stderr, "savesource: no target path\n");
		  else if (g_file_test(dest, G_FILE_TEST_EXISTS)) {
			  fprintf(stderr, "savesource: not overwriting %s\n", dest);
			  g_free(dest);
		  } else webkit_web_resource_get_data(res, NULL, savesourcecb, dest);
	  } else fprintf(stderr, "savesource: no main resource to save\n");
		)

	Z("savemhtml",
	  char *dest = make_savepath(URI(win), dldir(NULL));
	  if (!dest)
		  fprintf(stderr, "savemhtml: no target path\n");
	  else {
		  char *tmp = g_strdup_printf("%s.mhtml", dest);
		  g_free(dest);
		  if (g_file_test(tmp, G_FILE_TEST_EXISTS)) {
			  fprintf(stderr, "savemhtml: not overwriting %s\n", tmp);
			  g_free(tmp);
		  } else {
			  GFile *gfile = g_file_new_for_path(tmp);
			  g_free(tmp);
			  webkit_web_view_save_to_file
				  (win->kit, gfile, WEBKIT_SAVE_MODE_MHTML,
				   NULL, savemhtmlcb, gfile); }})

	Z("viewsource", viewsourceorheaders(win, VSH_SOURCE))
	Z("viewheaders", viewsourceorheaders(win, VSH_HEADERS))

	Z("revealhint",
	  int x; int y; char *txt;
	  gdk_window_get_device_position(gdkw(win->kitw), pointer(), &x, &y, NULL);
	  txt = parse_hintdata_at(win, x, y);
//	  fprintf(stderr, "pointer position: %d,%d hinttext=%s\n", x, y, txt ?: "<unknown>");
	  if (txt) send(win, Creveal, txt))

	Z("showcert",
	  GTlsCertificate *cert = win->failedcert ? win->failedcert : win->cert;
	  GcrCertificate *gcrt;
	  GByteArray *crt;
	  GtkWidget *widget;
	  GcrCertificateWidget *wcert;
	  if (cert) {
	    g_object_get(cert, "certificate", &crt, NULL);
	    gcrt = gcr_simple_certificate_new(crt->data, crt->len);
	    g_byte_array_unref(crt);

	    widget = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	    wcert = gcr_certificate_widget_new(gcrt);
	    g_object_unref(gcrt);

	    gtk_container_add(GTK_CONTAINER(widget), GTK_WIDGET(wcert));
	    gtk_widget_show_all(widget);
	  })

	Z("textlink", textlinktry(win));
	Z("raise"   , present(arg ? winbyid(arg) ?: win : win))
	ZZ("winpos", "winsize",
		agv = g_strsplit(arg ?: "100:100", ":", 2);
		(!strcmp(action, "winpos") ? gtk_window_move : gtk_window_resize)
			(win->win, atoi(*agv), atoi(agv[1] ?: exarg)))

	char *msg = g_strdup_printf("Invalid action! %s arg: %s", action, arg);
	showmsg(win, msg);
	puts(msg);
	g_free(msg);
	return false;

#undef ZZ
#undef Z
out:
	if (win) update(win);
	if (agv) g_strfreev(agv);
	return true;
}
bool run(Win *win, char* action, const char *arg)
{
	return _run(win, action, arg, NULL, NULL);
}
static bool setact(Win *win, char *key, const char *spare)
{
	char *act = getset(win, key);
	if (!act) return false;
	char **acta = g_strsplit(act, " ", 2);
	run(win, acta[0], acta[1] ?: spare);
	g_strfreev(acta);
	return true;
}


//@@win and cbs:
static gboolean focuscb(Win *win)
{
	if (LASTWIN->mode == Mlist && win != LASTWIN)
		tonormal(LASTWIN);

	g_ptr_array_remove(wins, win);
	g_ptr_array_insert(wins, 0, win);

	checkconf(NULL); //to create conf
//	fixhist(win); // only load events to history ;madhu 180604

	return false;
}


//@download
typedef struct {
	union {
		GtkWindow *win;
		GtkWidget *winw;
		GObject   *wino;
	};
	union {
		GtkProgressBar *prog;
		GtkWidget      *progw;
	};
	union {
		GtkBox    *box;
		GtkWidget *boxw;
	};
	union {
		GtkEntry  *ent;
		GtkWidget *entw;
	};
	WebKitDownload *dl;
	char   *name;
	char   *dldir;
	const char *dispname;
	guint64 len;
	bool    res;
	bool    finished;
	bool    operated;
	int     closemsec;
} DLWin;
static gboolean dlbtncb(GtkWidget *w, GdkEventButton *e, DLWin *win)
{
	if (win->finished)
		win->operated = true;
	return false;
}
static void addlabel(DLWin *win, const char *str)
{
	GtkWidget *lbl = gtk_label_new(str);
	gtk_label_set_selectable((GtkLabel *)lbl, true);
	gtk_box_pack_start(win->box, lbl, true, true, 0);
	gtk_label_set_ellipsize((GtkLabel *)lbl, PANGO_ELLIPSIZE_MIDDLE);
	gtk_widget_show_all(lbl);
	SIG(lbl, "button-press-event", dlbtncb, win);
}
static void setdltitle(DLWin *win, char *title) //eaten
{
	gtk_window_set_title(win->win, sfree(g_strconcat(
					suffix, *suffix ? "| " : "",
					sfree(g_strdup_printf("DL: %s", sfree(title))), NULL)));
}
static void dldestroycb(DLWin *win)
{
	g_ptr_array_remove(dlwins, win);

	if (!win->finished)
		webkit_download_cancel(win->dl);

	g_free(win->name);
	g_free(win->dldir);
	g_free(win);

	quitif();
}
static gboolean dlclosecb(DLWin *win)
{
	if (isin(dlwins, win) && !win->operated)
		gtk_widget_destroy(win->winw);

	return false;
}

static void dlfincb(DLWin *win)
{
	if (!isin(dlwins, win) || win->finished) return;

	win->finished = true;

	char *title;
	if (win->res)
	{
		title = g_strdup_printf("Finished: %s", win->dispname);
		gtk_progress_bar_set_fraction(win->prog, 1);

		char *fn = g_filename_from_uri(
				webkit_download_get_destination(win->dl), NULL, NULL);

		const char *nfn = NULL;
		if (win->ent)
		{
			nfn = gtk_entry_get_text(win->ent);
			if (strcmp(fn, nfn) &&
				(g_file_test(nfn, G_FILE_TEST_EXISTS) ||
				 g_rename(fn, nfn) != 0)
			)
				nfn = fn; //failed

			gtk_widget_hide(win->entw);
		}

		addlabel(win, sfree(g_strdup_printf("=>  %s", nfn)));
		dltimestamp(nfn,
			    webkit_download_get_response(win->dl),
			    webkit_download_get_request(win->dl));

		g_free(fn);

		if (win->closemsec)
			g_timeout_add(win->closemsec, (GSourceFunc)dlclosecb, win);
	}
	else
		title = g_strdup_printf("Failed: %s", win->dispname);

	setdltitle(win, title);
}
static void dlfailcb(WebKitDownload *wd, GError *err, DLWin *win)
{
	if (!isin(dlwins, win)) return; //cancelled

	win->finished = true;

	addlabel(win, err->message);
	setdltitle(win,
			g_strdup_printf("Failed: %s - %s", win->dispname, err->message));
}
static void dldatacb(DLWin *win)
{
	double p = webkit_download_get_estimated_progress(win->dl);
	gtk_progress_bar_set_fraction(win->prog, p);

	setdltitle(win, g_strdup_printf("%.2f%%: %s ", (p * 100), win->dispname));
}
//static void dlrescb(DLWin *win) {}

// set an absolute filename
static void dlrescb(DLWin *win, GParamSpec pspec, gpointer _win)
{
	WebKitURIRequest *req = webkit_download_get_request(win->dl);
	const char *requri = webkit_uri_request_get_uri(req);
	const char *base = win->dldir ?: dldir(NULL);
	char *dest = make_savepath(requri, base);
	if (!dest) {
		char *title = "Failed to make destination";
		addlabel(win, "Failed to make destination");
		gtk_window_set_title(win->win,title);
		webkit_download_cancel(win->dl);
		return;
	}
	if (g_file_test(dest, G_FILE_TEST_EXISTS)) {
		addlabel(win, "fail! file already exists");
		addlabel(win, "Refusing to overwrite");
		char *title = "Refusing to overwrite";
		gtk_window_set_title(win->win,title);
		webkit_download_cancel(win->dl);
		g_free(dest);
		return;
	}
	GError *gerror = NULL;
	char *uri = g_filename_to_uri(dest, NULL, &gerror);
	if (!uri) {
		fprintf_gerror(stderr, gerror, "dlrescb: g_filename_to_uri failed: path:%s basedir:%s\n", dest, base);
		char *title="Failed to make destination uri";
		addlabel(win, "failed to make uri destination uri");
		gtk_window_set_title(win->win,title);
		webkit_download_cancel(win->dl);
		g_free(dest);
		return;
	}
	g_message("on notify::response, dlrescb set download destination to %s", uri);
	webkit_download_set_destination(win->dl, uri);
	g_free(dest);
}

static void dldestcb(DLWin *win)
{

	win->entw = gtk_entry_new();
	gtk_entry_set_text(win->ent, sfree(g_filename_from_uri(
			webkit_download_get_destination(win->dl), NULL, NULL)));
	gtk_entry_set_alignment(win->ent, .5);

	gtk_box_pack_start(win->box, win->entw, true, true, 4);
	gtk_widget_show_all(win->entw);
}
static gboolean dldecidecb(WebKitDownload *pdl, char *name, DLWin *win)
{
	goto skip_set_destination;
	char *path = g_build_filename(win->dldir, name, NULL);

	if (strcmp(win->dldir, sfree(g_path_get_dirname(path))))
		GFA(path, g_build_filename(win->dldir, name = "noname", NULL))

	mkdirif(path);

	char *org = g_strdup(path);
	//Last ext is duplicated for keeping order and easily rename
	char *ext = strrchr(org, '.');
	if (!ext || ext == org || !*(ext + 1) ||
			strlen(ext) > 4 + 1) //have not support long ext
		ext = "";
	for (int i = 2; g_file_test(path, G_FILE_TEST_EXISTS); i++)
		GFA(path, g_strdup_printf("%s.%d%s", org, i, ext))
	g_free(org);

	webkit_download_set_destination(pdl, sfree(
				g_filename_to_uri(path, NULL, NULL)));

	g_free(path);

skip_set_destination:

	//set view data
	win->res = true;

	win->name     = g_strdup(name);
	win->dispname = win->name ?: "";
	addlabel(win, win->name);

	WebKitURIResponse *res = webkit_download_get_response(win->dl);
	addlabel(win, webkit_uri_response_get_mime_type(res));
	win->len =  webkit_uri_response_get_content_length(res);

	if (win->len)
		addlabel(win, sfree(g_format_size(win->len)));
	return true;
}
static gboolean dlkeycb(GtkWidget *w, GdkEventKey *ek, DLWin *win)
{
	if (GDK_KEY_q == ek->keyval &&
			(!win->ent || !gtk_widget_has_focus(win->entw)))
		gtk_widget_destroy(w);

	if (win->finished) win->operated = true;
	return false;
}
static gboolean acceptfocuscb(GtkWindow *w)
{
	gtk_window_set_accept_focus(w, true);
	return false;
}
static void downloadcb(WebKitWebContext *ctx, WebKitDownload *pdl)
{
	DLWin *win = g_new0(DLWin, 1);
	win->dl    = pdl;

	WebKitWebView *kit = webkit_download_get_web_view(pdl);
	Win *mainwin = kit ? g_object_get_data(G_OBJECT(kit), "win") : NULL;
	win->dldir   = mainwin && mainwin->fordl && *mainwin->fordl ?
		g_strdup(mainwin->fordl) : dldir(mainwin);
	win->closemsec = getsetint(mainwin, "dlwinclosemsec");

	win->winw  = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	setdltitle(win, g_strdup("Waiting for a response."));
	gtk_window_set_default_size(win->win, 400, -1);
	SIGW(win->wino, "destroy"         , dldestroycb, win);
	SIG( win->wino, "key-press-event" , dlkeycb    , win);

	win->boxw = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_container_add(GTK_CONTAINER(win->win), win->boxw);

	win->progw = gtk_progress_bar_new();
	gtk_box_pack_end(win->box, win->progw, true, true, 0);

	GObject *o = (GObject *)win->dl;
	SIG( o, "decide-destination" , dldecidecb, win);
	SIGW(o, "created-destination", dldestcb  , win);
	SIGW(o, "notify::response"   , dlrescb   , win);
	SIG( o, "failed"             , dlfailcb  , win);
	SIGW(o, "finished"           , dlfincb   , win);
	SIGW(o, "received-data"      , dldatacb  , win);

	Win *reqwin = !mainwin ? LASTWIN :
		mainwin->fordl ? g_object_get_data(G_OBJECT(kit), "caller") : mainwin;
	if (reqwin)
	{
		int gy;
		gdk_window_get_geometry(gdkw(reqwin->winw), NULL, &gy, NULL, NULL);
		int x, y;
		gtk_window_get_position(reqwin->win, &x, &y);
		gtk_window_move(win->win, MAX(0, x - 400), y + gy);
	}

	if (getsetbool(mainwin, "dlwinback") && LASTWIN &&
			gtk_window_is_active(LASTWIN->win))
	{
		gtk_window_set_accept_focus(win->win, false);
		gtk_widget_show_all(win->winw);
//not works
//		gdk_window_restack(gdkw(win->winw), gdkw(LASTWIN->winw), false);
//		gdk_window_lower();
		gtk_window_present(LASTWIN->win);
		g_timeout_add(100, (GSourceFunc)acceptfocuscb, win->win);
	} else {
		gtk_widget_show_all(win->winw);
	}

	addlabel(win, webkit_uri_request_get_uri(webkit_download_get_request(pdl)));
	g_ptr_array_insert(dlwins, 0, win);

	if (mainwin && mainwin->fordl)
		run(mainwin, "quit", NULL);
}


//@uri scheme
static char *histdata(bool rest, bool all)
{
	GSList *hist = NULL;
	int num = 0;
	int size = all ? 0 : confint("histviewsize");

	int start = 0;
	for (int j = 2; j > 0; j--) for (int i = histfnum - 1; i >= 0; i--)
	{
		if (!rest && size && num >= size) break;
		if (start >= histfnum) break;

		char *path = g_build_filename(histdir, hists[i], NULL);
		bool exists = g_file_test(path, G_FILE_TEST_EXISTS);

		if (start) start++;
		else if (exists)
		{
			struct stat info;
			stat(path, &info);
			if (info.st_size < MAXSIZE)
				start = 1;
		}

		if (start && exists)
		{
			GSList *lf = NULL;
			GIOChannel *io = g_io_channel_new_file(path, "r", NULL);
			char *line;
			while (g_io_channel_read_line(io, &line, NULL, NULL, NULL)
					== G_IO_STATUS_NORMAL)
			{
				char **stra = g_strsplit(line, " ", 3);
				if (stra[0] && stra[1])
				{
					if (stra[2]) g_strchomp(stra[2]);
					lf = g_slist_prepend(lf, stra);
					num++;
				}
				else
					g_strfreev(stra);

				g_free(line);
			}
			if (lf) hist = g_slist_append(hist, lf);
			g_io_channel_unref(io);
		}
		g_free(path);
	}

	if (!num)
		return g_strdup("<h1>No Data</h1>");

	GString *ret = g_string_new(NULL);
	g_string_append_printf(ret,
		"<html><meta charset=utf8>\n"
		"<style>\n"
		"* {border-radius:.4em;}\n"
		"p {margin:.4em 0; white-space:nowrap;}\n"
		"p > * {display:inline-block; vertical-align:middle;}"
		"p {padding:.2em; color:inherit; text-decoration:none;}\n"
		"p:hover {background-color:#faf6ff}\n"
		"time {font-family:monospace;}\n"
		"p > span {padding:0 .4em 0 .6em; white-space:normal; word-wrap:break-word;}\n"
		"i {font-size:.79em; color:#43a;}\n"
		//for img
		"em {min-width:%dpx; text-align:center;}\n"
		"img {"
		" box-shadow:0 .1em .1em 0 #ccf;"
		" display:block;"
		" margin:auto;"
		"}\n"
		"</style>\n"
		, confint("histimgsize"));

	int i = 0;
	GList *il = confint("histimgs") ? g_queue_peek_head_link(histimgs) : NULL;
	for (GSList *ns = hist; ns; ns = ns->next)
		for (GSList *next = ns->data; next; next = next->next)
	{
		if (!size) ;
		else if (rest)
		{
			if (size > i++)
			{
				if (il) il = il->next;
				continue;
			}
		}
		else if (size == i++)
		{
			if (num > size)
				g_string_append(ret,
						"<h3><i>"
						"<a href="APP":history/rest>Show Rest</a>"
						"&nbsp|&nbsp;"
						"<a href="APP":history/all>Show All</a>"
						"</i></h3>");
			goto loopout;
		}

		char **stra = next->data;
		char *escpd = g_markup_escape_text(stra[2] ?: stra[1], -1);

		if (il)
		{
			char *itag = !il->data ? NULL : g_strdup_printf(
					"<img src="APP":histimg/%"G_GUINT64_FORMAT"></img>",
					((Img *)il->data)->id);

			g_string_append_printf(ret,
					"<p><em>%s</em>"
					"<span>%s<br><a href=%s><i>%s</i></a><br><time>%.11s</time></span>\n",
					itag ?: "-", escpd, stra[1], FORDISP(stra[1]), stra[0]);

			g_free(itag);
			il = il->next;
		} else
			g_string_append_printf(ret,
					"<p><time>%.11s</time>"
					"<span>%s<br><a href=%s><i>%s</i></a></span>\n",
					stra[0], escpd, stra[1], FORDISP(stra[1]));

		g_free(escpd);
	}
loopout:

	for (GSList *ns = hist; ns; ns = ns->next)
		g_slist_free_full(ns->data, (GDestroyNotify)g_strfreev);
	g_slist_free(hist);

	return g_string_free(ret, false);
}
static char *helpdata()
{
	GString *ret = g_string_new(NULL);
	g_string_append_printf(ret,
		"<body style=margin:0>\n"
		"<pre style=padding:.3em;background-color:#ccc;font-size:large>"
		"Last Key: %s<br>Last MSG: %s</pre>\n"
		"<pre style=margin:.3em;font-size:large>\n"
		"%s\n"
		"mouse:\n"
		"  rocker gesture:\n"
		"    left press and       -        right: back\n"
		"    left press and move right and right: forward\n"
		"    left press and move up    and right: raise bottom window and close\n"
		"    left press and move down  and right: raise next   window and close\n"
		"  middle button:\n"
		"    on a link            : new background window\n"
		"    on free space        : winlist\n"
		"    press and move left  : raise bottom window\n"
		"    press and move right : raise next   window\n"
		"    press and move up    : go to top\n"
		"    press and move down  : go to bottom\n"
		"    press and scroll up  : go to top\n"
		"    press and scroll down: go to bottom\n"
		"\n"
		"context-menu:\n"
		"  You can add your own script to the context-menu. See 'menu' dir in\n"
		"  the config dir, or click 'editMenu' in the context-menu.\n"
		"  Available actions are in the 'key:' section below and\n"
		"  following values are set as environment valriables.\n"
		"   URI TITLE FOCUSURI LINK LINK_OR_URI LINKLABEL\n"
		"   LABEL_OR_TITLE MEDIA IMAGE MEDIA_IMAGE_LINK\n"
		"   PRIMARY/SELECTION SECONDARY CLIPBOARD\n"
		"   ISCALLBACK SUFFIX CURRENTSET DLDIR CONFDIR WINID WINSLEN\n"
		"   WINX WINY WIDTH HEIGHT CANBACK CANFORWARD\n"
		"  Of course it supports directories and '.'.\n"
		"  '.' hides it from the menu but still available in the accels.\n"
		"accels:\n"
		"  You can add your own keys to access context-menu items we added.\n"
		"  To add Ctrl-Z to GtkAccelMap, insert '&lt;Primary&gt;&lt;Shift&gt;z' to the\n"
		"  last \"\" in the file 'accels' in the conf directory assigned 'c'\n"
		"  key, and remove the ';' at the beginning of the line. alt is &lt;Alt&gt;.\n"
		"\n"
		"key:\n"
		"#%d - is ctrl\n"
		"#(null) is only for scripts\n"
		, lastkeyaction, lastmsg, usage, GDK_CONTROL_MASK);

	for (int i = 0; i < sizeof(dkeys) / sizeof(*dkeys); i++)
		g_string_append_printf(ret, "%d - %-11s: %-19s: %s\n",
				dkeys[i].mask,
				gdk_keyval_name(dkeys[i].key),
				dkeys[i].name,
				dkeys[i].desc ?: "");

	return g_string_free(ret, false);
}
static cairo_status_t faviconcairocb(void *p,
		const unsigned char *data, unsigned int len)
{
	g_memory_input_stream_add_data((GMemoryInputStream *)p,
			g_memdup(data, len), len, g_free);
	return CAIRO_STATUS_SUCCESS;
}
static void faviconcb(GObject *src, GAsyncResult *res, gpointer p)
{
	WebKitURISchemeRequest *req = p;
	cairo_surface_t *suf = webkit_favicon_database_get_favicon_finish(
			webkit_web_context_get_favicon_database(ctx), res, NULL);
	GInputStream *st = g_memory_input_stream_new();
	if (suf)
	{
		cairo_surface_write_to_png_stream(suf, faviconcairocb, st);
		cairo_surface_destroy(suf);
	}
	webkit_uri_scheme_request_finish(req, st, -1, "image/png");

	g_object_unref(st);
	g_object_unref(req);
}
static void schemecb(WebKitURISchemeRequest *req, gpointer p)
{
	WebKitWebView *kit = webkit_uri_scheme_request_get_web_view(req);
	Win *win = kit ? g_object_get_data(G_OBJECT(kit), "win") : NULL;
	if (win) win->scheme = true;

	const char *path = webkit_uri_scheme_request_get_path(req);

	if (g_str_has_prefix(path, "f/"))
	{
		char *unesc = g_uri_unescape_string(path + 2, NULL);
		g_object_ref(req);
		webkit_favicon_database_get_favicon(
				webkit_web_context_get_favicon_database(ctx),
				unesc, NULL, faviconcb, req);
		g_free(unesc);
		return;
	}

	char *type = NULL;
	char *data = NULL;
	gsize len = 0;
	if (g_str_has_prefix(path, "histimg/"))
	{
		char **args = g_strsplit(path, "/", 2);
		if (*(args + 1))
		{
			guint64 id = g_ascii_strtoull(args[1], NULL, 0);
			for (GList *next = g_queue_peek_head_link(histimgs);
					next; next = next->next)
			{
				Img *img = next->data;
				if (!img || img->id != id) continue;

				type = "image/jpeg";
				data = g_memdup(img->buf, len = img->size);
				break;
			}
		}
		g_strfreev(args);
	}
	else if (g_str_has_prefix(path, "i/"))
	{
		GdkPixbuf *pix = gtk_icon_theme_load_icon(
			gtk_icon_theme_get_default(), path + 2, 256, 0, NULL);
		if (pix)
		{
			type = "image/png";
			gdk_pixbuf_save_to_buffer(pix, &data, &len, "png", NULL, NULL);
			g_object_unref(pix);
		}
	}
	if (!type)
	{
		type = "text/html";
		if (g_str_has_prefix(path, "main"))
		{
			preparemd();
			g_spawn_command_line_sync(
					sfree(g_strdup_printf(
							getset(win, "generator") ?: "cat %s", mdpath)),
					&data, NULL, NULL, NULL);
		}
		else if (g_str_has_prefix(path, "history"))
			data = histdata(
					g_str_has_prefix(path + 7, "/rest"),
					g_str_has_prefix(path + 7, "/all"));
		else if (g_str_has_prefix(path, "help"))
			data = helpdata();
		else if (g_str_has_prefix(path, "data")) {
			aboutDataHandleRequest(req, ctx);
			goto done;
		} else if (g_str_has_prefix(path, "itp")) {
			aboutITPHandleRequest(req, ctx);
			goto done;
		}
		if (!data)
			data = g_strdup("<h1>Empty</h1>");
		len = strlen(data);
	}

	GInputStream *st = g_memory_input_stream_new_from_data(data, len, g_free);
	webkit_uri_scheme_request_finish(req, st, len, type);
	g_object_unref(st);

done:
	gtk_window_set_icon(win->win, NULL);
}


//@kit's cbs
static gboolean detachcb(GtkWidget * w)
{
	gtk_widget_grab_focus(w);
	return false;
}
static void drawhint(Win *win, cairo_t *cr, PangoFontDescription *desc,
		bool center, int x, int y, int w, int h,
		int len, bool head, char *txt)
{
	int r = x + w;
	int b = y + h;
	int fsize = pango_font_description_get_size(desc) / PANGO_SCALE;

	//area
	cairo_set_source_rgba(cr, .6, .4, .9, .1);

	arcrect(cr, fsize/2, x, y, r, b);
	cairo_fill(cr);

	if (!head) return;

	//hintelm
	txt += len;
	PangoLayout *layout = gtk_widget_create_pango_layout(win->winw, txt);
	pango_layout_set_font_description(layout, desc);

	PangoRectangle inkrect, logicrect;
	pango_layout_get_pixel_extents(layout, &inkrect, &logicrect);
	int m = fsize/4.1;
	w = logicrect.width + m*2;
	h = inkrect.height + m*2;
	x = (x + r - w) / 2;
	y = MAX(-m, y + (center ? h/2 : -h/2 - 1));

	cairo_pattern_t *ptrn =
		cairo_pattern_create_linear(x, 0,  x + w, 0);

	static GdkRGBA ctop, cbtm, ntop, nbtm;
	static bool ready;
	if (!ready)
	{
		gdk_rgba_parse(&ctop, "darkorange");
		gdk_rgba_parse(&cbtm, "red");
		gdk_rgba_parse(&ntop, "#649");
		gdk_rgba_parse(&nbtm, "#326");
		ready = true;
	}
#define Z(o, r) \
	cairo_pattern_add_color_stop_rgba(ptrn, o, r.red, r.green, r.blue, r.alpha);
	Z(0, (center ? ctop : ntop));
	Z(.3, (center ? cbtm : nbtm));
	Z(.7, (center ? cbtm : nbtm));
	Z(1, (center ? ctop : ntop));
#undef Z
	cairo_set_source(cr, ptrn);

	arcrect(cr, MIN(w, h)/4, x, y, x + w, y + h);
	cairo_fill(cr);

	cairo_set_source_rgba(cr, 1., 1., 1., 1.);
	cairo_move_to(cr, x + m, y + m - inkrect.y);
	pango_cairo_show_layout(cr, layout);

	cairo_pattern_destroy(ptrn);
	g_object_unref(layout);
}
static gboolean drawcb(GtkWidget *ww, cairo_t *cr, Win *win)
{
	//wayland with hardware-acceleration kills drawcb without this
	gdk_window_mark_paint_from_clip(gdkw(ww), cr);

	if (win->mode != Mlist && (win->lastx || win->lasty || win->mode == Mpointer))
	{
		guint csize = gdk_display_get_default_cursor_size(
				gtk_widget_get_display(win->winw));

		double x, y, size;
		if (win->mode == Mpointer)
			x = win->px, y = win->py, size = csize * .6;
		else
			x = win->lastx, y = win->lasty, size = csize * .2;

		cairo_move_to(cr, x, y - size);
		cairo_line_to(cr, x, y + size);
		cairo_move_to(cr, x - size, y);
		cairo_line_to(cr, x + size, y);

		cairo_set_line_width(cr, size / 6);
		colorb(win, cr, 1);
		cairo_stroke_preserve(cr);
		colorf(win, cr, 1);
		cairo_set_line_width(cr, size / 12);
		cairo_stroke(cr);
	}
	if (win->msg)
	{
		PangoLayout *layout =
			gtk_widget_create_pango_layout(win->winw, win->msg);
		PangoFontDescription *desc = pango_font_description_copy(
				pango_context_get_font_description(
					gtk_widget_get_pango_context(win->winw)));
		pango_font_description_set_size(desc,
				pango_font_description_get_size(desc) * 1.6);

		pango_layout_set_font_description(layout, desc);
		pango_font_description_free(desc);

		int w, h;
		pango_layout_get_pixel_size(layout, &w, &h);

		int y = gtk_widget_get_allocated_height(win->kitw) - h*1.4;
		y -= gtk_widget_get_visible(win->entw) ?
				gtk_widget_get_allocated_height(win->entw) : 0;

		colorb(win, cr, .8);
		cairo_rectangle(cr, 0, y, w + h*.7, h);
		cairo_fill(cr);

		colorf(win, cr, .9);
		cairo_move_to(cr, h*.6, y);
		pango_cairo_show_layout(cr, layout);

		g_object_unref(layout);
	}
	if (win->progd != 1)
	{
		guint32 fsize = MAX(10,
				webkit_settings_get_default_font_size(win->set));

		int h = gtk_widget_get_allocated_height(win->kitw);
		int w = gtk_widget_get_allocated_width(win->kitw);
		h -= gtk_widget_get_visible(win->entw) ?
				gtk_widget_get_allocated_height(win->entw) : 0;

		int px, py;
		gdk_window_get_device_position(
				gdkw(win->kitw), pointer(), &px, &py, NULL);

		double alpha = !gtk_widget_has_focus(win->kitw) ? .6 :
			px > 0 && px < w ? MIN(1, .3 + ABS(h - py) / (h * .1)): 1.0;

		double base = fsize/14. + (fsize/6.) * pow(1 - win->progd, 4);
		//* 2: for monitors hide bottom pixels when viewing top to bottom
		double y = h - base * 2;

		cairo_set_line_width(cr, base * 1.4);
		cairo_move_to(cr, 0, y);
		cairo_line_to(cr, w, y);
		colorb(win, cr, alpha * .6);
		cairo_stroke(cr);

		win->progrect = (GdkRectangle){0, y - base - 1, w, y + base * 2 + 2};

		cairo_set_line_width(cr, base * 2);
		cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
		cairo_move_to(cr,     w/2 * win->progd, y);
		cairo_line_to(cr, w - w/2 * win->progd, y);
		colorf(win, cr, alpha);
		cairo_stroke(cr);
	} else
		win->progrect.width = 0;
	if (win->hintdata)
	{
		PangoFontDescription *desc = pango_font_description_copy(
				pango_context_get_font_description(
					gtk_widget_get_pango_context(win->winw)));

		pango_font_description_set_family(desc, "monospace");
		pango_font_description_set_weight(desc, PANGO_WEIGHT_BOLD);

		double zoom = webkit_web_view_get_zoom_level(win->kit);
		char **hints = g_strsplit(win->hintdata, ";", -1);
		for (char **lh = hints; *lh && **lh; lh++)
		{
			char *h = *lh;
			//0   123*   141*   190*   164*  0*1FF //example
			h[7]=h[14]=h[21]=h[28]=h[32] = '\0';
#define Z(i) atoi(h + i) * zoom
			drawhint(win, cr, desc, *h == '1',
				Z(1), Z(8), Z(15), Z(22), atoi(h + 29),
				h[33] == '1', h + 34);

#undef Z
		}
		g_strfreev(hints);
		pango_font_description_free(desc);
	}

	winlist(win, 0, cr);
	return false;
}
static void destroycb(Win *win)
{
	g_ptr_array_remove(wins, win);

	quitif();

	g_free(win->winid);
	g_slist_free_full(win->ipcids, g_free);
	g_free(win->lasturiconf);
	g_free(win->lastreset);
	g_free(win->overset);
	g_free(win->msg);

	setresult(win, NULL);
	g_free(win->focusuri);

	g_slist_free_full(win->undo, g_free);
	g_slist_free_full(win->redo, g_free);
	g_free(win->lastsearch);

	g_free(win->histstr);
	g_free(win->hintdata);
	g_free(win->fordl);

	//spawn
	spawnfree(win->spawn, true);

	g_free(win->v.uri);
	g_free(win->v.mimetype);
	g_bytes_unref(win->v.source);
	g_bytes_unref(win->v.headers);

	g_free(win);
}
static void crashcb(Win *win)
{
	win->crashed = true;
	tonormal(win);
}
static void notifycb(Win *win) { update(win); }
static void drawprogif(Win *win, bool force)
{
	if ((win->progd != 1 || force) && win->progrect.width)
		gdk_window_invalidate_rect(gdkw(win->kitw), &win->progrect, TRUE);
}
static gboolean drawprogcb(Win *win)
{
	if (!isin(wins, win)) return false;
	double shift = win->prog + .4 * (1 - win->prog);
	if (shift - win->progd < 0) return true; //when reload prog is may mixed
	win->progd = shift - (shift - win->progd) * .96;
	drawprogif(win, false);
	return true;
}
static void progcb(Win *win)
{
	win->prog = webkit_web_view_get_estimated_load_progress(win->kit);
	//D(prog %f, win->prog)

	if (win->prog > .3) //.3 emits after other events just about
		updatehist(win);
}
static void favcb(Win *win)
{
	cairo_surface_t *suf;
	//workaround: webkit_web_view_get_favicon returns random icon when APP:
	if (!g_str_has_prefix(URI(win), APP":") &&
			(suf = webkit_web_view_get_favicon(win->kit)))
	{
		GdkPixbuf *pix = gdk_pixbuf_get_from_surface(suf, 0, 0,
					cairo_image_surface_get_width(suf),
					cairo_image_surface_get_height(suf));

		gtk_window_set_icon(win->win, pix);
		g_object_unref(pix);
	}
	else
		gtk_window_set_icon(win->win, NULL);
}
static bool checkppress(Win *win, guint key)
{
	if (!win->ppress || (key && key != win->ppress)) return false;
	win->ppress = 0;
	putbtn(win, GDK_BUTTON_RELEASE, win->pbtn);
	tonormal(win);
	return true;
}
static bool keyr = true;
static gboolean keycb(GtkWidget *w, GdkEventKey *ek, Win *win)
{
	if (ek->is_modifier) return false;

	if (win->mode == Mpointer &&
			(ek->keyval == GDK_KEY_space || ek->keyval == GDK_KEY_Return))
	{
		if (!win->ppress)
		{
			putbtn(win, GDK_BUTTON_PRESS, win->pbtn);
			win->ppress = ek->keyval;
		}
		return true;
	}

	keyr = true;
	char *action = ke2name(win, ek);

	if (action) fprintf(stderr,"keycb: action=%s\n", action);

	if (action && !strcmp(action, "tonormal"))
	{
		if (win->lastx || win->lasty)
		{
			win->lastx = win->lasty = 0;
			gtk_widget_queue_draw(win->canvas);
		}

		keyr = !(win->mode & (Mnormal | Minsert));

		if (win->mode == Mpointer)
			win->px = win->py = 0;

		if (win->mode == Mnormal)
		{
			send(win, Cblur, NULL);
			webkit_find_controller_search_finish(win->findct);
		}
		else
			tonormal(win);

		return keyr;
	}

	if (action && strcmp(action, "selectall") == 0) {
		if (GTK_IS_EDITABLE (w))
			gtk_editable_select_region (GTK_EDITABLE (w), 0, -1);
		 else
			 webkit_web_view_execute_editing_command (win->kit, "SelectAll");
		return true;
	}

	if ((!action || win->mode == Minsert) &&
			(ek->keyval == GDK_KEY_Tab || ek->keyval == GDK_KEY_ISO_Left_Tab))
		senddelay(win, Cmode, NULL);

	if (win->mode == Minsert)
	{
		if (ek->state & GDK_CONTROL_MASK &&
				(ek->keyval == GDK_KEY_z || ek->keyval == GDK_KEY_Z))
		{
			if (ek->state & GDK_SHIFT_MASK)
				webkit_web_view_execute_editing_command(win->kit, "Redo");
			else
				webkit_web_view_execute_editing_command(win->kit, "Undo");

			return true;
		}
		if (action && !strcmp(action, "textlink"))
			return run(win, action, NULL);

		return keyr = false;
	}

	if (win->mode & Mhint && !(ek->state & GDK_CONTROL_MASK) &&
			(ek->keyval == GDK_KEY_Tab || ek->keyval == GDK_KEY_Return
			 || (ek->keyval < 128
				 && strchr(getset(win, "hintkeys") ?: "", ek->keyval)))
	) {
		char key[2] = {0};
		*key = ek->keyval;
		send(win, Ckey, key);
		return true;
	}

	if (win->mode == Mlist)
	{
#define Z(str, func) if (action && !strcmp(action, str)) {func;}
		Z("scrolldown"  , winlist(win, GDK_KEY_Down , NULL))
		Z("scrollup"    , winlist(win, GDK_KEY_Up   , NULL))
		Z("scrollleft"  , winlist(win, GDK_KEY_Left , NULL))
		Z("scrollright" , winlist(win, GDK_KEY_Right, NULL))

		Z("arrowdown"  , winlist(win, GDK_KEY_Page_Down , NULL))
		Z("arrowup"    , winlist(win, GDK_KEY_Page_Up   , NULL))

		Z("quit"     , winlist(win, 3, NULL))
		Z("quitnext" , winlist(win, 3, NULL))
		Z("quitprev" , winlist(win, 3, NULL))

		Z("winlist"  , tonormal(win); return true;)
#undef Z
		switch (ek->keyval) {
		case GDK_KEY_Page_Down:
		case GDK_KEY_Page_Up:
		case GDK_KEY_Down:
		case GDK_KEY_Up:
		case GDK_KEY_Left:
		case GDK_KEY_Right:
			winlist(win, ek->keyval, NULL);
			break;

		case GDK_KEY_Return:
		case GDK_KEY_space:
			winlist(win, 1, NULL);
			return true;

		case GDK_KEY_BackSpace:
		case GDK_KEY_Delete:
			winlist(win, 3, NULL);
			return true;
		}
		gtk_widget_queue_draw(win->canvas);
		return true;
	}

	win->userreq = true;

	if (!action)
		return keyr = false;

	if (strcmp(action, "showhelp"))
		GFA(lastkeyaction, g_strdup_printf("%d+%s -> %s",
					ek->state, gdk_keyval_name(ek->keyval), action))
	run(win, action, NULL);

	return true;
}
static gboolean keyrcb(GtkWidget *w, GdkEventKey *ek, Win *win)
{
	if (checkppress(win, ek->keyval)) return true;

	if (ek->is_modifier) return false;
	return keyr;
}
static bool ignoretargetcb;
static void targetcb(
		WebKitWebView *w,
		WebKitHitTestResult *htr,
		guint m,
		Win *win)
{
	//workaround: when context-menu shown this is called with real pointer pos
	if (ignoretargetcb) return;
	setresult(win, htr);
	update(win);
}
static GdkEvent *pendingmiddlee;
static bool cancelbtn1r;
static bool cancelbtn3r;
static bool cancelmdlr;
static bool cancel23;
static gboolean btncb(GtkWidget *w, GdkEventButton *e, Win *win)
{
	win->userreq = true;
	if (e->type != GDK_BUTTON_PRESS) return cancel23;
	cancel23 = cancelbtn1r = cancelbtn3r = cancelmdlr = false;
	altcur(win, e->x, e->y); //clears if it is alt cur

	if (win->mode == Mlist)
	{
		win->cursorx = win->cursory = 0;
		if (e->button == 1)
			cancelbtn1r = true;
		if ((e->button != 1 && e->button != 3)
				|| !winlist(win, e->button, NULL))
			tonormal(win);

		if (e->button == 3)
		{
			cancelbtn3r = true;
			GdkEvent *ne = gdk_event_peek();
			if (!ne) return true;
			if (ne->type == GDK_2BUTTON_PRESS || ne->type == GDK_3BUTTON_PRESS)
				cancel23 = true;
			gdk_event_free(ne);
		}
		return true;
	}

	if (win->mode != Mpointer || !win->ppress)
	{
		if (win->oneditable)
			win->mode = Minsert;
		else
			win->mode = Mnormal;
	}

	update(win);

	//D(event button %d, e->button)
	switch (e->button) {
	case 1:
	case 2:
		win->lastx = e->x;
		win->lasty = e->y;
		gtk_widget_queue_draw(win->canvas);

	if (e->button == 1) break;
	{
		if (e->send_event)
		{
			win->lastx = win->lasty = 0;
			break;
		}

		if (pendingmiddlee)
			gdk_event_free(pendingmiddlee);
		pendingmiddlee = gdk_event_copy((GdkEvent *)e);
		return true;
	}
	case 3:
		if (!(e->state & GDK_BUTTON1_MASK))
			return win->crashed ?
				run(win, "reload", NULL) : false;

		if (!win->lastx && !win->lasty) break;
		cancel23 = cancelbtn1r = cancelbtn3r = true;

		double deltax = e->x - win->lastx,
		       deltay = e->y - win->lasty;

		if ((pow(deltax, 2) + pow(deltay, 2)) < thresholdp(win) * 9)
		{ //default
			setact(win, "rockerleft", URI(win));
		}
		else if (fabs(deltax) > fabs(deltay)) {
			if (deltax < 0) //left
				setact(win, "rockerleft", URI(win));
			else //right
				setact(win, "rockerright", URI(win));
		} else {
			if (deltay < 0) //up
				setact(win, "rockerup", URI(win));
			else //down
				setact(win, "rockerdown", URI(win));
		}

		return true;
	default:
		return setact(win,
				sfree(g_strdup_printf("button%d", e->button)), URI(win));
	}

	return false;
}
static gboolean btnrcb(GtkWidget *w, GdkEventButton *e, Win *win)
{
	switch (e->button) {
	case 1:
		win->lastx = win->lasty = 0;
		gtk_widget_queue_draw(win->canvas);

		if (cancelbtn1r) return true;
		break;
	case 2:
	{
		bool cancel = cancelmdlr || !(win->lastx + win->lasty);
		double deltax = e->x - win->lastx;
		double deltay = e->y - win->lasty;
		win->lastx = win->lasty = 0;

		if (cancel) return true;

		if ((pow(deltax, 2) + pow(deltay, 2)) < thresholdp(win) * 4)
		{ //default
			if (win->oneditable)
			{
				((GdkEventButton *)pendingmiddlee)->send_event = true;
				gtk_widget_event(win->kitw, pendingmiddlee);
				gdk_event_free(pendingmiddlee);
				pendingmiddlee = NULL;
			}
			else if (win->link)
				setact(win, "mdlbtnlinkaction", win->link);
			else if (gtk_window_is_active(win->win))
				setact(win, "mdlbtnspace", URI(win));
		}
		else if (fabs(deltax) > fabs(deltay)) {
			if (deltax < 0) //left
				setact(win, "mdlbtnleft", URI(win));
			else //right
				setact(win, "mdlbtnright", URI(win));
		} else {
			if (deltay < 0) //up
				setact(win, "mdlbtnup", URI(win));
			else //down
				setact(win, "mdlbtndown", URI(win));
		}

		gtk_widget_queue_draw(win->canvas);

		return true;
	}
	case 3:
		if (cancelbtn3r) return true;
		win->lastx = win->lasty = 0;
	}

	update(win);
	return false;
}
static void dragccb(GdkDragContext *ctx, GdkDragCancelReason reason, Win *win)
{
	if (reason != GDK_DRAG_CANCEL_NO_TARGET) return;

	GdkWindow *gw = gdkw(win->kitw);
	GdkDevice *gd = gdk_drag_context_get_device(ctx);
	GdkModifierType mask;
	gdk_device_get_state(gd, gw, NULL, &mask);

	if (mask & GDK_BUTTON1_MASK || mask & GDK_BUTTON3_MASK)
	{ //we assume this is right click though it only means a btn released
		double px, py;
		gdk_window_get_device_position_double(gw, gd, &px, &py, NULL);
		_putbtn(win, GDK_BUTTON_PRESS, 13, px, py);
		if (!(mask & GDK_BUTTON1_MASK))
			_putbtn(win, GDK_BUTTON_RELEASE, 1, px, py);
	}
}
static void dragbcb(GtkWidget *w, GdkDragContext *ctx ,Win *win)
{
	if (win->mode == Mpointer)
	{
		showmsg(win, "Pointer Mode does not support real drag");
		putkey(win, GDK_KEY_Escape);
		checkppress(win, 0);
	}
	else
		SIG(ctx, "cancel", dragccb, win);
}
static gboolean entercb(GtkWidget *w, GdkEventCrossing *e, Win *win)
{ //for checking drag end with button1
	static int mask = GDK_BUTTON1_MASK | GDK_BUTTON2_MASK | GDK_BUTTON3_MASK;
	if (!(e->state & mask) && win->lastx + win->lasty)
	{
		win->lastx = win->lasty = 0;
		gtk_widget_queue_draw(win->canvas);
	}

	checkppress(win, 0); //right click
	return false;
}
static gboolean leavecb(GtkWidget *w, GdkEventCrossing *e, Win *win)
{
	return false;
}
static gboolean motioncb(GtkWidget *w, GdkEventMotion *e, Win *win)
{
	if (win->mode == Mlist)
	{
		win->lastx = e->x;
		win->lasty = e->y;

		if (win->scrlf &&
				(pow(e->x - win->scrlx, 2) + pow(e->y - win->scrly, 2))
				 < thresholdp(win) * 4)
			return true;

		win->scrlf = false;
		win->scrlcur = 0;
		win->cursorx = win->cursory = 0;
		gtk_widget_queue_draw(win->canvas);

		static GdkCursor *hand = NULL;
		if (!hand) hand = gdk_cursor_new_for_display(
				gdk_display_get_default(), GDK_HAND2);
		gdk_window_set_cursor(gdkw(win->kitw),
				winlist(win, 0, NULL) ? hand : NULL);

		return true;
	}

	return false;
}

typedef struct {
	int times;
	GdkEvent *e;
} Scrl;
static int scrlcnt;
static gboolean multiscrlcb(Scrl *si)
{
	if (si->times--)
	{
		scrlcnt--;
		gdk_event_put(si->e);
		return true;
	}

	scrlcnt--;
	gdk_event_free(si->e);
	g_free(si);
	return false;
}
static gboolean scrollcb(GtkWidget *w, GdkEventScroll *pe, Win *win)
{
	if (pe->send_event) return false;

	if (win->mode == Mlist)
	{
		win->scrlx = pe->x;
		win->scrly = pe->y;
		winlist(win,
			pe->direction == GDK_SCROLL_UP || pe->delta_y < 0 ?
				GDK_KEY_Page_Up : GDK_KEY_Page_Down,
			NULL);
		gtk_widget_queue_draw(win->canvas);
		return true;
	}

	if (pe->state & GDK_BUTTON2_MASK && (
		((pe->direction == GDK_SCROLL_UP || pe->delta_y < 0) &&
		 setact(win, "pressscrollup", URI(win))
		) ||
		((pe->direction == GDK_SCROLL_DOWN || pe->delta_y > 0) &&
		 setact(win, "pressscrolldown", URI(win))
		) ))
	{
		cancelmdlr = true;
		return true;
	}

	if (scrlcnt > 222) return false;

	int times = getsetint(win, "multiplescroll");
	if (!times) return false;
	times--;

	if (pe->device == keyboard())
		;
	else if (scrlcnt)
		times += scrlcnt / 3;
	else
		times = 0;

	GdkEventScroll *es = kitevent(win, true, GDK_SCROLL);

	es->send_event = true;
	es->state      = pe->state;
	es->direction  = pe->direction;
	es->delta_x    = pe->delta_x;
	es->delta_y    = pe->delta_y;
	es->x          = pe->x;
	es->y          = pe->y;
	es->device     = pe->device;

	Scrl *si = g_new0(Scrl, 1);
	si->times = times;
	si->e = (void *)es;

	g_timeout_add(300 / (times + 4), (GSourceFunc)multiscrlcb, si);
	scrlcnt += times + 1;
	return false;
}
static bool urihandler(Win *win, const char *uri, char *group)
{
	if (!g_key_file_has_key(conf, group, "handler", NULL)) return false;

	char *buf = g_key_file_get_boolean(conf, group, "handlerunesc", NULL) ?
			g_uri_unescape_string(uri, NULL) : NULL;

	char *esccs = g_key_file_get_string(conf, group, "handlerescchrs", NULL);
	if (esccs && *esccs)
		GFA(buf, _escape(buf ?: uri, esccs))
	g_free(esccs);

	char *command = g_key_file_get_string(conf, group, "handler", NULL);
	GFA(command, g_strdup_printf(command, buf ?: uri))
	g_free(buf);

	run(win, "spawn", command);
	_showmsg(win, g_strdup_printf("Handled: %s", command));

	g_free(command);
	return true;
}

/* kill_cookies before allowing a url to be loaded.  If the accept
   policy is "Never", then don't send cookies.  Unfortunately because
   of webkit design this means we have to delete any cookies for that
   url before loading it.  This has to be done done asynchronously (in
   4 levels) through the decide-policy callback, where we first get a
   ref on the policy decision object to make that signal block.  Then
   asynchronously check the accept policy, and if it is "Never", then
   asynchronously retrieve all cookies for the request, then
   asynchronously delete those cookies.  When the last cookie is
   deleted the last ref on the decision object is also dropped, and
   this is deemed as a use decision. */

void
kill_cookies_cb2(GObject *cm, GAsyncResult *res, gpointer dec)
{
  GError *gerror = NULL;
  gboolean ret = webkit_cookie_manager_delete_cookie_finish
    ((WebKitCookieManager *)cm, res, &gerror);
  if (gerror) {
    fprintf_gerror(stderr, gerror, "kill_cookies_cb2: failed. unblocking use decision\n");
    g_object_unref(dec);
    g_error_free(gerror);
    return;
  }
  if (!ret) {
    fprintf(stderr, "kill_cookies_cb2: delete cookie failed. unblocking use decision\n");
    g_object_unref(dec);
    return;
  }
  g_object_unref(dec);
}

WebKitURIRequest *
decision_request_uri(WebKitPolicyDecision *dec)
{
	/* Another webkitgtk design gem: How can we know if dec is a
	 WebKitNavigationPolicyDecision or if it is a
	 WebKitResponcePolicyDecision?  This information is not
	 available. So check the "request" object property. This is
	 deprectated for the NavigationPolicyDecision, so jump through
	 that hoop too */
	WebKitURIRequest *req = NULL;
	g_object_get(dec, "request", &req, NULL);
	if (req == NULL) {
		WebKitNavigationAction *nav = NULL;
		g_object_get(dec, "navigation-action", &nav, NULL);
		if (nav) {
			req = webkit_navigation_action_get_request(nav);
			g_object_unref(nav); // call webkit_navigation_action_free() instead?
		}
	}
	return req;
}

void
kill_cookies_cb1(GObject *cm, GAsyncResult *res, gpointer dec)
{
  GError *gerror = NULL;
  GList *gl = webkit_cookie_manager_get_cookies_finish
    ((WebKitCookieManager *)cm, res, &gerror);
  if (gerror) {
    fprintf_gerror(stderr, gerror, "kill_cookies_cb1: failed. unblocking use decision\n");
    g_object_unref(dec);
    g_error_free(gerror);
    return;
  }
  if (!gl) {
    g_object_unref(dec);
    return;
  }
  while (gl) {
    g_object_ref(dec);
    WebKitURIRequest *req = decision_request_uri(dec);
    fprintf(stderr, "kill_cookies_cb1: deleting cookie for url: %s\n%s\n",
	    webkit_uri_request_get_uri(req),
	    soup_cookie_to_cookie_header(gl->data));
    g_object_unref(req);
    webkit_cookie_manager_delete_cookie
      ((WebKitCookieManager *)cm, gl->data, NULL, kill_cookies_cb2, dec);
    gl = gl->next;
  }
  g_object_unref(dec);
}

void
kill_cookies_cb0(GObject *cm, GAsyncResult *res, gpointer dec)
{
  GError *gerror = NULL;
  WebKitCookieAcceptPolicy pol = webkit_cookie_manager_get_accept_policy_finish
    ((WebKitCookieManager *) cm, res, &gerror);
  if (gerror) {
    fprintf_gerror(stderr, gerror, "kill_cookies_cb0: failed. unblocking use decision\n");
    g_object_unref(dec);
    g_error_free(gerror);
    return;
  }
  if (pol == WEBKIT_COOKIE_POLICY_ACCEPT_NEVER) {
    WebKitURIRequest *req = decision_request_uri(dec);
    webkit_cookie_manager_get_cookies
      ((WebKitCookieManager *)cm,
       webkit_uri_request_get_uri(req), NULL, kill_cookies_cb1, dec);
    g_object_unref(req);
  } else {
    g_object_unref(dec);
  }
}

void
kill_cookies(WebKitPolicyDecision *dec)
{
  g_object_ref(dec);
  webkit_cookie_manager_get_accept_policy
    (webkit_web_context_get_cookie_manager(ctx), NULL, kill_cookies_cb0, dec);
}

static gboolean policycb(
		WebKitWebView *v,
		void *dec, //WebKitPolicyDecision
		WebKitPolicyDecisionType type,
		Win *win)
{
	if (win->fordl && type == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION)
	{
		//webkit_policy_decision_download in nav is illegal but sends cookies.
		//it changes uri of wins and can't recover.
		webkit_policy_decision_download(dec);
		return true;
	} else 	if (type == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION) {
		kill_cookies(dec); //webkit_policy_decision_use(dec);
		return true;
	}

	if (type != WEBKIT_POLICY_DECISION_TYPE_RESPONSE)
	{
		WebKitNavigationAction *na =
			webkit_navigation_policy_decision_get_navigation_action(dec);
		WebKitURIRequest *req =
			webkit_navigation_action_get_request(na);

		if (eachuriconf(win, webkit_uri_request_get_uri(req), true, urihandler))
		{
			webkit_policy_decision_ignore(dec);
			return true;
		} else
		if (webkit_navigation_action_is_user_gesture(na))
			altcur(win, 0, 0);

		return false;
	}

	WebKitResponsePolicyDecision *rdec = dec;
	WebKitURIResponse *res = webkit_response_policy_decision_get_response(rdec);

	if (res && getsetbool(win, "stdoutheaders"))
		print_headers(webkit_uri_response_get_http_headers(res),
			      stdout,
			      "response ======>\n");

	if(!SOUP_STATUS_IS_SUCCESSFUL(webkit_uri_response_get_status_code(res))) {
		// ;madhu 180620 always load local file content
		webkit_policy_decision_use(dec);
		return true;
	}

	bool dl = false;
	char *msr = getset(win, "dlmimetypes");
	//unfortunately on nav get_main_resource returns prev page
	WebKitWebResource *mresrc = webkit_web_view_get_main_resource(win->kit);
	bool mainframe = !g_strcmp0(webkit_web_resource_get_uri(mresrc),
			webkit_uri_response_get_uri(res));
	if (msr && *msr && mainframe)
	{
		char **ms = g_strsplit(msr, ";", -1);
		const char *mime = webkit_uri_response_get_mime_type(res);
		for (char **m = ms; *m; m++)
			if (**m && (!strcmp(*m, "*") || g_str_has_prefix(mime, *m)))
			{
				dl = true;
				break;
			}
		g_strfreev(ms);
	}

	if (!dl && webkit_response_policy_decision_is_mime_type_supported(rdec))
	{
		if (mainframe)
			resetconf(win, webkit_uri_response_get_uri(res), 0);
		kill_cookies(dec); //webkit_policy_decision_use(dec);
	}
	else if (dl)
		webkit_policy_decision_download(dec);
	else {
		fprintf(stderr, "WIP: to fail\n");
		kill_cookies(dec); //webkit_policy_decision_use(dec);
	}

	return true;
}
static GtkWidget *createcb(Win *win)
{
	char *handle = getset(win, "newwinhandle");
	Win *new = NULL;

	if      (!g_strcmp0(handle, "notnew")) return win->kitw;
	else if (!g_strcmp0(handle, "ignore")) return NULL;
	else if (!g_strcmp0(handle, "back"  )) new = newwin(NULL, win, win, 1);
	else                       /*normal*/  new = newwin(NULL, win, win, 0);
	return new->kitw;
}
static gboolean sdialogcb(Win *win)
{
	if (getsetbool(win, "scriptdialog"))
		return false;
	showmsg(win, "Script dialog is blocked");
	return true;
}
static void setspawn(Win *win, char *key)
{
	char *fname = getset(win, key);
	if (!fname) return;
	char *path = sfree(g_build_filename(sfree(path2conf("menu")), fname, NULL));
	envspawn(spawnp(win, "", NULL , path, true) , false, NULL, NULL, 0);
}

static void
web_view_javascript_finished(GObject      *object,
			     GAsyncResult *result,
			     gpointer      user_data)
{
	WebKitJavascriptResult *js_result;
	JSValueRef              value;
	JSGlobalContextRef      context;
	GError                 *error = NULL;

	js_result = webkit_web_view_run_javascript_finish(WEBKIT_WEB_VIEW(object), result, &error);
	if (!js_result) {
		if (error->message)
			g_warning("Error running javascript: <<%s>>", error->message);
		else g_print("Javascript Result: null\n");
		g_error_free(error);
		return;
	}
	context = webkit_javascript_result_get_global_context(js_result);
	value = webkit_javascript_result_get_value(js_result);
	{
		JSStringRef js_str_value;
		char       *str_value;
		gsize       str_length;

		js_str_value = JSValueToStringCopy(context, value, NULL);
		str_length = JSStringGetMaximumUTF8CStringSize(js_str_value);
		str_value = (char *)g_malloc(str_length);
		JSStringGetUTF8CString(js_str_value, str_value, str_length);
		JSStringRelease(js_str_value);
//		g_print ("Script result: %s\n", str_value);
		g_free(str_value);
	}
//	if (JSValueIsString (context, value)) {
//	} else {
//		g_warning ("Error running javascript: unexpected return value");
//	}
	webkit_javascript_result_unref(js_result);
}

STATIC void
eval_javascript(Win *win, const char *script)
{
	WebKitSettings *s = webkit_web_view_get_settings(win->kit);
	gboolean orig = webkit_settings_get_enable_javascript(s);
#if JAVASCRIPT_MARKUP_SHENNANIGANS
	gboolean orig_markup = webkit_settings_get_enable_javascript_markup(s);
#endif
	if (!orig)
		webkit_settings_set_enable_javascript(s, TRUE);
	webkit_settings_set_enable_javascript_markup(s, FALSE);
	webkit_web_view_run_javascript(win->kit, script, NULL, web_view_javascript_finished, NULL);
	if (!orig)
		webkit_settings_set_enable_javascript(s, FALSE);
#if JAVASCRIPT_MARKUP_SHENNANIGANS
	if (orig_markup)
		webkit_settings_set_enable_javascript_markup(s, TRUE);
#endif
}

static char *resolvepath(const char *path)
{
	if (path[0] == '~' && (path[1] == '/' || path[1] == '\0'))
		return g_build_filename(g_get_home_dir(), path+1, NULL);
	return g_build_filename(path, NULL);
}

STATIC void
surfrunscript(Win *win)
{
	char *scriptfile = "~/.surf/script.js";
	char *script = 0;
	gsize l;
	static char *p = 0;
	if (!p) p =resolvepath(scriptfile); /* XXX not freed */
	if (g_file_get_contents(p, &script, &l, NULL) && l)
		eval_javascript(win, script);
	g_free(script);
}

static gboolean
loadtlsfailcb(WebKitWebView *k, char *uri, GTlsCertificate *cert,
	      GTlsCertificateFlags err, Win *win)
{
	GString *errmsg = g_string_new(NULL);
	char *html, *pem;

	fprintf(stderr, "LOAD FAILED WITH TLS ERRORS\n");


	win->failedcert = g_object_ref(cert);
	win->tlserr = err;
	win->errorpage = 1;
	if (err & G_TLS_CERTIFICATE_UNKNOWN_CA)
		g_string_append(errmsg,
		    "The signing certificate authority is not known.<br>");
	if (err & G_TLS_CERTIFICATE_BAD_IDENTITY)
		g_string_append(errmsg,
		    "The certificate does not match the expected identity "
		    "of the site that it was retrieved from.<br>");
	if (err & G_TLS_CERTIFICATE_NOT_ACTIVATED)
		g_string_append(errmsg,
		    "The certificate's activation time "
		    "is still in the future.<br>");
	if (err & G_TLS_CERTIFICATE_EXPIRED)
		g_string_append(errmsg, "The certificate has expired.<br>");
	if (err & G_TLS_CERTIFICATE_REVOKED)
		g_string_append(errmsg,
		    "The certificate has been revoked according to "
		    "the GTlsConnection's certificate revocation list.<br>");
	if (err & G_TLS_CERTIFICATE_INSECURE)
		g_string_append(errmsg,
		    "The certificate's algorithm is considered insecure.<br>");
	if (err & G_TLS_CERTIFICATE_GENERIC_ERROR)
		g_string_append(errmsg,
		    "Some error occurred validating the certificate.<br>");

	g_object_get(cert, "certificate-pem", &pem, NULL);
	html = g_strdup_printf("<p>Could not validate TLS for “%s”<br>%s</p>"
	                       "<p>You can inspect the following certificate "
	                       "with Ctrl-t (default keybinding).</p>"
	                       "<p><pre>%s</pre></p>", uri, errmsg->str, pem);
	g_free(pem);
	g_string_free(errmsg, TRUE);

	webkit_web_view_load_alternate_html(win->kit, html, uri, /*NULL*/ uri);
	g_free(html);
	return TRUE;
//	return FALSE;   defer to failcb
}

static void loadcb(WebKitWebView *k, WebKitLoadEvent event, Win *win)
{
	win->crashed = false;
	switch (event) {
	case WEBKIT_LOAD_STARTED:
		D(WEBKIT_LOAD_STARTED %s, URI(win))
		setatom(win, atomUri, URI(win));
		histperiod(win);
		if (tlwin == win) tlwin = NULL;
		win->scheme = false;
		setresult(win, NULL);
		GFA(win->focusuri, NULL)
		win->tlserr = 0;

		if (win->errorpage)
			win->errorpage = 0;
		else
			g_clear_object(&win->failedcert);

		if (win->mode == Minsert) send(win, Cblur, NULL); //clear im
		tonormal(win);
		if (win->userreq) {
			win->userreq = false; //currently not used
		}
		setspawn(win, "onstartmenu");

		//there is progcb before this event but sometimes it is
		//before page's prog and we can't get which is it.
		//policycb? no it emits even sub frames and of course
		//we can't get if it is sub or not.
		win->progd = 0;
		win->prog_start1 = g_get_monotonic_time();
		if (!win->drawprogcb)
			win->drawprogcb = g_timeout_add(30, (GSourceFunc)drawprogcb, win);
		gtk_widget_queue_draw(win->canvas);

		//loadcb is multi thread!? and send may block others by alert
		send(win, Cstart, NULL);

		//workaround
		win->maychanged = true;
		break;
	case WEBKIT_LOAD_REDIRECTED:
		D(WEBKIT_LOAD_REDIRECTED %s, URI(win))
		setatom(win, atomUri, URI(win));
		resetconf(win, NULL, 0);
		send(win, Cstart, NULL);

		break;
	case WEBKIT_LOAD_COMMITTED:
		D(WEBKIT_LOAD_COMMITED %s, URI(win))
		win->maychanged = false;
		if (!win->scheme && g_str_has_prefix(URI(win), APP":"))
		{
			webkit_web_view_reload(win->kit);
			break;
		}

		resetconf(win, NULL, 0);
		send(win, Con, "c");

		if (webkit_web_view_get_tls_info(win->kit, &win->cert, &win->tlserr))
			if (win->tlserr) showmsg(win, "TLS Error");
		setspawn(win, "onloadmenu");
		break;
	case WEBKIT_LOAD_FINISHED:
		D(WEBKIT_LOAD_FINISHED %s, URI(win))
		win->maychanged = false;

		// restore_adj: tell webprocess to asynchronously set
		// the page position to what was stored
		if (win->page_adj) {
			char *pstr = g_strdup_printf("%lu %lu", win->page_adj->h, win->page_adj->v);
			//fprintf(stderr, "restore_adj: %p: %s\n", win->page_adj, pstr);
			send(win, Cscrollposition, pstr);
			g_free(pstr);
			win->page_adj = 0;
		}


		if (g_strcmp0(win->lastreset, URI(win)))
		{ //for load-failed before commit e.g. download
			resetconf(win, NULL, 0);
			send(win, Cstart, NULL);
		}
		else if (win->scheme || !g_str_has_prefix(URI(win), APP":"))
		{
			fixhist(win);
			setspawn(win, "onloadedmenu");
			send(win, Con, "f");
		}

		surfrunscript(win);

		win->progd = 1;
		win->prog_end1 =  g_get_monotonic_time();
		_showmsg(win, g_strdup_printf
			 ("loaded in %"G_GINT64_FORMAT " ms",
			  (win->prog_end1 - win->prog_start1)/1000));
		drawprogif(win, true);
		//start is skipped when hist forward on seme pages
		if (win->drawprogcb)
			g_source_remove(win->drawprogcb);
		win->drawprogcb = 0;
		break;
	}
}
static gboolean failcb(WebKitWebView *k, WebKitLoadEvent event,
		char *uri, GError *err, Win *win)
{
	//D(failcb %d %d %s, err->domain, err->code, err->message)
	// 2042 6 Unacceptable TLS certificate
	// 2042 6 Error performing TLS handshake: An unexpected TLS packet was received.
	static char *last;
	if (err->code == 6 && confbool("ignoretlserr"))
	{
		static int count;
		if (g_strcmp0(last, uri))
			count = 0;
		else if (++count > 2) //three times
		{
			count = 0;
			return false;
		}

		GFA(last, g_strdup(uri))
		//webkit_web_view_reload(win->kit); //this reloads prev page
		webkit_web_view_load_uri(win->kit, uri);
		showmsg(win, "Reloaded by TLS error");
		return true;
	}
	return false;
}

//@contextmenu
typedef struct {
	GClosure *gc; //when dir gc and path are NULL
	char     *path;
	GSList   *actions;
} AItem;
static void clearai(gpointer p)
{
	AItem *a = p;
	if (a->gc)
	{
		g_free(a->path);
		gtk_accel_group_disconnect(accelg, a->gc);
	}
	else
		g_slist_free_full(a->actions, clearai);
	g_free(a);
}
static gboolean actioncb(char *path)
{
	envspawn(spawnp(LASTWIN, "", NULL, path, true), false, NULL, NULL, 0);
	return true;
}
static guint menuhash;
static GSList *dirmenu(
		WebKitContextMenu *menu,
		char *dir,
		char *parentaccel)
{
	GSList *ret = NULL;
	GSList *names = NULL;

	GDir *gd = g_dir_open(dir, 0, NULL);
	const char *dn;
	while ((dn = g_dir_read_name(gd)))
		names = g_slist_insert_sorted(names, g_strdup(dn), (GCompareFunc)strcmp);
	g_dir_close(gd);

	for (GSList *next = names; next; next = next->next)
	{
		char *org = next->data;
		char *name = org + 1;

		if (g_str_has_suffix(name, "---"))
		{
			if (menu)
				webkit_context_menu_append(menu,
					webkit_context_menu_item_new_separator());
			continue;
		}

		AItem *ai = g_new0(AItem, 1);
		bool nodata = false;

		char *laccelp = g_strconcat(parentaccel, "/", name, NULL);
		char *path = g_build_filename(dir, org, NULL);

		if (g_file_test(path, G_FILE_TEST_IS_DIR))
		{
			WebKitContextMenu *sub = NULL;
			if (menu && *org != '.')
				sub = webkit_context_menu_new();

			ai->actions = dirmenu(sub, path, laccelp);
			if (!ai->actions)
				nodata = true;
			else if (menu && *org != '.')
				webkit_context_menu_append(menu,
					webkit_context_menu_item_new_with_submenu(name, sub));

			g_free(path);
		} else {
			ai->path = path;
			addhash(path, &menuhash);
			ai->gc = g_cclosure_new_swap(G_CALLBACK(actioncb), path, NULL);
			gtk_accel_group_connect_by_path(accelg, laccelp, ai->gc);

			if (menu && *org != '.')
			{
				GSimpleAction *gsa = g_simple_action_new(laccelp, NULL);
				SIGW(gsa, "activate", actioncb, path);
				webkit_context_menu_append(menu,
						webkit_context_menu_item_new_from_gaction(
							(GAction *)gsa, name, NULL));
				g_object_unref(gsa);
			}
		}

		g_free(laccelp);
		if (nodata)
			g_free(ai);
		else
			ret = g_slist_append(ret, ai);
	}
	g_slist_free_full(names, g_free);
	return ret;
}
static void makemenu(WebKitContextMenu *menu); //declaration
static guint menudirtime;
static gboolean menudirtimecb(gpointer p)
{
	makemenu(NULL);
	menudirtime = 0;
	return false;
}
static void menudircb(
		GFileMonitor *m, GFile *f, GFile *o, GFileMonitorEvent e)
{
	if (e == G_FILE_MONITOR_EVENT_CREATED && menudirtime == 0)
		//For editors making temp files
		menudirtime = g_timeout_add(100, menudirtimecb, NULL);
}
static void addscript(char *dir, char *name, char *script)
{
	char *ap = g_build_filename(dir, name, NULL);
	FILE *f = fopen(ap, "w");
	fputs(script, f);
	fclose(f);
	g_chmod(ap, 0700);
	g_free(ap);
}
static char *menuitems[][2] =
 {{".openBackRange"   , APP" // shrange '"APP" // openback $MEDIA_IMAGE_LINK'"
},{".openNewSrcURI"   , APP" // shhint '"APP" // opennew $MEDIA_IMAGE_LINK'"
},{".openWithRef"     , APP" // shhint '"APP" // openwithref $MEDIA_IMAGE_LINK'"
},{"0editMenu"        , APP" // openconfigdir menu"
},{"1bookmark"        , APP" // bookmark \"$LINK_OR_URI $LABEL_OR_TITLE\""
},{"1duplicate"       , APP" // opennew $URI"
},{"1editLabelOrTitle", APP" // edituri \"$LABEL_OR_TITLE\""
},{"1history"         , APP" // showhistory ''"
},{"1windowList"      , APP" // winlist ''"
},{"2main"            , APP" // open "APP":main"
},{"3---"             , ""
},{"3openClipboard"   , APP" // open \"$CLIPBOARD\""
},{"3openClipboardNew", APP" // opennew \"$CLIPBOARD\""
},{"3openSelection"   , APP" // open \"$PRIMARY\""
},{"3openSelectionNew", APP" // opennew \"$PRIMARY\""
},{"6searchDictionary", APP" // open \"u $PRIMARY\""
},{"9---"             , ""
},{"cviewSource"      , APP" // shsrc 'd=\"$DLDIR/"APP"-source\" && tee > \"$d\" && mimeopen -n \"$d\"'"
},{"vchromium"        , "chromium $LINK_OR_URI"
},{"xnoSuffixProcess" , APP" / new $LINK_OR_URI"
},{"z---"             , ""
},{"zquitAll"         , APP" // quitall ''"
},{NULL}};
void makemenu(WebKitContextMenu *menu)
{
	static GSList *actions;
	static bool firsttime = true;

	char *dir = path2conf("menu");
	if (_mkdirif(dir, false))
		for (char *(*ss)[2] = menuitems; **ss; ss++)
			addscript(dir, **ss, (*ss)[1]);

	if (firsttime)
	{
		firsttime = false;
		accelg = gtk_accel_group_new();

		GFile *gf = g_file_new_for_path(dir);
		GFileMonitor *gm = g_file_monitor_directory(gf,
				G_FILE_MONITOR_NONE, NULL, NULL);
		SIG(gm, "changed", menudircb, NULL);
		g_object_unref(gf);

		accelp = path2conf("accels");
		monitor(accelp, checkaccels);

		if (g_file_test(accelp, G_FILE_TEST_EXISTS))
			gtk_accel_map_load(accelp);
	}

	if (actions)
		g_slist_free_full(actions, clearai);

	WebKitContextMenuItem *sep = NULL;
	if (menu)
		webkit_context_menu_append(menu,
			sep = webkit_context_menu_item_new_separator());

	guint lasthash = menuhash;
	menuhash = 0;

	actions = dirmenu(menu, dir, "<window>");

	if (menu && !actions)
		webkit_context_menu_remove(menu, sep);

	if (lasthash != menuhash)
	{
		cancelaccels = true;
		gtk_accel_map_save(accelp);
	}

	g_free(dir);
}

static void contextclosecb(WebKitWebView *k, Win *win)
{
	ignoretargetcb = false;
}
static gboolean contextcb(WebKitWebView *k,
		WebKitContextMenu   *menu,
		GdkEvent            *e,
		WebKitHitTestResult *htr,
		Win                 *win)
{
	setresult(win, htr);
	makemenu(menu);
	ignoretargetcb = true;
	return false;
}


//@entry
void enticon(Win *win, const char *name)
{
	if (!name) name =
		win->mode == Mfind    ? "edit-find"  :
		win->mode == Mopen    ? "go-jump"    :
		win->mode == Mopennew ? "window-new" : NULL;

	gtk_entry_set_icon_from_icon_name(win->ent, GTK_ENTRY_ICON_PRIMARY, name);
}
static gboolean focusincb(Win *win)
{
	//fprintf(stderr, "\nFOCUSINCB %lxd\n\n", win->xid);
	if (gtk_widget_get_visible(win->entw))
		tonormal(win);
	return false;
}
static gboolean entkeycb(GtkWidget *w, GdkEventKey *ke, Win *win)
{
	switch (ke->keyval) {
	case GDK_KEY_m:
		if (!(ke->state & GDK_CONTROL_MASK)) return false;
	case GDK_KEY_KP_Enter:
	case GDK_KEY_Return:
		switch (win->mode) {
		case Mfind:
			if (!win->infind || !findtxt(win) || strcmp(findtxt(win), getent(win)))
				run(win, "find", getent(win));

			senddelay(win, Cfocus, NULL);
			break;
		case Mopen:
			run(win, "open", getent(win)); break;
		case Mopennew:
			run(win, "opennew", getent(win)); break;
		default:
				g_assert_not_reached();
		}
		tonormal(win);
		return true;
	case GDK_KEY_Escape:
		if (win->mode == Mfind)
			webkit_find_controller_search_finish(win->findct);
		tonormal(win);
		return true;
	}

	if (!(ke->state & GDK_CONTROL_MASK)) return false;
	//ctrls
	static char *buf;
	int wpos = 0;
	GtkEditable *e = (void *)w;
	int pos = gtk_editable_get_position(e);
	switch (ke->keyval) {
	case GDK_KEY_Z:
	case GDK_KEY_n:
		undo(win, &win->redo, &win->undo); break;
	case GDK_KEY_z:
	case GDK_KEY_p:
		undo(win, &win->undo, &win->redo); break;

	case GDK_KEY_a:
		gtk_editable_set_position(e, 0); break;
	case GDK_KEY_e:
		gtk_editable_set_position(e, -1); break;
	case GDK_KEY_b:
		gtk_editable_set_position(e, MAX(0, pos - 1)); break;
	case GDK_KEY_f:
		gtk_editable_set_position(e, pos + 1); break;

	case GDK_KEY_d:
		gtk_editable_delete_text(e, pos, pos + 1); break;
	case GDK_KEY_h:
		gtk_editable_delete_text(e, pos - 1, pos); break;
	case GDK_KEY_k:
		GFA(buf, g_strdup(gtk_editable_get_chars(e, pos, -1)));
		gtk_editable_delete_text(e, pos, -1); break;
	case GDK_KEY_w:
		for (int i = pos; i > 0; i--)
		{
			char c = *sfree(gtk_editable_get_chars(e, i - 1, i));
			if (c != ' ' && c != '/')
				wpos = i - 1;
			else if (wpos)
				break;
		}
	case GDK_KEY_u:
		GFA(buf, g_strdup(gtk_editable_get_chars(e, wpos, pos)));
		gtk_editable_delete_text(e, wpos, pos);
		break;
	case GDK_KEY_y:
		wpos = pos;
		gtk_editable_insert_text(e, buf ?: "", -1, &wpos);
		gtk_editable_select_region(e, pos, wpos);
		break;
	case GDK_KEY_t:
		if (pos == 0) pos++;
		gtk_editable_set_position(e, -1);
		int chk = gtk_editable_get_position(e);
		if (chk < 2)
			break;
		if (chk == pos)
			pos--;

		char *rm = gtk_editable_get_chars(e, pos - 1, pos);
		          gtk_editable_delete_text(e, pos - 1, pos);
		gtk_editable_insert_text(e, rm, -1, &pos);
		gtk_editable_set_position(e, pos);
		g_free(rm);
		break;
	case GDK_KEY_l:
	{
		int ss, se;
		gtk_editable_get_selection_bounds(e, &ss, &se);
		char *str = gtk_editable_get_chars(e, 0, -1);
		for (char *c = str; *c; c++)
			if (ss == se || (c - str >= ss && c - str < se))
				*c = g_ascii_tolower(*c);

		gtk_editable_delete_text(e, 0, -1);
		gtk_editable_insert_text(e, str, -1, &pos);
		gtk_editable_select_region(e, ss, se);
		break;
	}
	default:
		return false;
	}

	return true;
}
static gboolean textcb(Win *win)
{
	if (win->mode == Mfind && gtk_widget_get_visible(win->entw))
	{
		if (strlen(getent(win)) > 2)
			run(win, "find", getent(win));
		else
		{
			enticon(win, NULL);
			webkit_find_controller_search_finish(win->findct);
		}
	}
	return false;
}
static void findfailedcb(Win *win)
{
	enticon(win, "dialog-warning");
	_showmsg(win, g_strdup_printf("Not found: '%s'", findtxt(win)));
}
static void foundcb(WebKitFindController *f, guint cnt, Win *win)
{
	enticon(win, NULL);
	_showmsg(win, cnt > 1 ? g_strdup_printf("%d", cnt) : NULL);
}

static gboolean openuricb(void **args)
{
	_openuri(args[0], args[1], isin(wins, args[2]) ? args[2] : NULL);
	g_free(args[1]);
	g_free(args);
	return FALSE;
}


gboolean
viewusrmsgrcv(WebKitWebContext *v, WebKitUserMessage *m, gpointer unused)
{
	WebKitUserMessage *r;
	GUnixFDList *gfd;
	const char *name;

	name = webkit_user_message_get_name(m);
	if (strcmp(name, "pageinit") != 0) {
		fprintf(stderr, "wyeb: Unknown UserMessage: %s\n", name);
		return TRUE;
	}
	GVariant *parameters = webkit_user_message_get_parameters (m);
	if (!parameters)
	  return;
	const char *_arg, **_agv;
	g_variant_get (parameters, "&s", &_arg);
	g_message("wyeb: pageinit: %s, arg", _arg);
	_agv = g_strsplit(_arg, ":", 2);

	Win *win = 0;
	int needle = atol(_agv[1]);
	for (int i = 0; i < wins->len; i++) {
	  Win *straw = wins->pdata[i];
	  if (needle == webkit_web_view_get_page_id(straw->kit))
	    win = straw;
	}
	g_message("viewusrmsgrcv: pageid = %s, win = %p", _agv[1], win);
	if (!win) {
	  win = LASTWIN;
	  g_message("using last win %p", win);
	}
	win->ipcids = g_slist_prepend(win->ipcids, g_strdup(_agv[0]));
	//when page proc recreated on some pages, webkit_web_view_get_page_id delays
	_send(win, Coverset, win->overset, atol(g_strdup(_agv[1])));
	return TRUE;
}


//@newwin
Win *newwin(const char *uri, Win *cbwin, Win *caller, int back)
{
	Win *win = g_new0(Win, 1);
	win->userreq = true;

/*
#ifdef GDK_WINDOWING_X11
	Window parent_xid= getenv("XEMBED")!=NULL?strtol(getenv("XEMBED"),NULL,0):0;
	if (parent_xid)
		plugto = parent_xid;
#endif
*/

	win->winw =
#ifdef GDK_WINDOWING_X11
		plugto ? gtk_plug_new(plugto) :
#endif
		gtk_window_new(GTK_WINDOW_TOPLEVEL);

	int w, h;
	if (caller)
	{
		win->overset = g_strdup(caller->overset);
		gtk_window_get_size(caller->win, &w, &h);
	}
	else
		w = confint("winwidth"), h = confint("winheight");
	gtk_window_set_default_size(win->win, w, h);

	if (back != 2)
		gtk_widget_show(win->winw);

	SIGW(win->wino, plugto ? "configure-event":"focus-in-event", focuscb, win);

	if (!ctx)
	{
		makemenu(NULL);
		preparewb();

		ephemeral = g_key_file_get_boolean(conf, "boot", "ephemeral", NULL)
// we should make all automation sessions ephemeral. However because
// wyeb sets it as a boot conf, the user will have to invoke ["wyeb"
// ["ephe" "automation" "bogus"]] where ephe has boot.ephemeral=true
// in its wyeb.ephe/main.conf
			|| cbwin == AUTOMATION_CBWIN
			;
		char *data  = g_build_filename(g_get_user_data_dir() , fullname, NULL);
		char *cache = g_build_filename(g_get_user_cache_dir(), fullname, NULL);
		WebKitWebsiteDataManager *mgr = webkit_website_data_manager_new(
				"base-data-directory" , data,
				"base-cache-directory", cache,
				"is-ephemeral", ephemeral, NULL);
		g_free(data);
		g_free(cache);

		ctx = g_object_new (WEBKIT_TYPE_WEB_CONTEXT,
                                    "website-data-manager", mgr,
                                    "process-swap-on-cross-site-navigation-enabled", FALSE,
                                    NULL);

		g_signal_connect(G_OBJECT(ctx), "user-message-received", G_CALLBACK(viewusrmsgrcv), NULL);

		proxy_settings_from_conf(); //on creation

		//cookie  //have to be after ctx are made
		WebKitCookieManager *cookiemgr =
			webkit_website_data_manager_get_cookie_manager(mgr);

		if (!ephemeral)
			//we assume cookies are conf
			webkit_cookie_manager_set_persistent_storage(cookiemgr,
					sfree(path2conf("cookies")),
					WEBKIT_COOKIE_PERSISTENT_STORAGE_TEXT);

		webkit_cookie_manager_set_accept_policy(cookiemgr,
				WEBKIT_COOKIE_POLICY_ACCEPT_NEVER);

		if (g_key_file_get_boolean(conf, "boot", "multiwebprocs", NULL))
			webkit_web_context_set_process_model(ctx,
					WEBKIT_PROCESS_MODEL_MULTIPLE_SECONDARY_PROCESSES);

		char **argv = g_key_file_get_string_list(
				conf, "boot", "extensionargs", NULL, NULL);
		char *args = g_strjoinv(";", argv);
		g_strfreev(argv);
		char *udata = g_strconcat(args,
				";"APP"abapi;", fullname, NULL);
		g_free(args);

		webkit_web_context_set_web_extensions_initialization_user_data(
				ctx, g_variant_new_string(udata));
		fprintf(stderr,"main newwin: initializing userdata=%s\n", udata);
		g_free(udata);

		// use env var if it is a directory
		char* extdir = (char *) g_getenv("WEBKIT_EXT_DIR");
		if (extdir && !g_file_test(extdir, G_FILE_TEST_IS_DIR)) {
			fprintf(stderr, "init_web_extensions: bogus WEBKIT_EXT_DIR=%s\n", extdir);
			extdir = NULL;
		} else {
			if (extdir) extdir = strdup(extdir);
		}

#ifdef DEBUG
		// or use executable location if extension is present
		if (!extdir) {
			char binpath[256];
			int ret = readlink("/proc/self/exe", binpath, sizeof(binpath)-1);
			g_assert(ret > 0 && ret < sizeof(binpath) - 1);
			binpath[ret] = 0;
			extdir = g_path_get_dirname(binpath);
			char *test = g_build_filename(extdir, "ext.so", NULL);
			if (!g_file_test(test, G_FILE_TEST_EXISTS)) {
				g_free(extdir);
				extdir = NULL;
			}
			g_free(test);
		}
#endif

		// or use compiled in defualt if extension is present
		if (!extdir) {
			char *test = g_build_filename(EXTENSION_DIR, "ext.so", NULL);
			if (g_file_test(test, G_FILE_TEST_EXISTS))
				extdir = strdup(EXTENSION_DIR);
			g_free(test);
		}

		if (extdir) {
			webkit_web_context_set_web_extensions_directory(ctx, extdir);
			g_free(extdir);
		} else {

#if DEBUG
		webkit_web_context_set_web_extensions_directory(ctx,
				sfree(g_get_current_dir()));
#else
		webkit_web_context_set_web_extensions_directory(ctx, EXTENSION_DIR);
#endif
		}

		SIG(ctx, "download-started", downloadcb, NULL);

		webkit_security_manager_register_uri_scheme_as_local(
				webkit_web_context_get_security_manager(ctx), APP);

		webkit_web_context_register_uri_scheme(
				ctx, APP, schemecb, NULL, NULL);

		if (g_key_file_get_boolean(conf, "boot", "enablefavicon", NULL))
			webkit_web_context_set_favicon_database_directory(ctx,
				sfree(g_build_filename(
						g_get_user_cache_dir(), fullname, "favicon", NULL)));

		if (confbool("ignoretlserr"))
			webkit_web_context_set_tls_errors_policy(ctx,
					WEBKIT_TLS_ERRORS_POLICY_IGNORE);

#if WEBKIT_MAJOR_VERSION > 2 || WEBKIT_MINOR_VERSION > 28
		if (confbool("itp"))
			webkit_website_data_manager_set_itp_enabled(
					webkit_web_context_get_website_data_manager(ctx), true);
#endif
		if (cbwin == AUTOMATION_CBWIN) {
			webkit_web_context_set_automation_allowed(ctx, TRUE);
			g_signal_connect(ctx, "automation-started", G_CALLBACK(automationStartedCallback), win);
		}
	}

	WebKitUserContentManager *cmgr = webkit_user_content_manager_new();
	(void) connect_ucm_aboutdata_script_callback(cmgr);
	gboolean singlewebproc = false;
        if (cbwin == AUTOMATION_CBWIN) {
               g_message("NEWWIN for automation");
               win->kito = g_object_new(WEBKIT_TYPE_WEB_VIEW,
					"web-context", ctx,
					"user-content-manager", cmgr,
					"is-controlled-by-automation", TRUE,
					NULL);
	} else if (cbwin) {
		g_message("NEWWIN related view for callback win");
		win->kito = g_object_new(WEBKIT_TYPE_WEB_VIEW,
					 "related-view", cbwin->kit, "user-content-manager", cmgr, 
					 "is-controlled-by-automation", caller ? webkit_web_view_is_controlled_by_automation(caller->kit) : FALSE,
					 NULL);
	} else if ((singlewebproc = !g_key_file_get_boolean(conf, "boot", "multiwebprocs", NULL)) && caller) {
		g_message("NEWWIN related view for caller win");
		win->kito = g_object_new(WEBKIT_TYPE_WEB_VIEW,
					 "related-view", caller->kit, "user-content-manager", cmgr,
					 "is-controlled-by-automation", caller ? webkit_web_view_is_controlled_by_automation(caller->kit) : FALSE,
					 NULL);
	} else if (singlewebproc && LASTWIN) {
		g_message("NEWWIN related view for LASTWIN");
		win->kito = g_object_new(WEBKIT_TYPE_WEB_VIEW,
					 "related-view", win->kit, "user-content-manager", cmgr,
					 "is-controlled-by-automation", caller ? webkit_web_view_is_controlled_by_automation(caller->kit) : FALSE,
					 NULL);
	} else {
		g_message("NEWWIN newwin");
		win->kito = g_object_new(WEBKIT_TYPE_WEB_VIEW,
			     "web-context", ctx, "user-content-manager", cmgr,
					 "is-controlled-by-automation", caller ? webkit_web_view_is_controlled_by_automation(caller->kit) : FALSE,
					 NULL);
	}

	g_object_set_data(win->kito, "win", win);
	g_object_set_data(win->kito, "caller", caller);

	gtk_window_add_accel_group(win->win, accelg);
	//workaround. without get_inspector inspector doesen't work
	//and have to grab forcus;
// inspector fail
//	SIGW(webkit_web_view_get_inspector(win->kit),
//			"detach", detachcb, win->kitw);

	win->set = webkit_settings_new();
	setprops(win, conf, DSET);
	webkit_web_view_set_settings(win->kit, win->set);
	g_object_unref(win->set);
	webkit_web_view_set_zoom_level(win->kit, confdouble("zoom"));
	setcss(win, getset(win, "usercss"));
	setscripts(win, getset(win, "userscripts"));
	gdk_rgba_parse(&win->rgba, getset(win, "msgcolor") ?: "");

	GObject *o = win->kito;
	SIGW(o, "destroy"              , destroycb , win);
	SIGW(o, "web-process-crashed"  , crashcb   , win);
	SIGW(o, "notify::title"        , notifycb  , win);
	SIGW(o, "notify::uri"          , notifycb  , win);
	SIGW(o, "notify::estimated-load-progress", progcb, win);

	if (g_key_file_get_boolean(conf, "boot", "enablefavicon", NULL))
		SIGW(o, "notify::favicon"      , favcb     , win);

	SIG( o, "key-press-event"      , keycb     , win);
	SIG( o, "key-release-event"    , keyrcb    , win);
	SIG( o, "mouse-target-changed" , targetcb  , win);
	SIG( o, "button-press-event"   , btncb     , win);
	SIG( o, "button-release-event" , btnrcb    , win);
	SIG( o, "drag-begin"           , dragbcb   , win);
	SIG( o, "enter-notify-event"   , entercb   , win);
	SIG( o, "leave-notify-event"   , leavecb   , win);
	SIG( o, "motion-notify-event"  , motioncb  , win);
	SIG( o, "scroll-event"         , scrollcb  , win);

	SIG( o, "decide-policy"        , policycb  , win);
	SIGW(o, "create"               , createcb  , win);
	SIGW(o, "close"                , gtk_widget_destroy, win->winw);
	SIGW(o, "script-dialog"        , sdialogcb , win);
	SIG( o, "load-changed"         , loadcb    , win);
	SIG( o, "load-failed"          , failcb    , win);
	SIG( o, "load-failed-with-tls-errors", loadtlsfailcb, win);

	SIG( o, "context-menu"         , contextcb , win);
	SIG( o, "context-menu-dismissed", contextclosecb , win);

	//for entry
	SIGW(o, "focus-in-event"       , focusincb , win);

	win->findct = webkit_web_view_get_find_controller(win->kit);
	SIGW(win->findct, "failed-to-find-text", findfailedcb, win);
	SIG( win->findct, "found-text"         , foundcb     , win);

	//entry
	win->entw = gtk_entry_new();
	SIG(win->ento, "key-press-event", entkeycb, win);
	GtkEntryBuffer *buf = gtk_entry_get_buffer(win->ent);
	SIGW(buf, "inserted-text", textcb, win);
	SIGW(buf, "deleted-text" , textcb, win);

	//label
	win->lblw = gtk_label_new("");
	gtk_label_set_selectable(win->lbl, true);
	gtk_label_set_ellipsize(win->lbl, PANGO_ELLIPSIZE_MIDDLE);
	gtk_label_set_xalign(win->lbl, 0);
//	gtk_label_set_line_wrap(win->lbl, true);
//	gtk_label_set_line_wrap_mode(win->lbl, PANGO_WRAP_CHAR);
//	gtk_label_set_use_markup(win->lbl, TRUE);
	SIGW(win->lblw, "notify::visible", update, win);

	GtkWidget *ol = gtk_overlay_new();
	gtk_container_add(GTK_CONTAINER(ol), win->kitw);
	gtk_widget_set_valign(win->entw, GTK_ALIGN_END);
	gtk_overlay_add_overlay(GTK_OVERLAY(ol), win->entw);

	GtkWidget *box  = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_box_pack_start(GTK_BOX(box), win->lblw, false, true, 0);
	gtk_box_pack_end(  GTK_BOX(box), ol       , true , true, 0);

	gtk_container_add(GTK_CONTAINER(win->win), box);

	g_ptr_array_add(wins, win);
	if (back == 2)
		return win;

	if (caller)
	{
		webkit_web_view_set_zoom_level(win->kit,
				webkit_web_view_get_zoom_level(caller->kit));
		win->undo = g_slist_copy_deep(caller->undo, (GCopyFunc)g_strdup, NULL);
		win->redo = g_slist_copy_deep(caller->redo, (GCopyFunc)g_strdup, NULL);
		win->lastsearch = g_strdup(findtxt(caller) ?: caller->lastsearch);
		gtk_entry_set_text(win->ent, getent(caller));
	}

	gtk_widget_show_all(box);
	gtk_widget_hide(win->entw);
	gtk_widget_grab_focus(win->kitw);

	SIGA(G_OBJECT(win->canvas = win->kitw), "draw", drawcb, win);

	win->xid = gdk_x11_window_get_xid(gtk_widget_get_window
					  (GTK_WIDGET(win->win)));
	snprintf(win->sxid, sizeof(win->sxid)/sizeof((win->sxid)[0]),
		 "%lu", win->xid);
	win->dpy = gdk_x11_get_default_xdisplay();
	gdk_window_set_events(gtk_widget_get_window(GTK_WIDGET(win->win)),
			      GDK_ALL_EVENTS_MASK);
	gdk_window_add_filter(gtk_widget_get_window(GTK_WIDGET(win->win)),
			      processx, win);

	if (atomGo == 0) initatoms(win);

	// enable default content blocker if it is set
	WebKitUserContentFilter *cf = NULL;
        if (opContentFilter(CONTENT_FILTER_STORE_LOAD, NULL, NULL, &cf)) {
		webkit_user_content_manager_add_filter(cmgr, cf);
		webkit_user_content_filter_unref(cf);
	}

	present(back && LASTWIN ? LASTWIN : win);

	if (!cbwin || cbwin == AUTOMATION_CBWIN)
		g_timeout_add(40, (GSourceFunc) openuricb,
				g_memdup((void *[]){win, g_strdup(uri), caller},
				   sizeof(void *) * 3));

	return win;
}

static int reuse = 1;

static void
reusemode(Win *win, const char *arg) {
	if (strcmp(arg, "status") == 0) {
		_showmsg(win, g_strdup_printf("reuse windows mode is %s\n",
					      reuse ? "on" : "off"));
	} else if (strcmp(arg, "on") == 0) {
		int old = reuse;
		reuse = 1;
		_showmsg(win, g_strdup_printf("reuse windows on: %schanged\n",
					     old == reuse ? "un" : ""));
	} else if (strcmp(arg, "off") == 0) {
		int old = reuse;
		reuse = 0;
		_showmsg(win, g_strdup_printf("reuse windows off: %schanged\n",
					     old == reuse ? "un" : ""));
	} else if (strcmp(arg, "toggle") == 0) {
		int old = reuse;
		reuse = old ? 0 : 1;
		_showmsg(win, g_strdup_printf("reuse windows toggle  %schanged: now %s\n",
					     old == reuse ? "un" : "",
					     reuse ? "ON" : "OFF"));
	} else {
		_showmsg(win, g_strdup_printf("unknown argument to reusemode: %s\n", arg));
	}
}

#if SOUP_MAJOR_VERSION == 2
#include "soup-uri-copy.c"
#else
#define soup_uri_copy3 soup_uri_copy
#define soup_uri_equal3 soup_uri_equal
#endif

static gboolean
maybe_reuse(Win *curwin, const char *uri, gboolean new_if_reuse_fails)
{
	if (!reuse) return false;
	Win *win = NULL;
	GUri *suri0 = NULL;
	g_autoptr(GUri) suriX = uri ? g_uri_parse(uri, SOUP_HTTP_URI_FLAGS, NULL) : NULL;
	g_autoptr(GUri) copyX = NULL;
	gboolean fragmentp = suriX ? g_uri_get_fragment(suriX) != NULL : 0;
	if (suriX) {
		copyX = soup_uri_copy3(suriX, SOUP_URI_FRAGMENT, NULL, SOUP_URI_NONE);
		suri0 = copyX;
	} else {
		fprintf(stderr, "NULL GUri for URI %s.\n", uri);
		suri0 = suriX;
	}

	for (int i = 0; i < wins->len; i++) {
		Win *owin = (Win *) (wins->pdata[i]);
		const char *ouri = URI(owin);
		if (ouri && *ouri) {
			if (suri0) {
				g_autoptr(GUri) souriX = g_uri_parse(ouri, SOUP_HTTP_URI_FLAGS, NULL);
				g_autoptr(GUri) souri = soup_uri_copy3(souriX, SOUP_URI_FRAGMENT, NULL, SOUP_URI_NONE);
				if (soup_uri_equal3(suri0, souri)) {
					win = owin;
					break;
				}
			}
		} else {
			fprintf(stderr, "NULL GUri for URI %s.\n", ouri);
			if (!strcmp(uri, URI(owin))) {
				win = owin; break;
			}
		}
	}
	if (win) {
		present(win);
		if (fragmentp) {
			fprintf(stderr,"\n\nFRAGMENT; ouri=%s opening uri: %s\n\n", URI(win), uri);
			webkit_web_view_load_uri(win->kit, uri);
		}
		return true;
	}
	if (new_if_reuse_fails) {
		newwin(uri, NULL, curwin, 0);
		return true;
	}
	return false;
}

static Win *
maybe_newwin(const char *uri, Win *cbwin, Win *caller, int back)
{
	  Win *last = LASTWIN, *win = NULL;
	  fprintf(stderr, "maybe_newwin: LASTWIN=%p\n", LASTWIN);
	  if (reuse && LASTWIN && maybe_reuse(LASTWIN, uri, true)) {
		  fprintf(stderr, "reuse %s succeeded. last=%p LASTWIN=%p \n", uri, last, LASTWIN);
		  win = LASTWIN;
	  } else {
		  win = newwin(uri, cbwin, caller, back);
	  }
	  return win;
}


//@main
static void runline(const char *line, char *cdir, char *exarg)
{
	char **args = g_strsplit(line, ":", 3);

	char *arg = args[2];
	if (!*arg) arg = NULL;

	fprintf(stderr,"main ipccb: args[0]=%s args[1]=%s args[2]=%s\n",
		args[0], args[1], args[2]);

	if (!strcmp(args[0], "0"))
		_run(LASTWIN, args[1], arg, cdir, exarg);
	else
	{
		Win *win = winbyid(args[0]);
		if (win)
			_run(win, args[1], arg, cdir, exarg);
	}

	g_strfreev(args);
}
void ipccb(const char *line)
{
	//m is from main
	if (*line != 'm') return runline(line, NULL, NULL);

	char **args = g_strsplit(line, ":", 4);
	int clen = atoi(args[1]);
	int elen = atoi(args[2]);
	char *cdir = g_strndup(args[3], clen);
	char *exarg = elen == 0 ? NULL : g_strndup(args[3] + clen, elen);

	runline(args[3] + clen + elen, cdir, exarg);

	g_free(cdir);
	g_free(exarg);
	g_strfreev(args);
}
int main(int argc, char **argv)
{
#if DEBUG
//	g_log_set_always_fatal(G_LOG_LEVEL_CRITICAL);
	DD(This bin is compiled with DEBUG)
#endif

	if (argc == 2 && (
			!strcmp(argv[1], "-h") ||
			!strcmp(argv[1], "--help"))
	) {
		g_print("%s", usage);
		exit(0);
	}

	if (argc >= 4)
		suffix = argv[1];
	const char *envsuf = g_getenv("SUFFIX") ?: "";
	if (!strcmp(suffix, "//")) suffix = g_strdup(envsuf);
	if (!strcmp(suffix, "/")) suffix = "";
	if (!strcmp(envsuf, "/")) envsuf = "";
	const char *winid =
		!strcmp(suffix,  envsuf) ? g_getenv("WINID") : NULL;
	if (!winid || !*winid) winid = "0";

	fullname = g_strconcat(OLDNAME, suffix, NULL); //for backward
	if (!g_file_test(path2conf(NULL), G_FILE_TEST_EXISTS))
		GFA(fullname, g_strconcat(DIRNAME, suffix, NULL));

	char *exarg = "";
	if (argc > 4)
	{
		exarg = argv[4];
		argc = 4;
	}

	char *action = argc > 2 ? argv[argc - 2] : "new";
	char *uri    = argc > 1 ? argv[argc - 1] : NULL;

	if (!*action) action = "new";
	if (uri && !*uri) uri = NULL;
	if (argc == 2 && uri && g_file_test(uri, G_FILE_TEST_EXISTS)) {
		char *resolved_path = realpath(uri, NULL);
		if (!resolved_path) {
			fprintf(stderr, "accessing %s: ", uri);
			perror("realpath");
		} else {
			GError *err = NULL;
			char *ret = g_filename_to_uri(resolved_path, NULL, &err);
			if (err)  {
				fprintf_gerror(stderr, err, "g_filename_to_uri(%s) failed\n", resolved_path);
				uri = resolved_path;
			} else {
				uri = ret;
				g_free(resolved_path);
			}
		}
	}

	char *cwd = g_get_current_dir();
	char *sendstr = g_strdup_printf("m:%zu:%zu:%s%s%s:%s:%s",
			strlen(cwd), strlen(exarg), cwd, exarg, winid, action, uri ?: "");
	g_free(cwd);

	int lock = open(ipcpath("lock"), O_RDONLY | O_CREAT, S_IRUSR);
	flock(lock, LOCK_EX);

	if (ipcsend("main", sendstr)) exit(0);

	//start main
	histdir = g_build_filename(
			g_get_user_cache_dir(), fullname, "history", NULL);

	setenv("HISTFN", histdir, false); /* XXX user can override */
	g_set_prgname(fullname);
	gtk_init(0, NULL);
	checkconf(NULL);
	ipcwatch("main", g_main_context_default());

	close(lock);

	if (g_key_file_get_boolean(conf, "boot",
				"unsetGTK_OVERLAY_SCROLLING", NULL))
		g_unsetenv("GTK_OVERLAY_SCROLLING");

	GtkCssProvider *cssp = gtk_css_provider_new();
	gtk_css_provider_load_from_data(cssp,
			"overlay entry{margin:0 1em 1em 1em; border:none; opacity:.92;}"
			"tooltip *{padding:0}menuitem{padding:.2em}" , -1, NULL);
	gtk_style_context_add_provider_for_screen(
			gdk_display_get_default_screen(gdk_display_get_default()),
			(void *)cssp, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref(cssp);

	//icon
	GdkPixbuf *pix = gtk_icon_theme_load_icon(
		gtk_icon_theme_get_default(), APP, 128, 0, NULL);
	if (!pix) {
		static cairo_surface_t *png = NULL;
		png = cairo_image_surface_create_from_png(
#if !DEBUG
			"/usr/share/pixmaps/"
#endif
			"wyeb.png");
		if (png) {
			pix = gdk_pixbuf_get_from_surface
				(png, 0, 0,
				 cairo_image_surface_get_width(png),
				 cairo_image_surface_get_height(png));
		}
		else {
			g_print("icon file not loaded\n");
		}
	}
	if (pix)
	{
		gtk_window_set_default_icon(pix);
		g_object_unref(pix);
	}

	wins = g_ptr_array_new();
	dlwins = g_ptr_array_new();
	histimgs = g_queue_new();

	if (_run(NULL, action, uri, cwd, *exarg ? exarg : NULL)) {
#ifdef MKCLPLUG
	if (!(g_strcmp0(g_getenv("WYEB_CL"),"none") == 0))
		initmkclplug(NULL, NULL);
#endif
		gtk_main();
	} else
		exit(1);
	exit(0);
}
