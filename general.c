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

#include <math.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <glib/gstdio.h>
#include <regex.h>

#ifndef STATIC
#ifdef MKCLPLUG
#define STATIC __attribute__((visibility("default")))
#else
#define STATIC static
#endif
#endif


/*
 * enable-javascript-markup is exposed in 2.24.0. enable-javascript is
 * required for JSC and should always be true. If using 2.24.0+
 * user-level toggling of javascript only affects
 * enable-javascript-markup.
 */
#if WEBKIT_CHECK_VERSION(2,24,0)
#define JAVASCRIPT_MARKUP_SHENNANIGANS 1
#else
#define JAVASCRIPT_MARKUP_SHENNANIGANS 0
#endif

#define OLDNAME  "wyebrowser"
#define DIRNAME  "wyeb."
#define APP      "wyeb"

#define DSET     "set;"
#define MIMEOPEN "mimeopen -n %s"
#define HINTKEYS "fsedagwrvxqcz"
//bt324"
#define DSEARCH  "https://www.google.com/search?q=%s"
#define DSEARCHKEY "g"

//for webkit2gtk4.0. 4.1 has this
#ifndef SOUP_HTTP_URI_FLAGS
#define SOUP_HTTP_URI_FLAGS (G_URI_FLAGS_HAS_PASSWORD | G_URI_FLAGS_ENCODED_PATH | G_URI_FLAGS_ENCODED_QUERY | G_URI_FLAGS_ENCODED_FRAGMENT | G_URI_FLAGS_SCHEME_NORMALIZE)
#endif

#if ! DEBUG + 0
#undef DEBUG
#define DEBUG 0
#endif

#if DEBUG
# define D(f, ...) g_print("#"#f"\n", __VA_ARGS__);
# define DNN(f, ...) g_print(#f, __VA_ARGS__);
# define DD(a) g_print("#"#a"\n");
# define DENUM(v, e) if (v == e) D(%s:%3d is %s, #v, v, #e);
#else
# define D(...) ;
# define DNN(...) ;
# define DD(a) ;
# define DENUM(v, e) ;
#endif

// this app depends on WEBKIT_MAJOR_VERSION > 2 || WEBKIT_MINOR_VERSION > 16

#if WEBKIT_MAJOR_VERSION > 2 || WEBKIT_MINOR_VERSION > 22
# define V24 1
#else
# define V24 0
#endif

#if WEBKIT_MAJOR_VERSION > 2 || WEBKIT_MINOR_VERSION > 20
# define V22 1
#else
# define V22 0
#endif

#ifdef MAINC
#if ! v22
#include <JavaScriptCore/JSStringRef.h>
#endif
#endif


#define SIG(o, n, c, u) \
	g_signal_connect(o, n, G_CALLBACK(c), u)
#define SIGA(o, n, c, u) \
	g_signal_connect_after(o, n, G_CALLBACK(c), u)
#define SIGW(o, n, c, u) \
	g_signal_connect_swapped(o, n, G_CALLBACK(c), u)
#define GFA(p, v) {void *__b = p; p = v; g_free(__b);}

STATIC char *sfree(char *p)
{
	static void *s;
	g_free(s);
	return s = p;
}

STATIC char *fullname = "";
STATIC GKeyFile *conf;
STATIC char *confpath;

typedef struct _WP WP;

typedef enum {
	Cload   = 'L',
	Coverset= 'O',
	Cstart  = 's',
	Con     = 'o',

	//hint
	Ckey    = 'k',
	Cclick  = 'c',
	Clink   = 'l',
	Curi    = 'u',
	Ctext   = 't',
	Cspawn  = 'S',
	Crange  = 'r',
	Cimage  = 'i',
	Crm     = 'R',

	Creveal = 'z',

	Cmode   = 'm',
	Cfocus  = 'f',
	Cblur   = 'b',
	Cwhite  = 'w',
	Ctlset  = 'T',
	Ctlget  = 'g',
	Cwithref= 'W',
	Cscroll = 'v',

	Cfree   = 'F',
	Cw3mmode= '3',
	Coffline= 'K',
	Cscrollposition = 32,
} Coms;


