/***************************************************************************************************
SDLMAIN.CPP - SDL2 replacement for WINMAIN.CPP (Linux port of SLAB6)

PORT NOTICE: this file is NOT part of Ken Silverman's original release. It re-implements, on top
of SDL2, the interface declared in SYSMAIN.H, which the original implemented with DirectDraw and
DirectInput in WINMAIN.CPP (written by Ken Silverman (http://advsys.net/ken), additional
modifications by Tom Dobrowolski). The behaviour of the key buffer, the mouse and the main loop
follows that file. Credit for the original design and for SLAB6 belongs to Ken Silverman.

License of the original code: no commercial exploitation, do not remove the author's name or
credit, and make it clear that modified code is modified.

What this layer provides (see SYSMAIN.H):
  - main loop:   initapp() once, then doframe() until quitloop(), then uninitapp()
  - video:       8-bit paletted framebuffer (startdirectdraw/stopdirectdraw/nextpage/updatepalette)
  - keyboard:    keystatus[] / ext_keystatus[] indexed by DirectInput scancodes, keyread() FIFO
  - mouse:       readmouse() with relative motion, buttons, wheel; setmouseout/ismouseout
  - timer:       readklock()
Not provided: sound (SLAB6 builds with /DNOSOUND), the Windows menu bar, dialogs and clipboard.
***************************************************************************************************/

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "sysmain.h"
#include "s6win.h"

#define KEYBUFSIZ 256

//Program flow ------------------------------------------------------------------------------------
char *prognam = (char *)"SDL App";
long progresiz = 0;
long cputype = (1<<0)+(1<<4); //FPU + RDTSC bits, as the Windows layer required
long (*sdlmain_closehook)() = 0;

static int quitprogram = 0, vetoedclose = 0;
static long ActiveApp = 1, alwaysactive = 0;

void quitloop () { quitprogram = 1; }
void setalwaysactive (long active) { alwaysactive = active; }
long canrender () { return(ActiveApp); }
void ddflip2gdi () { } //Windows: handed the screen back to GDI before dialogs. Nothing to do here.
void smartsleep (long timeoutms) { if (timeoutms > 0) SDL_Delay((Uint32)timeoutms); }

static void fatal (const char *msg)
{
	fprintf(stderr,"%s: %s\n",prognam,msg);
	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,prognam,msg,0);
}

//Timer -------------------------------------------------------------------------------------------
static double klockbase = -1.0;

static double nowsec ()
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC,&ts);
	return((double)ts.tv_sec + (double)ts.tv_nsec*1e-9);
}
void initklock () { klockbase = nowsec(); }
void readklock (double *tim) { if (klockbase < 0.0) initklock(); (*tim) = nowsec()-klockbase; }

//Video -------------------------------------------------------------------------------------------
validmodetype curvidmodeinfo;
long xres = 640, yres = 480, colbits = 8, fullscreen = 0, maxpages = 1;
PALETTEENTRY pal[256];
long ddrawuseemulation = 1; //We always draw into our own 8-bit buffer (= DirectDraw "emulation" mode)

static SDL_Window *gwin = 0;
static SDL_Renderer *grend = 0;
static SDL_Texture *gtex = 0;
static unsigned char *fbuf = 0;
static long fbpl = 0, fbx = 0, fby = 0;
static Uint32 pal32[256];

static int allocvideo (long x, long y)
{
	long pitch = ((x+7)&~7);
	unsigned char *nb;
	SDL_Texture *nt;
	void *px; int tpitch;

	if ((x < 1) || (y < 1)) return(0);
	nb = (unsigned char *)calloc(pitch*y+256,1); //+256: slack, the SSE renderer may write past the end
	if (!nb) return(0);
	nt = SDL_CreateTexture(grend,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,(int)x,(int)y);
	if (!nt) { free(nb); return(0); }
	if (SDL_LockTexture(nt,0,&px,&tpitch) == 0) { memset(px,0,(size_t)tpitch*(size_t)y); SDL_UnlockTexture(nt); }
	if (gtex) SDL_DestroyTexture(gtex);
	free(fbuf);
	gtex = nt; fbuf = nb; fbpl = pitch; fbx = x; fby = y;
	return(1);
}

static void presentlast ()
{
	if ((!grend) || (!gtex)) return;
	SDL_RenderClear(grend);
	SDL_RenderCopy(grend,gtex,0,0);
	SDL_RenderPresent(grend);
}

