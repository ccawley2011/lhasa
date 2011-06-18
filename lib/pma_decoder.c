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

//
// Decoder for PMarc -pm2- compression format.  PMarc is a variant
// of LHA commonly used on the MSX computer architecture.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "lha_decoder.h"

#include "bit_stream_reader.c"

// Size of the ring buffer (in bytes) used to store past history
// for copies.

#define RING_BUFFER_SIZE    8192

// Upper bit is set in a node value to indicate a leaf.

#define TREE_NODE_LEAF      0x80

typedef enum
{
	PMA_REBUILD_UNBUILT,          // At start of stream
	PMA_REBUILD_BUILD1,           // After 1KiB
	PMA_REBUILD_BUILD2,           // After 2KiB
	PMA_REBUILD_BUILD3,           // After 4KiB
	PMA_REBUILD_CONTINUING,       // 8KiB onwards...
} PMARebuildState;

typedef struct
{
	BitStreamReader bit_stream_reader;

	// State of decode tree.

	PMARebuildState tree_state;

	// Number of bytes until we initiate a tree rebuild.

	size_t tree_rebuild_remaining;

	// History ring buffer, for copies:

	uint8_t ringbuf[RING_BUFFER_SIZE];
	unsigned int ringbuf_pos;

	// Array representing the huffman tree used for representing
	// code values. A given node of the tree has children
	// code_tree[n] and code_tree[n + 1].  code_tree[0] is the
	// root node.

	uint8_t code_tree[65];

	// If zero, we don't need an offset tree:

	int need_offset_tree;

	// Array representing huffman tree used to look up offsets.
	// Same format as code_tree[].

	uint8_t offset_tree[17];

} LHAPMADecoder;

// Structure used to hold data needed to build the tree.

typedef struct
{
	// The tree data and its size (must not be exceeded)

	uint8_t *tree;
	unsigned int tree_len;

	// Counter used to allocate entries from the tree.
	// Every time a new node is allocated, this increase by 2.

	unsigned int tree_allocated;

	// Circular buffer of available tree entries.  These are
	// indices into tree[], and either reference a tree node's
	// child pointer (left or right) or the root node pointer.
	// As we add leaves to the tree, they are read from here.

	uint8_t entries[32];

	// The next tree entry.

	unsigned int next_entry;

	// The number of entries in the queue.

	unsigned int entries_len;
} TreeBuildData;

static int lha_pma_decoder_init(void *data, LHADecoderCallback callback,
                                void *callback_data)
{
	LHAPMADecoder *decoder = data;

	bit_stream_reader_init(&decoder->bit_stream_reader,
	                       callback, callback_data);

	// Tree has not been built yet.  It needs to be built on
	// the first call to read().

	decoder->tree_state = PMA_REBUILD_UNBUILT;
	decoder->tree_rebuild_remaining = 0;

	// Initialize ring buffer contents.

	memset(&decoder->ringbuf, ' ', RING_BUFFER_SIZE);

	// Initialize the lookup trees to a known state.

	memset(&decoder->code_tree, TREE_NODE_LEAF,
	       sizeof(decoder->code_tree));
	memset(&decoder->offset_tree, TREE_NODE_LEAF,
	       sizeof(decoder->offset_tree));

	return 1;
}

// Add an entry to the tree entry queue.

static void add_queue_entry(TreeBuildData *build, uint8_t index)
{
	if (build->entries_len >= 32) {
		return;
	}

	build->entries[(build->next_entry + build->entries_len) % 32] = index;
	++build->entries_len;
}

// Read an entry from the tree entry queue.

static uint8_t read_queue_entry(TreeBuildData *build)
{
	uint8_t result;

	if (build->entries_len == 0) {
		return 0;
	}

	result = build->entries[build->next_entry];
	build->next_entry = (build->next_entry + 1) % 32;
	--build->entries_len;

	return result;
}

// "Expand" the list of queue entries. This generates a new child
// node at each of the entries currently in the queue, adding the
// children of those nodes into the queue to replace them.
// The effect of this is to add an extra level to the tree, and
// to increase the tree depth of the indices in the queue.

static void expand_queue(TreeBuildData *build)
{
	unsigned int num_nodes, i;
	unsigned int node;
	uint8_t entry_index;

	num_nodes = build->entries_len;

	for (i = 0; i < num_nodes; ++i) {

		if (build->tree_allocated >= build->tree_len) {
			return;
		}

		// Allocate a new node.

		node = build->tree_allocated;
		build->tree_allocated += 2;

		// Add into tree at the next available location.

		entry_index = read_queue_entry(build);
		build->tree[entry_index] = (uint8_t) node;

		// Add child pointers of this node.

		add_queue_entry(build, node);
		add_queue_entry(build, node + 1);
	}
}