//@conf
typedef struct {
	char *group;
	char *key;
	char *val;
	char *desc;
} Conf;
STATIC Conf dconf[] = {
	{"all"   , "winwidth"     , "800"},
	{"all"   , "winheight"    , "600"},
	{"all"   , "zoom"         , "1.000"},
	{"all"   , "ignoretlserr" , "false"},
	{"all"   , "itp"          , "false"},
	{"all"   , "histviewsize" , "99"},
	{"all"   , "histimgs"     , "99"},
	{"all"   , "histimgsize"  , "222"},
	{"all"   , "keepproc"     , "false"},
	//compatibility
	{"all"   , "pointerwarp"  , "false"},

//	{"all"   , "configreload" , "true",
//			"reload last window when whiteblack.conf or reldomain are changed"},

	{"boot"  , "enablefavicon", "true"},
	{"boot"  , "extensionargs", "adblock:true;"},
	{"boot"  , "multiwebprocs", "true"},
	{"boot"  , "ephemeral"    , "false"},
	{"boot"  , "unsetGTK_OVERLAY_SCROLLING", "true", "workaround"},

//	{"search", DSEARCHKEY     , DSEARCH},
//	{"search", "f"            , "https://www.google.com/search?q=%s&btnI=I"},

	{"search", "b"            , "https://www.bing.com/?q=%s"},
	{"search", "d"            , "https://duckduckgo.com/html/?q=%s"},
	{"search", "g"            , "https://www.google.co.in/search?num=50&hl=en&q=%s&gbv=1"},
	{"search", "u"            , "https://www.urbandictionary.com/define.php?term=%s"},
	{"search", "o"            , "https://en.lexico.com/en/definition/%s"},
	{"search", "w"            , "https://en.wikipedia.org/wiki/%s"},

	{"template", "na"         , "%s"},
	{"template", "h"          , "http://%s"},

	{"set:v"     , "enable-caret-browsing", "true"},
	{"set:v"     , "hackedhint4js"        , "false"},
#if  JAVASCRIPT_MARKUP_SHENNANIGANS
	{"set:script", "enable-javascript-markup" , "true"},
	{"set:script", "enable-javascript"    , "true"},
	{"set:noscript", "enable-javascript"  , "false"},
#else
	{"set:script", "enable-javascript"    , "true"},
#endif
	{"set:rel",    "reldomaindataonly"    , "false"},
	{"set:image" , "auto-load-images"     , "true"},
	{"set:image" , "linkformat"   , "[![]("APP":F) %.40s ](%s)"},
	{"set:image" , "linkdata"     , "tu"},

	//core
	{DSET    , "editor"           , MIMEOPEN,
		"\ncore\n\n"
		"editor=xterm -e mimeopen %s\n"
		"editor=gvim --servername "APP" --remote-silent \"%s\""
	},
	{DSET    , "mdeditor"         , ""},
	{DSET    , "diropener"        , MIMEOPEN, "diropener=xterm -e mimeopen %s"},
	{DSET    , "generator"        , "markdown -f -style %s"},

	//misc
	{DSET    , "usercss"          , "user.css", "\nmisc\n\nusercss=user.css;user2.css"},
	{DSET    , "userscripts"      , ""},
	{DSET    , "search"           , DSEARCHKEY, "search="DSEARCH},
	{DSET    , "searchstrmax"     , "99"},
	{DSET    , "addressbar"       , "true"},
	{DSET    , "msgcolor"         , "#c07"},
	{DSET    , "msgmsec"          , "2000"},
	{DSET    , "keepfavicondb"    , "false"},
	{DSET    , "newwinhandle"     , "normal", "notnew | ignore | back | normal"},
	{DSET    , "scriptdialog"     , "true"},

	//loading
	{DSET    , "adblock"          , "true",
		"\nloading\n\nadblock has a point only while "APP"adblock is working."
	},
	{DSET    , "reldomaindataonly", "true"},
	{DSET    , "reldomaincutheads", "www.;wiki.;bbs.;developer."},
	{DSET    , "showblocked"      , "true"},
	{DSET    , "stdoutheaders"    , "false"},
	{DSET    , "removeheaders"    , "",
		"removeheaders=Upgrade-Insecure-Requests;Referer;"},
	{DSET    , "rmnoscripttag"    , "false"},
	{DSET    , "w3mmode"          , "one" }, // one, off, same_host

	//bookmark
	{DSET    , "linkformat"       , "[%.40s ](%s)", "\nbookmark\n"},
	{DSET    , "linkdata"         , "tu", "t: title, u: uri, f: favicon"},

	//hint
	{DSET    , "hintkeys"        , HINTKEYS, "\nhint\n"},
	{DSET    , "hackedhint4js"    , "true"},
	{DSET    , "hintrangemax"     , "9"},
	{DSET    , "rangeloopusec"    , "90000"},

	//dl
	{DSET    , "dlwithheaders"    , "false", "\ndownload\n"},
	{DSET    , "dlmimetypes"      , "",
		"dlmimetypes=text/plain;video/;audio/;application/\n"
		"dlmimetypes=*"},
	{DSET    , "dlsubdir"         , ""},
	{DSET    , "dlwinback"        , "true"},
	{DSET    , "dlwinclosemsec"   , "10000"},

	//script
	{DSET    , "spawnmsg"         , "true", "\nscript\n"},

	{DSET    , "onstartmenu"      , "",
		"onstartmenu exec a file in the menu dir when load started before redirect"},
	{DSET    , "onloadmenu"       , "", "when load commited"},
	{DSET    , "onloadedmenu"     , "", "when load finished"},

	//input
	{DSET    , "multiplescroll"   , "2", "\ninput\n"},
	{DSET    , "keybindswaps"     , "",
		"keybindswaps=Xx;ZZ;zZ ->if typed x: x to X, if Z: Z to Z"},
	{DSET    , "hjkl2arrowkeys"   , "false",
		"hjkl's defaults are scrolls, not arrow keys"},
	{DSET    , "mdlbtnlinkaction" , "openback"},
	{DSET    , "mdlbtnspace"      , "winlist"},
	{DSET    , "mdlbtnleft"       , "prevwin"},
	{DSET    , "mdlbtnright"      , "nextwin"},
	{DSET    , "mdlbtnup"         , "top"},
	{DSET    , "mdlbtndown"       , "bottom"},
	{DSET    , "pressscrollup"    , "top"},
	{DSET    , "pressscrolldown"  , "bottom"},
	{DSET    , "rockerleft"       , "back"},
	{DSET    , "rockerright"      , "forward"},
	{DSET    , "rockerup"         , "quitprev"},
	{DSET    , "rockerdown"       , "quitnext"},
	{DSET    , "button8"          , "back"},
	{DSET    , "button9"          , "forward"},

	//changes
	//{DSET      , "auto-load-images" , "false"},
	//{DSET      , "enable-plugins"   , "false"},
	//{DSET      , "enable-java"      , "false"},
	//{DSET      , "enable-fullscreen", "false"},

	{DSET      , "default-font-size", "17"},
	{DSET      , "default-monospace-font-size", "17"},
	{DSET      , "minimum-font-size", "14"},
	{DSET      , "default-font-family", "Serif"},
//	{DSET      , "enable-developer-extras, "false"},
	{DSET      , "enable-site-specific-quirks", "false"},
	{DSET      , "enable-write-console-messages-to-stdout", "true"},
	{DSET      , "allow-file-access-from-file-urls", "true"},

};
#ifdef MAINC
STATIC bool confbool(char *key)
{ return g_key_file_get_boolean(conf, "all", key, NULL); }
STATIC int confint(char *key)
{ return g_key_file_get_integer(conf, "all", key, NULL); }
STATIC double confdouble(char *key)
{ return g_key_file_get_double(conf, "all", key, NULL); }
#endif
STATIC char *confcstr(char *key)
{//return is static string
	static char *str;
	GFA(str, g_key_file_get_string(conf, "all", key, NULL))
	return str ? *str ? str : NULL : NULL;
}
STATIC char *getset(WP *wp, char *key)
{//return is static string
	if (!wp)
	{
		static char *ret;
		GFA(ret, g_key_file_get_string(conf, DSET, key, NULL))
		return ret ? *ret ? ret : NULL : NULL;
	}
	return confcstr(key) ?: g_object_get_data(wp->seto, key);
}
STATIC bool getsetbool(WP *wp, char *key)
{ return !g_strcmp0(getset(wp, key), "true"); }
STATIC int getsetint(WP *wp, char *key)
{ return atoi(getset(wp, key) ?: "0"); }
#ifdef MAINC
STATIC char **getsetsplit(WP *wp, char *key)
{
	char *tmp = getset(wp, key);
	return tmp ? g_strsplit(tmp, ";", -1) : NULL;
}
#endif


