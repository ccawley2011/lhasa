#
# Copyright (c) 2011, 2012, Simon Howard
#
# Permission to use, copy, modify, and/or distribute this software
# for any purpose with or without fee is hereby granted, provided
# that the above copyright notice and this permission notice appear
# in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
# WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
# AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
# CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
# LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
# NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
# CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
#
#
# This file contains common definitions for use across the
# different tests.


# Fail when a command fails:

set -eu

# Location of tests and the test-build version of the lha tool:

test_base="$PWD"

# Wrapper command to invoke the test-lha tool and print an error
# if the command exits with a failure.

test_lha() {
	if ! "$test_base/../src/test-lha" "$@"; then
		fail "Command failed: lha $*"
	fi
}

# These options are used to run the Unix LHA tool to gather output
# for comparison. When GATHER=true, canonical output is gathered.

LHA_TOOL=/usr/bin/lha
GATHER=false
#GATHER=true

# Exit with failure with an error message.

fail() {
	for d in "$@"; do
		echo "$d"
	done >&2
	exit 1
}

# There doesn't seem to be a portable way to do this in shell. Sigh.

if stat -c "%Y" / >/dev/null 2>&1; then
	STAT_COMMAND_TYPE="GNU"
else
	STAT_COMMAND_TYPE="BSD"
fi

# Read file modification time.

file_mod_time() {
	if [ $STAT_COMMAND_TYPE = "GNU" ]; then
		stat -c "%Y" "$@"
	else
		stat -f "%m" "$@"
	fi
}

# Read file permissions.

file_perms() {
	if [ $STAT_COMMAND_TYPE = "GNU" ]; then
		stat -c "%a" "$@"
	else
		stat -f "%p" "$@"
	fi
}