// Add all codes to the tree that have the specified length.
// Returns non-zero if there are any entries in code_lengths[] still
// waiting to be added to the tree.

static int add_codes_with_length(TreeBuildData *build,
                                 uint8_t *code_lengths,
                                 unsigned int num_code_lengths,
                                 unsigned int code_len)
{
	unsigned int codes_remaining;
	unsigned int i;
	unsigned int node;

	codes_remaining = 0;

	for (i = 0; i < num_code_lengths; ++i) {

		// Does this code belong at this depth in the tree?

		if (code_lengths[i] == code_len) {
			node = read_queue_entry(build);

			build->tree[node] = i | TREE_NODE_LEAF;
		}

		// More work to be done after this pass?

		else if (code_lengths[i] > code_len) {
			codes_remaining = 1;
		}
	}

	return codes_remaining;
}

// Build a tree, given the specified array of codes indicating the
// required depth within the tree at which each code should be
// located.

static void build_tree(uint8_t *tree, size_t tree_len,
                       uint8_t *code_lengths, unsigned int num_code_lengths)
{
	TreeBuildData build;
	unsigned int code_len;

	build.tree = tree;
	build.tree_len = tree_len;

	// Start with a single entry in the queue - the root node
	// pointer.

	build.entries[0] = 0;
	build.next_entry = 0;
	build.entries_len = 1;

	// We always have the root ...

	build.tree_allocated = 1;

	// Iterate over each possible code length.
	// Note: code_len == 0 is deliberately skipped over, as 0
	// indicates "not used".

	code_len = 0;

	do {
		// Advance to the next code length by allocating extra
		// nodes to the tree - the slots waiting in the queue
		// will now be one level deeper in the tree (and the
		// codes 1 bit longer).

		expand_queue(&build);
		++code_len;

		// Add all codes that have this length.

	} while (add_codes_with_length(&build, code_lengths,
		                       num_code_lengths, code_len));
}

// Read the list of code lengths to use for the code tree and construct
// the code_tree structure.

static int read_code_tree(LHAPMADecoder *decoder)
{
	uint8_t code_lengths[31];
	int num_codes, min_code_length, length_bits, val;
	unsigned int i;

	// Read the number of codes in the tree.

	num_codes = read_bits(&decoder->bit_stream_reader, 5);

	// Read min_code_length, which is used as an offset.

	min_code_length = read_bits(&decoder->bit_stream_reader, 3);

	if (min_code_length < 0 || num_codes < 0) {
		return 0;
	}

	// Store flag variable indicating whether we want to read
	// the offset tree as well.

	decoder->need_offset_tree
	    = num_codes >= 10
	   && !(num_codes == 29 && min_code_length == 0);

	// Minimum length of zero means a tree containing a single code.

	if (min_code_length == 0) {
		decoder->code_tree[0] = TREE_NODE_LEAF | (num_codes - 1);
		return 1;
	}

	// How many bits are used to represent each table entry?

	length_bits = read_bits(&decoder->bit_stream_reader, 3);

	if (length_bits < 0) {
		return 0;
	}

	// Read table of code lengths:

	for (i = 0; i < num_codes; ++i) {

		// Read a table entry.  A value of zero represents an
		// unused code.  Otherwise the value represents
		// an offset from the minimum length (previously read).

		val = read_bits(&decoder->bit_stream_reader, length_bits);

		if (val < 0) {
			return 0;
		} else if (val == 0) {
			code_lengths[i] = 0;
		} else {
			code_lengths[i] = (uint8_t) (min_code_length + val - 1);
		}
	}

	// Build the tree.

	build_tree(decoder->code_tree, sizeof(decoder->code_tree),
	           code_lengths, num_codes);

	return 1;
}

// Read the code lengths for the offset tree and construct the offset
// tree lookup table.