STATIC char *path2conf(const char *name)
{
	return g_build_filename(
			g_get_user_config_dir(), fullname, name, NULL);
}

STATIC bool setprop(WP *wp, GKeyFile *kf, char *group, char *key)
{
	if (!g_key_file_has_key(kf, group, key, NULL)) return false;
	char *val = g_key_file_get_string(kf, group, key, NULL);
	g_object_set_data_full(wp->seto, key, *val ? val : NULL, g_free);
	return true;
}
STATIC void setprops(WP *wp, GKeyFile *kf, char *group)
{
	//sets
	static int deps;
	if (deps > 99) return;
	char **sets = g_key_file_get_string_list(kf, group, "sets", NULL, NULL);
	for (char **set = sets; set && *set; set++) {
		char *setstr = g_strdup_printf("set:%s", *set);
		deps++;
		setprops(wp, kf, setstr);
		deps--;
		g_free(setstr);
	}
	g_strfreev(sets);

	//D(set props group: %s, group)
#ifdef MAINC
	_kitprops(true, wp->seto, kf, group);
#else
	setprop(wp, kf, group, "javascript-can-open-windows-automatically");
	if (setprop(wp, kf, group, "user-agent") && strcmp(group, DSET))
		wp->setagent = true;
	if (setprop(wp, kf, group, "accept") && strcmp(group, DSET))
		wp->setaccept = true;
#endif

	//non webkit settings
	int len = sizeof(dconf) / sizeof(*dconf);
	for (int i = 0; i < len; i++)
		if (!strcmp(dconf[i].group, DSET))
			setprop(wp, kf, group, dconf[i].key);
}

