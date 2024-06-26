#include <X11/X.h>
#include <X11/Xatom.h>

#include <sys/types.h>
#include <sys/wait.h>
 
static Atom atomGo =  0;
static Atom atomFind =  0;
static Atom atomUri = 0;
static Atom atomCharset = 0;
static Atom atomStyle = 0;

static void
setatom(Win *win, int a, const char *v)
{
	XChangeProperty(win->dpy, win->xid,
	                a, XA_STRING, 8, PropModeReplace,
	                (unsigned char *)v, strlen(v) + 1);
	XSync(win->dpy, False);
}

static void
initatoms(Win *win)
{
	if (!atomGo) atomGo = XInternAtom(win->dpy, "_SURF_GO", False);
	if (!atomFind) atomFind = XInternAtom(win->dpy, "_SURF_FIND", False);
	if (!atomUri) atomUri = XInternAtom(win->dpy, "_SURF_URI", False);
	if (!atomCharset) atomCharset = XInternAtom(win->dpy, "_SURF_CHARSET", False);
	if (!atomStyle) atomStyle = XInternAtom(win->dpy, "_SURF_STYLE", False);
	setatom(win, atomGo, "");
	setatom(win, atomFind, "");
	setatom(win, atomUri, "about:blank");
	setatom(win, atomCharset, "");
	setatom(win, atomStyle, "");
}

static const char *
getatom(Win* win, int a)
{
	static char buf[BUFSIZ];
	Atom adummy;
	int idummy;
	unsigned long ldummy;
	unsigned char *p = NULL;
	XSync(win->dpy, False);
	XGetWindowProperty(win->dpy, win->xid, a, 0L, sizeof(buf), False,
			   XA_STRING, &adummy, &idummy, &ldummy, &ldummy, &p);
	if (p) strncpy(buf, (char *)p, sizeof(buf) - 1);
	else buf[0] = '\0';
	XFree(p);
	return buf;
}

// SETPROP(readprop,wyebcmd,prompt):
// omnibar maintains a history file for each setprop and uses it to
// get a list of choices and presents the choices via dmenu
static char* xwinid;
#define SETPROP(r, s, p) { \
	const char * const v[] = { "/bin/sh", "-c", "omnihist-wyeb goto \"$0\" \"$1\" \"$2\" \"$3\"", xwinid, r, s, p, NULL }; \
	surfexec(v); \
}

static void
surfexec(const char * const v[])
{
	GError *err = NULL;
	g_subprocess_newv(v, G_SUBPROCESS_FLAGS_NONE, &err);
	if (err) {
		fprintf(stdout, "surfexec: g_subprocess_new failed: %s: %d: %s\n", g_quark_to_string(err->domain), err->code, err->message);
	}
}

static void
surfaddhist(const char * uri)
{
	const char * const v[] = { "/bin/sh", "-c", "omnihist-wyeb addhist \"$0\"", (char *) uri, NULL };
	surfexec(v);
}

static bool run(Win *win, char* action, const char *arg); //declaration

static GdkFilterReturn
processx(GdkXEvent *e, GdkEvent *event, gpointer user_data)
{
	Win *win = (Win *)user_data;
	XPropertyEvent *ev;
	if (((XEvent *)e)->type == PropertyNotify) {
		ev = &((XEvent *)e)->xproperty;
		if (ev->state == PropertyNewValue) {
			if (ev->atom ==  atomFind) {
				const char * s = getatom(win, atomFind);
				if (s && *s) {
					run(win, "find", s);
					return GDK_FILTER_REMOVE;
				}
			}
			if (ev->atom ==  atomGo) {
				const char * s = getatom(win, atomGo);
				if (s && *s) {
					run(win, "open", s);
					return GDK_FILTER_REMOVE;
				}
			}
			if (ev->atom == atomCharset) {
				const char * s = getatom(win, atomCharset);
				if (s && *s) {
					run(win, "customcharset", s);
					return GDK_FILTER_REMOVE;
				}
			}
			if (ev->atom == atomStyle) {
				const char * s = getatom(win, atomStyle);
				if (s && *s) {
					run(win, "applystyle", s);
					return GDK_FILTER_REMOVE;
				}
			}
		}
	}
	return GDK_FILTER_CONTINUE;
}

