#!/bin/sh
# Adds Shattered Stars and its setup program to your desktop's application menu, for you
# only (no root): two .desktop files in ~/.local/share/applications and the icon in
# ~/.local/share/icons/hicolor (or under $XDG_DATA_HOME). They point at the programs in this
# folder, so run it again if you move the folder. Optional: the game runs without it, but
# GNOME and KDE Plasma on Wayland show a program's name and icon in the taskbar only when
# its .desktop file is installed.
#
#   ./install-desktop-entry.sh               install (or update)
#   ./install-desktop-entry.sh --uninstall   remove what it installed
#
# Nothing else is changed: settings, memory cards and mods stay in ~/.config/shattered-stars.
set -eu

here=$(cd "$(dirname "$0")" && pwd)
data=${XDG_DATA_HOME:-$HOME/.local/share}
id=io.github.IronJade.ShatteredStars
apps="$data/applications"
icons="$data/icons/hicolor"
sizes="16 24 32 48 64 128 256"

refresh() {
  # Desktops watch these folders, so this is only a nudge. An icon cache is updated only
  # where one already exists (a new one would go stale when other programs add icons).
  command -v update-desktop-database > /dev/null 2>&1 && update-desktop-database -q "$apps" 2> /dev/null || true
  if [ -f "$icons/icon-theme.cache" ] && command -v gtk-update-icon-cache > /dev/null 2>&1; then
    gtk-update-icon-cache -q -t -f "$icons" 2> /dev/null || true
  elif [ -d "$icons" ]; then
    touch "$icons"
  fi
}

if [ "${1:-}" = --uninstall ]; then
  rm -f "$apps/$id.desktop" "$apps/$id.Setup.desktop" "$icons/scalable/apps/$id.svg"
  for size in $sizes; do rm -f "$icons/${size}x$size/apps/$id.png"; done
  refresh
  echo "Removed Shattered Stars from the application menu."
  exit 0
elif [ $# -gt 0 ]; then
  sed -n '2,12s/^# \{0,1\}//p' "$0"
  exit 2
fi

for program in shattered-stars shattered-stars-setup; do
  [ -x "$here/$program" ] || { echo "error: $here/$program not found; run this from the unpacked folder" >&2; exit 1; }
done
case $here in
  *%*) echo "warning: the folder's path has a % in it; GNOME's menu can't start programs from such a path" >&2 ;;
esac

# A .desktop file's Exec= value: the path in double quotes, with ", `, $ and \ escaped for
# the Exec field, then \ escaped again for the file's string syntax, and % doubled.
exec_value() {
  printf '"%s"' "$(printf '%s' "$1" | sed -e 's/[\\"`$]/\\&/g' -e 's/\\/\\\\/g' -e 's/%/%%/g')"
}
# A plain string value (TryExec=, Path=): only \ is escaped.
string_value() {
  printf '%s' "$1" | sed -e 's/\\/\\\\/g'
}
# sed replacement text: escape \, & and the | delimiter.
sed_text() {
  printf '%s' "$1" | sed -e 's/[\\&|]/\\&/g'
}

mkdir -p "$apps"
for entry in "$id" "$id.Setup"; do
  src="$here/desktop/$entry.desktop"
  [ -f "$src" ] || { echo "error: $src not found" >&2; exit 1; }
  game=$(sed_text "$(exec_value "$here/shattered-stars")")
  setup=$(sed_text "$(exec_value "$here/shattered-stars-setup")")
  try_game=$(sed_text "$(string_value "$here/shattered-stars")")
  try_setup=$(sed_text "$(string_value "$here/shattered-stars-setup")")
  dir=$(sed_text "$(string_value "$here")")
  sed -e "s|^Exec=shattered-stars-setup\$|Exec=$setup|" \
      -e "s|^Exec=shattered-stars\$|Exec=$game|" \
      -e "s|^TryExec=shattered-stars-setup\$|TryExec=$try_setup|" \
      -e "s|^TryExec=shattered-stars\$|TryExec=$try_game|" \
      -e "/^TryExec=/a\\
Path=$dir" \
      "$src" > "$apps/$entry.desktop.tmp"
  chmod 644 "$apps/$entry.desktop.tmp"
  mv -f "$apps/$entry.desktop.tmp" "$apps/$entry.desktop"
done

for size in $sizes; do
  mkdir -p "$icons/${size}x$size/apps"
  cp -f "$here/desktop/icons/hicolor/${size}x$size/apps/$id.png" "$icons/${size}x$size/apps/$id.png"
done
mkdir -p "$icons/scalable/apps"
cp -f "$here/desktop/icons/hicolor/scalable/apps/$id.svg" "$icons/scalable/apps/$id.svg"
refresh
echo "Added Shattered Stars and Shattered Stars Setup to the application menu ($apps)."
