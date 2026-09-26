/*
 * ttpanel - a front panel for the Atari System V desktop, in the manner of
 * CDE's and IRIX's: a bevelled bar along the bottom of the screen with a
 * button per program and a clock. A click starts the program.
 *
 * The buttons come from ~/.ttpanelrc, one per line:
 *
 *     Label : icon : command
 *
 * where icon is one of term, files, edit, calc, book, load, game, clock,
 * mail, paint, tool, and the command runs through /bin/sh. '#' starts a
 * comment. Without the file the panel offers a terminal, the file manager,
 * an editor, a calculator, the manuals, a load meter and OpenUA.
 *
 * With olvwm (as patched for Atari System V) the panel also has a
 * workspace switcher - a button per screen of the virtual desktop, read
 * from the root's _OLVWM_DESKTOP and chosen with an _OLVWM_GOTO message -
 * and a Log out button, which sends _OLVWM_EXIT: olvwm then asks to
 * confirm, as its Workspace menu's Exit does.
 *
 * Options: -top (at the top of the screen instead), -fn FONT.
 *
 * Plain Xlib, so it runs on the R6.3 libraries and draws in a handful of
 * colours; the icons are drawn, not bitmaps.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

#define BTN_W	64
#define BTN_H	58
#define PAD	5
#define MAXBTN	24

typedef struct {
	char	label[32];
	char	icon[16];
	char	cmd[256];
	int	x;
} Button;

static Display *dpy;
static int	scr;
static Window	win;
static GC	gc;
static XFontStruct *font, *bigfont;
static int	panel_w, panel_h;
static Button	btn[MAXBTN];
static int	nbtn;
static int	pressed = -1;
static int	clock_x;
static int	ws_x, ws_cols, ws_rows, ws_cur;	/* the switcher; ws_cols 0 = none */
static int	exit_x;
static Atom	a_desktop, a_goto, a_exit;

/* the Indigo Magic / CDE greys and blues, and the icons' colours */
enum { C_FACE, C_LIGHT, C_SHADOW, C_DARK, C_TEXT, C_BLUE, C_WHITE,
       C_YELLOW, C_RED, C_GREEN, C_SCREEN, C_TAN, C_N };
static const char *cname[C_N] = {
	"#a8b4c4", "#dde4ee", "#6c7888", "#3c4450", "#101418", "#3c5c8c",
	"#ffffff", "#e8c860", "#c83c3c", "#4c9c50", "#1c3c2c", "#d8c49c"
};
static unsigned long col[C_N];

static const char *defaults =
	"Terminal : term  : xterm\n"
	"Files    : files : xfm\n"
	"Editor   : edit  : textedit\n"
	"Calc     : calc  : xcalc\n"
	"Manuals  : book  : xman\n"
	"Load     : load  : xload\n"
	"OpenUA   : game  : uagame\n";

