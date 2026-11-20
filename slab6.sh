#!/bin/sh
# SLAB6 launcher (Flatpak). Started without arguments it begins in Documents, so the models and the F12
# screenshots (SL6D0000.PCX ...) you save end up somewhere you can reach. With a model on the command line
# the folder is left alone (relative paths keep working).
export SDL_VIDEO_X11_WMCLASS=advsys.net.Slab6 SDL_APP_ID=advsys.net.Slab6
here=$(cd "$(dirname "$0")" && pwd) #absolute, because we may change directory below
if [ $# -eq 0 ]; then
	for d in "$HOME/Documents" "$HOME"; do [ -d "$d" ] && [ -w "$d" ] && cd "$d" && break; done
fi
exec "$here/slab6-bin" "$@"