static void
print_headers(SoupMessageHeaders *headers, FILE *stream, const char *label)
{
	SoupMessageHeadersIter iter;
	char *name, *value;
	int printed_label = 0;
	if (!headers) return;
	soup_message_headers_iter_init (&iter, headers);
	while (soup_message_headers_iter_next(&iter, (const char **) &name, (const char **) &value) == TRUE) {
		if (!printed_label) {
			if (label) fputs(label, stream);
			printed_label = 1;
		}
		fprintf(stream, "%s: %s\n", name, value);
	}
}


// ---------------------------------------------------------------------
//
//
//

typedef union {
	int i;
	float f;
	const void *v;
} Arg;

/* cmdprompt system */
typedef struct {
        char *cmdname;
        void (*func)(Win *w, const Arg *arg);
        const Arg arg;
	char *wyebrun;
} Cmd;

void foo(Win *w, const Arg *a) { fprintf(stderr, "foo: win->sxid=%s\n", w->sxid); }

static void send(Win *win, Coms type, const char *args); // fwd decl
STATIC void resetconf(Win *, const char*, int);	   //fwd decl
STATIC void eval_javascript(Win *win, const char *script); //fwd decl
void w3mmode_set_status(Win *w, const Arg *a) { send(w, Cw3mmode, (char *) a->v); }
static void viewsourceorheaders(Win *win, viewsourceorheaders_mode flag);
void cmd_viewsourceorheaders(Win *w, const Arg *a) { viewsourceorheaders(w, a->i); }
void cmd_js(Win *w, const Arg *a) {eval_javascript(w, a->v); }

void cmd_send_set3(Win *win, const Arg *a) {
	const char *arg = a->v;
	if (win->overset == NULL) {
		win->overset = g_strdup(arg);
		return;
	}
	char **elems = g_strsplit(win->overset, "/", 5);
	int i, nelem = 0, found = -1;
	for(i = 0; elems[i]; nelem++, i++) {
		if (strcmp(elems[i], a->v) == 0) {
			found = i;
		}
	}
	if (found > -1) {	// remove it
		if (nelem == 1) {
			GFA(win->overset, NULL);
		} else {
			int j;
			char * tmp = elems[i]; // block shift left
			for (j = found; elems[j+1]; j++) elems[j] = elems[j+1];
			g_assert(elems[j+1] == NULL);
			elems[j] = NULL;
			GFA(win->overset, g_strjoinv("/", elems));
			elems[j] = tmp;	// so strv can free it
		}
	} else if (strcmp(win->overset, "") == 0) {
		GFA(win->overset, g_strdup(arg));
	} else {
		char *tmp = g_strdup_printf("%s/%s", arg, win->overset);
		g_free(win->overset);
		win->overset = tmp;
	}
	g_strfreev(elems);
	resetconf(win, NULL, 2);
}


// this is meant to be called from a load-failed page from
// loadtlsfailcb below. It relies on the fact that loadtlsfailcb calls
// webkit_web_view_load_alternate_html with the same uri as the
// base_uri. loadfailedtls should assert g_assert(v == c->view); note
// that there is no way of blocking the host again with webkit

static void
cmd_add_security_exception(Win *win, const Arg *a)
{
	GTlsCertificate *cert = win->failedcert;
	if (! win->failedcert) return;

	fprintf(stderr, "ADD SECURITY EXCEPTION\n");
	const char *uri = URI(win);
	if ((uri = strstr(uri, "https://"))) {
		uri += sizeof("https://") - 1;
		// XXX make sure there is a slash first?
		char * host = g_strndup(uri, strchr(uri, '/') - uri);
		webkit_web_context_allow_tls_certificate_for_host(
			webkit_web_view_get_context(win->kit), cert, host);
		g_free(host);
	}
}

