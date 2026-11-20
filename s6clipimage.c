/***************************************************************************************************
S6CLIPIMAGE.C - reads an image from the clipboard (Linux port of SLAB6)

PORT NOTICE: this file is NOT part of Ken Silverman's original SLAB6 release. Windows SLAB6 pasted
the clipboard bitmap (CF_BITMAP) with Shift+Insert. SDL2 cannot read images from the clipboard, so
this helper does it: it asks the X11 CLIPBOARD selection for image/png (or image/bmp), decodes the
picture, and writes it to stdout as

   "S6IM"  uint32 width  uint32 height   (little endian)   then width*height*4 bytes, B,G,R,A, top row first

which is the 32-bit layout the Windows code produced. Large pictures are cropped to --max=N pixels
(default 1024) from the top-left corner; SLAB6 never uses more than 256 in each direction.
If there is no X11 display it tries `wl-paste --type image/png` (Wayland sessions without XWayland).

Exit status: 0 an image was written, 1 the clipboard holds no (supported) image, 2 error.
Build: gcc s6clipimage.c -lX11 -lz -o slab6-clipimage
PNG: all color types and bit depths except interlaced files. BMP: 1, 4, 8, 24 and 32 bits per pixel.
***************************************************************************************************/
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <zlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/wait.h>

#define MAXDIM 16384
#define MAXPIXELS (100u*1000u*1000u)
#define TIMEOUT_MS 3000

static int fail (const char *msg) { fprintf(stderr,"slab6-clipimage: %s\n",msg); return(2); }

	//---- decoding ------------------------------------------------------------------------------------