void updatepalette (long start, long danum) //Pure data; safe to call before the window exists
{
	long i;
	for(i=start;(i<start+danum)&&(i<256);i++)
		if (i >= 0) pal32[i] = 0xff000000u + ((Uint32)pal[i].peRed<<16) + ((Uint32)pal[i].peGreen<<8) + (Uint32)pal[i].peBlue;
}

long startdirectdraw (intptr_t *vidplc, long *dabpl, long *daxres, long *dayres)
{
	if (!fbuf) return(0);
	(*vidplc) = (intptr_t)fbuf; (*dabpl) = fbpl; (*daxres) = fbx; (*dayres) = fby;
	return(1);
}
void stopdirectdraw () { }

long clearscreen (long fillcolor)
{
	if (!fbuf) return(0);
	memset(fbuf,(int)(fillcolor&255),(size_t)(fbpl*fby));
	return(1);
}

	//Test hook: SDLMAIN_DUMP=file.ppm [SDLMAIN_DUMPFRAME=n] writes the n-th presented frame (default 5)
	//as a PPM image and quits. Lets the program be checked headlessly (SDL_VIDEODRIVER=dummy).
static void dumpframe (const char *path)
{
	FILE *fil = fopen(path,"wb");
	long x, y;
	if (!fil) return;
	fprintf(fil,"P6\n%d %d\n255\n",(int)fbx,(int)fby);
	for(y=0;y<fby;y++)
		for(x=0;x<fbx;x++)
		{
			Uint32 c = pal32[fbuf[y*fbpl+x]];
			fputc((int)((c>>16)&255),fil); fputc((int)((c>>8)&255),fil); fputc((int)(c&255),fil);
		}
	fclose(fil);
}

void nextpage ()
{
	static long frames = 0;
	void *px; int tpitch; long x, y;
	if ((!grend) || (!gtex) || (!fbuf)) return;
	if (SDL_LockTexture(gtex,0,&px,&tpitch) == 0)
	{
		for(y=0;y<fby;y++)
		{
			const unsigned char *s = &fbuf[y*fbpl];
			Uint32 *d = (Uint32 *)((char *)px + (size_t)y*(size_t)tpitch);
			for(x=0;x<fbx;x++) d[x] = pal32[s[x]];
		}
		SDL_UnlockTexture(gtex);
	}
	presentlast();
	frames++;
	{
		const char *dp = getenv("SDLMAIN_DUMP"), *df = getenv("SDLMAIN_DUMPFRAME");
		if ((dp) && (frames == (df ? atol(df) : 5))) { dumpframe(dp); quitprogram = 1; }
	}
}

long getvalidmodelist (validmodetype **validmodelist) { (*validmodelist) = 0; return(0); } //Not used by SLAB6

