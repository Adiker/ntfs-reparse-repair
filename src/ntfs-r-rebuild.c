/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Adiker
 *
 * Experimental rebuild of $Extend/$Reparse:$R.
 *
 * The device mode is intentionally difficult to invoke: it requires a stable
 * /dev/disk/by-id partition path, exact expected key and byte counts, a
 * writable block device, and a literal data-loss acknowledgement.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "attrib.h"
#include "index.h"
#include "inode.h"
#include "layout.h"
#include "logging.h"
#include "types.h"
#include "volume.h"

struct reparse_index_entry {
	INDEX_ENTRY_HEADER header;
	REPARSE_INDEX_KEY key;
	le32 filling;
} __attribute__((__packed__));

static ntfschar index_name[] = {
	const_cpu_to_le16('$'), const_cpu_to_le16('R')
};

static int parse_positive(const char *text, unsigned long long maximum,
		unsigned long long *value)
{
	char *end;
	unsigned long long parsed;

	errno = 0;
	parsed = strtoull(text, &end, 10);
	if (errno || !*text || *end || !parsed || parsed > maximum)
		return -1;
	*value = parsed;
	return 0;
}

static int validate_key_target(ntfs_volume *vol, const REPARSE_INDEX_KEY *key)
{
	u64 ref = le64_to_cpu(key->file_id);
	u32 index_tag = le32_to_cpu(key->reparse_tag);
	ntfs_inode *ni;
	ntfs_attr *na;
	le32 attr_tag;
	int valid = 0;

	ni = ntfs_inode_open(vol, MREF(ref));
	if (!ni)
		return 0;
	if (!(le16_to_cpu(ni->mrec->flags) & MFT_RECORD_IN_USE) ||
			le16_to_cpu(ni->mrec->sequence_number) != MSEQNO(ref) ||
			!(ni->flags & FILE_ATTR_REPARSE_POINT))
		goto out;
	na = ntfs_attr_open(ni, AT_REPARSE_POINT, AT_UNNAMED, 0);
	if (!na)
		goto out;
	if (ntfs_attr_pread(na, 0, sizeof(attr_tag), &attr_tag) ==
			sizeof(attr_tag) && le32_to_cpu(attr_tag) == index_tag)
		valid = 1;
	ntfs_attr_close(na);
out:
	ntfs_inode_close(ni);
	return valid;
}

static int collect_and_validate(ntfs_volume *vol, ntfs_index_context *ctx,
		REPARSE_INDEX_KEY *keys, size_t capacity, size_t *key_count)
{
	REPARSE_INDEX_KEY zero_key;
	INDEX_ENTRY *entry;
	size_t count = 0;

	memset(&zero_key, 0, sizeof(zero_key));
	ntfs_index_ctx_reinit(ctx);
	errno = 0;
	if (ntfs_index_lookup(&zero_key, sizeof(zero_key), ctx) == 0) {
		entry = ctx->entry;
	} else if (errno == ENOENT) {
		entry = ctx->entry;
		if (entry && (entry->ie_flags & INDEX_ENTRY_END))
			entry = ntfs_index_next(entry, ctx);
	} else {
		fprintf(stderr, "Initial index lookup failed: %s\n", strerror(errno));
		return -1;
	}
	while (entry) {
		if (entry->ie_flags & INDEX_ENTRY_END)
			break;
		if (count >= capacity ||
				le16_to_cpu(entry->key_length) != sizeof(REPARSE_INDEX_KEY)) {
			fprintf(stderr, "Invalid key count or key length.\n");
			return -1;
		}
		memcpy(&keys[count], &entry->key.reparse,
			sizeof(REPARSE_INDEX_KEY));
		if (!validate_key_target(vol, &keys[count])) {
			fprintf(stderr,
				"Refusing non-matching key: tag=0x%08x ref=0x%016" PRIx64 "\n",
				le32_to_cpu(keys[count].reparse_tag),
				le64_to_cpu(keys[count].file_id));
			return -1;
		}
		if (count && !memcmp(&keys[count - 1], &keys[count],
				sizeof(REPARSE_INDEX_KEY))) {
			fprintf(stderr, "Refusing duplicate key.\n");
			return -1;
		}
		++count;
		entry = ntfs_index_next(entry, ctx);
	}
	if (count != capacity) {
		fprintf(stderr, "Refusing key count %zu; expected %zu.\n",
			count, capacity);
		return -1;
	}
	*key_count = count;
	return 0;
}

