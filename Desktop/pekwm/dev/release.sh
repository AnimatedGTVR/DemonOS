#!/bin/sh
#
# Script used to check and generate a release.
#

BUILD_PARALLEL="-j4"
dir=$(cd `dirname $0`/.. ; pwd)
if test "x$1" = "xfossil"; then
	FOSSIL="yes"
fi

fail() {
	echo `date +'%Y-%m-%d %H:%M:%S'` "ERROR: $@"
	exit 1
}

progress() {
	echo `date +'%Y-%m-%d %H:%M:%S'` $@
}

progress "verify autotools and cmake versions are the same"
at_version=`awk '/AC_INIT/ { print $2 }' < "$dir/configure.ac" | sed 's/[^0-9.]//g'`
cm_version=`grep 'set(pekwm.*VERSION_' "$dir/CMakeLists.txt" | sed 's/.* //'| sed 's/)//' | tr '\n' '.' | sed 's/\.$//'`
if test "x$at_version" != "x$cm_version"; then
	fail "at_version $at_version does not match cm_version $cm_version"
fi

progress "verify NEWS.md contains latest version"
head -1 "$dir/NEWS.md" | grep "pekwm-$at_version" >/dev/null
if test $? -ne 0; then
	fail "NEWS.md version does not match $at_version"
fi

progress "run code formatting, unit and system tests"
$dir/dev/check.sh >"$dir/release.log" 2>&1
if test $? -ne 0; then
	fail "dev/check.sh failed"
fi

progress "generate distribution tarball"
make dist >>"$dir/release.log" 2>&1
if test $? -ne 0; then
	fail "failed to generate distribution tarball"
fi

progress "clean out reltest files"
rm -rf reltest || fail "failed to remove existing reltest dirs"
mkdir -p reltest/at reltest/cm || fail "failed to create build dirs"

progress "build release using autotools"
cd "$dir/reltest/at" || fail "failed to enter reltest/at"
tar xzf "$dir/pekwm-${at_version}.tar.gz" || fail "failed to extract release"
cd pekwm-${at_version} || fail "failed to enter release directory"
./configure --prefix="$dir/reltest/at-install" \
	>>"$dir/release.log" 2>&1 || fail "failed to configure"
make $BUILD_PARALLEL >>"$dir/release.log" 2>&1 || "failed to build"
make install >>"$dir/release.log" 2>&1 || fail "failed to install"
"$dir/reltest/at-install/bin/pekwm_wm" --standalone --version \
	> "$dir/reltest/at.features"
grep "pekwm: version ${at_version}" "$dir/reltest/at.features"
if test $? -ne 0; then
	fail "failed to verify autotools pekwm version"
fi

progress "build release using cmake"
cd "$dir/reltest/cm" || fail "failed to enter reltest/cm"
tar xzf "$dir/pekwm-${at_version}.tar.gz" || fail "failed to extract release"
cd pekwm-${at_version} || fail "failed to enter release directory"
mkdir build || fail "failed to create build directory"
cd build || fail "failed to enter build directory"
cmake -G Ninja -DCMAKE_INSTALL_PREFIX="$dir/reltest/cm-install" .. \
	>>"$dir/release.log" 2>&1 || fail "failed to configure"
ninja >>"$dir/release.log" 2>&1 || fail "failed to build"
ninja install >>"$dir/release.log" 2>&1 || fail "failed to install"
"$dir/reltest/cm-install/bin/pekwm_wm" --standalone --version \
	> "$dir/reltest/cm.features"
grep "pekwm: version ${at_version}" "$dir/reltest/cm.features"
if test $? -ne 0; then
	fail "failed to verify cmake pekwm version"
fi

progress "verify build pekwm has same features"
diff -u "$dir/reltest/at.features" "$dir/reltest/cm.features"
if test $? -ne 0; then
	fail "failed to verify pekwm features"
fi

progress "verify installation contains same files"
find "$dir/reltest/at-install" |sed 's@.*reltest/at-install@@' \
	| sort > "$dir/reltest/at.files"
find "$dir/reltest/cm-install" |sed 's@.*reltest/cm-install@@' \
	| sort > "$dir/reltest/cm.files"
diff -u "$dir/reltest/at.files" "$dir/reltest/cm.files" \
	|| fail "installs are not equal"

if test "x$FOSSIL" = "xyes"; then
	fossil commit -m "Set version ${at_version}" \
		--tag release-${at_version} \
		|| fail "failed to create release tag"
	fossil unversioned add pekwm-${at_version}.tar.gz \
		|| fail "failed to add pekwm release tarball"
	fossil unversioned sync \
		|| fail "failed to sync unversioned files"
fi