long changeres (long x, long y, long dacolbits, long dafullscreen)
{
	(void)dacolbits; //Only 8-bit is supported
	if (gwin)
	{
		if (dafullscreen != fullscreen) SDL_SetWindowFullscreen(gwin,dafullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
		if (!dafullscreen) SDL_SetWindowSize(gwin,(int)x,(int)y);
		SDL_RenderSetLogicalSize(grend,dafullscreen ? (int)x : 0,dafullscreen ? (int)y : 0);
	}
	xres = x; yres = y; fullscreen = dafullscreen;
	if (gwin) return(allocvideo(x,y));
	return(1);
}

unsigned long sdlmain_peek (long x, long y) //Reads back a pixel of the last presented frame
{
	Uint32 px = 0;
	SDL_Rect r;
	r.x = (int)x; r.y = (int)y; r.w = 1; r.h = 1;
	if (!grend) return(0);
	if (SDL_RenderReadPixels(grend,&r,SDL_PIXELFORMAT_ARGB8888,&px,4) < 0) return(0);
	return((unsigned long)px);
}

//Keyboard ----------------------------------------------------------------------------------------
char keystatus[256];     //bit0=1:down
char ext_keystatus[256]; //bit0=1:down,bit1=1:was down

static long keybuf[KEYBUFSIZ], keybufr = 0, keybufw = 0, lastkey = -1, shkeystatus = 0, kbdchanges = 0;
static long kbd_acquire = 1;

	//SDL (USB HID) scancode -> DirectInput (PC/AT set 1, extended keys +0x80) scancode.
	//SLAB6 tests keystatus[0x2a], (i>>8)&255 == 0x58, etc., so the values must match DirectInput's.
static int dikfromsdl (int sc)
{
	switch (sc)
	{
		case SDL_SCANCODE_ESCAPE: return(0x01);
		case SDL_SCANCODE_1: return(0x02); case SDL_SCANCODE_2: return(0x03); case SDL_SCANCODE_3: return(0x04);
		case SDL_SCANCODE_4: return(0x05); case SDL_SCANCODE_5: return(0x06); case SDL_SCANCODE_6: return(0x07);
		case SDL_SCANCODE_7: return(0x08); case SDL_SCANCODE_8: return(0x09); case SDL_SCANCODE_9: return(0x0a);
		case SDL_SCANCODE_0: return(0x0b);
		case SDL_SCANCODE_MINUS: return(0x0c); case SDL_SCANCODE_EQUALS: return(0x0d);
		case SDL_SCANCODE_BACKSPACE: return(0x0e); case SDL_SCANCODE_TAB: return(0x0f);
		case SDL_SCANCODE_Q: return(0x10); case SDL_SCANCODE_W: return(0x11); case SDL_SCANCODE_E: return(0x12);
		case SDL_SCANCODE_R: return(0x13); case SDL_SCANCODE_T: return(0x14); case SDL_SCANCODE_Y: return(0x15);
		case SDL_SCANCODE_U: return(0x16); case SDL_SCANCODE_I: return(0x17); case SDL_SCANCODE_O: return(0x18);
		case SDL_SCANCODE_P: return(0x19);
		case SDL_SCANCODE_LEFTBRACKET: return(0x1a); case SDL_SCANCODE_RIGHTBRACKET: return(0x1b);
		case SDL_SCANCODE_RETURN: return(0x1c); case SDL_SCANCODE_LCTRL: return(0x1d);
		case SDL_SCANCODE_A: return(0x1e); case SDL_SCANCODE_S: return(0x1f); case SDL_SCANCODE_D: return(0x20);
		case SDL_SCANCODE_F: return(0x21); case SDL_SCANCODE_G: return(0x22); case SDL_SCANCODE_H: return(0x23);
		case SDL_SCANCODE_J: return(0x24); case SDL_SCANCODE_K: return(0x25); case SDL_SCANCODE_L: return(0x26);
		case SDL_SCANCODE_SEMICOLON: return(0x27); case SDL_SCANCODE_APOSTROPHE: return(0x28);
		case SDL_SCANCODE_GRAVE: return(0x29); case SDL_SCANCODE_LSHIFT: return(0x2a);
		case SDL_SCANCODE_BACKSLASH: case SDL_SCANCODE_NONUSHASH: return(0x2b);
		case SDL_SCANCODE_Z: return(0x2c); case SDL_SCANCODE_X: return(0x2d); case SDL_SCANCODE_C: return(0x2e);
		case SDL_SCANCODE_V: return(0x2f); case SDL_SCANCODE_B: return(0x30); case SDL_SCANCODE_N: return(0x31);
		case SDL_SCANCODE_M: return(0x32); case SDL_SCANCODE_COMMA: return(0x33); case SDL_SCANCODE_PERIOD: return(0x34);
		case SDL_SCANCODE_SLASH: return(0x35); case SDL_SCANCODE_RSHIFT: return(0x36);
		case SDL_SCANCODE_KP_MULTIPLY: return(0x37); case SDL_SCANCODE_LALT: return(0x38);
		case SDL_SCANCODE_SPACE: return(0x39); case SDL_SCANCODE_CAPSLOCK: return(0x3a);
		case SDL_SCANCODE_F1: return(0x3b); case SDL_SCANCODE_F2: return(0x3c); case SDL_SCANCODE_F3: return(0x3d);
		case SDL_SCANCODE_F4: return(0x3e); case SDL_SCANCODE_F5: return(0x3f); case SDL_SCANCODE_F6: return(0x40);
		case SDL_SCANCODE_F7: return(0x41); case SDL_SCANCODE_F8: return(0x42); case SDL_SCANCODE_F9: return(0x43);
		case SDL_SCANCODE_F10: return(0x44);
		case SDL_SCANCODE_NUMLOCKCLEAR: return(0x45); case SDL_SCANCODE_SCROLLLOCK: return(0x46);
		case SDL_SCANCODE_KP_7: return(0x47); case SDL_SCANCODE_KP_8: return(0x48); case SDL_SCANCODE_KP_9: return(0x49);
		case SDL_SCANCODE_KP_MINUS: return(0x4a);
		case SDL_SCANCODE_KP_4: return(0x4b); case SDL_SCANCODE_KP_5: return(0x4c); case SDL_SCANCODE_KP_6: return(0x4d);
		case SDL_SCANCODE_KP_PLUS: return(0x4e);
		case SDL_SCANCODE_KP_1: return(0x4f); case SDL_SCANCODE_KP_2: return(0x50); case SDL_SCANCODE_KP_3: return(0x51);
		case SDL_SCANCODE_KP_0: return(0x52); case SDL_SCANCODE_KP_PERIOD: return(0x53);
		case SDL_SCANCODE_NONUSBACKSLASH: return(0x56);
		case SDL_SCANCODE_F11: return(0x57); case SDL_SCANCODE_F12: return(0x58);
		case SDL_SCANCODE_KP_ENTER: return(0x9c); case SDL_SCANCODE_RCTRL: return(0x9d);
		case SDL_SCANCODE_KP_DIVIDE: return(0xb5); case SDL_SCANCODE_PRINTSCREEN: return(0xb7);
		case SDL_SCANCODE_RALT: return(0xb8); case SDL_SCANCODE_PAUSE: return(0xc5);
		case SDL_SCANCODE_HOME: return(0xc7); case SDL_SCANCODE_UP: return(0xc8); case SDL_SCANCODE_PAGEUP: return(0xc9);
		case SDL_SCANCODE_LEFT: return(0xcb); case SDL_SCANCODE_RIGHT: return(0xcd); case SDL_SCANCODE_END: return(0xcf);
		case SDL_SCANCODE_DOWN: return(0xd0); case SDL_SCANCODE_PAGEDOWN: return(0xd1);
		case SDL_SCANCODE_INSERT: return(0xd2); case SDL_SCANCODE_DELETE: return(0xd3);
		case SDL_SCANCODE_LGUI: return(0xdb); case SDL_SCANCODE_RGUI: return(0xdc);
		case SDL_SCANCODE_APPLICATION: return(0xdd);
		default: return(0);
	}
}

static void keypush (long v) //FIFO write. If the FIFO is full the key is dropped (as in Winmain.cpp)
{
	long n = ((keybufw+1)&(KEYBUFSIZ-1));
	if (n == keybufr) { lastkey = -1; return; }
	keybuf[keybufw] = v; lastkey = keybufw; keybufw = n;
}

long keyread ()
{
	long i;
	if (keybufr == keybufw) return(0);
	i = keybuf[keybufr]; keybufr = ((keybufr+1)&(KEYBUFSIZ-1));
	return(i);
}

	//Events are gathered between frames (breath()); readkeyboard() reports how many key
	//changes arrived since the previous call, like the DirectInput buffered read it replaces.
long readkeyboard () { long r = kbdchanges; kbdchanges = 0; return(r); }

static void dokey (const SDL_KeyboardEvent *k, int down)
{
	int dik = dikfromsdl((int)k->keysym.scancode);
	long modbit = 0;
	if (!dik) return;
	switch (dik) //Modifier bits that keyread() values carry in bits 16-21
	{
		case 0x2a: modbit = (1<<16); break; case 0x36: modbit = (1<<17); break;
		case 0x1d: modbit = (1<<18); break; case 0x9d: modbit = (1<<19); break;
		case 0x38: modbit = (1<<20); break; case 0xb8: modbit = (1<<21); break;
	}
	if (down)
	{
		if (!k->repeat) { keystatus[dik] = 1; ext_keystatus[dik] = 1|2; kbdchanges++; }
		if (modbit) { shkeystatus |= modbit; return; }
		keypush((((long)dik)<<8) + shkeystatus); //OS auto-repeat also lands here, like WM_KEYDOWN did
	}
	else
	{
		keystatus[dik] = 0; ext_keystatus[dik] &= ~1; kbdchanges++;
		shkeystatus &= ~modbit; lastkey = -1;
	}
}

static void dotext (const char *t) //Plays the role of WM_CHAR: ASCII goes into the low byte of the last key
{
	long a = 0;
	unsigned char c0 = (unsigned char)t[0];
	if (lastkey < 0) return;
	if (shkeystatus & ((1<<20)|(1<<21))) { lastkey = -1; return; } //Alt+key makes no WM_CHAR
	if (c0 < 0x80) a = c0;
	else if ((c0&0xe0) == 0xc0) { long cp = ((c0&0x1f)<<6) + (((unsigned char)t[1])&0x3f); if (cp < 256) a = cp; } //Latin-1
	keybuf[lastkey] |= a; lastkey = -1;
}

//Mouse -------------------------------------------------------------------------------------------
char ext_mbstatus[8];  //bit0=1:down,bit1=1:was down, bits6&7=mwheel
long ext_mwheel = 0;
long mousmoth = 1;     //Smoothing is not implemented (it relied on DirectInput timestamps); variable kept for SLAB6
float mousper = 1.0;

static long mouse_acquire = 1, gbstatus = 0, suspended = 0;
static float accx = 0, accy = 0, accz = 0;
static void (*setmousein)(long,long) = 0;

static void setrelative (long on)
{
	SDL_SetRelativeMouseMode(on ? SDL_TRUE : SDL_FALSE);
	accx = accy = accz = 0; gbstatus = 0;
}

void readmouse (float *fmousx, float *fmousy, float *fmousz, long *bstatus)
{
	if (!mouse_acquire) { (*fmousx) = 0; (*fmousy) = 0; (*fmousz) = 0; (*bstatus) = 0; return; }
	(*fmousx) = accx; (*fmousy) = accy; (*fmousz) = accz; (*bstatus) = gbstatus;
	accx = accy = accz = 0;
}
void readmouse (float *fmousx, float *fmousy, long *bstatus)
{
	float fmousz;
	readmouse(fmousx,fmousy,&fmousz,bstatus);
}

void setacquire (long mouse, long kbd)
{
	if (mouse_acquire != mouse)
	{
		if ((ActiveApp) && (gwin)) setrelative(mouse);
		mouse_acquire = mouse;
	}
	kbd_acquire = kbd;
}

	//SLAB6 draws its own cursor. When that cursor reaches the window border it calls setmouseout()
	//to hand the real cursor back to the desktop, and regains the mouse through the setmousein
	//callback when the real cursor re-enters the window (see SDL_MOUSEMOTION below).
void setmouseout (void (*in)(long,long), long x, long y)
{
	if (fullscreen) return;
	setmousein = in;
	setacquire(0,kbd_acquire);
	if (gwin)
	{
		SDL_WarpMouseInWindow(gwin,(int)x,(int)y);
			//Some SDL video drivers answer a warp with a synthetic SDL_MOUSEMOTION. If it were handled it
			//would look like the cursor coming back and the mouse would be re-acquired at once.
		SDL_PumpEvents(); SDL_FlushEvent(SDL_MOUSEMOTION);
	}
}

long ismouseout (long x, long y) //x,y relative to the top-left corner of the window
{
	if (fullscreen) return(0);
	return(((unsigned long)x >= (unsigned long)xres) || ((unsigned long)y >= (unsigned long)yres));
}

static void domousebutton (int sdlbtn, int down)
{
	int i;
	switch (sdlbtn)
	{
		case SDL_BUTTON_LEFT: i = 0; break;  case SDL_BUTTON_RIGHT: i = 1; break;
		case SDL_BUTTON_MIDDLE: i = 2; break; case SDL_BUTTON_X1: i = 3; break;
		default: return;
	}
	if (down) ext_mbstatus[i] = 1|2; else ext_mbstatus[i] &= 2;
	if (mouse_acquire) { if (down) gbstatus |= (1<<i); else gbstatus &= ~(1<<i); }
}

//Events ------------------------------------------------------------------------------------------
static void askclose ()
{
	if ((!sdlmain_closehook) || (sdlmain_closehook())) quitprogram = 1; else vetoedclose = 1;
}

static void loseactive ()
{
	ActiveApp = 0;
	memset(keystatus,0,sizeof(keystatus));
	{ long i; for(i=0;i<256;i++) ext_keystatus[i] &= ~1; }
	shkeystatus = 0; lastkey = -1; gbstatus = 0;
	if ((mouse_acquire) && (gwin)) SDL_SetRelativeMouseMode(SDL_FALSE);
}
static void gainactive ()
{
	ActiveApp = 1;
	if ((mouse_acquire) && (gwin) && (!suspended)) setrelative(1);
}

static void handleevent (const SDL_Event *e)
{
	int y;
	switch (e->type)
	{
		case SDL_QUIT: if (!vetoedclose) askclose(); break; //SDL sends this right after WINDOWEVENT_CLOSE
		case SDL_WINDOWEVENT:
			switch (e->window.event)
			{
				case SDL_WINDOWEVENT_CLOSE: askclose(); break;
				case SDL_WINDOWEVENT_FOCUS_LOST: case SDL_WINDOWEVENT_MINIMIZED: loseactive(); break;
				case SDL_WINDOWEVENT_FOCUS_GAINED: case SDL_WINDOWEVENT_RESTORED: gainactive(); break;
				case SDL_WINDOWEVENT_SHOWN: case SDL_WINDOWEVENT_EXPOSED: presentlast(); break;
				case SDL_WINDOWEVENT_SIZE_CHANGED:
					if ((progresiz) && (!fullscreen) && ((e->window.data1 != xres) || (e->window.data2 != yres)))
						{ xres = e->window.data1; yres = e->window.data2; allocvideo(xres,yres); }
					break;
				default: break;
			}
			break;
		case SDL_KEYDOWN: dokey(&e->key,1); break;
		case SDL_KEYUP: dokey(&e->key,0); break;
		case SDL_TEXTINPUT: dotext(e->text.text); break;
		case SDL_MOUSEMOTION:
			if (mouse_acquire) { accx += (float)e->motion.xrel; accy += (float)e->motion.yrel; }
			else if ((setmousein) && (ActiveApp)) //real cursor came back into the window: take the mouse again
			{
				void (*in)(long,long) = setmousein;
				setmousein = 0; mouse_acquire = 1; setrelative(1);
				in(e->motion.x,e->motion.y);
			}
			break;
		case SDL_MOUSEBUTTONDOWN: domousebutton(e->button.button,1); break;
		case SDL_MOUSEBUTTONUP: domousebutton(e->button.button,0); break;
		case SDL_MOUSEWHEEL:
			y = e->wheel.y; if (e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED) y = -y;
			y *= 120; //WHEEL_DELTA units, as DirectInput reported
			if (y > 0) ext_mbstatus[6] = 2; else if (y < 0) ext_mbstatus[7] = 2;
			ext_mwheel += y;
			if (mouse_acquire) accz += (float)y;
			break;
		default: break;
	}
}

static void breath (int waitms) //Counterpart of Winmain.cpp's breath(): pump events between frames
{
	SDL_Event e;
	vetoedclose = 0;
	if (waitms > 0) { if (SDL_WaitEventTimeout(&e,waitms)) handleevent(&e); }
	while (SDL_PollEvent(&e)) handleevent(&e);
}

		//An external program (the native file dialog) is about to take the screen: free the mouse, and give
	//it back afterwards. Keys held down meanwhile would never see their release, so forget them.
void sdlmain_suspendinput (long on)
{
	if (on) { if ((!suspended) && (mouse_acquire) && (gwin)) { SDL_SetRelativeMouseMode(SDL_FALSE); suspended = 1; } }
	else if (suspended) { suspended = 0; if ((mouse_acquire) && (ActiveApp) && (gwin)) setrelative(1); }
	memset(keystatus,0,sizeof(keystatus)); shkeystatus = 0; lastkey = -1;
}

//For modal loops outside doframe (file selectors): handle window events, then yield a little CPU
void sdlmain_pump () { breath(0); SDL_Delay(5); }
long sdlmain_wantquit () { return(quitprogram); }

//Message boxes (stand-ins for Win32 MessageBox and SLAB6's custom KVX dialog) ---------------------
	//Returns the index of the button pressed, or -1 if no dialog could be shown (no window yet,
	//headless SDL driver, ...). The mouse is handed back to the desktop while the box is open.
static int showbox (const char *caption, const char *text, int n, const char *lab[3])
{
	SDL_MessageBoxButtonData b[3];
	SDL_MessageBoxData d;
	int i, id = -1;
	long wasrel;

	if (!gwin) { fprintf(stderr,"%s: %s\n",caption,text); return(-1); }
	for(i=0;i<n;i++)
	{
		b[i].flags = ((i == 0) ? SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT : 0) | ((i == n-1) ? SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT : 0);
		b[i].buttonid = i; b[i].text = lab[i];
	}
	memset(&d,0,sizeof(d));
	d.flags = SDL_MESSAGEBOX_INFORMATION; d.window = gwin; d.title = caption; d.message = text; d.numbuttons = n; d.buttons = b;
	wasrel = ((mouse_acquire) && (ActiveApp));
	if (wasrel) SDL_SetRelativeMouseMode(SDL_FALSE);
	if (SDL_ShowMessageBox(&d,&id) < 0) { id = -1; fprintf(stderr,"%s: %s\n",caption,text); }
	memset(keystatus,0,sizeof(keystatus)); shkeystatus = 0; lastkey = -1; //key-ups went to the dialog
	if (wasrel) setrelative(1);
	return(id);
}

int s6_choose3 (const char *caption, const char *text, const char *b0, const char *b1, const char *b2)
{
	const char *lab[3]; lab[0] = b0; lab[1] = b1; lab[2] = b2;
	return(showbox(caption,text,3,lab));
}

int s6_messagebox (const char *text, const char *caption, int type) //MB_* in, ID* out (see s6win.h)
{
	const char *lab[3]; int n, id;
	switch (type)
	{
		case MB_OKCANCEL:    lab[0] = "OK";  lab[1] = "Cancel"; n = 2; break;
		case MB_YESNO:       lab[0] = "Yes"; lab[1] = "No";     n = 2; break;
		case MB_YESNOCANCEL: lab[0] = "Yes"; lab[1] = "No"; lab[2] = "Cancel"; n = 3; break;
		default:             lab[0] = "OK"; n = 1; break;
	}
	id = showbox(caption,text,n,lab);
	if (id < 0) id = 0; //no dialog possible: take the first (affirmative) answer
	switch (type)
	{
		case MB_OKCANCEL:    return((id == 0) ? IDOK : IDCANCEL);
		case MB_YESNO:       return((id == 0) ? IDYES : IDNO);
		case MB_YESNOCANCEL: return((id == 0) ? IDYES : ((id == 1) ? IDNO : IDCANCEL));
		default:             return(IDOK);
	}
}

//Main --------------------------------------------------------------------------------------------
int main (int argc, char **argv)
{
	Uint32 wflags;

	initklock();
	memset(keystatus,0,sizeof(keystatus)); memset(ext_keystatus,0,sizeof(ext_keystatus));
	memset(ext_mbstatus,0,sizeof(ext_mbstatus));

	if (initapp(argc,argv) < 0) return(-1); //Sets prognam, xres, yres, fullscreen, progresiz, palette, ...
	if (colbits != 8) { fprintf(stderr,"%s: only 8-bit video is supported, using 8\n",prognam); colbits = 8; }

	if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS) < 0) { fatal(SDL_GetError()); return(-1); }
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,"nearest");

	wflags = SDL_WINDOW_SHOWN;
	if (fullscreen) wflags |= SDL_WINDOW_FULLSCREEN_DESKTOP; else if (progresiz) wflags |= SDL_WINDOW_RESIZABLE;
	gwin = SDL_CreateWindow(prognam,SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,(int)xres,(int)yres,wflags);
	if (!gwin) { fatal(SDL_GetError()); SDL_Quit(); return(-1); }
	grend = SDL_CreateRenderer(gwin,-1,0);
	if (!grend) grend = SDL_CreateRenderer(gwin,-1,SDL_RENDERER_SOFTWARE);
	if (!grend) { fatal(SDL_GetError()); SDL_DestroyWindow(gwin); SDL_Quit(); return(-1); }
	if (fullscreen) SDL_RenderSetLogicalSize(grend,(int)xres,(int)yres); //scale xres*yres to the desktop
	if (!allocvideo(xres,yres)) { fatal("could not allocate the video buffer"); SDL_Quit(); return(-1); }

	SDL_ShowCursor(SDL_DISABLE); //Windows version used wc.hCursor = 0
	SDL_StartTextInput();
	if (mouse_acquire) setrelative(1);

	breath(0);
	while (!quitprogram)
	{
		if ((alwaysactive) || (ActiveApp)) { doframe(); breath(0); }
		else breath(100); //Like WaitMessage(): idle while the window is inactive
	}

	uninitapp();
	if (gtex) SDL_DestroyTexture(gtex);
	if (grend) SDL_DestroyRenderer(grend);
	if (gwin) SDL_DestroyWindow(gwin);
	free(fbuf);
	SDL_Quit();
	return(0);
}