STATIC GSList *regs;
STATIC GSList *regsrev;
STATIC void makeuriregs() {
	int i = 0, j = 0;
	for (GSList *next = regs; next; next = next->next)
	{
		regfree(((void **)next->data)[0]);
		g_free( ((void **)next->data)[1]);
	}
	g_slist_free_full(regs, g_free);
	regs = NULL;
	g_slist_free(regsrev);
	regsrev = NULL;

	char **groups = g_key_file_get_groups(conf, NULL);
	for (char **next = groups; *next; next++)
	{
		i++;
		char *gl = *next;
		if (!g_str_has_prefix(gl, "uri:")) continue;

		char *g = gl;
		char *tofree = NULL;
		if (g_key_file_has_key(conf, g, "reg", NULL))
		{
			g = tofree = g_key_file_get_value(conf, g, "reg", NULL);
		} else {
			g += 4;
		}

		void **reg = g_new(void*, 2);
		*reg = g_new(regex_t, 1);
//		if (regcomp(*reg, g, REG_EXTENDED | REG_NOSUB))
		if (!g || regcomp(*reg, g, REG_EXTENDED))
		{ //failed
			g_free(*reg);
			g_free(reg);
		} else {
			j++;
			reg[1] = g_strdup(gl);
			regsrev = g_slist_prepend(regsrev, reg);
//			g_message("makeuriconf: i=%d j=%d uri=%s", i, j, reg[1]);
		}
		g_free(tofree);
	}
	g_strfreev(groups);

//	g_message("makeuriconf: len(regsrev)=%d", g_slist_length(regsrev));
	for (GSList *_next = regsrev; _next; _next = _next->next)
		regs = g_slist_prepend(regs, _next->data);
//	g_message("makeuriconf: len(regsrev)=%d", g_slist_length(regsrev));
//	g_message("makeuriconf: len(regs)=%dx", g_slist_length(regs));
}
STATIC bool eachuriconf(WP *wp, const char* uri, bool lastone,
		bool (*func)(WP *, const char *uri, char *group))
{
	bool ret = false;
	regmatch_t match[2];
	for (GSList *reg = lastone ? regsrev : regs; reg; reg = reg->next)
		if (regexec(*(void **)reg->data, uri, 2, match, 0) == 0)
		{
			char *m = match[1].rm_so == -1 ? NULL : g_strndup(
					uri + match[1].rm_so, match[1].rm_eo - match[1].rm_so);

			ret = func(wp, m ?: uri, ((void **)reg->data)[1]) || ret;
			g_free(m);
			if (lastone) break;
		}

	return ret;
}
STATIC bool seturiprops(WP *wp, const char *uri, char *group)
{
	setprops(wp, conf, group);
	return true;
}

