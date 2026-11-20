#!/bin/sh
set -e
cd "$(dirname "$0")"

# No ejecutar como root: flatpak --user necesita la sesión del usuario.
if [ "$(id -u)" = "0" ]; then
  echo "ERROR: no ejecutes este script con sudo/root; hazlo como tu usuario normal." >&2
  exit 1
fi


# Sin bus D-Bus de sesión (SSH, TTY, terminal sin entorno gráfico), crea uno temporal.
if [ -z "$DBUS_SESSION_BUS_ADDRESS" ]; then
  if command -v dbus-run-session >/dev/null 2>&1; then
    exec dbus-run-session -- "$0" "$@"
  else
    echo "AVISO: no hay sesión D-Bus y falta dbus-run-session (paquete dbus-daemon / dbus-x11)." >&2
  fi
fi
RT="${RT_VERSION:-25.08}"   # versión del runtime; puedes usar: RT_VERSION=24.08 ./build.sh

flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install -y --user flathub "org.freedesktop.Platform//$RT" "org.freedesktop.Sdk//$RT"

echo "--- Runtimes instalados ---"
flatpak list --user --runtime | grep -i "freedesktop" || true

# Usa el runtime elegido sin editar el manifiesto original
sed "s/runtime-version: '.*'/runtime-version: '$RT'/" advsys.net.Slab6.yml > .manifest-build.yml


BUILDER="flatpak-builder"


$BUILDER --user --install-deps-from=flathub --force-clean --repo=repo build-dir .manifest-build.yml
flatpak build-bundle repo Slab6.flatpak advsys.net.Slab6
rm -f .manifest-build.yml
echo "Listo: Slab6.flatpak (instalar con: flatpak install --user Slab6.flatpak)"
