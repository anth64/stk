#!/bin/sh

OS=$(uname -s | tr '[:upper:]' '[:lower:]')

if [ "$OS" = "linux" ]; then
	MK_FILE="gmake.mk"
else
	MK_FILE="bmake.mk"
fi

make -f "$MK_FILE" "$@"