STATIC void _resetconf(WP *wp, const char *uri, bool force)
{
	//clearing. don't worry about reg and handler they are not set
	if (force || (wp->lasturiconf && g_strcmp0(wp->lasturiconf, uri)))
		setprops(wp, conf, DSET);
	GFA(wp->lasturiconf, NULL)

	GFA(wp->lastreset, g_strdup(uri))
	if (uri && eachuriconf(wp, uri, false, seturiprops))
		GFA(wp->lasturiconf, g_strdup(uri))
	if (wp->overset) {
		char **sets = g_strsplit(wp->overset, "/", -1);
		for (char **set = sets; *set; set++)
		{
			char *setstr = g_strdup_printf("set:%s", *set);
			setprops(wp, conf, setstr);
			g_free(setstr);
		}
		g_strfreev(sets);
	}
}
STATIC void initconf(GKeyFile *kf)
{
	if (conf) g_key_file_free(conf);
	conf = kf ?: g_key_file_new();
	makeuriregs();

	int len = sizeof(dconf) / sizeof(*dconf);
	for (int i = 0; i < len; i++)
	{
		Conf c = dconf[i];

		if (g_key_file_has_key(conf, c.group, c.key, NULL)) continue;
		if (kf)
		{
			if (!strcmp(c.group, "search")) continue;
			if (!strcmp(c.group, "template")) continue;
			if (g_str_has_prefix(c.group, "set:")) continue;
		}

		g_key_file_set_value(conf, c.group, c.key, c.val);
		if (c.desc)
			g_key_file_set_comment(conf, c.group, c.key, c.desc, NULL);
	}

#ifdef MAINC
	if (!kf)
	{
		g_key_file_set_value(  conf, DSET, "dummy", "endof"APP);
		g_key_file_set_comment(conf, DSET, "dummy", "\nWebkit's settings\n", NULL);
		g_key_file_remove_key( conf, DSET, "dummy", NULL);
	}

	//fill vals not set
	if (LASTWIN)
		_kitprops(false, LASTWIN->seto, conf, DSET);
	else {
		WebKitSettings *set = webkit_settings_new();
		_kitprops(false, (GObject *)set, conf, DSET);
		g_object_unref(set);
	}

	if (kf) return;

	//sample and comment
	g_key_file_set_comment(conf, "all", NULL,
			"Basically "APP" doesn't cut spaces."
			" Also true is only 'true' not 'True'"
			"\n\n'all' overwrites the 'set's", NULL);
	g_key_file_set_comment(conf, "template", NULL,
			"Unlike the search group, the arg is not escaped\n"
			"but can be called the same as the search", NULL);
	g_key_file_set_comment(conf, "set:v", NULL,
			"Settings set. You can add set:*\n"
			"It is enabled by actions(set/set2/setstack) or included by others"
			, NULL);
	g_key_file_set_comment(conf, DSET, NULL,
			"\n\nDefaults of 'set's\n"
			"You can use set;'s keys in set:* and uri:*\n\n", NULL);
	g_key_file_set_comment(conf, DSET, "hardware-acceleration-policy",
			"ON_DEMAND | ALWAYS | NEVER", NULL);

	const char *sample = "uri:^https?://(www\\.)?foo\\.bar/.*";

#if JAVASCRIPT_MARKUP_SHENNANIGANS
	g_key_file_set_boolean(conf, sample, "enable-javascript", true);
	g_key_file_set_boolean(conf, sample, "enable-javascript-markup", false);
#else
	g_key_file_set_boolean(conf, sample, "enable-javascript", false);
#endif
	g_key_file_set_comment(conf, sample, NULL,
			"After 'uri:' is regular expressions for the setting set.\n"
			"preferential order of groups: lower > upper > '"DSET"'"
			, NULL);

	sample = "uri:^foo|a-zA-Z0-9|*";

	g_key_file_set_string(conf, sample, "reg", "^foo[^a-zA-Z0-9]*$");
	g_key_file_set_comment(conf, sample, "reg",
			"Use reg if the regular expression has []"
			, NULL);
	g_key_file_set_string(conf, sample, "handler", "chromium %s");
	g_key_file_set_comment(conf, sample, "handler",
			"handler cancels request before sent and\n"
			"spawns the command with a URI matched the 'uri:'"
			, NULL);
	g_key_file_set_string(conf, sample, "handlerunesc", "false");
	g_key_file_set_string(conf, sample, "handlerescchrs", "");

	g_key_file_set_string(conf, sample, "sets", "image;script");
	g_key_file_set_comment(conf, sample, "sets",
			"include sets" , NULL);
#endif
}


