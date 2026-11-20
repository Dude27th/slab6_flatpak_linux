/***************************************************************************************************
S6MENU.H - in-app menu bar for the Linux port (step 4, phase 2)

PORT NOTICE: this file is NOT part of Ken Silverman's original SLAB6 release. The Windows version
used a native menu bar (LoadMenuIndirect/SetMenu). Here the same menu definition (the menuadd()
calls in mymenuinit) is recorded into a tree, drawn on top of the 8-bit framebuffer with SLAB6's
own 6x8 font, and driven by the mouse and the keyboard. Choosing an item calls mymenufunc() with
the same command id the native menu would have sent.

Mouse: click a top entry to open it, hover to move between entries and sub-menus, click an item to
run it, click anywhere else to close. While the mouse is over the bar or a menu, its buttons do not
reach the editor. Keyboard: F10 or Alt+<underlined letter> opens, arrows move, Enter runs, Esc backs
out, a letter picks the item with that underlined letter.
Included by slab6.c (Linux only), after print6x8/gdd/closestcol and before mymenufunc/doframe.
***************************************************************************************************/
#ifndef S6MENU_H
#define S6MENU_H

#define S6M_BARH 10
#define S6M_ROWH 10
#define S6M_SEPH 5
#define S6M_MAXITEMS 160
#define S6M_MAXLEVEL 6

static long mymenufunc (WPARAM wparam, LPARAM lparam); //defined later in slab6.c

typedef struct
{
	char label[64], hint[32];
	long flags, id;
	short parent, first, last, next;
	signed char mpos; char mnem, checked;
} s6m_t;

static s6m_t s6m_it[S6M_MAXITEMS];
static long s6m_n = 0, s6m_sp = -1;
static short s6m_stk[16];
static short s6m_rootfirst = -1, s6m_rootlast = -1;

	//State of the menu interaction
static long s6m_active = 0;    //menu is open (mouse or keyboard)
static long s6m_depth = 0;     //number of open drop-down lists (0: only the bar is focused)
static long s6m_owned = 0;     //a mouse press was taken by the menu: keep eating buttons until released
static long s6m_oraw = 0;      //raw mouse buttons of the previous frame
static short s6m_sel[S6M_MAXLEVEL+2]; //[0]: highlighted bar entry; [k]: highlighted row of list k
static long s6m_bx0[S6M_MAXLEVEL+2], s6m_by0[S6M_MAXLEVEL+2], s6m_bx1[S6M_MAXLEVEL+2], s6m_by1[S6M_MAXLEVEL+2];
static char s6m_prevkey[256];

	//---- building the tree (same calls as the Windows menu template builder) ------------------------

static void s6m_endlevel ()
{
	while (s6m_sp >= 0)
	{
		long o = s6m_stk[s6m_sp--];
		if ((o < 0) || (!(s6m_it[o].flags & MF_END))) break;
	}
}

static short *menustart (short *sbuf)
{
	long i;
	s6m_n = 0; s6m_sp = 0; s6m_stk[0] = -1; s6m_rootfirst = s6m_rootlast = -1;
	for(i=0;i<S6M_MAXLEVEL+2;i++) s6m_sel[i] = -1;
	return(sbuf);
}

static short *menuadd (short *sptr, const char *st, long flags, long id)
{
	long i, k, o;
	char ch;
	s6m_t *it;

	if (s6m_n >= S6M_MAXITEMS) return(sptr);
	i = s6m_n++; it = &s6m_it[i];
	memset(it,0,sizeof(*it)); it->first = it->last = it->next = -1; it->mpos = -1;

	k = 0;
	for(o=0;(st[o]) && (st[o] != '\t');o++) //"&X" underlines X; "&&" is a literal &
	{
		ch = st[o];
		if (ch == '&')
		{
			if (st[o+1] == '&') { o++; ch = '&'; }
			else
			{
				if ((it->mpos < 0) && (st[o+1]) && (st[o+1] != '\t'))
					{ it->mpos = (signed char)k; it->mnem = st[o+1]; if ((it->mnem >= 'A') && (it->mnem <= 'Z')) it->mnem += 32; }
				continue;
			}
		}
		if (k < (long)sizeof(it->label)-1) it->label[k++] = ch;
	}
	it->label[k] = 0;
	if (st[o] == '\t') { strncpy(it->hint,&st[o+1],sizeof(it->hint)-1); it->hint[sizeof(it->hint)-1] = 0; }

	it->flags = flags; it->id = id; it->checked = ((flags & MF_CHECKED) != 0);
	it->parent = (short)((s6m_sp >= 0) ? s6m_stk[s6m_sp] : -1);
	if (it->parent >= 0)
	{
		s6m_t *p = &s6m_it[it->parent];
		if (p->last >= 0) s6m_it[p->last].next = (short)i; else p->first = (short)i;
		p->last = (short)i;
	}
	else
	{
		if (s6m_rootlast >= 0) s6m_it[s6m_rootlast].next = (short)i; else s6m_rootfirst = (short)i;
		s6m_rootlast = (short)i;
	}
	if (flags & MF_POPUP) { if (s6m_sp < 14) s6m_stk[++s6m_sp] = (short)i; } //its entries follow
	else if (flags & MF_END) s6m_endlevel();
	return(sptr);
}

