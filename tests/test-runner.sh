#!/bin/bash
HUD_ENABLE_APPSTACK=1
export HUD_ENABLE_APPSTACK
DIR="$1"
export PATH="$2:$PATH"
TEST_CASE="$3"
REPORT_FILE="$4"
if [ "x$5" == "xOFF" ]; then
HUD_DISABLE_VOICE=1
fi
. "${DIR}/run-xvfb.sh"
gtester --verbose -k -o "${REPORT_FILE}" "${TEST_CASE}"
RESULT=$?
which gtester2xunit && gtester2xunit "${REPORT_FILE}"
exit $RESULT