#define JS_F "function replace(reg,rep){old=window.document.documentElement.innerHTML;window.document.documentElement.innerHTML=old.replace(RegExp(reg,\"g\"),rep);}function displaynone(){replace(\"display:[\\t]*none\",\"\")}function dump(string){console.log(string);}function hide(tagname){count=0;for(var elem of document.getElementsByTagName(tagname)){	if(elem instanceof Element){	 if(elem.style.display!=\"none\"){		elem.style.display=\"none\";		count++	}	}}dump(\"hide(\"+tagname+\"):\"+count+\"\\n\");return count;}function showtag(tagname){count=0;for(var elem of document.getElementsByTagName(tagname)){	if(elem instanceof Element){	 if(elem.style.display==\"none\"){		elem.style.display=\"\";		count++	}	}}dump(\"showtag(\"+tagname+\"):\"+count+\"\\n\");return count;}function settags(tagname,attribute,value){count=0;for(var elem of document.getElementsByTagName(tagname)){	if(elem instanceof Element){	 elem.style.setProperty(attribute,value);	 count++;	}}dump(\"settag(tag=\"+tagname+\",attr=\"+attribute+\",val=\",	value+\"):\"+count+\"\\n\");return count;}function hideClass(name){count=0;for(var elem of document.getElementsByClassName(name)){	if(elem instanceof Element){	 if(elem.style.display!=\"none\"){		elem.style.display=\"none\";		count++	}	}}dump(\"hideClass(\"+name+\"):\"+count+\"\\n\");return count;}function purgeClass(name){count=0;for(var elem of document.getElementsByClassName(name)){	if(elem instanceof Element){		elem.parentNode.removeChild(elem);		count++;	}}dump(\"purgeClass(\"+name+\"):\"+count+\"\\n\");return count;}function showClass(name){count=0;for(var elem of document.getElementsByClassName(name)){	if(elem instanceof Element){	 if(elem.style.display==\"none\"){		elem.style.display=\"\";		count++	}	}}dump(\"showClass(\"+name+\"):\"+count+\"\\n\");return count;}function setClassAttr(className,attribute,value){count=0;for(var elem of document.getElementsByClassName(className)){	if(elem instanceof Element){	 elem.style.setProperty(attribute,value);	 count++;	}}dump(\"setClassAttr(class=\"+className+\",attr=\"+attribute+\",val=\",	value+\"):\"+count+\"\\n\");return count;}"


#ifdef MKCLPLUG
static void
initmkclplug(Win *win, const Arg *a) {
#if defined(WYEB_ECL) || defined(WYEB_MKCL)
    enum wyeb_cl_t { wyeb_cl_none, wyeb_cl_default, wyeb_cl_ecl,
      wyeb_cl_mkcl } wyeb_cl;
    const char *wyeb_cl_s = g_getenv("WYEB_CL");
    if (wyeb_cl_s == NULL)
      wyeb_cl = wyeb_cl_default;
    else if (g_ascii_strcasecmp(wyeb_cl_s, "none") == 0)
      wyeb_cl = wyeb_cl_none;
    else if (g_ascii_strcasecmp(wyeb_cl_s, "ecl") == 0)
      wyeb_cl = wyeb_cl_ecl;
    else if (g_ascii_strcasecmp(wyeb_cl_s, "mkcl") == 0)
      wyeb_cl = wyeb_cl_mkcl;
    else {
      fprintf(stderr, "unknown value for env var WYEB_CL: %s. Wanted one of ecl mkcl or none. Treating as none.\n", wyeb_cl_s);
      wyeb_cl = wyeb_cl_none;
    }
    // start with wyeb_cl=none and load at runtime with surfcmd
    if (a && a->v) {
	    fprintf(stderr, "initmkclplugin: maybe override %s with %s\n",
		    (char *) wyeb_cl_s, (char *) a->v);
	    if (g_ascii_strcasecmp(a->v, "ecl") == 0)
		    wyeb_cl = wyeb_cl_ecl;
	    else if (g_ascii_strcasecmp(a->v, "mkcl") == 0)
		    wyeb_cl = wyeb_cl_mkcl;
    }
#if defined(WYEB_ECL)
    extern void ecl_initialize(char *app);
    if (wyeb_cl == wyeb_cl_default) wyeb_cl = wyeb_cl_ecl;
    if (wyeb_cl == wyeb_cl_ecl)
      ecl_initialize("eclplugtest");
#endif
#if defined(WYEB_MKCL)
    extern void mkcl_initialize(char *app);
    if (wyeb_cl == wyeb_cl_default) wyeb_cl = wyeb_cl_ecl;
    if (wyeb_cl == wyeb_cl_mkcl)
      mkcl_initialize("mkclplugtest");
#endif
#endif
}
#endif

