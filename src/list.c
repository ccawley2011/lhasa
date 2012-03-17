/*

Copyright (c) 2011, Simon Howard

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include <unistd.h>
#include <sys/stat.h>

#include "lha_reader.h"
#include "filter.h"

static float compression_percent(size_t compressed, size_t uncompressed)
{
	float factor;

	if (uncompressed > 0) {
		factor = (float) compressed / (float) uncompressed;
	} else {
		factor = 1.0f;
	}

	return factor * 100.0f;
}

typedef struct
{
	unsigned int num_files;
	unsigned int compressed_length;
	unsigned int length;
	unsigned int timestamp;
} FileStatistics;

typedef struct
{
	char *name;
	unsigned int width;
	void (*handler)(LHAFileHeader *header);
	void (*footer)(FileStatistics *stats);
} ListColumn;

// Display OS type:

static char *os_type_to_string(uint8_t os_type)
{
	switch (os_type) {
		case LHA_OS_TYPE_MSDOS:
			return "[MS-DOS]";
		case LHA_OS_TYPE_WIN95:
			return "[Win9x]";
		case LHA_OS_TYPE_WINNT:
			return "[WinNT]";
		case LHA_OS_TYPE_UNIX:
			return "[Unix]";
		case LHA_OS_TYPE_OS2:
			return "[OS/2]";
		case LHA_OS_TYPE_CPM:
			return "[CP/M]";
		case LHA_OS_TYPE_MACOS:
			return "[Mac OS]";
		case LHA_OS_TYPE_JAVA:
			return "[Java]";
		case LHA_OS_TYPE_FLEX:
			return "[FLEX]";
		case LHA_OS_TYPE_RUNSER:
			return "[Runser]";
		case LHA_OS_TYPE_TOWNSOS:
			return "[TownsOS]";
		case LHA_OS_TYPE_OS9:
			return "[OS-9]";
		case LHA_OS_TYPE_OS9_68K:
			return "[OS-9/68K]";
		case LHA_OS_TYPE_OS386:
			return "[OS-386]";
		case LHA_OS_TYPE_HUMAN68K:
			return "[Human68K]";
		case LHA_OS_TYPE_ATARI:
			return "[Atari]";
		case LHA_OS_TYPE_UNKNOWN:
			return "[generic]";
		default:
			return "[unknown]";
	}
}

// File permissions

static void permission_column_print(LHAFileHeader *header)
{
	const char *perms = "drwxrwxrwx";
	unsigned int i;

	if ((header->extra_flags & LHA_FILE_UNIX_PERMS) == 0) {
		printf("%-10s", os_type_to_string(header->os_type));
		return;
	}

	for (i = 0; i < 10; ++i) {
		if (header->unix_perms & (1U << (9 - i))) {
			printf("%c", perms[i]);
		} else {
			printf("-");
		}
	}
}

static void permission_column_footer(FileStatistics *stats)
{
	printf(" Total    ");
}

static ListColumn permission_column = {
	" PERMSSN", 10,
	permission_column_print,
	permission_column_footer
};

// Unix UID/GID

static void unix_uid_gid_column_print(LHAFileHeader *header)
{
	if (header->extra_flags & LHA_FILE_UNIX_UID_GID) {
		printf("%5i/%-5i", header->unix_uid, header->unix_gid);
	} else {
		printf("           ");
	}
}

static void unix_uid_gid_column_footer(FileStatistics *stats)
{
	// The UID/GID column has the total number of files
	// listed below it.

	if (stats->num_files == 1) {
		printf("%5i file ", stats->num_files);
	} else {
		printf("%5i files", stats->num_files);
	}
}

static ListColumn unix_uid_gid_column = {
	" UID  GID", 11,
	unix_uid_gid_column_print,
	unix_uid_gid_column_footer
};

// Compressed file size

static void packed_column_print(LHAFileHeader *header)
{
	printf("%7lu", (unsigned long) header->compressed_length);
}

static void packed_column_footer(FileStatistics *stats)
{
	printf("%7lu", (unsigned long) stats->compressed_length);
}

static ListColumn packed_column = {
	" PACKED", 7,
	packed_column_print,
	packed_column_footer
};

// Uncompressed file size

static void size_column_print(LHAFileHeader *header)
{
	printf("%7lu", (unsigned long) header->length);
}

static void size_column_footer(FileStatistics *stats)
{
	printf("%7lu", (unsigned long) stats->length);
}

static ListColumn size_column = {
	"   SIZE", 7,
	size_column_print,
	size_column_footer
};

// Compression ratio

static void ratio_column_print(LHAFileHeader *header)
{
	if (!strcmp(header->compress_method, "-lhd-")) {
		printf("******");
	} else {
		printf("%5.1f%%", compression_percent(header->compressed_length,
		                                      header->length));
	}
}

static void ratio_column_footer(FileStatistics *stats)
{
	printf("%5.1f%%", compression_percent(stats->compressed_length,
	                                      stats->length));
}

static ListColumn ratio_column = {
	" RATIO", 6,
	ratio_column_print,
	ratio_column_footer
};

// Compression method and CRC checksum

static void method_crc_column_print(LHAFileHeader *header)
{
	printf("%-5s %04x", header->compress_method, header->crc);
}

static ListColumn method_crc_column = {
	"METHOD CRC", 10,
	method_crc_column_print
};

// File timestamp

static void output_timestamp(unsigned int timestamp)
{
	const char *months[] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};
	struct tm *ts;
	time_t tmp;

	if (timestamp == 0) {
		printf("            ");
		return;
	}

	tmp = (time_t) timestamp;
	ts = localtime(&tmp);

	// Print date:

	printf("%s %2d ", months[ts->tm_mon], ts->tm_mday);

	// If this is an old time (more than 6 months), print the year.
	// For recent timestamps, print the time.

	tmp = time(NULL);

	if ((time_t) timestamp > tmp - 6 * 30 * 24 * 60 * 60) {
		printf("%02i:%02i", ts->tm_hour, ts->tm_min);
	} else {
		printf(" %04i", ts->tm_year + 1900);
	}
}

static void timestamp_column_print(LHAFileHeader *header)
{
	output_timestamp(header->timestamp);
};

static void timestamp_column_footer(FileStatistics *stats)
{
	output_timestamp(stats->timestamp);
};

static ListColumn timestamp_column = {
	"    STAMP", 12,
	timestamp_column_print,
	timestamp_column_footer
};

// Filename

static void name_column_print(LHAFileHeader *header)
{
	if (header->path != NULL) {
		printf("%s", header->path);
	}
	printf("%s", header->filename);
}

static ListColumn name_column = {
	"       NAME", 20,
	name_column_print
};

static ListColumn short_name_column = {
	"      NAME", 13,
	name_column_print
};

// Print the names of the column headings at the top of the file list.

static void print_list_headings(ListColumn **columns)
{
	unsigned int i, j;

	for (i = 0; columns[i] != NULL; ++i) {
		j = (unsigned) printf("%s", columns[i]->name);

		if (columns[i + 1] != NULL) {
			for (; j < columns[i]->width + 1; ++j) {
				printf(" ");
			}
		}
	}

	printf("\n");
}

// Print separator lines shown at top and bottom of file list.

static void print_list_separators(ListColumn **columns)
{
	unsigned int i, j;

	for (i = 0; columns[i] != NULL; ++i) {
		for (j = 0; j < columns[i]->width; ++j) {
			printf("-");
		}

		if (columns[i + 1] != NULL) {
			printf(" ");
		}
	}

	printf("\n");
}

// Print a row in the list corresponding to a file.

static void print_columns(ListColumn **columns, LHAFileHeader *header)
{
	unsigned int i;

	for (i = 0; columns[i] != NULL; ++i) {
		columns[i]->handler(header);

		if (columns[i + 1] != NULL) {
			printf(" ");
		}
	}

	printf("\n");
}

// Print footer information shown at end of list (overall file stats)

static void print_footers(ListColumn **columns, FileStatistics *stats)
{
	unsigned int i, j, len;
	unsigned int num_columns;

	// Work out how many columns there are to print, ignoring trailing
	// columns that have no footer:

	num_columns = 0;

	for (i = 0; columns[i] != NULL; ++i) {
		++num_columns;
	}

	while (num_columns > 0 && columns[num_columns-1]->footer == NULL) {
		--num_columns;
	}

	// Print footers for each column.
	// Some columns do not have footers: fill in with spaces instead.

	for (i = 0; i < num_columns; ++i) {
		if (columns[i]->footer != NULL) {
			columns[i]->footer(stats);
		} else if (columns[i + 1] != NULL) {
			len = strlen(columns[i]->name);

			for (j = 0; j < len; ++j) {
				printf(" ");
			}
		}

		if (i + 1 < num_columns) {
			printf(" ");
		}
	}

	printf("\n");
}

static unsigned int read_file_timestamp(FILE *fstream)
{
	struct stat data;

	if (fstat(fileno(fstream), &data) != 0) {
		return (unsigned int) -1;
	}

	return (unsigned int) data.st_mtime;
}

// List contents of file, using the specified columns.
// Different columns are provided for basic and verbose modes.

static void list_file_contents(LHAFilter *filter, FILE *fstream,
                               ListColumn **columns)
{
	FileStatistics stats;

	print_list_headings(columns);
	print_list_separators(columns);

	stats.num_files = 0;
	stats.length = 0;
	stats.compressed_length = 0;
	stats.timestamp = read_file_timestamp(fstream);

	for (;;) {
		LHAFileHeader *header;

		header = lha_filter_next_file(filter);

		if (header == NULL) {
			break;
		}

		print_columns(columns, header);

		++stats.num_files;
		stats.length += header->length;
		stats.compressed_length += header->compressed_length;
	}

	print_list_separators(columns);
	print_footers(columns, &stats);
}

static ListColumn *normal_column_headers[] = {
	&permission_column,
	&unix_uid_gid_column,
	&size_column,
	&ratio_column,
	&timestamp_column,
	&name_column,
	NULL
};

// lha -l command.

void list_file_basic(LHAFilter *filter, FILE *fstream)
{
	list_file_contents(filter, fstream, normal_column_headers);
}

static ListColumn *verbose_column_headers[] = {
	&permission_column,
	&unix_uid_gid_column,
	&packed_column,
	&size_column,
	&ratio_column,
	&method_crc_column,
	&timestamp_column,
	&short_name_column,
	NULL
};

// lha -v command.

void list_file_verbose(LHAFilter *filter, FILE *fstream)
{
	list_file_contents(filter, fstream, verbose_column_headers);
}

