#!/bin/bash
DIR="$1"
export PATH="$2:$PATH"
TEST_CASE="$3"
REPORT_FILE="$4"
cd "${DIR}"
. run-xvfb.sh
gtester --verbose -k -o "${REPORT_FILE}" "${TEST_CASE}"
RESULT=$?
gtester2xunit "${REPORT_FILE}"
exit $RESULT