static Cmd choices[] = {
	{ "foo",		foo,	{ 0 } },
	{ "reload-with-charset", NULL, { 0 }, "surfcharset" },
	{ "w3mmode-one", w3mmode_set_status, { .v = "one" } },
	{ "w3mmode-same-host", w3mmode_set_status, { .v = "same_host" } },
	{ "w3mmode-off", w3mmode_set_status, { .v = "off" } },
	{ "w3mmode-status", w3mmode_set_status, { .v = "status" } },
	{ "w3mmode-use-conf", w3mmode_set_status, { .v = "use_conf" } },
	{ "view-html", cmd_viewsourceorheaders, { .i = VSH_HTML } },
	{ "view-source", cmd_viewsourceorheaders, { .i = VSH_SOURCE } },
	{ "view-headers", cmd_viewsourceorheaders, { .i = VSH_HEADERS } },
	{ "toggle-javascript", cmd_send_set3, { .v = "script" } },
	{ "toggle-images", cmd_send_set3, { .v = "image" } },
	{ "toggle-reldomain", cmd_send_set3, { .v = "rel" } },
	{ "save-source", NULL, { 0 }, "savesource" },
	{ "save-mhtml", NULL, { 0 }, "savemhtml" },
	{ "toggle-stylesheets", cmd_js, { . v =  "var s = window.document.styleSheets; for (var i = 0; i < s.length; i++) s[i].disabled = !s[i].disabled; \"cookie\";" } },
	{ "twitter", cmd_js, { . v = "{hide(\"svg\");hide(\"button\");setClassAttr(\"PlayableMedia-player\",\"padding-bottom\",\"0\");setClassAttr(\"AdaptiveMedia-singlePhoto\",\"padding-top\",\"0\");hideClass(\"dismiss-module\");}" } },
	{ "musings", cmd_js, { . v = "{replace(\"font-size:[^;]+;\",\"\");replace(\"font-family:[^;]+;\",\"\");}" } },
	{ "add_security_exception", cmd_add_security_exception, { 0 } },
	{ "cache-on", NULL, { .v = "on" }, "cachemodel" },
	{ "cache-off", NULL, { .v = "off" }, "cachemodel" },
	{ "cache-mem", NULL, { .v = "mem" }, "cachemodel" },
	{ "cache-status", NULL, { .v = "status" }, "cachemodel" },
	{ "history-on", NULL, { .v = "on" }, "historymode" },
	{ "history-off", NULL, { .v = "off" }, "historymode" },
	{ "history-status", NULL, { .v = "status" }, "historymode" },
	{ "cookies-on", NULL, { .v = "on" }, "cookiepolicy" },
	{ "cookies-off", NULL, { .v = "off" }, "cookiepolicy" },
	{ "cookies-status", NULL, { .v = "status" }, "cookiepolicy" },
	{ "toggle-cookies", NULL, { .v = "cycle" }, "cookiepolicy" },
	{ "proxy-status", NULL, { .v = "status" }, "proxymode" },
	{ "proxy-system", NULL, { .v = "default" }, "proxymode" },
	{ "proxy-on", NULL, { .v = "custom" }, "proxymode" },
	{ "proxy-off", NULL, { .v = "no_proxy" }, "proxymode" },
	{ "showcert", NULL, { 0 }, "showcert" },

	{ "apply-style", NULL, { 0 }, "surfapplystyle" },

#if JAVASCRIPT_MARKUP_SHENNANIGANS
	{ "toggle-noscript", cmd_send_set3, { .v = "noscript" } },
#endif

	{ "toggle-reuse-windows", NULL, { .v = "toggle" }, "reusemode" },
	{ "reuse-windows-status", NULL, { .v = "status" }, "reusemode" },
	{ "reuse-windows-on", NULL, { .v = "on" }, "reusemode" },
	{ "reuse-windows-off", NULL, { .v = "off" }, "reusemode" },

	{ "reload-as-html5", NULL, { 0 }, "reloadashtml5" },
	{ "readermode", NULL, { 0 }, "readermode" },

#ifdef MKCLPLUG
#ifdef WYEB_MKCL
	{ "initmkclplug-mkcl", initmkclplug, { .v = "mkcl" } },
#endif
#ifdef WYEB_ECL
	{ "initmkclplug-ecl", initmkclplug, { .v = "ecl" } },
#endif
#endif

};