static int read_offset_tree(LHAPMADecoder *decoder,
                            unsigned int num_offsets)
{
	uint8_t offset_lengths[8];
	unsigned int off;
	unsigned int single_offset, num_codes;
	int len;

	// Read 'num_offsets' 3-bit length values.  For each offset
	// value 'off', offset_lengths[off] is the length of the
	// code that will represent 'off', or 0 if it will not
	// appear within the tree.

	num_codes = 0;

	for (off = 0; off < num_offsets; ++off) {
		len = read_bits(&decoder->bit_stream_reader, 3);

		if (len < 0) {
			return 0;
		}

		offset_lengths[off] = (uint8_t) len;

		// Track how many actual codes were in the tree.

		if (len != 0) {
			single_offset = off;
			++num_codes;
		}
	}

	// If there was a single code, this is a single node tree.

	if (num_codes == 1) {
		decoder->offset_tree[0] = single_offset | TREE_NODE_LEAF;
		return 1;
	}

	// Build the tree.

	build_tree(decoder->offset_tree, sizeof(decoder->offset_tree),
	           offset_lengths, num_offsets);

	return 1;
}

// Rebuild the decode trees used to compress data.  This is called when
// decoder->tree_rebuild_remaining reaches zero.

static void rebuild_tree(LHAPMADecoder *decoder)
{
	switch (decoder->tree_state) {

		// Initial tree build, from start of stream:

		case PMA_REBUILD_UNBUILT:
			read_code_tree(decoder);
			read_offset_tree(decoder, 5);
			decoder->tree_state = PMA_REBUILD_BUILD1;
			decoder->tree_rebuild_remaining = 1024;
			break;

		// Tree rebuild after 1KiB of data has been read:

		case PMA_REBUILD_BUILD1:
			read_offset_tree(decoder, 6);
			decoder->tree_state = PMA_REBUILD_BUILD2;
			decoder->tree_rebuild_remaining = 1024;
			break;

		// Tree rebuild after 2KiB of data has been read:

		case PMA_REBUILD_BUILD2:
			read_offset_tree(decoder, 7);
			decoder->tree_state = PMA_REBUILD_BUILD3;
			decoder->tree_rebuild_remaining = 2048;
			break;

		// Tree rebuild after 4KiB of data has been read:

		case PMA_REBUILD_BUILD3:
			if (read_bit(&decoder->bit_stream_reader) == 1) {
				read_code_tree(decoder);
			}
			read_offset_tree(decoder, 8);
			decoder->tree_state = PMA_REBUILD_CONTINUING;
			decoder->tree_rebuild_remaining = 4096;
			break;

		// Tree rebuild after 8KiB of data has been read,
		// and every 4KiB after that:

		case PMA_REBUILD_CONTINUING:
			if (read_bit(&decoder->bit_stream_reader) == 1) {
				read_code_tree(decoder);
				read_offset_tree(decoder, 8);
			}
			decoder->tree_rebuild_remaining = 4096;
			break;
	}
}

// Read bits from the input stream, traversing the specified tree
// from the root node until we reach a leaf.  The leaf value is
// returned.

static int read_from_tree(LHAPMADecoder *decoder, uint8_t *tree)
{
	unsigned int code;
	int bit;

	// Start from root.

	code = 0;

	while ((code & TREE_NODE_LEAF) == 0) {

		bit = read_bit(&decoder->bit_stream_reader);

		if (bit < 0) {
			return -1;
		}

		code = tree[code + bit];
	}

	// Mask off leaf bit to get the plain code.

	return code & ~TREE_NODE_LEAF;
}

static void output_byte(LHAPMADecoder *decoder, uint8_t *buf,
                        size_t *buf_len, uint8_t b)
{
	// Add to history ring buffer.

	decoder->ringbuf[decoder->ringbuf_pos] = b;
	decoder->ringbuf_pos = (decoder->ringbuf_pos + 1) % RING_BUFFER_SIZE;

	// Add to output buffer.

	buf[*buf_len] = b;
	++*buf_len;

	// TODO: History list

	// Count down until it is time to perform a rebuild of the
	// lookup trees.

	--decoder->tree_rebuild_remaining;

	if (decoder->tree_rebuild_remaining == 0) {
		rebuild_tree(decoder);
	}
}

static size_t lha_pma_decoder_read(void *data, uint8_t *buf)
{
	LHAPMADecoder *decoder = data;
	size_t result;
	int code;

	// On first pass through, build initial lookup trees.

	if (decoder->tree_state == PMA_REBUILD_UNBUILT) {
		rebuild_tree(decoder);
	}

	result = 0;

	code = read_from_tree(decoder, decoder->code_tree);

	if (code < 0) {
		return 0;
	}



	return result;
}

LHADecoderType lha_pma_decoder = {
	lha_pma_decoder_init,
	NULL,
	lha_pma_decoder_read,
	sizeof(LHAPMADecoder),
	0 // TODO
};