void s6m_check (long id, int checked) //CheckMenuItem()
{
	long i;
	for(i=0;i<s6m_n;i++)
		if ((s6m_it[i].id == id) && (!(s6m_it[i].flags & (MF_POPUP|MF_SEPARATOR)))) s6m_it[i].checked = (char)(checked != 0);
}

	//---- small helpers ------------------------------------------------------------------------------

static long s6m_disabled (long i) { return((s6m_it[i].flags & (MF_GRAYED|MF_DISABLED|MF_SEPARATOR)) != 0); }
static long s6m_rowh (long i) { return((s6m_it[i].flags & MF_SEPARATOR) ? S6M_SEPH : S6M_ROWH); }
static long s6m_firstok (long parent) //first selectable entry of a list (-1 if none)
{
	long c;
	for(c=(parent < 0) ? s6m_rootfirst : s6m_it[parent].first;c>=0;c=s6m_it[c].next) if (!s6m_disabled(c)) return(c);
	return(-1);
}
static long s6m_firstof (long item) { return((s6m_it[item].parent < 0) ? s6m_rootfirst : s6m_it[s6m_it[item].parent].first); }
static long s6m_nextok (long item, long dir) //next/previous selectable sibling, wrapping around
{
	long c, n = 0, i, k;
	for(c=s6m_firstof(item);c>=0;c=s6m_it[c].next) n++;
	c = item;
	for(k=0;k<n;k++)
	{
		if (dir > 0) { c = s6m_it[c].next; if (c < 0) c = s6m_firstof(item); }
		else { long p = s6m_firstof(item); i = -1; while ((p >= 0) && (p != c)) { i = p; p = s6m_it[p].next; } if (i < 0) { for(i=c;s6m_it[i].next>=0;i=s6m_it[i].next); } c = i; }
		if (!s6m_disabled(c)) return(c);
	}
	return(item);
}
static long s6m_ispopup (long i) { return((i >= 0) && ((s6m_it[i].flags & MF_POPUP) != 0)); }

static void s6m_barcell (long item, long *x0, long *x1)
{
	long i, x = 0, w;
	for(i=s6m_rootfirst;i>=0;i=s6m_it[i].next)
	{
		w = strlen(s6m_it[i].label)*6 + 12;
		if (i == item) { (*x0) = x; (*x1) = x+w; return; }
		x += w;
	}
	(*x0) = (*x1) = 0;
}

	//Boxes of the open lists. List k belongs to the popup entry s6m_sel[k-1].
static void s6m_layout ()
{
	long k, o, c, w, h, maxl, maxh, cx0, cx1, x0, y0, y;
	for(k=1;k<=s6m_depth;k++)
	{
		o = s6m_sel[k-1];
		maxl = maxh = 0; h = 2;
		for(c=s6m_it[o].first;c>=0;c=s6m_it[c].next)
		{
			h += s6m_rowh(c);
			if (!(s6m_it[c].flags & MF_SEPARATOR))
			{
				maxl = max(maxl,(long)strlen(s6m_it[c].label)*6);
				maxh = max(maxh,(long)strlen(s6m_it[c].hint)*6);
			}
		}
		w = 12 + maxl + (maxh ? 14+maxh : 0) + 10;
		if (k == 1) { s6m_barcell(o,&cx0,&cx1); x0 = cx0; y0 = S6M_BARH; if (x0+w > gdd.x) x0 = gdd.x-w; }
		else
		{
			x0 = s6m_bx1[k-1]-1;
			y = s6m_by0[k-1]+1;
			for(c=s6m_firstof(o);(c >= 0) && (c != o);c=s6m_it[c].next) y += s6m_rowh(c);
			y0 = y;
			if (x0+w > gdd.x) x0 = s6m_bx0[k-1]-w+1;
		}
		if (y0+h > gdd.y) y0 = gdd.y-h;
		if (x0 < 0) x0 = 0;
		if (y0 < S6M_BARH) y0 = S6M_BARH;
		s6m_bx0[k] = x0; s6m_by0[k] = y0; s6m_bx1[k] = x0+w; s6m_by1[k] = y0+h;
	}
}

	//What is under the cursor? Returns the entry, or -1 (nothing / empty bar), -2 (menu border), and the level (-1 outside).