//@misc
STATIC bool _mkdirif(char *path, bool isfile)
{
	bool ret = false;
	char *dir = isfile ? g_path_get_dirname(path) : path;

	if (!g_file_test(dir, G_FILE_TEST_EXISTS))
		ret = !g_mkdir_with_parents(dir, 0700);

	g_assert(g_file_test(dir, G_FILE_TEST_IS_DIR));

	if (isfile) g_free(dir);

	return ret;
}
STATIC void mkdirif(char *path)
{
	_mkdirif(path, true);
}

STATIC char *_escape(const char *str, char *esc)
{
	gulong len = 0;
	for (const char *c = str; *c; c++)
	{
		len++;
		for (char *e = esc; *e; e++)
			if (*e == *c)
			{
				len++;
				break;
			}
	}
	char ret[len + 1];
	ret[len] = '\0';

	gulong i = 0;
	for (const char *c = str; *c; c++)
	{
		for (char *e = esc; *e; e++)
			if (*e == *c)
			{
				ret[i++] = '\\';
				break;
			}

		ret[i++] = *c;
	}

	return g_strdup(ret);
}
STATIC char *regesc(const char *str)
{
	return _escape(str, ".?+");
}


//@ipc
STATIC char *ipcpath(char *name)
{
	static char *path;
	GFA(path, g_build_filename(g_get_user_runtime_dir(), fullname, name, NULL));
	mkdirif(path);
	return path;
}

STATIC void ipccb(const char *line);
STATIC gboolean ipcgencb(GIOChannel *ch, GIOCondition c, gpointer p)
{
	char *line;
//	GError *err = NULL;
	g_io_channel_read_line(ch, &line, NULL, NULL, NULL);
//	if (err)
//	{
//		D("ioerr: ", err->message);
//		g_error_free(err);
//	}
	if (!line) return true;
	g_strchomp(line);

	//D(receive %s, line)
	char *unesc = g_strcompress(line);
	ipccb(unesc);
	g_free(unesc);
	g_free(line);
	return true;
}

STATIC bool ipcsend(char *name, char *str) {
	char *path = ipcpath(name);
	bool ret = false;
	int cpipe = 0;
	if (
		(g_file_test(path, G_FILE_TEST_EXISTS)) &&
		(cpipe = open(path, O_WRONLY | O_NONBLOCK)))
	{
		D(ipcsend start %s %s, name, str)
		char *esc = g_strescape(str, "");
		char *send = g_strconcat(esc, "\n", NULL);
		int len = strlen(send);
		if (len > PIPE_BUF)
			fcntl(cpipe, F_SETFL, 0);
		ret = write(cpipe, send, len) == len;
		g_free(send);
		g_free(esc);
		close(cpipe);

		D(ipcsend -end- %s %s %d, name, str, ret)
	}
	return ret;
}
STATIC void ipcwatch(char *name, GMainContext *ctx) {
	char *path = ipcpath(name);

	if (!g_file_test(path, G_FILE_TEST_EXISTS))
		mkfifo(path, 0600);

	GIOChannel *io = g_io_channel_new_file(path, "r+", NULL);
	GSource *watch = g_io_create_watch(io, G_IO_IN);
	g_io_channel_unref(io);
	g_source_set_callback(watch, (GSourceFunc)ipcgencb, NULL, NULL);
	g_source_attach(watch, ctx);
	g_source_unref(watch);
}
int
fprintf_gerror(FILE *stream, GError *gerror, const char *fmt, ...)
{
	va_list ap;
	int n, m = 0;
	if (gerror)
		m = fprintf(stream, "GERROR:%s:%d: %s\n",
			    g_quark_to_string(gerror->domain),
			    gerror->code, gerror->message);
	va_start(ap, fmt);
	n = vfprintf(stream, fmt, ap);
	va_end(ap);
	return m+n;
}


