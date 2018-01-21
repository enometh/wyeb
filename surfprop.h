#include <X11/X.h>
#include <X11/Xatom.h>

#include <sys/types.h>
#include <sys/wait.h>
 
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
	const char * const v[] = { "/bin/sh", "-c", "omnihist-wyeb goto \"$0\" \"$1\" \"$2\"", xwinid, r, s, p, NULL }; \
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

static Cmd choices[] = {
	{ "foo",		foo,	{ 0 } },
};

void surf_cmdprompt(Win *w)
{
	char buf[1024];
	int buflen = sizeof(buf);
	int pipefd[2], kikefd[2], cpid;
	int len, maxlen = -1, nread, pos, i;
	char *s;

	if (pipe(pipefd) == -1 || pipe(kikefd) == -1) {
		perror("pipe()");
		return;
	}

	if ((cpid = fork()) == -1) {
		perror("fork()");
		return;
	}

	if (cpid == 0) { /* child reads from pipe[0] writes to kike[1] */
		fflush(stderr);

		close(pipefd[1]);
		close(kikefd[0]);

		if (dup2(pipefd[0], STDIN_FILENO) == -1) {
			perror("dup2");
			return;
		}
		if (dup2(kikefd[1], STDOUT_FILENO) == -1) {
			perror("dup2");
			return;
		}
		close(pipefd[0]);
		close(kikefd[1]);

		xwinid = w->sxid;
		if (execlp("dmenu", "dmenu", "-l", "10",  "-p", "M-x", "-w", xwinid, NULL))
			perror("execlp(dmenu)");
	}

	/* Parent writes to pipe[1] and reads from kike[0] */
	close(pipefd[0]);
	close(kikefd[1]);

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
	    (nread = read(kikefd[0], &buf[pos], buflen - pos)) > 0;
	    pos += nread);

	close(kikefd[0]);

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
