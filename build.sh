#!/bin/sh

OS=$(uname -s | tr '[:upper:]' '[:lower:]')

if [ "$OS" = "linux" ]; then
	MK_FILE="gmake.mk"
else
	MK_FILE="bmake.mk"
fi

HAS_INSTALL=0
HAS_UNINSTALL=0
for arg in "$@"; do
	case "$arg" in
		install)   HAS_INSTALL=1 ;;
		uninstall) HAS_UNINSTALL=1 ;;
	esac
done

if [ "$HAS_INSTALL" = "1" ] || [ "$HAS_UNINSTALL" = "1" ]; then
	if [ "$HAS_INSTALL" = "1" ]; then
		make -f "$MK_FILE" release
	fi

	if [ "$(id -u)" = "0" ]; then
		PRIV=""
	elif command -v doas > /dev/null 2>&1; then
		PRIV="doas"
	elif command -v sudo > /dev/null 2>&1; then
		PRIV="sudo"
	else
		echo "error: install requires root. neither doas nor sudo found." >&2
		exit 1
	fi

	$PRIV make -f "$MK_FILE" "$@"
else
	make -f "$MK_FILE" "$@"
fi