static long s6m_hit (long mx, long my, long *lvl)
{
	long k, c, y, x0, x1;
	for(k=s6m_depth;k>=1;k--)
		if ((mx >= s6m_bx0[k]) && (mx < s6m_bx1[k]) && (my >= s6m_by0[k]) && (my < s6m_by1[k]))
		{
			(*lvl) = k; y = s6m_by0[k]+1;
			for(c=s6m_it[s6m_sel[k-1]].first;c>=0;c=s6m_it[c].next)
			{
				if ((my >= y) && (my < y+s6m_rowh(c))) return((s6m_it[c].flags & MF_SEPARATOR) ? -2 : c);
				y += s6m_rowh(c);
			}
			return(-2);
		}
	if ((my >= 0) && (my < S6M_BARH))
	{
		(*lvl) = 0;
		for(c=s6m_rootfirst;c>=0;c=s6m_it[c].next) { s6m_barcell(c,&x0,&x1); if ((mx >= x0) && (mx < x1)) return(c); }
		return(-1);
	}
	(*lvl) = -1; return(-1);
}

	//---- interaction --------------------------------------------------------------------------------

static void s6m_close () { s6m_active = 0; s6m_depth = 0; s6m_sel[0] = -1; s6m_sel[1] = -1; }

static void s6m_run (long item) //Run a command like the native menu would (mymenufunc may block in a dialog)
{
	long id = s6m_it[item].id;
	s6m_close();
	mymenufunc((WPARAM)id,0);
}

	//Make `item` the highlighted entry of level lvl (0 = bar), opening its list if it is a popup
static void s6m_select (long lvl, long item, long openfirst)
{
	s6m_sel[lvl] = (short)item;
	if ((s6m_ispopup(item)) && (lvl < S6M_MAXLEVEL-1))
	{
		s6m_depth = lvl+1;
		s6m_sel[lvl+1] = (short)(openfirst ? s6m_firstok(item) : -1);
	}
	else s6m_depth = lvl;
}

static void s6m_openbar (long item, long openfirst)
{
	s6m_active = 1; s6m_select(0,item,openfirst);
}

static void s6m_key (long sc, long asc) //one key press while the menu is active
{
	long lvl = s6m_depth, cur = s6m_sel[lvl], c, k;
	switch(sc)
	{
		case 0x01: if (s6m_depth > 0) s6m_depth--; else s6m_close(); return;      //Esc
		case 0x44: s6m_close(); return;                                              //F10
		case 0xd0: //Down
			if (lvl == 0) { if (s6m_ispopup(cur)) s6m_select(0,cur,1); }
			else if (cur < 0) s6m_sel[lvl] = (short)s6m_firstok(s6m_sel[lvl-1]);
			else s6m_sel[lvl] = (short)s6m_nextok(cur,1);
			return;
		case 0xc8: //Up
			if (lvl > 0) { if (cur < 0) s6m_sel[lvl] = (short)s6m_firstok(s6m_sel[lvl-1]); else s6m_sel[lvl] = (short)s6m_nextok(cur,-1); }
			return;
		case 0xcd: //Right: open a sub-menu, else next bar entry
			if ((lvl > 0) && (s6m_ispopup(cur))) { s6m_select(lvl,cur,1); return; }
			c = s6m_sel[0]; if (c >= 0) { s6m_select(0,s6m_nextok(c,1),1); if (!s6m_ispopup(s6m_sel[0])) s6m_depth = 0; }
			return;
		case 0xcb: //Left: close a sub-menu, else previous bar entry
			if (lvl >= 2) { s6m_depth--; return; }
			c = s6m_sel[0]; if (c >= 0) { s6m_select(0,s6m_nextok(c,-1),1); if (!s6m_ispopup(s6m_sel[0])) s6m_depth = 0; }
			return;
		case 0x1c: case 0x9c: case 0x39: //Enter / keypad Enter / Space
			if (cur < 0) return;
			if (s6m_ispopup(cur)) s6m_select(lvl,cur,1); else s6m_run(cur);
			return;
	}
	if ((asc >= 'A') && (asc <= 'Z')) asc += 32;
	if ((asc >= 'a') && (asc <= 'z')) //underlined letter in the current list
	{
		k = (lvl == 0) ? s6m_rootfirst : s6m_it[s6m_sel[lvl-1]].first;
		for(c=k;c>=0;c=s6m_it[c].next)
			if ((s6m_it[c].mnem == asc) && (!s6m_disabled(c)))
			{
				if (s6m_ispopup(c)) s6m_select(lvl,c,1); else { s6m_sel[lvl] = (short)c; s6m_run(c); }
				return;
			}
	}
}