static void trim(char *s)
{
	char *p = s, *e;

	while (*p == ' ' || *p == '\t')
		p++;
	memmove(s, p, strlen(p) + 1);
	e = s + strlen(s);
	while (e > s && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\n' || e[-1] == '\r'))
		*--e = 0;
}

static void add_line(char *line)
{
	char *a, *b;
	Button *bt;

	if (nbtn >= MAXBTN)
		return;
	trim(line);
	if (line[0] == 0 || line[0] == '#')
		return;
	a = strchr(line, ':');
	b = a ? strchr(a + 1, ':') : NULL;
	if (b == NULL)
		return;
	*a = *b = 0;
	bt = &btn[nbtn];
	strncpy(bt->label, line, sizeof bt->label - 1);
	strncpy(bt->icon, a + 1, sizeof bt->icon - 1);
	strncpy(bt->cmd, b + 1, sizeof bt->cmd - 1);
	trim(bt->label);
	trim(bt->icon);
	trim(bt->cmd);
	if (bt->cmd[0])
		nbtn++;
}

static void load_config(void)
{
	char path[512], line[512];
	const char *home = getenv("HOME");
	FILE *f;

	sprintf(path, "%s/.ttpanelrc", home ? home : "");
	f = fopen(path, "r");
	if (f != NULL) {
		while (fgets(line, sizeof line, f) != NULL)
			add_line(line);
		fclose(f);
	}
	if (nbtn == 0) {
		const char *p = defaults, *q;

		while (*p) {
			q = strchr(p, '\n');
			if (q == NULL)
				q = p + strlen(p);
			if (q - p < (int)sizeof line) {
				memcpy(line, p, (size_t)(q - p));
				line[q - p] = 0;
				add_line(line);
			}
			p = *q ? q + 1 : q;
		}
	}
}

/* run cmd through the shell, detached: the child's child is the program,
 * and the child exits at once (so nothing is left to reap) */
static void launch(const char *cmd)
{
	pid_t p = fork();

	if (p == 0) {
		setsid();
		if (fork() == 0) {
			close(ConnectionNumber(dpy));
			execl("/bin/sh", "sh", "-c", cmd, (char *)0);
			_exit(127);
		}
		_exit(0);
	}
	if (p > 0) {
		int st;
		waitpid(p, &st, 0);
	}
}

/* --- drawing -------------------------------------------------------------- */

static void fg(int c) { XSetForeground(dpy, gc, col[c]); }
static void rect(int c, int x, int y, int w, int h) { fg(c); XFillRectangle(dpy, win, gc, x, y, w, h); }
static void line(int c, int x0, int y0, int x1, int y1) { fg(c); XDrawLine(dpy, win, gc, x0, y0, x1, y1); }

/* a bevelled box: raised or sunk, 2 pixels */
static void bevel(int x, int y, int w, int h, int sunk)
{
	int hi = sunk ? C_SHADOW : C_LIGHT, lo = sunk ? C_LIGHT : C_SHADOW;

	line(hi, x, y, x + w - 1, y);
	line(hi, x, y + 1, x + w - 2, y + 1);
	line(hi, x, y, x, y + h - 1);
	line(hi, x + 1, y, x + 1, y + h - 2);
	line(lo, x + 1, y + h - 1, x + w - 1, y + h - 1);
	line(lo, x + 2, y + h - 2, x + w - 1, y + h - 2);
	line(lo, x + w - 1, y + 1, x + w - 1, y + h - 1);
	line(lo, x + w - 2, y + 2, x + w - 2, y + h - 1);
}

/* the icons, in a 32x32 box at (x, y) */
static void icon(const char *name, int x, int y)
{
	int i;

	if (!strcmp(name, "term")) {
		rect(C_DARK, x + 2, y + 3, 28, 21);
		rect(C_SCREEN, x + 4, y + 5, 24, 17);
		line(C_GREEN, x + 6, y + 8, x + 9, y + 11);
		line(C_GREEN, x + 9, y + 11, x + 6, y + 14);
		line(C_GREEN, x + 11, y + 15, x + 17, y + 15);
		rect(C_SHADOW, x + 12, y + 24, 8, 3);
		rect(C_DARK, x + 7, y + 27, 18, 3);
	} else if (!strcmp(name, "files")) {
		rect(C_DARK, x + 2, y + 7, 12, 4);
		rect(C_YELLOW, x + 3, y + 8, 10, 3);
		rect(C_DARK, x + 2, y + 10, 28, 18);
		rect(C_YELLOW, x + 3, y + 11, 26, 16);
		line(C_WHITE, x + 4, y + 12, x + 27, y + 12);
	} else if (!strcmp(name, "edit")) {
		rect(C_DARK, x + 6, y + 2, 20, 28);
		rect(C_WHITE, x + 7, y + 3, 18, 26);
		for (i = 0; i < 6; i++)
			line(C_BLUE, x + 9, y + 7 + 4 * i, x + (i == 5 ? 17 : 22), y + 7 + 4 * i);
		line(C_RED, x + 26, y + 10, x + 16, y + 26);
		line(C_RED, x + 27, y + 11, x + 17, y + 27);
	} else if (!strcmp(name, "calc")) {
		rect(C_DARK, x + 6, y + 2, 20, 28);
		rect(C_SHADOW, x + 7, y + 3, 18, 26);
		rect(C_SCREEN, x + 9, y + 5, 14, 6);
		line(C_GREEN, x + 16, y + 8, x + 21, y + 8);
		for (i = 0; i < 9; i++)
			rect(i == 8 ? C_RED : C_LIGHT, x + 9 + 5 * (i % 3), y + 13 + 5 * (i / 3), 4, 4);
	} else if (!strcmp(name, "book")) {
		rect(C_DARK, x + 4, y + 4, 24, 24);
		rect(C_BLUE, x + 5, y + 5, 22, 22);
		rect(C_WHITE, x + 24, y + 6, 3, 20);
		rect(C_YELLOW, x + 9, y + 10, 12, 3);
		rect(C_YELLOW, x + 9, y + 15, 8, 2);
	} else if (!strcmp(name, "load")) {
		rect(C_DARK, x + 2, y + 4, 28, 24);
		rect(C_SCREEN, x + 3, y + 5, 26, 22);
		{
			static const int h[] = { 4, 7, 5, 11, 15, 9, 13, 18, 12, 8, 14, 17, 10 };
			for (i = 0; i < 13; i++)
				line(C_GREEN, x + 4 + 2 * i, y + 26, x + 4 + 2 * i, y + 26 - h[i]);
		}
	} else if (!strcmp(name, "game")) {
		/* a shield, as on FRUA's cursor */
		rect(C_DARK, x + 6, y + 3, 20, 16);
		rect(C_BLUE, x + 7, y + 4, 18, 15);
		for (i = 0; i < 10; i++) {
			line(C_DARK, x + 6 + i, y + 19 + i, x + 25 - i, y + 19 + i);
			if (i < 9)
				line(C_BLUE, x + 7 + i, y + 19 + i, x + 24 - i, y + 19 + i);
		}
		rect(C_YELLOW, x + 15, y + 6, 2, 16);
		rect(C_YELLOW, x + 10, y + 11, 12, 2);
	} else if (!strcmp(name, "mail")) {
		rect(C_DARK, x + 3, y + 8, 26, 17);
		rect(C_WHITE, x + 4, y + 9, 24, 15);
		line(C_DARK, x + 4, y + 9, x + 16, y + 18);
		line(C_DARK, x + 28, y + 9, x + 16, y + 18);
	} else if (!strcmp(name, "paint")) {
		rect(C_DARK, x + 3, y + 6, 26, 20);
		rect(C_TAN, x + 4, y + 7, 24, 18);
		rect(C_RED, x + 7, y + 10, 5, 4);
		rect(C_BLUE, x + 14, y + 10, 5, 4);
		rect(C_GREEN, x + 21, y + 10, 5, 4);
		rect(C_YELLOW, x + 7, y + 17, 5, 4);
	} else if (!strcmp(name, "clock")) {
		fg(C_DARK); XFillArc(dpy, win, gc, x + 3, y + 3, 26, 26, 0, 360 * 64);
		fg(C_WHITE); XFillArc(dpy, win, gc, x + 5, y + 5, 22, 22, 0, 360 * 64);
		line(C_DARK, x + 16, y + 16, x + 16, y + 8);
		line(C_DARK, x + 16, y + 16, x + 22, y + 16);
	} else if (!strcmp(name, "exit")) {
		/* a door ajar, and an arrow out of it */
		rect(C_DARK, x + 5, y + 3, 16, 26);
		rect(C_TAN, x + 6, y + 4, 14, 24);
		rect(C_DARK, x + 8, y + 5, 10, 24);
		rect(C_SHADOW, x + 9, y + 6, 8, 22);
		rect(C_YELLOW, x + 15, y + 16, 2, 2);
		rect(C_RED, x + 19, y + 14, 8, 4);
		for (i = 0; i < 5; i++)
			line(C_RED, x + 26 + i, y + 11 + i, x + 26 + i, y + 20 - i);
	} else {			/* tool, or anything unknown: a gear-ish box */
		rect(C_DARK, x + 8, y + 8, 16, 16);
		rect(C_SHADOW, x + 9, y + 9, 14, 14);
		for (i = 0; i < 4; i++) {
			rect(C_DARK, x + 14, y + 3 + 23 * (i & 1), 4, 5);
			rect(C_DARK, x + 3 + 23 * (i & 1), y + 14, 5, 4);
		}
		rect(C_FACE, x + 13, y + 13, 6, 6);
	}
}

static void draw_button(int i)
{
	Button *b = &btn[i];
	int y = PAD, w, sunk = (i == pressed);

	rect(C_FACE, b->x, y, BTN_W, BTN_H);
	bevel(b->x, y, BTN_W, BTN_H, sunk);
	icon(b->icon, b->x + (BTN_W - 32) / 2 + sunk, y + 5 + sunk);
	w = XTextWidth(font, b->label, (int)strlen(b->label));
	fg(C_TEXT);
	XDrawString(dpy, win, gc, b->x + (BTN_W - w) / 2 + sunk,
		    y + BTN_H - 6 + sunk, b->label, (int)strlen(b->label));
}

static void draw_clock(void)
{
	char t[16], d[32];
	time_t now = time(NULL);
	struct tm *tm = localtime(&now);
	static const char *wd[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
	static const char *mo[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
				    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	int w = 110, tw, dw;

	sprintf(t, "%2d:%02d", tm->tm_hour, tm->tm_min);
	sprintf(d, "%s %d %s", wd[tm->tm_wday], tm->tm_mday, mo[tm->tm_mon]);
	rect(C_SCREEN, clock_x, PAD, w, BTN_H);
	bevel(clock_x, PAD, w, BTN_H, 1);
	tw = XTextWidth(bigfont, t, (int)strlen(t));
	dw = XTextWidth(font, d, (int)strlen(d));
	XSetFont(dpy, gc, bigfont->fid);
	fg(C_GREEN);
	XDrawString(dpy, win, gc, clock_x + (w - tw) / 2, PAD + 8 + bigfont->ascent,
		    t, (int)strlen(t));
	XSetFont(dpy, gc, font->fid);
	fg(C_LIGHT);
	XDrawString(dpy, win, gc, clock_x + (w - dw) / 2, PAD + BTN_H - 8, d, (int)strlen(d));
}

/* the workspace switcher: ws_cols x ws_rows cells, the one on show sunk */
#define WS_W	24

static void ws_cell(int n, int *x, int *y, int *w, int *h)
{
	int c = (n - 1) % ws_cols, r = (n - 1) / ws_cols;

	*h = (BTN_H - (ws_rows - 1) * 2) / ws_rows;
	*w = WS_W;
	*x = ws_x + c * (WS_W + 2);
	*y = PAD + r * (*h + 2);
}

static void draw_switcher(void)
{
	int n, x, y, w, h, tw;
	char s[4];

	if (ws_cols == 0)
		return;
	for (n = 1; n <= ws_cols * ws_rows; n++) {
		int on = (n == ws_cur);

		ws_cell(n, &x, &y, &w, &h);
		rect(on ? C_BLUE : C_FACE, x, y, w, h);
		bevel(x, y, w, h, on);
		sprintf(s, "%d", n);
		tw = XTextWidth(font, s, (int)strlen(s));
		fg(on ? C_WHITE : C_TEXT);
		XDrawString(dpy, win, gc, x + (w - tw) / 2 + on,
			    y + (h + font->ascent) / 2 + on, s, (int)strlen(s));
	}
}

static void draw_exit(int sunk)
{
	const char *l = "Log out";
	int w = XTextWidth(font, l, (int)strlen(l));

	rect(C_FACE, exit_x, PAD, BTN_W, BTN_H);
	bevel(exit_x, PAD, BTN_W, BTN_H, sunk);
	icon("exit", exit_x + (BTN_W - 32) / 2 + sunk, PAD + 5 + sunk);
	fg(C_TEXT);
	XDrawString(dpy, win, gc, exit_x + (BTN_W - w) / 2 + sunk,
		    PAD + BTN_H - 6 + sunk, l, (int)strlen(l));
}

static void draw_all(void)
{
	int i;

	rect(C_FACE, 0, 0, panel_w, panel_h);
	bevel(0, 0, panel_w, panel_h, 0);
	for (i = 0; i < nbtn; i++)
		draw_button(i);
	draw_switcher();
	draw_clock();
	draw_exit(0);
}

/* olvwm's _OLVWM_DESKTOP: columns, rows, the screen on show */
static void read_desktop(void)
{
	Atom type;
	int fmt;
	unsigned long n, after;
	unsigned char *p = NULL;

	ws_cols = ws_rows = ws_cur = 0;
	if (XGetWindowProperty(dpy, RootWindow(dpy, scr), a_desktop, 0, 3, False,
			       XA_INTEGER, &type, &fmt, &n, &after, &p) == Success
	    && p != NULL && fmt == 32 && n == 3) {
		long *v = (long *)p;

		/* olvwm goes to screens 1-10 */
		if (v[0] > 0 && v[1] > 0 && v[0] * v[1] <= 10) {
			ws_cols = (int)v[0];
			ws_rows = (int)v[1];
			ws_cur = (int)v[2];
		}
	}
	if (p != NULL)
		XFree((char *)p);
}

/* positions, and the panel's width, from what it holds */
static void layout(int sw)
{
	int i, x = PAD;

	for (i = 0; i < nbtn; i++) {
		btn[i].x = x;
		x += BTN_W + 2;
	}
	x += 6;
	ws_x = x;
	if (ws_cols)
		x += ws_cols * (WS_W + 2) + 6;
	clock_x = x;
	x += 110 + 4;
	exit_x = x;
	panel_w = x + BTN_W + PAD;
	if (panel_w > sw)
		panel_w = sw;
}

static void send_root(Atom what, long arg)
{
	XEvent e;

	memset(&e, 0, sizeof e);
	e.xclient.type = ClientMessage;
	e.xclient.window = RootWindow(dpy, scr);
	e.xclient.message_type = what;
	e.xclient.format = 32;
	e.xclient.data.l[0] = arg;
	XSendEvent(dpy, RootWindow(dpy, scr), False,
		   SubstructureRedirectMask | SubstructureNotifyMask, &e);
	XFlush(dpy);
}

/* what is at (x, y): a launcher's index, 100 + n for switcher screen n,
 * 200 for Log out, -1 for nothing */
static int hit(int x, int y)
{
	int i, cx, cy, cw, ch;

	if (y < PAD || y >= PAD + BTN_H)
		return -1;
	for (i = 0; i < nbtn; i++)
		if (x >= btn[i].x && x < btn[i].x + BTN_W)
			return i;
	for (i = 1; ws_cols && i <= ws_cols * ws_rows; i++) {
		ws_cell(i, &cx, &cy, &cw, &ch);
		if (x >= cx && x < cx + cw && y >= cy && y < cy + ch)
			return 100 + i;
	}
	if (x >= exit_x && x < exit_x + BTN_W)
		return 200;
	return -1;
}

int main(int argc, char **argv)
{
	XSetWindowAttributes wa;
	XColor xc;
	Colormap cm;
	int i, top = 0, sw, sh;
	const char *fn = "-*-helvetica-bold-r-normal--10-*";
	XEvent ev;
	int last_min = -1;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-top"))
			top = 1;
		else if (!strcmp(argv[i], "-fn") && i + 1 < argc)
			fn = argv[++i];
	}
	if ((dpy = XOpenDisplay(NULL)) == NULL) {
		fprintf(stderr, "ttpanel: cannot open the display\n");
		return 1;
	}
	signal(SIGCHLD, SIG_IGN);
	scr = DefaultScreen(dpy);
	sw = DisplayWidth(dpy, scr);
	sh = DisplayHeight(dpy, scr);
	cm = DefaultColormap(dpy, scr);
	for (i = 0; i < C_N; i++) {
		col[i] = (i == C_WHITE || i == C_LIGHT) ? WhitePixel(dpy, scr) : BlackPixel(dpy, scr);
		if (XParseColor(dpy, cm, cname[i], &xc) && XAllocColor(dpy, cm, &xc))
			col[i] = xc.pixel;
	}
	if ((font = XLoadQueryFont(dpy, fn)) == NULL &&
	    (font = XLoadQueryFont(dpy, "6x10")) == NULL &&
	    (font = XLoadQueryFont(dpy, "fixed")) == NULL) {
		fprintf(stderr, "ttpanel: no font\n");
		return 1;
	}
	if ((bigfont = XLoadQueryFont(dpy, "-*-helvetica-bold-r-normal--24-*")) == NULL &&
	    (bigfont = XLoadQueryFont(dpy, "12x24")) == NULL)
		bigfont = font;

	load_config();
	a_desktop = XInternAtom(dpy, "_OLVWM_DESKTOP", False);
	a_goto = XInternAtom(dpy, "_OLVWM_GOTO", False);
	a_exit = XInternAtom(dpy, "_OLVWM_EXIT", False);
	/* the window manager may start after us: watch for its property */
	XSelectInput(dpy, RootWindow(dpy, scr), PropertyChangeMask);
	read_desktop();
	panel_h = BTN_H + 2 * PAD;
	layout(sw);

	/* no frame: the window manager leaves an override-redirect window
	 * alone, and the panel keeps itself on top when covered */
	wa.override_redirect = True;
	wa.background_pixel = col[C_FACE];
	wa.event_mask = ExposureMask | ButtonPressMask | ButtonReleaseMask |
			VisibilityChangeMask;
	win = XCreateWindow(dpy, RootWindow(dpy, scr), (sw - panel_w) / 2,
			    top ? 0 : sh - panel_h, panel_w, panel_h, 0,
			    CopyFromParent, InputOutput, CopyFromParent,
			    CWOverrideRedirect | CWBackPixel | CWEventMask, &wa);
	XStoreName(dpy, win, "ttpanel");
	gc = XCreateGC(dpy, win, 0, NULL);
	XSetFont(dpy, gc, font->fid);
	XMapRaised(dpy, win);

	for (;;) {
		/* wake at least once a minute for the clock */
		while (XPending(dpy) == 0) {
			fd_set fds;
			struct timeval tv;
			time_t now = time(NULL);
			struct tm *tm = localtime(&now);

			if (tm->tm_min != last_min) {
				last_min = tm->tm_min;
				draw_clock();
				XFlush(dpy);
			}
			FD_ZERO(&fds);
			FD_SET(ConnectionNumber(dpy), &fds);
			tv.tv_sec = 60 - tm->tm_sec;
			tv.tv_usec = 0;
			select(ConnectionNumber(dpy) + 1, &fds, NULL, NULL, &tv);
		}
		XNextEvent(dpy, &ev);
		switch (ev.type) {
		case Expose:
			if (ev.xexpose.count == 0)
				draw_all();
			break;
		case VisibilityNotify:
			if (ev.xvisibility.state != VisibilityUnobscured)
				XRaiseWindow(dpy, win);
			break;
		case PropertyNotify:
			if (ev.xproperty.atom == a_desktop) {
				int oc = ws_cols, orr = ws_rows;

				read_desktop();
				if (ws_cols != oc || ws_rows != orr) {
					layout(sw);
					XMoveResizeWindow(dpy, win, (sw - panel_w) / 2,
							  top ? 0 : sh - panel_h,
							  panel_w, panel_h);
					draw_all();
				} else
					draw_switcher();
			}
			break;
		case ButtonPress:
			pressed = hit(ev.xbutton.x, ev.xbutton.y);
			if (pressed >= 0 && pressed < MAXBTN)
				draw_button(pressed);
			else if (pressed == 200)
				draw_exit(1);
			break;
		case ButtonRelease:
			if (pressed >= 0) {
				int was = pressed;

				pressed = -1;
				if (was < MAXBTN)
					draw_button(was);
				else if (was == 200)
					draw_exit(0);
				XFlush(dpy);
				if (hit(ev.xbutton.x, ev.xbutton.y) != was)
					break;
				if (was < MAXBTN)
					launch(btn[was].cmd);
				else if (was > 100 && was <= 110)
					send_root(a_goto, (long)(was - 100));
				else if (was == 200)
					send_root(a_exit, 0L);
			}
			break;
		}
	}
}
