#!/bin/sh

DISPLAY=`grep 'global DISPLAY=' test.pluxinc| sed 's/.*=//' | sed 's/]//'`
PEKWM_CONFIG_FILE=$PWD/config/pekwm.config.sys
export DISPLAY PEKWM_CONFIG_FILE
$VALGRIND ../../build/src/sys/pekwm_sys --log-level trace
