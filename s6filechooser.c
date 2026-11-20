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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct { GMainLoop *loop; int result; char *path; } ctx_t;

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
	if ((dir[0] == '/') && (save || filename[0])) g_variant_builder_add(&opts,"{sv}","current_folder",g_variant_new_bytestring(dir));
	if ((save) && (base[0]) && (strcmp(base,"."))) g_variant_builder_add(&opts,"{sv}","current_name",g_variant_new_string(base));

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

	if (ctx.result == 0) { printf("%s\n",ctx.path); fflush(stdout); }
	return(ctx.result);
}