static const char s6m_letterkeys[] = "qwertyuiop\0\0\0\0asdfghjkl\0\0\0\0\0\0zxcvbnm"; //index+0x10 = scancode

static void s6m_keys () //called once per frame, before the editor looks at the keyboard
{
	long k, c, sc, any = 0;
	if (!s6m_active)
	{
		if ((keystatus[0x44]) && (!s6m_prevkey[0x44]) && (s6m_rootfirst >= 0)) s6m_openbar(s6m_rootfirst,1); //F10
		else if ((keystatus[0x38]) || (keystatus[0xb8])) //Alt+letter
			for(c=s6m_rootfirst;c>=0;c=s6m_it[c].next)
			{
				if (!s6m_it[c].mnem) continue;
				for(sc=0x10;sc<=0x32;sc++)
					if ((keystatus[sc]) && (!s6m_prevkey[sc]) && (s6m_letterkeys[sc-0x10] == s6m_it[c].mnem)) { s6m_openbar(c,1); break; }
			}
	}
	if (s6m_active)
	{
		any = 1;
		while ((k = keyread()) != 0) if (s6m_active) s6m_key((k>>8)&255,k&255);
	}
	memcpy(s6m_prevkey,keystatus,sizeof(s6m_prevkey));
	if (any) { memset(keystatus,0,256); memset(s6m_prevkey,0,sizeof(s6m_prevkey)); } //keep the keys away from the editor
}

	//Returns nonzero if the menu took this frame's mouse input (the caller then hides it from the editor).
static long s6m_mouse (long mx, long my, long bst)
{
	long hit, lvl = -1, press = (bst & ~s6m_oraw & 1), eat = 0;

	s6m_layout();
	hit = s6m_hit(mx,my,&lvl);
	if (s6m_active)
	{
		eat = 1;
		if (hit >= 0)
		{
			if (lvl == 0) { if (s6m_sel[0] != hit) s6m_select(0,hit,0); }
			else if (s6m_sel[lvl] != hit) s6m_select(lvl,hit,0);
		}
		if (press)
		{
			if (hit >= 0)
			{
				if (s6m_ispopup(hit)) { s6m_select(lvl,hit,0); }
				else s6m_run(hit);
			}
			else if ((hit == -2) || ((hit == -1) && (lvl == 0))) { /*border or empty bar: keep open*/ }
			else s6m_close();
			s6m_owned = 1;
		}
	}
	else if ((press) && (my >= 0) && (my < S6M_BARH))
	{
		eat = 1; s6m_owned = 1;
		if (hit >= 0) { if (s6m_ispopup(hit)) s6m_openbar(hit,0); else s6m_run(hit); }
	}
	if (s6m_owned) { if (bst) eat = 1; else s6m_owned = 0; }
	s6m_oraw = bst;
	return(eat);
}

static void s6m_frame (long mx, long my, long *bst, long *obst) //per frame, right after the mouse was read
{
	s6m_keys();
	if (s6m_mouse(mx,my,*bst)) { (*bst) = 0; (*obst) = 0; }
}

	//---- drawing ------------------------------------------------------------------------------------

static char s6m_rgb (long r, long g, long b) { return(closestcol[((r>>2)<<12)+((g>>2)<<6)+(b>>2)]); }