static uint32_t be32 (const unsigned char *p) { return(((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]); }
static uint32_t le32 (const unsigned char *p) { return(((uint32_t)p[3]<<24)|((uint32_t)p[2]<<16)|((uint32_t)p[1]<<8)|p[0]); }
static uint32_t le16 (const unsigned char *p) { return(((uint32_t)p[1]<<8)|p[0]); }

static int paeth (int a, int b, int c)
{
	int p = a+b-c, pa = abs(p-a), pb = abs(p-b), pc = abs(p-c);
	return((pa <= pb && pa <= pc) ? a : ((pb <= pc) ? b : c));
}

	//Returns 0 ok (out = malloc'd BGRA), otherwise an error text through *why
static int decode_png (const unsigned char *p, size_t n, unsigned char **out, uint32_t *ow, uint32_t *oh, const char **why)
{
	uint32_t w = 0, h = 0, len, i, x, y;
	int depth = 0, ctype = 0, interlace = 0, haveihdr = 0, channels, bpp, k;
	unsigned char pal[256][4], *idat = 0, *raw = 0, *res = 0;
	size_t idatlen = 0, idatcap = 0, rowbytes, rawlen, pos = 8;
	uLongf destlen;

	*why = 0; memset(pal,0,sizeof(pal)); for(i=0;i<256;i++) pal[i][3] = 255;
	if ((n < 8) || (memcmp(p,"\x89PNG\r\n\x1a\n",8))) { *why = "not a PNG"; return(1); }
	while (pos+12 <= n)
	{
		len = be32(p+pos);
		if ((size_t)len > n-pos-12) { *why = "truncated PNG chunk"; goto bad; }
		const unsigned char *d = p+pos+8;
		if (!memcmp(p+pos+4,"IHDR",4))
		{
			if (len < 13) { *why = "bad IHDR"; goto bad; }
			w = be32(d); h = be32(d+4); depth = d[8]; ctype = d[9]; interlace = d[12]; haveihdr = 1;
		}
		else if (!memcmp(p+pos+4,"PLTE",4)) { for(i=0;(i < len/3) && (i < 256);i++) { pal[i][0] = d[i*3]; pal[i][1] = d[i*3+1]; pal[i][2] = d[i*3+2]; } }
		else if (!memcmp(p+pos+4,"tRNS",4)) { if (ctype == 3) for(i=0;(i < len) && (i < 256);i++) pal[i][3] = d[i]; }
		else if (!memcmp(p+pos+4,"IDAT",4))
		{
			if (idatlen+len > idatcap) { idatcap = (idatlen+len)*2+1024; idat = (unsigned char *)realloc(idat,idatcap); if (!idat) { *why = "out of memory"; goto bad; } }
			memcpy(idat+idatlen,d,len); idatlen += len;
		}
		else if (!memcmp(p+pos+4,"IEND",4)) break;
		pos += 12+(size_t)len;
	}
	if ((!haveihdr) || (!idat)) { *why = "PNG without image data"; goto bad; }
	if ((w < 1) || (h < 1) || (w > MAXDIM) || (h > MAXDIM) || ((uint64_t)w*h > MAXPIXELS)) { *why = "PNG size not supported"; goto bad; }
	if (interlace) { *why = "interlaced PNG not supported"; goto bad; }
	switch (ctype) { case 0: channels = 1; break; case 2: channels = 3; break; case 3: channels = 1; break; case 4: channels = 2; break; case 6: channels = 4; break; default: *why = "bad PNG color type"; goto bad; }
	if (((ctype == 0) && (depth != 1) && (depth != 2) && (depth != 4) && (depth != 8) && (depth != 16)) ||
		 ((ctype == 3) && (depth != 1) && (depth != 2) && (depth != 4) && (depth != 8)) ||
		 (((ctype == 2) || (ctype == 4) || (ctype == 6)) && (depth != 8) && (depth != 16))) { *why = "bad PNG bit depth"; goto bad; }
	bpp = (depth*channels+7)/8; rowbytes = ((size_t)w*depth*channels+7)/8;
	rawlen = (rowbytes+1)*h; destlen = (uLongf)rawlen;
	raw = (unsigned char *)malloc(rawlen);
	if ((!raw) || (uncompress(raw,&destlen,idat,(uLong)idatlen) != Z_OK) || (destlen != rawlen)) { *why = "PNG data is corrupt"; goto bad; }

	for(y=0;y<h;y++) //undo the row filters
	{
		unsigned char *row = raw+y*(rowbytes+1), *prev = y ? row-(rowbytes+1) : 0;
		int ft = row[0]; row++; if (prev) prev++;
		if (ft > 4) { *why = "bad PNG filter"; goto bad; }
		for(x=0;x<rowbytes;x++)
		{
			int a = (x >= (uint32_t)bpp) ? row[x-bpp] : 0, b = prev ? prev[x] : 0, c = ((prev) && (x >= (uint32_t)bpp)) ? prev[x-bpp] : 0;
			switch (ft) { case 1: row[x] += a; break; case 2: row[x] += b; break; case 3: row[x] += (a+b)>>1; break; case 4: row[x] += paeth(a,b,c); break; }
		}
	}

	res = (unsigned char *)malloc((size_t)w*h*4);
	if (!res) { *why = "out of memory"; goto bad; }
	for(y=0;y<h;y++)
	{
		const unsigned char *row = raw+y*(rowbytes+1)+1;
		unsigned char *o = res+(size_t)y*w*4;
		for(x=0;x<w;x++,o+=4)
		{
			int r, g, b, a = 255;
			switch (ctype)
			{
				case 0: case 3:
					if (depth == 16) { r = g = b = row[x*2]; }
					else
					{
						int per = 8/depth, v = (row[x/per]>>(8-depth*(1+(int)(x%per))))&((1<<depth)-1);
						if (ctype == 3) { r = pal[v][0]; g = pal[v][1]; b = pal[v][2]; a = pal[v][3]; }
						else r = g = b = (v*255)/((1<<depth)-1);
					}
					break;
				case 2: k = (depth == 16) ? 2 : 1; r = row[x*3*k]; g = row[x*3*k+k]; b = row[x*3*k+2*k]; break;
				case 4: k = (depth == 16) ? 2 : 1; r = g = b = row[x*2*k]; a = row[x*2*k+k]; break;
				default: k = (depth == 16) ? 2 : 1; r = row[x*4*k]; g = row[x*4*k+k]; b = row[x*4*k+2*k]; a = row[x*4*k+3*k]; break;
			}
			o[0] = (unsigned char)b; o[1] = (unsigned char)g; o[2] = (unsigned char)r; o[3] = (unsigned char)a;
		}
	}
	free(idat); free(raw);
	*out = res; *ow = w; *oh = h;
	return(0);
bad:
	free(idat); free(raw); free(res);
	return(1);
}

static int decode_bmp (const unsigned char *p, size_t n, unsigned char **out, uint32_t *ow, uint32_t *oh, const char **why)
{
	size_t dataoff, palette_at, stride;
	uint32_t hsize, comp, ncol, x, y, w, h;
	int32_t iw, ih;
	int bpp, topdown, i;
	unsigned char pal[256][4], *res;
	const unsigned char *hdr, *base = p;
	size_t basen = n;

	*why = 0;
	if ((n >= 14) && (p[0] == 'B') && (p[1] == 'M')) { dataoff = le32(p+10); hdr = p+14; n -= 14; p += 14; if (dataoff >= 14) dataoff -= 14; else dataoff = 0; }
	else { hdr = p; dataoff = 0; }
	if (n < 40) { *why = "BMP header too short"; return(1); }
	hsize = le32(hdr); iw = (int32_t)le32(hdr+4); ih = (int32_t)le32(hdr+8); bpp = (int)le16(hdr+14); comp = le32(hdr+16); ncol = le32(hdr+32);
	if ((hsize < 40) || (hsize > n)) { *why = "BMP header not supported"; return(1); }
	topdown = (ih < 0); w = (uint32_t)((iw < 0) ? -iw : iw); h = (uint32_t)(topdown ? -ih : ih);
	if ((w < 1) || (h < 1) || (w > MAXDIM) || (h > MAXDIM) || ((uint64_t)w*h > MAXPIXELS)) { *why = "BMP size not supported"; return(1); }
	if ((comp != 0) && (comp != 3)) { *why = "compressed BMP not supported"; return(1); }
	if ((bpp != 1) && (bpp != 4) && (bpp != 8) && (bpp != 24) && (bpp != 32)) { *why = "BMP bit depth not supported"; return(1); }
	memset(pal,0,sizeof(pal));
	palette_at = hsize; if ((hsize == 40) && (comp == 3)) palette_at += 12; //bit field masks follow a plain 40 byte header
	if (bpp <= 8)
	{
		if ((ncol == 0) || (ncol > 256)) ncol = 1u<<bpp;
		if (hsize+ncol*4 > n) { *why = "BMP palette truncated"; return(1); }
		for(i=0;i<(int)ncol;i++) { pal[i][0] = hdr[hsize+i*4]; pal[i][1] = hdr[hsize+i*4+1]; pal[i][2] = hdr[hsize+i*4+2]; }
		palette_at += ncol*4; (void)palette_at;
	}
	if (!dataoff) dataoff = hsize + ((hsize == 40 && comp == 3) ? 12 : 0) + ((bpp <= 8) ? ncol*4 : 0);
	stride = (((size_t)w*bpp+31)/32)*4;
	if (dataoff+stride*h > n) { *why = "BMP pixel data truncated"; return(1); }
	(void)base; (void)basen;
	res = (unsigned char *)malloc((size_t)w*h*4);
	if (!res) { *why = "out of memory"; return(1); }
	for(y=0;y<h;y++)
	{
		const unsigned char *row = hdr+dataoff+stride*(topdown ? y : h-1-y);
		unsigned char *o = res+(size_t)y*w*4;
		for(x=0;x<w;x++,o+=4)
		{
			if (bpp == 24) { o[0] = row[x*3]; o[1] = row[x*3+1]; o[2] = row[x*3+2]; o[3] = 255; }
			else if (bpp == 32) { o[0] = row[x*4]; o[1] = row[x*4+1]; o[2] = row[x*4+2]; o[3] = (comp == 3) ? row[x*4+3] : 255; } //BI_RGB: the 4th byte is unused
			else
			{
				int per = 8/bpp, v = (row[x/per]>>(8-bpp*(1+(int)(x%per))))&((1<<bpp)-1);
				o[0] = pal[v][0]; o[1] = pal[v][1]; o[2] = pal[v][2]; o[3] = 255;
			}
		}
	}
	*out = res; *ow = w; *oh = h;
	return(0);
}

static int decode_image (const unsigned char *p, size_t n, unsigned char **out, uint32_t *w, uint32_t *h, const char **why)
{
	if ((n >= 8) && (!memcmp(p,"\x89PNG",4))) return(decode_png(p,n,out,w,h,why));
	if ((n >= 2) && (p[0] == 'B') && (p[1] == 'M')) return(decode_bmp(p,n,out,w,h,why));
	if ((n >= 4) && ((le32(p) == 40) || (le32(p) == 108) || (le32(p) == 124))) return(decode_bmp(p,n,out,w,h,why)); //DIB without file header
	*why = "unknown image format"; return(1);
}

	//---- X11 selection -------------------------------------------------------------------------------

static int next_event (Display *d, XEvent *ev, int ms)
{
	int fd = ConnectionNumber(d);
	for(;;)
	{
		fd_set fds; struct timeval tv;
		if (XPending(d)) { XNextEvent(d,ev); return(1); }
		FD_ZERO(&fds); FD_SET(fd,&fds); tv.tv_sec = ms/1000; tv.tv_usec = (ms%1000)*1000;
		if (select(fd+1,&fds,0,0,&tv) <= 0) return(0);
	}
}

	//1: data in *buf (malloc'd), 0: the owner has nothing for that target, -1: error/timeout
static int fetch (Display *d, Window w, Atom sel, Atom target, Atom prop, Atom incr, unsigned char **buf, size_t *len)
{
	XEvent ev;
	Atom type; int fmt; unsigned long nitems, after; unsigned char *data = 0;
	size_t cap = 0, n = 0, got;
	int isincr = 0;

	*buf = 0; *len = 0;
	XDeleteProperty(d,w,prop);
	XConvertSelection(d,sel,target,prop,w,CurrentTime); XFlush(d);
	for(;;)
	{
		if (!next_event(d,&ev,TIMEOUT_MS)) return(-1);
		if ((ev.type == SelectionNotify) && (ev.xselection.selection == sel) && (ev.xselection.target == target)) break;
	}
	if (ev.xselection.property == None) return(0);

	for(;;)
	{
		data = 0;
		if (XGetWindowProperty(d,w,prop,0,16*1024*1024,True,AnyPropertyType,&type,&fmt,&nitems,&after,&data) != Success) return(-1);
		if ((!isincr) && (type == incr)) //big data arrives in pieces: deleting the property asks for the first one
		{
			isincr = 1; if (data) XFree(data); XFlush(d);
			for(;;)
			{
				if (!next_event(d,&ev,TIMEOUT_MS)) { free(*buf); *buf = 0; return(-1); }
				if ((ev.type == PropertyNotify) && (ev.xproperty.window == w) && (ev.xproperty.atom == prop) && (ev.xproperty.state == PropertyNewValue)) break;
			}
			continue;
		}
		got = (size_t)nitems*(size_t)(fmt/8) ; if (fmt == 32) got = (size_t)nitems*sizeof(long); //Xlib stores 32 bit items as long
		if (n+got+1 > cap) { cap = (n+got+1)*2; *buf = (unsigned char *)realloc(*buf,cap); if (!*buf) { if (data) XFree(data); return(-1); } }
		if ((data) && (got)) memcpy(*buf+n,data,got);
		if (data) XFree(data);
		n += got;
		if (n > 256u*1024u*1024u) { free(*buf); *buf = 0; return(-1); }
		if (!isincr) break;
		if (got == 0) break; //an empty piece ends the transfer
		XFlush(d);
		for(;;)
		{
			if (!next_event(d,&ev,TIMEOUT_MS)) { free(*buf); *buf = 0; return(-1); }
			if ((ev.type == PropertyNotify) && (ev.xproperty.window == w) && (ev.xproperty.atom == prop) && (ev.xproperty.state == PropertyNewValue)) break;
		}
	}
	*len = n; return(1);
}

static int read_x11 (unsigned char **png, size_t *plen)
{
	static const char *wanted[] = { "image/png", "image/bmp", "image/x-bmp", "image/x-MS-bmp", "image/x-ms-bmp" };
	Display *d = XOpenDisplay(0);
	Window w;
	Atom clip, targets, incr, prop, a[5];
	unsigned char *tl = 0; size_t tlen = 0;
	int i, j, r, ok[5] = { 0, 0, 0, 0, 0 }, any = 0, tr;

	if (!d) return(-2);
	w = XCreateSimpleWindow(d,DefaultRootWindow(d),0,0,1,1,0,0,0);
	XSelectInput(d,w,PropertyChangeMask);
	clip = XInternAtom(d,"CLIPBOARD",False); targets = XInternAtom(d,"TARGETS",False); incr = XInternAtom(d,"INCR",False); prop = XInternAtom(d,"SLAB6_CLIP",False);
	for(i=0;i<5;i++) a[i] = XInternAtom(d,wanted[i],False);

	tr = fetch(d,w,clip,targets,prop,incr,&tl,&tlen); //which formats does the owner offer?
	if ((tr == 1) && (tlen >= sizeof(long)))
		for(j=0;j<(int)(tlen/sizeof(long));j++) for(i=0;i<5;i++) if (((long *)tl)[j] == (long)a[i]) { ok[i] = 1; any = 1; }
	free(tl);
	if ((tr == 1) && (!any)) { XCloseDisplay(d); return(-1); } //an owner exists and offers no image
	for(i=0;i<5;i++)
	{
		if ((tr == 1) && (!ok[i])) continue; //(if the owner would not list its targets, just try them all)
		r = fetch(d,w,clip,a[i],prop,incr,png,plen);
		if (r == 1) { XCloseDisplay(d); return(0); }
	}
	XCloseDisplay(d);
	return(-1);
}

static int read_wayland (unsigned char **png, size_t *plen)
{
	FILE *f = popen("wl-paste --no-newline --type image/png 2>/dev/null","r");
	size_t cap = 65536, n = 0, k;
	unsigned char *b;
	if (!f) return(-2);
	b = (unsigned char *)malloc(cap);
	while ((b) && ((k = fread(b+n,1,cap-n,f)) > 0))
	{
		n += k; if (n == cap) { cap *= 2; if (cap > 256u*1024u*1024u) break; b = (unsigned char *)realloc(b,cap); }
	}
	k = (size_t)pclose(f);
	if ((WIFEXITED((int)k)) && (WEXITSTATUS((int)k) == 127)) { free(b); return(-2); } //wl-paste is not installed
	if ((!b) || (n < 8)) { free(b); return(-1); }
	*png = b; *plen = n; return(0);
}

int main (int argc, char **argv)
{
	unsigned char *raw = 0, *img = 0;
	size_t rawlen = 0;
	uint32_t w, h, cw, ch, y, maxd = 1024;
	const char *why = 0;
	unsigned char hdr[12];
	int i, r;

	for(i=1;i<argc;i++) if (!strncmp(argv[i],"--max=",6)) { maxd = (uint32_t)atoi(&argv[i][6]); if (maxd < 1) maxd = 1; }

	r = read_x11(&raw,&rawlen);
	if (r == -2) r = read_wayland(&raw,&rawlen); //no X11 display
	if (r == -2) return(fail("no X11 display and no wl-paste"));
	if (r != 0) return(1); //no image on the clipboard

	if (decode_image(raw,rawlen,&img,&w,&h,&why)) { fprintf(stderr,"slab6-clipimage: %s\n",why ? why : "decode error"); return(why && !strncmp(why,"unknown",7) ? 1 : 2); }
	free(raw);

	cw = (w < maxd) ? w : maxd; ch = (h < maxd) ? h : maxd;
	memcpy(hdr,"S6IM",4); hdr[4] = cw&255; hdr[5] = (cw>>8)&255; hdr[6] = (cw>>16)&255; hdr[7] = cw>>24;
	hdr[8] = ch&255; hdr[9] = (ch>>8)&255; hdr[10] = (ch>>16)&255; hdr[11] = ch>>24;
	if (fwrite(hdr,1,12,stdout) != 12) return(2);
	for(y=0;y<ch;y++) if (fwrite(img+(size_t)y*w*4,4,cw,stdout) != cw) return(2);
	fflush(stdout);
	return(0);
}
