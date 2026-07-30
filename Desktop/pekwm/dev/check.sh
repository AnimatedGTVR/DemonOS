#!/bin/sh
#
# Run through code formatting, unit and system level checks. This should bne
# run before committing code.
#

abort()
{
	echo "$@"
	exit 1
}

top="$(cd "`dirname $0`"/.. && pwd)"

"$top/dev/code_check" "$top/src" "$top/src/lib" "$top/src/tk" "$top/src/wm" \
	"$top/test" "$top/test/system" \
	|| abort "code_check failed"

if test -d "$top/build/test"; then
	(cd "$top/build" ; ctest) || abort "unit test(s) failed"
else
	make -C "$top/test" test || abort "unit test(s) failed"
fi

(cd "$top/test/system" ; plux *.plux) || abort "system test(s) failed"