void surf_cmdprompt(Win *w)
{
	char buf[1024];
	int buflen = sizeof(buf);
	int pipefd[2], bikefd[2], cpid;
	int len, maxlen = -1, nread, pos, i;
	char *s;

	if (pipe(pipefd) == -1 || pipe(bikefd) == -1) {
		perror("pipe()");
		return;
	}

	if ((cpid = fork()) == -1) {
		perror("fork()");
		return;
	}

	if (cpid == 0) { /* child reads from pipe[0] writes to bike[1] */
		fflush(stderr);

		close(pipefd[1]);
		close(bikefd[0]);

		if (dup2(pipefd[0], STDIN_FILENO) == -1) {
			perror("dup2");
			return;
		}
		if (dup2(bikefd[1], STDOUT_FILENO) == -1) {
			perror("dup2");
			return;
		}
		close(pipefd[0]);
		close(bikefd[1]);

		xwinid = w->sxid;
		if (execlp("dmenu", "dmenu", "-l", "10",  "-p", "M-x", "-w", xwinid, NULL))
			perror("execlp(dmenu)");
	}

	/* Parent writes to pipe[1] and reads from bike[0] */
	close(pipefd[0]);
	close(bikefd[1]);

	static Cmd * pchoices[sizeof(choices)/sizeof(choices[0])];
	static int pchoices_initialized = 0;
	if (!pchoices_initialized) {
		for (i = 0; i < sizeof(choices)/sizeof(choices[0]); i++)
			pchoices[i] = &choices[i];
		pchoices_initialized = 1;
	}

	for(i = 0; i < sizeof(choices)/sizeof(choices[0]); i++) {
		s = pchoices[i]->cmdname;
		len = strlen(s);
		maxlen = (len > maxlen) ? len : maxlen;
		if (len > buflen - 1)
			fprintf(stderr, "promptcmd: WARNING: choice string %s too long: %d: will be truncated: %d\n", s, len, buflen - 1);
		write(pipefd[1], s, len);
		if (i + 1 < sizeof(choices)/sizeof(choices[0]))
			write(pipefd[1], "\n", 1);
	}

	close(pipefd[1]);	/* Reader will see EOF */
	wait(NULL); 		/* wait for the child */

	for(pos = 0;
	    (nread = read(bikefd[0], &buf[pos], buflen - pos)) > 0;
	    pos += nread);

	close(bikefd[0]);

	// null terminate buf. this chops off trailing newline if it
	// fit, and truncates it if it did not fit.
	buf[pos ? pos - 1 : 0] = 0;

	// If a non-NULL position pointer was supplied, return the
	// index into choices which was selected in it.  Stop at the
	// first match.  This is certainly the wrong result when
	// multiple choice strings have been truncated to the same
	// prefix.

	for (i = 0; i < sizeof(choices)/sizeof(choices[0]); i++) {
		s = pchoices[i]->cmdname;
		if (strncmp(s, buf, buflen - 1) == 0) {
			int j;
			if (i != 0)
				for (j = 0; j < i; j++) {
					Cmd * tmp = pchoices[j];
					pchoices[j] = pchoices[i];
					pchoices[i] = tmp;
				}
			if (pchoices[0]->wyebrun)
				run(w, pchoices[0]->wyebrun, (pchoices[0]->arg).v);
			else
				pchoices[0]->func(w, &(pchoices[0]->arg));
			return;
		}
	}
	if (buf[0])
		fprintf(stderr, "promptcmd: WARNING: unknown command %s\n",
			buf);
}
