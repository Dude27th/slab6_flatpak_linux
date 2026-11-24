/***************************************************************************************************
S6FILECHOOSER.C - file chooser through the XDG Desktop Portal (Linux port of SLAB6)

PORT NOTICE: this file is NOT part of Ken Silverman's original SLAB6 release.

Inside a Flatpak sandbox there is no zenity and the app cannot see most of the disk, so the desktop's own
file chooser has to be reached through the portal (org.freedesktop.portal.FileChooser). The portal also
hands the app access to the chosen file (it shows up under /run/user/UID/doc/...). SLAB6 starts this helper
the way it starts zenity, so the command line is zenity's:

  slab6-filechooser [--save] [--title=TEXT] [--filename=PATH] [--file-filter="Name | *.a *.b"]... [other zenity options are ignored]

Prints the chosen path on stdout. Exit status: 0 chosen, 1 cancelled, 2 the portal is not available / failed.
Build: gcc s6filechooser.c $(pkg-config --cflags --libs gio-2.0) -o slab6-filechooser
***************************************************************************************************/
#include <gio/gio.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct { GMainLoop *loop; int result; char *path; } ctx_t;

	//---- remember the folder -----------------------------------------------------------------------------------
	//A file chosen outside the folders the sandbox can see comes back as /run/user/UID/doc/ID/name, a folder that only holds that one
	//file: useless as the place to start the next dialog in. So the helper keeps its own memory of the REAL (host) folder of the last
	//chosen file, asks the document portal (Documents.Info) for it when needed, and starts the next dialog there.
	//Small log of what the helper decides: <cache dir>/slab6/filechooser.log (kept under 20 KB)
static void dbg (const char *fmt, ...)
{
	char *f, *d, *line;
	FILE *fil;
	va_list ap;
	long sz;
	f = g_build_filename(g_get_user_cache_dir(),"slab6","filechooser.log",NULL); d = g_path_get_dirname(f);
	g_mkdir_with_parents(d,0700);
	if ((fil = fopen(f,"r")) != 0) { fseek(fil,0,SEEK_END); sz = ftell(fil); fclose(fil); if (sz > 20000) remove(f); }
	va_start(ap,fmt); line = g_strdup_vprintf(fmt,ap); va_end(ap);
	if ((fil = fopen(f,"a")) != 0) { fprintf(fil,"%s\n",line); fclose(fil); }
	g_free(line); g_free(d); g_free(f);
}

static int is_docpath (const char *path)
{
	return((!strncmp(path,"/run/flatpak/doc/",17)) || ((!strncmp(path,"/run/user/",10)) && (strstr(path,"/doc/") != 0))); //(the sandbox shows the same folder under both names)
}

static char *lastdir_file ()
{
	return(g_build_filename(g_get_user_cache_dir(),"slab6","filechooser-lastdir",NULL));
}

static char *read_lastdir ()
{
	char *f = lastdir_file(), *txt = 0;
	gsize n = 0;
	if ((g_file_get_contents(f,&txt,&n,0)) && (txt))
	{
		g_strstrip(txt);
		if (txt[0] == '/') { g_free(f); return(txt); }
		g_free(txt);
	}
	g_free(f);
	return(0);
}

static void save_lastdir (const char *dir)
{
	char *f = lastdir_file(), *d = g_path_get_dirname(f);
	if ((dir) && (dir[0] == '/') && (!is_docpath(dir)))
	{
		g_mkdir_with_parents(d,0700);
		g_file_set_contents(f,dir,-1,0);
	}
	g_free(d); g_free(f);
}

	//Folder (as the host sees it) of a chosen file; 0 if it can't be found out.
	//Documents.Info is refused inside the sandbox ("Not allowed in sandbox"). Documents.GetHostPaths takes an array of strings (the
	//portal reported its signature as "(as)"), so it is asked with the document id, then with the whole path. The answer is a dictionary
	//whose value is the host path, either a string or a byte string: both are accepted.
static const char *first_host_path (GVariant *r, char **keep)
{
	GVariant *dict, *entry, *v, *in;
	GVariantIter it;
	const char *hp = 0;

	(*keep) = 0;
	if ((!r) || (g_variant_n_children(r) < 1)) return(0);
	dict = g_variant_get_child_value(r,0);
	g_variant_iter_init(&it,dict);
	if ((entry = g_variant_iter_next_value(&it)) != 0)
	{
		v = g_variant_get_child_value(entry,1);
		in = g_variant_is_of_type(v,G_VARIANT_TYPE_VARIANT) ? g_variant_get_variant(v) : g_variant_ref(v);
		if (g_variant_is_of_type(in,G_VARIANT_TYPE_STRING)) { (*keep) = g_strdup(g_variant_get_string(in,0)); hp = (*keep); }
		else if (g_variant_is_of_type(in,G_VARIANT_TYPE_BYTESTRING)) { (*keep) = g_strdup(g_variant_get_bytestring(in)); hp = (*keep); }
		g_variant_unref(in); g_variant_unref(v); g_variant_unref(entry);
	}
	g_variant_unref(dict);
	return(hp);
}

