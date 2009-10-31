/* KbOSD - Keyboard on OSD - GPLv3.
 * Home: http://gitorious.org/kbosd
 * (c)2009 Cedric Cellier, Radics Áron
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <unistd.h>
#include <sys/select.h>
#include <X11/Xlib.h>
#include <X11/extensions/shape.h>
#include <X11/extensions/XTest.h>

extern void stay_on_top(Display *, Window);

static Display *dis;
static int screen;
static Window win;
static GC gc, mask_gc;
static Pixmap mask;
static XGCValues xgcv;
static unsigned win_width = 480, win_height = 600;
static XFontStruct *font;
static int font_width, font_height, font_offset_x, font_offset_y;
static unsigned long kb_color[2];
static unsigned timeout;	// delay of inactivity before hiding the KB
static time_t hide_time;	// after this timestamp, we will hide the KB
static bool visible;	// weither the KB is currently visible
static bool shifted;
static int map;
static int color;

static unsigned short int vibrator_strength;
static unsigned int vibrator_time;
static bool with_vibrator;
static FILE *vib_fd;

#define nb_cols 6
#define nb_top_rows 6
#define nb_bot_rows 2
#define nb_rows (nb_top_rows+nb_bot_rows)
static int col_width, row_height;
static int border_left, border_right, border_top, border_bottom;
static unsigned osd_width, osd_height;

#define SHIFT_KEYCODE 62
#define KN(n,c) { { n, n }, false, (c), NOT_HELD }
#define KS(n1,n2,c) { { n1, n2 }, false, (c), NOT_HELD }
#define KH(n,c) { { n, n }, true, (c), NOT_HELD }

static struct key {
	char name[2][4];
	bool hold;	// must keep pressed nutil next key that's not hold
	unsigned code;	// use xev to find keycodes ! Special value 0 and 1 for internal functions
	enum held_state { NOT_HELD, HELD_ONCE, KEEP_HELD } held_state;
} top_kbmap[2][nb_top_rows][nb_cols] = {
	{
		{KS("q","Q",24),KS("w","W",25),KS("e","E",26),KS("r","R",27),KS("t","T",28),KS("y","Y",29)},
		{KS("u","U",30),KS("i","I",31),KS("o","O",32),KS("p","P",33),KS("[","{",34),KS("]","}",35)},
		{KS("a","A",38),KS("s","S",39),KS("d","D",40),KS("f","F",41),KS("g","G",42),KS("h","H",43)},
		{KS("j","J",44),KS("k","K",45),KS("l","L",46),KS("m","M",58),KS(";",":",47),KS("'","\"",48)},
		{KS("z","Z",52),KS("x","X",53),KS("c","C",54),KS("v","V",55),KS("b","B",56),KS("n","N",57)},
		{KS(",","<",59),KS(".",">",60),KS("=","+",21),KS("\\","|",51),KS("`","~",23),KS("/","?",61)}
	}, {
		{KN("F1",67),KN("F2",68),KN("F3",69),KN("F4",70),KN("F5",71),KN("F6",72)},
		{KN("F7",73),KN("F8",74),KN("F9",75),KN("F10",76),KN("F11",95),KN("F12",96)},
		{KN("Tab",23),KS("=","+",21),KS("\\","|",51),KS("`","~",23),KS("/","?",61),KN("Esc",9)},
		{KS("1","!",10),KS("2","@",11),KS("3","#",12),KS("4","$",13),KS("5","%",14),KS("6","^",15)},
		{KS("7","&",16),KS("8","*",17),KS("9","(",18),KS("0",")",19),KN("Up",98),KS("-","_",20)},
		{KN("Sys",111),KN("PUp",99),KN("PDo",105),KN("Lft",100),KN("Dwn",104),KN("Rgt",102)}
	}
}, bot_kbmap[nb_bot_rows][nb_cols] = {
	{KH("Shf",62),KN("",65),KN("",65),KN("Ins",106),KN("Del",107),KN("Bak",22)},
	{KN("Col",0),KN("Tgl",1),KH("Alt",64),KH("AlG",113),KH("Ctl",37),KN("Ret",36)}
};

static void read_layout(char const *path)
{
	FILE *f = fopen(path, "r");
	if (! f) {
		perror(path);
		return;
	}

	for (unsigned line=0; line < (nb_top_rows*2+nb_bot_rows); line++) {
		for (unsigned col=0; col < nb_cols; col++) {
			struct key *key = &(line < nb_top_rows*2 ? top_kbmap[line/nb_top_rows] : bot_kbmap)[line % nb_top_rows][col];
			unsigned u2bool;
			if (4 != fscanf(f, " %3s %3s %u %u",
				key->name[0], key->name[1], &key->code, &u2bool)) {
				fprintf(stderr, "Cannot parse file '%s' at line %u\n", path, line);
				return;
			}
			key->hold = !!u2bool;
			for (unsigned n=0; n<2; n++) {	// As we cannot scanf spaces
				if (0 == strcmp(key->name[n], "___")) memset(key->name[n], ' ', 3);
			}
		}
	}
}

static struct key *key_at(unsigned col, unsigned row)
{
	assert(col < nb_cols && row < nb_rows);
	if (row < nb_top_rows) {
		return &top_kbmap[map][row][col];
	}
	return &bot_kbmap[row-nb_top_rows][col];
}

static void vibrate(void) 
{
	fprintf(vib_fd, "%d\n", vibrator_strength);
	fflush(vib_fd);
	usleep(vibrator_time);
	fprintf(vib_fd, "0\n");
	fflush(vib_fd);
};

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
	//printf("Show mask %s\n", shifted ? "Shifted":"Unshifted");

	// Update mask
	XSetForeground(dis, mask_gc, BlackPixel(dis, screen));
	XFillRectangle(dis, mask, mask_gc, 0, 0, osd_width, osd_height);
	XSetForeground(dis, mask_gc, WhitePixel(dis, screen));
	for (unsigned col = 0; col < nb_cols; col++) {
		for (unsigned row = 0; row < nb_rows; row++) {
			int const x_col = col * col_width;
			int const x = x_col + (col_width - font_width)/2 - font_offset_x;
			int const y_row = row * row_height;
			int const y = y_row + (row_height - font_height)/2 - font_offset_y;
			struct key const *key = key_at(col, row);
			switch (key->held_state) {
				case NOT_HELD: break;
				case KEEP_HELD:
					XDrawRectangle(dis, mask, mask_gc, x_col+4, y_row+4, col_width-8, row_height-8);
					// pass
				case HELD_ONCE:
					XDrawRectangle(dis, mask, mask_gc, x_col+1, y_row+1, col_width-2, row_height-2);
					break;
			}
			// FIXME: compute the actual width of the string
			int const len = strlen(key->name[shifted]);
			XDrawString(dis, mask, mask_gc, x-(font_width*len)/2, y, key->name[shifted], len);
		}
	}
	
	XShapeCombineMask(dis, win, ShapeBounding, 0, 0, mask, ShapeSet);

	// Update window
	XSetForeground(dis, gc, kb_color[color]);
	XFillRectangle(dis, win, gc, 0, 0, osd_width, osd_height);

	visible = true;
}

static void hide_mask(void)
{
	// Update mask
	XSetForeground(dis, mask_gc, BlackPixel(dis, screen));
	XFillRectangle(dis, mask, mask_gc, 0, 0, osd_width, osd_height);
	XShapeCombineMask(dis, win, ShapeBounding, 0, 0, mask, ShapeSet);
	
	visible = false;
}

// Release all previously held keys, sending fake release event.
// Return true if some keys were actually held.
static bool release_all_held(bool just_once)
{
	bool ret = false;

	for (unsigned col = 0; col < nb_cols; col++) {
		for (unsigned row = 0; row < nb_rows; row++) {
			struct key *key = key_at(col, row);
			if (
				(just_once && key->held_state == HELD_ONCE) ||
				(!just_once && key->held_state != NOT_HELD))
			{
				key->held_state = NOT_HELD;
				XTestFakeKeyEvent(dis, key->code, False, CurrentTime);
				if (key->code == SHIFT_KEYCODE) shifted = false;
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
		(void)release_all_held(false);
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
		border_left, border_top, osd_width, osd_height,
		0,
		DefaultDepth(dis, screen),
		CopyFromParent,
		DefaultVisual(dis, screen),
		CWOverrideRedirect,
		&setwinattr);
	XSelectInput(dis, win, ExposureMask|ButtonPressMask|ButtonReleaseMask);
	XStoreName(dis, win, "KOSD");
	gc = XCreateGC(dis, win, 0,0);
	stay_on_top(dis, win);

	mask = XCreatePixmap(dis, win, osd_width, osd_height, 1);
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
	(void)release_all_held(false);
	if (font) XFreeFont(dis, font);
	XUngrabPointer(dis, CurrentTime);
	XFreeGC(dis, gc);
	XDestroyWindow(dis, win);
	XCloseDisplay(dis);	
}

static void hit(int x, int y, int press)
{
	unsigned const col = (x * nb_cols) / osd_width;
	unsigned const row = (y * nb_rows) / osd_height;
	if (row >= nb_rows || col >= nb_cols) return;

	struct key *key = key_at(col, row);
	//printf("%s key %c\n", press ? "Press":"Release", key->unshifted);

	bool need_show = false;
	hide_time = time(NULL) + timeout;
	
	if (! visible) {
		need_show = true;
	} else if (press) {
		if (key->code == 0) {
			// Change color
			color ^= 1;
			need_show = true;
		} else if (key->code == 1) {
			// Togle map
			map ^= 1;
			need_show = true;
		} else {
			XTestFakeKeyEvent(dis, key->code, True, CurrentTime);
			if (with_vibrator) vibrate();
			if (key->code == SHIFT_KEYCODE) {
				shifted = true;
				need_show = true;
			}
		}
	} else {	// release
		if (key->code <= 1) return;
		if (key->hold) {	// Must not release at once
			switch (key->held_state) {
				case NOT_HELD:
					key->held_state = HELD_ONCE;
					break;
				case HELD_ONCE:
					key->held_state = KEEP_HELD;
					break;
				case KEEP_HELD:
					key->held_state = NOT_HELD;
					XTestFakeKeyEvent(dis, key->code, False, CurrentTime);
					if (key->code == SHIFT_KEYCODE) shifted = false;
					break;
			}
			need_show = true;
		} else {	// normal key
			XTestFakeKeyEvent(dis, key->code, False, CurrentTime);
			// Release all previously hold keys (but the onces that are "locked")
			need_show = release_all_held(true);
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
	kb_color[0]   = get_config_int("KBOSD_COLOR", 0xFFFFFF);
	kb_color[1]   = get_config_int("KBOSD_COLOR_ALT", 0xFFE0);
	osd_width     = win_width - (border_left+border_right);
	osd_height    = win_height - (border_top+border_bottom);
	col_width     = osd_width / nb_cols;
	row_height    = osd_height / nb_rows;

	char const *vibrator_file;
	vibrator_strength = get_config_int("KBOSD_VIBRATOR_STRENGTH", 150); // 0 - 255
	vibrator_time     = get_config_int("KBOSD_VIBRATOR_TIME", 50000);     // in micro-seconds
	vibrator_file     = get_config_str("KBOSD_VIBRATOR_FILE", "/sys/class/leds/neo1973:vibrator/brightness");  // on openmoko
	with_vibrator     = vibrator_strength && vibrator_time && vibrator_file;
	if (with_vibrator && !(vib_fd = fopen(vibrator_file, "w"))) {
		perror(vibrator_file);
		with_vibrator = false;
	}

	char const *layout_path = get_config_str("KBOSD_LAYOUT", NULL);
	if (layout_path) read_layout(layout_path);

	/* Start timeout
	 * If you look for you keys longer than 4s, then you need practice !
	 */
	timeout   = get_config_int("KBOSD_TIMEOUT", 4);
	hide_time = time(NULL) + timeout;
	visible = true;

	open_X();
	event_loop();
	close_X();

	if (vib_fd) fclose(vib_fd);
	return 0;
}
