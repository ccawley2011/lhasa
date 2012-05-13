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

#ifndef LHASA_PUBLIC_LHA_FILE_HEADER_H
#define LHASA_PUBLIC_LHA_FILE_HEADER_H

#include <inttypes.h>

#define LHA_FILE_UNIX_PERMS            0x01
#define LHA_FILE_UNIX_UID_GID          0x02
#define LHA_FILE_COMMON_CRC            0x04
#define LHA_FILE_WINDOWS_TIMESTAMPS    0x08

// Common OS types:

#define LHA_OS_TYPE_UNKNOWN            0x00
#define LHA_OS_TYPE_MSDOS              'M'  /* Microsoft MS/DOS */
#define LHA_OS_TYPE_WIN95              'w'  /* Microsoft Windows 95 */
#define LHA_OS_TYPE_WINNT              'W'  /* Microsoft Windows NT */
#define LHA_OS_TYPE_UNIX               'U'  /* Generic Unix */
#define LHA_OS_TYPE_OS2                '2'  /* IBM OS/2 */
#define LHA_OS_TYPE_MACOS              'm'  /* Apple classic Mac OS */
#define LHA_OS_TYPE_AMIGA              'A'  /* Amiga */
#define LHA_OS_TYPE_ATARI              'a'  /* Atari ST */

// Obscure:

#define LHA_OS_TYPE_JAVA               'J'  /* Java */
#define LHA_OS_TYPE_CPM                'C'  /* Digital Research CP/M */
#define LHA_OS_TYPE_FLEX               'F'  /* Digital Research FlexOS */
#define LHA_OS_TYPE_RUNSER             'R'
#define LHA_OS_TYPE_TOWNSOS            'T'  /* Fujitsu FM Towns */
#define LHA_OS_TYPE_OS9                '9'  /* Microware OS-9 */
#define LHA_OS_TYPE_OS9_68K            'K'  /* Microware OS-9 - 68k */
#define LHA_OS_TYPE_OS386              '3'
#define LHA_OS_TYPE_HUMAN68K           'H'  /* Sharp X68000 Human68K OS */

// Compression type for a stored directory:

#define LHA_COMPRESS_TYPE_DIR   "-lhd-"

typedef struct _LHAFileHeader LHAFileHeader;

struct _LHAFileHeader {

	// Internal fields, do not touch!

	unsigned int _refcount;
	LHAFileHeader *_next;

	// Path (directory) and filename. Either of these may be NULL,
	// but not both - a directory entry (LHA_COMPRESS_TYPE_DIR) always
	// has a non-NULL path, and a non-directory entry always has a
	// non-NULL filename.

	char *path;
	char *filename;

	// Decoded fields:

	char compress_method[6];
	size_t compressed_length;
	size_t length;
	uint8_t header_level;
	uint8_t os_type;
	uint16_t crc;
	unsigned int timestamp;
	uint8_t *raw_data;
	size_t raw_data_len;
	unsigned int extra_flags;

	// Optional data (from extended headers):

	unsigned int unix_perms;
	unsigned int unix_uid;
	unsigned int unix_gid;
	char *unix_group;
	char *unix_username;
	uint16_t common_crc;
	uint64_t win_creation_time;
	uint64_t win_modification_time;
	uint64_t win_access_time;
};

/**
 * Free a file header structure.
 *
 * @param header         The file header to free.
 */

void lha_file_header_free(LHAFileHeader *header);

/**
 * Add a reference to the specified file header, to stop it from being
 * freed.
 *
 * @param header         The file header to add a reference to.
 */

void lha_file_header_add_ref(LHAFileHeader *header);

#endif /* #ifndef LHASA_PUBLIC_LHA_FILE_HEADER_H */
