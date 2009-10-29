/* KbOSD - Keyboard on OSD - GPLv3.
 * Home: http://gitorious.org/kbosd
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <sys/select.h>
#include <X11/Xlib.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/XTest.h>

static Display *dis;
static int screen;
static Window win;
static GC gc, mask_gc;
static Pixmap mask;
static XGCValues xgcv;
static unsigned win_width = 480, win_height = 600;
static XFontStruct *font;
static int font_width, font_height, font_offset_x, font_offset_y;
static unsigned long kb_color;
static unsigned timeout;	// delay of inactivity before hiding the KB
static time_t hide_time;	// after this timestamp, we will hide the KB
static int visible;	// weither the KB is currently visible

#define SHIFT_COL 4
#define SHIFT_ROW 8
#define nb_cols 6
#define nb_rows 9
static int col_width, row_height;
static int border_left, border_right, border_top, border_bottom;

static struct key {
	char const *name;	// takes precedence over (un)shifter char
	char unshifted, shifted;
	unsigned code;	// use xev to find keycodes !
	int hold:1;	// must keep pressed nutil next key that's not hold
	int held:1;	// currently held down
} kbmap[nb_rows][nb_cols] = {
	{ {"Esc",'E','E', 9,0,0}, {0,'=','+',21,0,0},{0,'\\','|',22,0,0}, {0,'`','~',23,0,0}, {0,'/','?',61,0,0}, {"Bak",'B','B',22,0,0} },
	{ {"Tab",'T','T',23,0,0}, {0,'1','!',10,0,0}, {0,'2','@',11,0,0}, {0,'3','#',12,0,0}, {0,'4','$',13,0,0}, {0,'5','%',14,0,0} },
	{ {0,'6','^',15,0,0}, {0,'7','&',16,0,0}, {0,'8','*',17,0,0}, {0,'9','(',18,0,0}, {0,'0',')',19,0,0}, {0,'-','_',20,0,0} },
	{ {0,'q','Q',24,0,0}, {0,'w','W',25,0,0}, {0,'e','E',26,0,0}, {0,'r','R',27,0,0}, {0,'t','T',28,0,0}, {0,'y','Y',29,0,0} },
	{ {0,'a','A',38,0,0}, {0,'s','S',39,0,0}, {0,'d','D',40,0,0}, {0,'f','F',41,0,0}, {0,'g','G',42,0,0}, {0,'h','H',43,0,0} },
	{ {0,'z','Z',52,0,0}, {0,'x','X',53,0,0}, {0,'c','C',54,0,0}, {0,'v','V',55,0,0}, {0,'b','B',56,0,0}, {0,'n','N',57,0,0} },
	{ {0,'u','U',30,0,0}, {0,'i','I',31,0,0}, {0,'o','O',32,0,0}, {0,'p','P',33,0,0}, {0,'[','{',34,0,0}, {0,']','}',35,0,0} },
	{ {0,'j','J',44,0,0}, {0,'k','K',45,0,0}, {0,'l','L',46,0,0}, {0,';',':',47,0,0},{0,'\'','"',48,0,0}, {"Ctl",'C','C',37,1,0} },
	{ {0,'m','M',58,0,0}, {0,',','<',59,0,0}, {0,'.','>',60,0,0}, {0,' ',' ',65,0,0}, {"Shf",'S','S',62,1,0}, {"Ret",'R','R',36,0,0} },
};

static struct key *key_at(unsigned col, unsigned row)
{
	return &kbmap[row][col];
}

static char const *get_config_str(char const *varname, char const *defaultval)
{
	char const *val = getenv(varname);
	return val ? val : defaultval;
}

static unsigned long get_config_int(char const *varname, unsigned long defaultval)
{
	char const *val = getenv(varname);
	if (! val) return defaultval;
	char *end;
	unsigned long i = strtoul(val, &end, 0);
	if (*end) {
		fprintf(stderr, "Garbage at the end of '%s', using default instead (%lu)\n",
			val, defaultval);
		return defaultval;
	}
	return i;
}

static void show_mask(void)
{
	bool const shifted = key_at(SHIFT_COL, SHIFT_ROW)->held;
	printf("Show mask %s\n", shifted ? "Shifted":"Unshifted");

	// Update mask
	XSetForeground(dis, mask_gc, BlackPixel(dis, screen));
	XFillRectangle(dis, mask, mask_gc, 0, 0, win_width, win_height);
	XSetForeground(dis, mask_gc, WhitePixel(dis, screen));
	for (unsigned col = 0; col < nb_cols; col++) {
		for (unsigned row = 0; row < nb_rows; row++) {
			int const x_col = border_left + col * col_width;
			int const x = x_col + (col_width - font_width)/2 - font_offset_x;
			int const y_row = border_top + row * row_height;
			int const y = y_row + (row_height - font_height)/2 - font_offset_y;
			struct key const *key = key_at(col, row);
			if (key->held) {
				XDrawRectangle(dis, mask, mask_gc, x_col+1, y_row+1, col_width-2, row_height-2);
			}
			if (key->name) {
				// FIXME: compute the actual width of the string
				XDrawString(dis, mask, mask_gc, x-font_width, y, key->name, 3);
			} else {
				XDrawString(dis, mask, mask_gc, x, y, shifted ? &key->shifted : &key->unshifted, 1);
			}
		}
	}
	
	XShapeCombineMask(dis, win, ShapeBounding, 0, 0, mask, ShapeSet);

	// Update window
	XSetForeground(dis, gc, kb_color);
	XFillRectangle(dis, win, gc, 0, 0, win_width, win_height);

	visible = 1;
}

static void hide_mask(void)
{
	// Update mask
	XSetForeground(dis, mask_gc, BlackPixel(dis, screen));
	XFillRectangle(dis, mask, mask_gc, 0, 0, win_width, win_height);
	XShapeCombineMask(dis, win, ShapeBounding, 0, 0, mask, ShapeSet);
	
	visible = 0;
}

// Release all previously held keys, sending fake release event.
// Return true if some keys were actually held.
static bool release_all_held(void)
{
	bool ret = false;

	for (unsigned col = 0; col < nb_cols; col++) {
		for (unsigned row = 0; row < nb_rows; row++) {
			struct key *key = key_at(col, row);
			if (key->held) {
				key->held = 0;
				XTestFakeKeyEvent(dis, key->code, False, CurrentTime);
				ret = true;
			}
		}
	}

	return ret;
}

static void redraw(void)
{
	static int inited = 0;
	if (! inited) {
		inited = 1;
		if (GrabSuccess != XGrabPointer(dis, win, True, ButtonPressMask|ButtonReleaseMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime)) {
			fprintf(stderr, "Cannot grab pointer !\n");
		}
		// Just in case the previous invocation quit with some left pending Press events.
		(void)release_all_held();
		show_mask();
	}
}

static void open_X(void)
{
	dis = XOpenDisplay(NULL);
	screen = DefaultScreen(dis);

	XSetWindowAttributes setwinattr = { .override_redirect = 1 };
	win = XCreateWindow(
		dis, XRootWindow(dis, screen),
		0, 0, win_width, win_height,
		0,
		DefaultDepth(dis, screen),
		CopyFromParent,
		DefaultVisual(dis, screen),
		CWOverrideRedirect,
		&setwinattr);
	XSelectInput(dis, win, ExposureMask|ButtonPressMask|ButtonReleaseMask);
	XStoreName(dis, win, "KOSD");

	gc = XCreateGC(dis, win, 0,0);

	mask = XCreatePixmap(dis, win, win_width, win_height, 1);
	mask_gc = XCreateGC(dis, mask, 0, &xgcv);

	char const *fontname = get_config_str("KBOSD_FONT", "-sony-*-*-*-*-*-24-*-*-*-*-*-*-*");
	font = XLoadQueryFont(dis, fontname);
	if (font) {
		XSetFont(dis, mask_gc, font->fid);
		font_width = font->max_bounds.rbearing - font->min_bounds.lbearing;
		font_height = font->max_bounds.ascent + font->max_bounds.descent;
		font_offset_x = font->min_bounds.lbearing;
		font_offset_y = -font-> max_bounds.ascent;
	} else {
		fprintf(stderr, "Cannot load font '%s'\n", fontname);
	} 

	XClearWindow(dis, win);
	XMapRaised(dis, win);
	XFlush(dis);
}

static void close_X(void)
{
	(void)release_all_held();
	if (font) XFreeFont(dis, font);
	XUngrabPointer(dis, CurrentTime);
	XFreeGC(dis, gc);
	XDestroyWindow(dis, win);
	XCloseDisplay(dis);	
}

static void hit(int x, int y, int press)
{
	unsigned const col = ((x-border_left) * nb_cols) / (win_width  - (border_left+border_right));
	unsigned const row = ((y-border_top) * nb_rows) / (win_height - (border_top+border_bottom));
	if (row >= nb_rows || col >= nb_cols) return;

	struct key *key = key_at(col, row);
	printf("%s key %c\n", press ? "Press":"Release", key->unshifted);

	bool need_show = false;
	hide_time = time(NULL) + timeout;
	
	if (! visible) {
		need_show = true;
	} else if (press) {
		XTestFakeKeyEvent(dis, key->code, True, CurrentTime);
	} else {	// release
		if (key->hold) {	// Must not release at once
			if (! key->held) {
				key->held = 1;
			} else {	// The key was already hold : release it
				key->held = 0;
				XTestFakeKeyEvent(dis, key->code, False, CurrentTime);
			}
			need_show = true;
		} else {	// normal key
			XTestFakeKeyEvent(dis, key->code, False, CurrentTime);
			// Release all previously hold keys
			need_show = release_all_held();
		}
	}

	if (need_show) show_mask();
}

static void event_loop(void)
{
	int fd = ConnectionNumber(dis);
	fd_set fds;

	while (1) {
		// Handle all available events
		while (XPending(dis)) {
			XEvent event;
			XNextEvent(dis, &event);

			if (event.type == Expose && event.xexpose.count == 0) {
				redraw();
			} else if (event.type == ButtonPress) {
				hit(event.xbutton.x, event.xbutton.y, 1);
			} else if (event.type == ButtonRelease) {
				hit(event.xbutton.x, event.xbutton.y, 0);
			}
		}
		// Wait for X11 to move or 1s max.
		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		select(fd+1, &fds, NULL, NULL, &(struct timeval){ .tv_usec=0, .tv_sec=1 });
		if (time(NULL) > hide_time && visible) {
			hide_mask();
		}
	}
}

int main(void)
{
	/* These defaults are OK for Hackable:1 */
	border_left   = get_config_int("KBOSD_BORDER_LEFT", 0);
	border_right  = get_config_int("KBOSD_BORDER_RIGHT", 0);
	border_top    = get_config_int("KBOSD_BORDER_TOP", 20);
	border_bottom = get_config_int("KBOSD_BORDER_BOTTOM", 0);
	kb_color      = get_config_int("KBOSD_COLOR", 0xFFFFFF);

	col_width  = (win_width  - (border_left+border_right)) / nb_cols;
	row_height = (win_height - (border_top+border_bottom)) / nb_rows;

	/* Start timeout
	 * If you look for you keys longer than 4s, then you need practice !
	 */
	timeout   = get_config_int("KBOSD_TIMEOUT", 4);
	hide_time = time(NULL) + timeout;
	visible = 1;

	open_X();
	event_loop();
	close_X();
	return 0;
}