static char *host_dir_of (GDBusConnection *conn, const char *path)
{
	const char *p, *e, *hp = 0;
	char *id, *keep = 0, *dir = 0;
	GVariant *r;
	GError *err = 0;
	int attempt;

	if (!is_docpath(path)) return(g_path_get_dirname(path));
	p = strstr(path,"/doc/")+5; //.../doc/ID/name  or  .../doc/by-app/APPID/ID/name
	if (!strncmp(p,"by-app/",7)) { p = strchr(p+7,'/'); if (!p) return(0); p++; }
	e = strchr(p,'/');
	id = e ? g_strndup(p,(gsize)(e-p)) : g_strdup(p);
	for(attempt=0;(attempt < 2) && (!dir);attempt++)
	{
		const char *arg = attempt ? path : id;
		GVariantBuilder ab;
		g_variant_builder_init(&ab,G_VARIANT_TYPE("as"));
		g_variant_builder_add(&ab,"s",arg);
		err = 0;
		r = g_dbus_connection_call_sync(conn,"org.freedesktop.portal.Documents","/org/freedesktop/portal/documents",
			"org.freedesktop.portal.Documents","GetHostPaths",g_variant_new("(as)",&ab),0,G_DBUS_CALL_FLAGS_NONE,2000,0,&err);
		if (!r) { dbg("Documents.GetHostPaths(%s) failed: %s",arg,err ? err->message : "?"); if (err) g_error_free(err); continue; }
		dbg("Documents.GetHostPaths(%s) answered %s",arg,g_variant_get_type_string(r));
		hp = first_host_path(r,&keep);
		dbg("Documents.GetHostPaths(%s) host path: %s",arg,hp ? hp : "(none)");
		if ((hp) && (hp[0] == '/')) dir = g_path_get_dirname(hp);
		g_free(keep); keep = 0;
		g_variant_unref(r);
	}
	g_free(id);
	return(dir);
}

static void on_response (GDBusConnection *c, const gchar *sender, const gchar *path, const gchar *iface,
								 const gchar *signal, GVariant *params, gpointer data)
{
	ctx_t *x = (ctx_t *)data;
	guint32 code = 2;
	GVariant *res = 0;
	(void)c; (void)sender; (void)path; (void)iface; (void)signal;

	g_variant_get(params,"(u@a{sv})",&code,&res);
	if (code == 0)
	{
		GVariant *uris = g_variant_lookup_value(res,"uris",G_VARIANT_TYPE("as"));
		x->result = 2;
		if ((uris) && (g_variant_n_children(uris) > 0))
		{
			const gchar *u = 0;
			g_variant_get_child(uris,0,"&s",&u);
			x->path = g_filename_from_uri(u,0,0);
			if (x->path) x->result = 0;
		}
		if (uris) g_variant_unref(uris);
	}
	else x->result = (code == 1) ? 1 : 2; //1: the user cancelled
	if (res) g_variant_unref(res);
	g_main_loop_quit(x->loop);
}

	//"Name | *.a *.b" -> ("Name", [(0,"*.a"),(0,"*.b")])
static GVariant *make_filter (const char *spec)
{
	char *copy = g_strdup(spec), *bar = strchr(copy,'|'), *pats, *save = 0, *tok, *name;
	GVariantBuilder pb;
	GVariant *v;

	if (bar) { *bar = 0; pats = bar+1; name = g_strstrip(copy); } else { pats = copy; name = (char *)"Files"; }
	g_variant_builder_init(&pb,G_VARIANT_TYPE("a(us)"));
	for(tok=strtok_r(pats," \t",&save);tok;tok=strtok_r(0," \t",&save))
		g_variant_builder_add(&pb,"(us)",0u,tok); //0 = glob pattern
	v = g_variant_ref_sink(g_variant_new("(sa(us))",name,&pb));
	g_free(copy);
	return(v);
}