static int reset_index_attributes(ntfs_inode *ni,
		ntfs_index_context **ctx_ptr)
{
	ntfs_attr *ia_na = NULL;
	ntfs_attr *bm_na = NULL;
	ntfs_attr *ir_na = NULL;
	INDEX_ROOT *ir;
	INDEX_ENTRY *end_entry;
	int index_len;
	int root_size;

	if (*ctx_ptr) {
		ntfs_index_ctx_put(*ctx_ptr);
		*ctx_ptr = NULL;
	}
	ia_na = ntfs_attr_open(ni, AT_INDEX_ALLOCATION, index_name, 2);
	if (ia_na) {
		if (ntfs_attr_rm(ia_na)) {
			fprintf(stderr, "Removing $INDEX_ALLOCATION failed: %s\n",
				strerror(errno));
			ntfs_attr_close(ia_na);
			return -1;
		}
		ntfs_attr_close(ia_na);
		printf("REMOVED_INDEX_ALLOCATION=YES\n");
	}
	bm_na = ntfs_attr_open(ni, AT_BITMAP, index_name, 2);
	if (bm_na) {
		if (ntfs_attr_rm(bm_na)) {
			fprintf(stderr, "Removing $BITMAP failed: %s\n", strerror(errno));
			ntfs_attr_close(bm_na);
			return -1;
		}
		ntfs_attr_close(bm_na);
		printf("REMOVED_INDEX_BITMAP=YES\n");
	}
	ir_na = ntfs_attr_open(ni, AT_INDEX_ROOT, index_name, 2);
	if (!ir_na) {
		fprintf(stderr, "Opening $INDEX_ROOT failed: %s\n", strerror(errno));
		return -1;
	}
	root_size = sizeof(INDEX_ROOT) + sizeof(INDEX_ENTRY_HEADER);
	if (ntfs_attr_truncate(ir_na, root_size)) {
		fprintf(stderr, "Truncating $INDEX_ROOT failed: %s\n", strerror(errno));
		ntfs_attr_close(ir_na);
		return -1;
	}
	ir = ntfs_ir_lookup2(ni, index_name, 2);
	if (!ir) {
		fprintf(stderr, "Reading reset $INDEX_ROOT failed: %s\n", strerror(errno));
		ntfs_attr_close(ir_na);
		return -1;
	}
	index_len = sizeof(INDEX_HEADER) + sizeof(INDEX_ENTRY_HEADER);
	/* View indexes use zero here.  AT_REPARSE_POINT (0xc0) appears readable to
	 * libntfs but makes Windows CHKDSK recreate the root. */
	ir->type = AT_UNUSED;
	ir->collation_rule = COLLATION_NTOFS_ULONGS;
	ir->index_block_size = cpu_to_le32(ni->vol->indx_record_size);
	if (ni->vol->cluster_size <= ni->vol->indx_record_size)
		ir->clusters_per_index_block = ni->vol->indx_record_size >>
			ni->vol->cluster_size_bits;
	else
		ir->clusters_per_index_block = ni->vol->indx_record_size >>
			NTFS_BLOCK_SIZE_BITS;
	ir->index.entries_offset = const_cpu_to_le32(sizeof(INDEX_HEADER));
	ir->index.index_length = cpu_to_le32(index_len);
	ir->index.allocated_size = cpu_to_le32(index_len);
	ir->index.ih_flags = SMALL_INDEX;
	end_entry = (INDEX_ENTRY *)((u8 *)ir + sizeof(INDEX_ROOT));
	/* END has only the 16-byte header.  sizeof(INDEX_ENTRY) is much larger. */
	memset(end_entry, 0, sizeof(INDEX_ENTRY_HEADER));
	end_entry->length = const_cpu_to_le16(sizeof(INDEX_ENTRY_HEADER));
	end_entry->ie_flags = INDEX_ENTRY_END;
	ntfs_inode_mark_dirty(ni);
	ntfs_attr_close(ir_na);
	printf("RESET_INDEX_ROOT=YES\n");
	*ctx_ptr = ntfs_index_ctx_get(ni, index_name, 2);
	if (!*ctx_ptr) {
		fprintf(stderr, "Opening fresh $R context failed: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

static int add_key(ntfs_index_context *ctx, const REPARSE_INDEX_KEY *key)
{
	struct reparse_index_entry entry;

	memset(&entry, 0, sizeof(entry));
	entry.header.data_offset = const_cpu_to_le16(
		sizeof(INDEX_ENTRY_HEADER) + sizeof(REPARSE_INDEX_KEY));
	entry.header.data_length = const_cpu_to_le16(0);
	entry.header.length = const_cpu_to_le16(sizeof(entry));
	entry.header.key_length = const_cpu_to_le16(sizeof(REPARSE_INDEX_KEY));
	memcpy(&entry.key, key, sizeof(*key));
	ntfs_index_ctx_reinit(ctx);
	if (ntfs_ie_add(ctx, (INDEX_ENTRY *)&entry)) {
		fprintf(stderr,
			"Key insertion failed: tag=0x%08x ref=0x%016" PRIx64
			" error=%s\n", le32_to_cpu(key->reparse_tag),
			le64_to_cpu(key->file_id), strerror(errno));
		return -1;
	}
	ntfs_index_entry_mark_dirty(ctx);
	NInoSetDirty(ctx->ni);
	return 0;
}

int main(int argc, char **argv)
{
	struct stat st;
	ntfs_volume *vol = NULL;
	ntfs_inode *reparse_inode = NULL;
	ntfs_index_context *ctx = NULL;
	REPARSE_INDEX_KEY *keys = NULL;
	unsigned long long expected_count_ull;
	unsigned long long expected_bytes = 0;
	size_t expected_count;
	size_t key_count = 0;
	size_t inserted = 0;
	size_t i;
	int device_mode;
	int read_only;
	int fd;
	int result = 1;

	device_mode = argc == 6 && !strcmp(argv[2], "--apply-device");
	if (!device_mode && !(argc == 4 && !strcmp(argv[2], "--test-image"))) {
		fprintf(stderr,
			"Usage:\n  %s IMAGE --test-image EXPECTED_KEYS\n"
			"  %s /dev/disk/by-id/...-partN --apply-device EXPECTED_KEYS "
			"EXPECTED_BYTES DATA_LOSS_IS_POSSIBLE\n", argv[0], argv[0]);
		return 2;
	}
	if (parse_positive(argv[3], 1048576ULL, &expected_count_ull)) {
		fprintf(stderr, "Invalid expected key count.\n");
		return 2;
	}
	expected_count = (size_t)expected_count_ull;
	if (stat(argv[1], &st)) {
		fprintf(stderr, "Cannot stat target: %s\n", strerror(errno));
		return 3;
	}
	if (!device_mode && !S_ISREG(st.st_mode)) {
		fprintf(stderr, "Test mode requires a regular file.\n");
		return 3;
	}
	if (device_mode) {
		if (!S_ISBLK(st.st_mode) ||
				strncmp(argv[1], "/dev/disk/by-id/", 16) ||
				!strstr(argv[1], "-part") ||
				strcmp(argv[5], "DATA_LOSS_IS_POSSIBLE") ||
				parse_positive(argv[4], UINT64_MAX, &expected_bytes)) {
			fprintf(stderr, "REFUSING_UNSAFE_DEVICE_INVOCATION\n");
			return 3;
		}
		fd = open(argv[1], O_RDONLY);
		if (fd < 0 || ioctl(fd, BLKGETSIZE64, &expected_count_ull) ||
				expected_count_ull != expected_bytes ||
				ioctl(fd, BLKROGET, &read_only) || read_only) {
			if (fd >= 0)
				close(fd);
			fprintf(stderr, "REFUSING_DEVICE_SIZE_OR_READONLY_STATE\n");
			return 3;
		}
		close(fd);
	}
	keys = calloc(expected_count, sizeof(*keys));
	if (!keys)
		return 3;
	setlinebuf(stdout);
	ntfs_log_set_handler(ntfs_log_handler_outerr);
	printf("WRITE_SCOPE=%s\nTARGET=%s\nEXPECTED_KEYS=%zu\n",
		device_mode ? "ACKNOWLEDGED_BY_ID_BLOCK_DEVICE" :
		"REGULAR_FILE_TEST_IMAGE", argv[1], expected_count);
	vol = ntfs_mount(argv[1], NTFS_MNT_RECOVER | NTFS_MNT_EXCLUSIVE);
	if (!vol) {
		fprintf(stderr, "Read-write mount failed: %s\n", strerror(errno));
		free(keys);
		return 4;
	}
	if (NVolReadOnly(vol)) {
		fprintf(stderr, "REFUSING_READ_ONLY_FALLBACK_MOUNT\n");
		ntfs_umount(vol, FALSE);
		free(keys);
		return 5;
	}
	reparse_inode = ntfs_inode_open(vol, 26);
	if (!reparse_inode) {
		fprintf(stderr, "Opening $Reparse failed: %s\n", strerror(errno));
		goto out;
	}
	ctx = ntfs_index_ctx_get(reparse_inode, index_name, 2);
	if (!ctx) {
		fprintf(stderr, "Opening $R failed: %s\n", strerror(errno));
		goto out;
	}
	if (collect_and_validate(vol, ctx, keys, expected_count, &key_count))
		goto out;
	printf("PRECHECK_VALID_KEYS=%zu\n", key_count);
	if (reset_index_attributes(reparse_inode, &ctx))
		goto out;
	for (i = 0; i < key_count; ++i) {
		if (add_key(ctx, &keys[i]))
			goto out;
		++inserted;
	}
	printf("INSERTED_KEYS=%zu\n", inserted);
	result = 0;
out:
	if (ctx)
		ntfs_index_ctx_put(ctx);
	if (reparse_inode)
		ntfs_inode_close(reparse_inode);
	if (vol && ntfs_umount(vol, FALSE)) {
		fprintf(stderr, "Unmount failed: %s\n", strerror(errno));
		result = 1;
	}
	free(keys);
	printf("REBUILD_RESULT=%s\n", result ? "FAILED" : "SUCCESS");
	return result;
}