static void s6m_rect (long x0, long y0, long x1, long y1, char col)
{
	long x, y;
	char *p;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > gdd.x) x1 = gdd.x;
	if (y1 > gdd.y) y1 = gdd.y;
	for(y=y0;y<y1;y++) { p = (char *)(gdd.f+y*gdd.p+x0); for(x=x0;x<x1;x++) *p++ = col; }
}

static void s6m_draw (long cx, long cy) //after the editor drew everything (cursor included)
{
	long i, c, k, x0, x1, y, rh, hl;
	char bar, line, txt, hlbg, lbg, dis, chk;

	if ((gdd.x < 1) || (gdd.y < S6M_BARH)) return;
	bar = s6m_rgb(36,36,52); line = s6m_rgb(100,100,130); txt = s6m_rgb(230,230,230);
	hlbg = s6m_rgb(40,90,170); lbg = s6m_rgb(28,28,40); dis = s6m_rgb(120,120,120); chk = s6m_rgb(255,230,120);

	s6m_layout();
	s6m_rect(0,0,gdd.x,S6M_BARH-1,bar); s6m_rect(0,S6M_BARH-1,gdd.x,S6M_BARH,line);
	for(i=s6m_rootfirst;i>=0;i=s6m_it[i].next)
	{
		s6m_barcell(i,&x0,&x1);
		hl = ((s6m_active) && (s6m_sel[0] == i));
		if (hl) s6m_rect(x0,0,x1,S6M_BARH-1,hlbg);
		print6x8(x0+6,1,hl ? whitecol : txt,-1L,"%s",s6m_it[i].label);
		if (s6m_it[i].mpos >= 0) s6m_rect(x0+6+s6m_it[i].mpos*6,8,x0+12+s6m_it[i].mpos*6,9,hl ? whitecol : txt);
	}
	for(k=1;k<=s6m_depth;k++)
	{
		s6m_rect(s6m_bx0[k],s6m_by0[k],s6m_bx1[k],s6m_by1[k],line);
		s6m_rect(s6m_bx0[k]+1,s6m_by0[k]+1,s6m_bx1[k]-1,s6m_by1[k]-1,lbg);
		y = s6m_by0[k]+1;
		for(c=s6m_it[s6m_sel[k-1]].first;c>=0;c=s6m_it[c].next)
		{
			rh = s6m_rowh(c);
			if (s6m_it[c].flags & MF_SEPARATOR) { s6m_rect(s6m_bx0[k]+3,y+2,s6m_bx1[k]-3,y+3,line); y += rh; continue; }
			hl = (s6m_sel[k] == c);
			if (hl) s6m_rect(s6m_bx0[k]+1,y,s6m_bx1[k]-1,y+rh,hlbg);
			if (s6m_it[c].checked) s6m_rect(s6m_bx0[k]+4,y+3,s6m_bx0[k]+8,y+7,chk);
			print6x8(s6m_bx0[k]+12,y+1,s6m_disabled(c) ? dis : (hl ? whitecol : txt),-1L,"%s",s6m_it[c].label);
			if (s6m_it[c].mpos >= 0) s6m_rect(s6m_bx0[k]+12+s6m_it[c].mpos*6,y+8,s6m_bx0[k]+18+s6m_it[c].mpos*6,y+9,hl ? whitecol : txt);
			if (s6m_it[c].hint[0]) print6x8(s6m_bx1[k]-(long)strlen(s6m_it[c].hint)*6-12,y+1,hl ? whitecol : dis,-1L,"%s",s6m_it[c].hint);
			if (s6m_ispopup(c)) print6x8(s6m_bx1[k]-9,y+1,hl ? whitecol : txt,-1L,">");
			y += rh;
		}
	}
	if ((s6m_active) || (cy < S6M_BARH)) //the editor's own crosshair would be hidden under the menu
		for(i=-4;i<=4;i++)
		{
			if (((unsigned long)(cx+i) < (unsigned long)gdd.x) && ((unsigned long)cy < (unsigned long)gdd.y)) *(char *)(gdd.f+cy*gdd.p+cx+i) = whitecol;
			if (((unsigned long)cx < (unsigned long)gdd.x) && ((unsigned long)(cy+i) < (unsigned long)gdd.y)) *(char *)(gdd.f+(cy+i)*gdd.p+cx) = whitecol;
		}
}

#endif