int main (int argc, char **argv)
{
	int i, save = 0, nfilters = 0;
	const char *title = "Select a file", *filename = "";
	GVariant *filters[16];
	GVariantBuilder opts, fl;
	GError *err = 0;
	GDBusConnection *conn;
	char *dir, *base, *me, *p, req_path[512], token[64];
	const char *sender;
	GVariant *reply;
	ctx_t ctx;
	guint sub;
	const char *handle = 0;

	for(i=1;i<argc;i++)
	{
		if (!strcmp(argv[i],"--save")) save = 1;
		else if (!strncmp(argv[i],"--title=",8)) title = &argv[i][8];
		else if (!strncmp(argv[i],"--filename=",11)) filename = &argv[i][11];
		else if ((!strncmp(argv[i],"--file-filter=",14)) && (nfilters < 16)) filters[nfilters++] = make_filter(&argv[i][14]);
	}

	conn = g_bus_get_sync(G_BUS_TYPE_SESSION,0,&err);
	if (!conn) { fprintf(stderr,"slab6-filechooser: no session bus: %s\n",err ? err->message : "?"); return(2); }

	sender = g_dbus_connection_get_unique_name(conn); //":1.42" -> "1_42" as the portal builds the request path
	me = g_strdup(sender && sender[0] == ':' ? sender+1 : (sender ? sender : "0"));
	for(p=me;*p;p++) if (*p == '.') *p = '_';
	snprintf(token,sizeof(token),"slab6_%d",(int)getpid());
	snprintf(req_path,sizeof(req_path),"/org/freedesktop/portal/desktop/request/%s/%s",me,token);

	g_variant_builder_init(&opts,G_VARIANT_TYPE_VARDICT);
	g_variant_builder_add(&opts,"{sv}","handle_token",g_variant_new_string(token));
	g_variant_builder_add(&opts,"{sv}","modal",g_variant_new_boolean(TRUE));
	if (nfilters > 0)
	{
		g_variant_builder_init(&fl,G_VARIANT_TYPE("a(sa(us))"));
		for(i=0;i<nfilters;i++) g_variant_builder_add_value(&fl,filters[i]);
		g_variant_builder_add(&opts,"{sv}","filters",g_variant_builder_end(&fl));
		g_variant_builder_add(&opts,"{sv}","current_filter",filters[0]);
	}
	dir = g_path_get_dirname(filename[0] ? filename : "/");
	base = g_path_get_basename(filename[0] ? filename : "");
	if ((filename[0]) && (filename[strlen(filename)-1] == '/')) //a folder: start there, no file name
	{
		size_t n;
		g_free(dir); dir = g_strdup(filename); base[0] = 0;
		for(n=strlen(dir);(n > 1) && (dir[n-1] == '/');n--) dir[n-1] = 0;
	}
	{
		char *startdir = 0;
		if ((dir[0] == '/') && (save || filename[0]) && (!is_docpath(dir))) startdir = g_strdup(dir); //a real folder from SLAB6
		else startdir = read_lastdir(); //none, or a document portal folder: use the real folder of the last chosen file instead
		dbg("%s: filename=%s start folder=%s",save ? "save" : "open",filename,startdir ? startdir : "(none)");
		if (startdir) { g_variant_builder_add(&opts,"{sv}","current_folder",g_variant_new_bytestring(startdir)); g_free(startdir); }
	}
	if ((save) && (base[0]) && (strcmp(base,"."))) g_variant_builder_add(&opts,"{sv}","current_name",g_variant_new_string(base));

	{
		GVariant *vr = g_dbus_connection_call_sync(conn,"org.freedesktop.portal.Desktop","/org/freedesktop/portal/desktop","org.freedesktop.DBus.Properties",
			"Get",g_variant_new("(ss)","org.freedesktop.portal.FileChooser","version"),G_VARIANT_TYPE("(v)"),G_DBUS_CALL_FLAGS_NONE,1000,0,0);
		if (vr) { GVariant *in = 0; g_variant_get(vr,"(v)",&in); dbg("FileChooser portal version %u",(unsigned)g_variant_get_uint32(in)); g_variant_unref(in); g_variant_unref(vr); }
		else dbg("FileChooser portal version: unknown");
	}
	ctx.loop = g_main_loop_new(0,FALSE); ctx.result = 2; ctx.path = 0;
	sub = g_dbus_connection_signal_subscribe(conn,"org.freedesktop.portal.Desktop","org.freedesktop.portal.Request","Response",
		req_path,0,G_DBUS_SIGNAL_FLAGS_NONE,on_response,&ctx,0);

	reply = g_dbus_connection_call_sync(conn,"org.freedesktop.portal.Desktop","/org/freedesktop/portal/desktop",
		"org.freedesktop.portal.FileChooser",save ? "SaveFile" : "OpenFile",
		g_variant_new("(ssa{sv})","",title,&opts),G_VARIANT_TYPE("(o)"),G_DBUS_CALL_FLAGS_NONE,-1,0,&err);
	if (!reply) { fprintf(stderr,"slab6-filechooser: portal call failed: %s\n",err ? err->message : "?"); return(2); }

	g_variant_get(reply,"(&o)",&handle);
	if ((handle) && (strcmp(handle,req_path))) //older portals: listen on the path they gave us
		g_dbus_connection_signal_subscribe(conn,"org.freedesktop.portal.Desktop","org.freedesktop.portal.Request","Response",
			handle,0,G_DBUS_SIGNAL_FLAGS_NONE,on_response,&ctx,0);

	g_main_loop_run(ctx.loop);
	g_dbus_connection_signal_unsubscribe(conn,sub);

	if (ctx.result == 0)
	{
		char *hd;
		printf("%s\n",ctx.path); fflush(stdout);
		hd = host_dir_of(conn,ctx.path); //remember where the file really is, for the next dialog
		dbg("chosen %s, remembered folder %s",ctx.path,hd ? hd : "(none)");
		if (hd) { save_lastdir(hd); g_free(hd); }
	}
	return(ctx.result);
}
