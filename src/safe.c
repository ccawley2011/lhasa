/*

Copyright (c) 2011, 2012, Simon Howard

Permission to use, copy, modify, and/or distribute this software
for any purpose with or without fee is hereby granted, provided
that the above copyright notice and this permission notice appear
in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR
CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

// Routines for safe terminal output.
//
// Data in LHA files (eg. filenames) may contain malicious string
// data. If printed carelessly, this can include terminal emulator
// commands that cause very unpleasant things to occur. For more
// information, see:
//
//  http://marc.info/?l=bugtraq&m=104612710031920&w=2
//
// Quote:
// > Many of the features supported by popular terminal emulator
// > software can be abused when un-trusted data is displayed on the
// > screen. The impact of this abuse can range from annoying screen
// > garbage to a complete system compromise.

// TODO: This file depends on vasprintf(), which is a non-standard
// function (_GNU_SOURCE above). Develop a compatible workaround
// for operating systems that don't have it.

// TODO: This may not be ideal behavior for handling files with
// names that contain Unicode characters.

// Version of printf() that strips out any potentially malicious
// characters from the outputted string.
// Note: all escape characters are considered potentially malicious,
// including newline characters.

int safe_printf(char *format, ...)
{
	va_list args;
	int result;
	unsigned char *str;
	unsigned char *p;

	va_start(args, format);
	result = vasprintf((char **) &str, format, args);

	for (p = str; *p != '\0'; ++p) {

		// Accept only plain ASCII characters.
		// Control characters (0x00-0x1f) are rejected,
		// as is 0x7f and all characters in the upper range.

		if (*p < 0x20 || *p >= 0x7f) {
			*p = '?';
		}
	}

	printf("%s", str);

	free(str);
	va_end(args);

	return result;
}
