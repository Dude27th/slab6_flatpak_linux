/***************************************************************************************************
S6WIN.H - emulation of the few Win32 calls SLAB6 uses outside the system layer (Linux port, step 4)

PORT NOTICE: this file is NOT part of Ken Silverman's original SLAB6 release. slab6.c uses a small
part of the Windows API directly: file enumeration (FindFirstFile...), the current directory,
MessageBox, and a few types. They are emulated here with POSIX/SDL so the program code stays
unchanged. The bigger Windows-only pieces (menu bar, native file dialogs, KVX type dialog,
clipboard bitmap) are not emulated: slab6.c has #ifdef _WIN32 / #else branches for them.
Credit for SLAB6 belongs to Ken Silverman.
***************************************************************************************************/
#ifndef S6WIN_H
#define S6WIN_H

#include "s6long.h"
#include <dirent.h>
#include <fnmatch.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

	//Types and macros -------------------------------------------------------------------------------
typedef intptr_t WPARAM, LPARAM, LRESULT;
typedef unsigned int UINT;
typedef int BOOL;
typedef void *HWND, *HINSTANCE, *HMENU, *HANDLE;
#define CALLBACK
#define LOWORD(l) ((unsigned short)(((unsigned long)(l))&0xffff))
#define HIWORD(l) ((unsigned short)((((unsigned long)(l))>>16)&0xffff))
#define S6_PATHSEP '/'
#define S6_PATHSEPSTR "/"

	//Messages ---------------------------------------------------------------------------------------
#define MB_OK 0
#define MB_OKCANCEL 1
#define MB_YESNOCANCEL 3
#define MB_YESNO 4
#define IDOK 1
#define IDCANCEL 2
#define IDYES 6
#define IDNO 7
#define ghwnd ((HWND)0)
#define MessageBox(hwnd,text,caption,type) s6_messagebox((text),(caption),(type))
extern int s6_messagebox (const char *text, const char *caption, int type); //sdlmain.cpp
	//Three-button choice with your own labels. Returns 0,1,2 for the button, -1 if there was no dialog.
extern int s6_choose3 (const char *caption, const char *text, const char *b0, const char *b1, const char *b2);

	//Menu bar (s6menu.h) --------------------------------------
#define MF_GRAYED 0x1
#define MF_DISABLED 0x2
#define MF_CHECKED 0x8
#define MF_POPUP 0x10
#define MF_END 0x80
#define MF_SEPARATOR 0x800
extern void s6m_check (long id, int checked); //slab6.c (s6menu.h)
#define CheckMenuItem(menu,id,flags) s6m_check((id),(((flags)&MF_CHECKED) != 0))
#define S6M_TOPY 10 //first row below the menu bar

	//Current directory ------------------------------------------------------------------------------
static inline int GetCurrentDirectory (unsigned long size, char *buf)
{
	if (!getcwd(buf,size)) { if (size) buf[0] = 0; return(0); }
	return((int)strlen(buf));
}
static inline int SetCurrentDirectory (const char *path) { return(chdir(path) == 0); }

	//File enumeration (always lists the current directory, as SLAB6 uses it) ------------------------
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define FILE_ATTRIBUTE_HIDDEN 0x2
#define FILE_ATTRIBUTE_DIRECTORY 0x10
typedef struct { unsigned long dwFileAttributes; char cFileName[MAX_PATH]; } WIN32_FIND_DATA;

typedef struct { DIR *d; char pat[256]; } s6find_t;

static inline int s6find_next (s6find_t *f, WIN32_FIND_DATA *fd)
{
	struct dirent *e;
	struct stat st;
	while ((e = readdir(f->d)) != 0)
	{
		if (strcmp(f->pat,"*.*")) //Win32's "*.*" means everything, fnmatch's would need a dot
			if (fnmatch(f->pat,e->d_name,FNM_CASEFOLD)) continue;
		strncpy(fd->cFileName,e->d_name,MAX_PATH-1); fd->cFileName[MAX_PATH-1] = 0;
		fd->dwFileAttributes = 0;
		if ((stat(e->d_name,&st) == 0) && (S_ISDIR(st.st_mode))) fd->dwFileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
		if ((e->d_name[0] == '.') && (strcmp(e->d_name,".."))) fd->dwFileAttributes |= FILE_ATTRIBUTE_HIDDEN; //dotfiles
		return(1);
	}
	return(0);
}
static inline HANDLE FindFirstFile (const char *pattern, WIN32_FIND_DATA *fd)
{
	s6find_t *f = (s6find_t *)malloc(sizeof(s6find_t));
	if (!f) return(INVALID_HANDLE_VALUE);
	strncpy(f->pat,pattern,sizeof(f->pat)-1); f->pat[sizeof(f->pat)-1] = 0;
	f->d = opendir(".");
	if ((!f->d) || (!s6find_next(f,fd))) { if (f->d) closedir(f->d); free(f); return(INVALID_HANDLE_VALUE); }
	return((HANDLE)f);
}
static inline int FindNextFile (HANDLE h, WIN32_FIND_DATA *fd) { return(s6find_next((s6find_t *)h,fd)); }
static inline int FindClose (HANDLE h) { s6find_t *f = (s6find_t *)h; closedir(f->d); free(f); return(1); }

#endif
