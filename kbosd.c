#include <stdlib.h>
#include <stdio.h>
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

#define nb_cols 6
#define nb_rows 9
#define border 10

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
	{ {0,'m','M',58,0,0}, {0,',','<',59,0,0}, {0,'.','>',60,0,0}, {0,' ',' ',61,0,0}, {0,'S','S',62,1,0}, {"Ret",'R','R',36,0,0} },
};

static struct key *key_at(unsigned col, unsigned row)
{
	return &kbmap[row][col];
}

static void update_mask(int shifted)
{
	XSetForeground(dis, mask_gc, BlackPixel(dis, screen));
	XFillRectangle(dis, mask, mask_gc, 0, 0, win_width, win_height);
	XSetForeground(dis, mask_gc, WhitePixel(dis, screen));
	for (unsigned col = 0; col < nb_cols; col++) {
		for (unsigned row = 0; row < nb_rows; row++) {
			int const W = (win_width  - 2*border) / nb_cols;
			int const x = border + col * W + (W - font_width)/2 - font_offset_x;
			int const H = (win_height - 2*border) / nb_rows;
			int const y = border + row * H + (H - font_height)/2 - font_offset_y;
			struct key const *key = key_at(col, row);
			if (key->name) {
				// FIXME: compute the actual width of the string
				XDrawString(dis, mask, mask_gc, x-font_width, y, key->name, 3);
			} else {
				XDrawString(dis, mask, mask_gc, x, y, shifted ? &key->shifted : &key->unshifted, 1);
			}
		}
	}
	
	XShapeCombineMask(dis, win, ShapeBounding, 0, 0, mask, ShapeSet);
	XSetForeground(dis, gc, WhitePixel(dis, screen));
	XFillRectangle(dis, win, gc, 0, 0, win_width, win_height);
}

static void redraw(void)
{
	static int inited = 0;
	if (! inited) {
		inited = 1;
		if (GrabSuccess != XGrabPointer(dis, win, True, ButtonPressMask|ButtonReleaseMask, GrabModeAsync, GrabModeAsync, None, None, CurrentTime)) {
			fprintf(stderr, "Cannot grab pointer !\n");
		}
		update_mask(0);
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

	char const *fontname = "-sony-*-*-*-*-*-24-*-*-*-*-*-*-*";
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
	if (font) XFreeFont(dis, font);
	XUngrabPointer(dis, CurrentTime);
	XFreeGC(dis, gc);
	XDestroyWindow(dis, win);
	XCloseDisplay(dis);	
}

static void hit(int x, int y, int press)
{
	unsigned const col = ((x-border) * nb_cols) / (win_width  - 2*border);
	unsigned const row = ((y-border) * nb_rows) / (win_height - 2*border);
	if (row >= nb_rows || col >= nb_cols) return;
	struct key *key = key_at(col, row);

	//printf("%s @ (%i,%i) => (%u,%u) => '%c'\n", press ? "Press":"Release", x, y, col, row, key->unshifted);

#	define SHIFT_KEYCODE 62
	if (press) {
		XTestFakeKeyEvent(dis, key->code, True, CurrentTime);
	} else {
		if (key->hold) {	// Must not release at once
			if (! key->held) {
				key->held = 1;
				if (key->code == SHIFT_KEYCODE) update_mask(1);
			} else {	// The key was already hold : release it
				key->held = 0;
				XTestFakeKeyEvent(dis, key->code, False, CurrentTime);
				if (key->code == SHIFT_KEYCODE) update_mask(0);
			}
		} else {
			XTestFakeKeyEvent(dis, key->code, False, CurrentTime);
			// Release all previously hold keys
			for (unsigned col = 0; col < nb_cols; col++) {
				for (unsigned row = 0; row < nb_rows; row++) {
					struct key *k = key_at(col, row);
					if (k->held) {
						k->held = 0;
						XTestFakeKeyEvent(dis, k->code, False, CurrentTime);
						if (k->code == SHIFT_KEYCODE) update_mask(0);
					}
				}
			}
		}
	}
}

static void event_loop(void)
{
	XEvent event;
	
	while (1) {		
		XNextEvent(dis, &event);

		if (event.type == Expose && event.xexpose.count == 0) {
			redraw();
		} else if (event.type == ButtonPress) {
			hit(event.xbutton.x, event.xbutton.y, 1);
		} else if (event.type == ButtonRelease) {
			hit(event.xbutton.x, event.xbutton.y, 0);
		}
	}
}

int main(void)
{
	open_X();
	event_loop();
	close_X();
	return 0;
}
