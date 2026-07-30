#!/bin/sh

ROOT=$(cd `dirname $0`/.. && pwd)
ENUM_NAMES="$ROOT/dev/enum_names.py"

# lib
$ENUM_NAMES lib "$ROOT/src/lib/LibNames.cc" "$ROOT/src/lib/LibNames.hh" \
	"$ROOT/src/lib/pekwm_types.hh" "$ROOT/src/lib/Debug.hh" \
	"$ROOT/src/lib/X11.hh"
# tk
$ENUM_NAMES tk "$ROOT/src/tk/TkNames.cc" "$ROOT/src/tk/TkNames.hh" \
	"$ROOT/src/tk/Action.hh" "$ROOT/src/tk/ColorPalette.hh" \
	"$ROOT/src/tk/PFont.hh"

# wm

$ENUM_NAMES wm "$ROOT/src/wm/WmNames.cc" "$ROOT/src/wm/WmNames.hh" \
	"$ROOT/src/wm/AutoProperties.hh" "$ROOT/src/wm/WinLayouter.hh"

