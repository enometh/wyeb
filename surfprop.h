#include <X11/X.h>
#include <X11/Xatom.h>

static Atom atomGo =  0;
static Atom atomFind =  0;
static Atom atomUri = 0;
static Atom atomCharset = 0;

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
	setatom(win, atomGo, "");
	setatom(win, atomFind, "");
	setatom(win, atomUri, "about:blank");
	setatom(win, atomCharset, "");
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
