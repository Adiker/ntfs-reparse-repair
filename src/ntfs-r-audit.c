/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 Adiker
 *
 * Read-only semantic auditor for $Extend/$Reparse:$R.
 *
 * There is deliberately no write path in this program.  It compares every
 * index key with its referenced MFT record and then scans allocated MFT
 * records in the opposite direction to detect missing index keys.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "attrib.h"
#include "index.h"
#include "inode.h"
#include "layout.h"
#include "logging.h"
#include "mft.h"
#include "types.h"
#include "volume.h"

struct mft_scan_stats {
	int reparse_count;
	int indexed_count;
	int missing_count;
	int zero_links_count;
	int parse_errors;
};

static int validate_target(ntfs_volume *vol, u64 ref, u32 index_tag,
		char *reason, size_t reason_size)
{
	ntfs_inode *ni;
	ntfs_attr *na;
	le32 attr_tag;
	u16 expected_sequence = MSEQNO(ref);
	u16 actual_sequence;
	int valid = 0;

	ni = ntfs_inode_open(vol, MREF(ref));
	if (!ni) {
		snprintf(reason, reason_size, "MFT_OPEN_FAILED:%s", strerror(errno));
		return 0;
	}
	actual_sequence = le16_to_cpu(ni->mrec->sequence_number);
	if (!(le16_to_cpu(ni->mrec->flags) & MFT_RECORD_IN_USE)) {
		snprintf(reason, reason_size, "MFT_NOT_IN_USE:actual_seq=%u",
			actual_sequence);
		goto out;
	}
	if (actual_sequence != expected_sequence) {
		snprintf(reason, reason_size,
			"SEQUENCE_MISMATCH:expected=%u,actual=%u",
			expected_sequence, actual_sequence);
		goto out;
	}
	if (!(ni->flags & FILE_ATTR_REPARSE_POINT)) {
		snprintf(reason, reason_size, "FILE_FLAG_REPARSE_MISSING");
		goto out;
	}
	na = ntfs_attr_open(ni, AT_REPARSE_POINT, AT_UNNAMED, 0);
	if (!na) {
		snprintf(reason, reason_size, "REPARSE_ATTRIBUTE_MISSING:%s",
			strerror(errno));
		goto out;
	}
	if (ntfs_attr_pread(na, 0, sizeof(attr_tag), &attr_tag) !=
			sizeof(attr_tag)) {
		snprintf(reason, reason_size, "REPARSE_TAG_READ_FAILED:%s",
			strerror(errno));
		ntfs_attr_close(na);
		goto out;
	}
	ntfs_attr_close(na);
	if (le32_to_cpu(attr_tag) != index_tag) {
		snprintf(reason, reason_size,
			"TAG_MISMATCH:index=0x%08x,attribute=0x%08x",
			index_tag, le32_to_cpu(attr_tag));
		goto out;
	}
	snprintf(reason, reason_size, "MATCHING_MFT_AND_REPARSE_ATTRIBUTE");
	valid = 1;
out:
	ntfs_inode_close(ni);
	return valid;
}

/* Return 1 for a supported resident reparse attribute, 0 for none, and a
 * negative/unsupported code for a record that cannot be proven safe. */
static int resident_reparse_tag(const MFT_RECORD *mrec, u32 record_size,
		u32 *tag)
{
	u32 offset = le16_to_cpu(mrec->attrs_offset);
	u32 bytes_in_use = le32_to_cpu(mrec->bytes_in_use);

	if (bytes_in_use > record_size || offset >= bytes_in_use)
		return -1;
	while (offset <= bytes_in_use && bytes_in_use - offset >= 8) {
		const ATTR_RECORD *attr =
			(const ATTR_RECORD *)((const u8 *)mrec + offset);
		le32 disk_type;
		u32 type;
		u32 length;

		memcpy(&disk_type, &attr->type, sizeof(disk_type));
		type = le32_to_cpu(disk_type);
		if (type == 0xffffffffU)
			return 0;
		if (bytes_in_use - offset < 24)
			return -1;
		length = le32_to_cpu(attr->length);
		if (length < 24 || length > bytes_in_use - offset)
			return -1;
		if (type == 0x000000c0U) {
			u16 value_offset = le16_to_cpu(attr->value_offset);

			if (attr->non_resident)
				return 2;
			if (le32_to_cpu(attr->value_length) < sizeof(le32) ||
					value_offset > length - sizeof(le32))
				return -1;
			memcpy(tag, (const u8 *)attr + value_offset, sizeof(le32));
			*tag = le32_to_cpu(*tag);
			return 1;
		}
		offset += length;
	}
	return -1;
}

static int scan_mft_reparse_records(ntfs_volume *vol,
		ntfs_index_context *ctx, struct mft_scan_stats *stats)
{
	MFT_RECORD *mrec;
	u8 *mft_bitmap;
	s64 bitmap_size = vol->mftbmp_na->initialized_size;
	u64 record_count;
	u64 mft_no;

	memset(stats, 0, sizeof(*stats));
	if (bitmap_size <= 0) {
		fprintf(stderr, "MFT_BITMAP_SIZE_INVALID=%" PRId64 "\n", bitmap_size);
		return -1;
	}
	mft_bitmap = malloc(bitmap_size);
	if (!mft_bitmap)
		return -1;
	if (ntfs_attr_pread(vol->mftbmp_na, 0, bitmap_size, mft_bitmap) !=
			bitmap_size) {
		fprintf(stderr, "MFT_BITMAP_READ_FAILED=%s\n", strerror(errno));
		free(mft_bitmap);
		return -1;
	}
	record_count = vol->mft_na->initialized_size / vol->mft_record_size;
	if (record_count > (u64)bitmap_size * 8)
		record_count = (u64)bitmap_size * 8;
	mrec = malloc(vol->mft_record_size);
	if (!mrec) {
		free(mft_bitmap);
		return -1;
	}
	printf("MFT_SCAN_RECORD_COUNT=%" PRIu64 "\n", record_count);
	for (mft_no = 0; mft_no < record_count; ++mft_no) {
		u32 tag = 0;
		int tag_status;
		u16 sequence;
		u16 links;
		u64 ref;
		REPARSE_INDEX_KEY key;
		int indexed;

		if (!(mft_bitmap[mft_no >> 3] & (1U << (mft_no & 7))))
			continue;
		if (ntfs_mft_record_read(vol, mft_no, mrec)) {
			++stats->parse_errors;
			continue;
		}
		if (!ntfs_is_mft_record(mrec->magic) ||
				!(le16_to_cpu(mrec->flags) & MFT_RECORD_IN_USE))
			continue;
		tag_status = resident_reparse_tag(mrec, vol->mft_record_size, &tag);
		if (tag_status == 0)
			continue;
		if (tag_status != 1) {
			fprintf(stderr,
				"MFT_REPARSE_PARSE_UNSUPPORTED_OR_INVALID=%" PRIu64
				" STATUS=%d\n", mft_no, tag_status);
			++stats->parse_errors;
			continue;
		}
		sequence = le16_to_cpu(mrec->sequence_number);
		links = le16_to_cpu(mrec->link_count);
		ref = MK_MREF(mft_no, sequence);
		key.reparse_tag = cpu_to_le32(tag);
		key.file_id = cpu_to_le64(ref);
		ntfs_index_ctx_reinit(ctx);
		errno = 0;
		indexed = ntfs_index_lookup(&key, sizeof(key), ctx) == 0;
		if (!indexed && errno != ENOENT) {
			fprintf(stderr,
				"MFT_TO_INDEX_LOOKUP_ERROR REF=0x%016" PRIx64
				" ERRNO=%d:%s\n", ref, errno, strerror(errno));
			++stats->parse_errors;
		}
		++stats->reparse_count;
		if (indexed)
			++stats->indexed_count;
		else
			++stats->missing_count;
		if (!links)
			++stats->zero_links_count;
		printf("MFT_REPARSE=%03d TAG=0x%08x REF=0x%016" PRIx64
			" MFT=%" PRIu64 " SEQ=%u LINKS=%u INDEX=%s\n",
			stats->reparse_count, tag, ref, mft_no, sequence, links,
			indexed ? "PRESENT" : "MISSING");
	}
	free(mrec);
	free(mft_bitmap);
	printf("MFT_SUMMARY_REPARSE=%d\n", stats->reparse_count);
	printf("MFT_SUMMARY_INDEX_PRESENT=%d\n", stats->indexed_count);
	printf("MFT_SUMMARY_INDEX_MISSING=%d\n", stats->missing_count);
	printf("MFT_SUMMARY_ZERO_LINKS=%d\n", stats->zero_links_count);
	printf("MFT_SUMMARY_PARSE_ERRORS=%d\n", stats->parse_errors);
	printf("MFT_SEMANTIC_SCAN_COMPLETE=%s\n",
		stats->parse_errors ? "NO" : "YES");
	return stats->parse_errors ? -1 : 0;
}

int main(int argc, char **argv)
{
	static ntfschar index_name[] = {
		const_cpu_to_le16('$'), const_cpu_to_le16('R')
	};
	REPARSE_INDEX_KEY zero_key;
	REPARSE_INDEX_KEY previous_key;
	ntfs_volume *vol = NULL;
	ntfs_inode *reparse_inode = NULL;
	ntfs_index_context *ctx = NULL;
	INDEX_ENTRY *entry;
	struct mft_scan_stats mft_stats;
	int total = 0;
	int valid = 0;
	int stale = 0;
	int duplicates = 0;
	int iteration_error = 0;
	int have_previous = 0;
	int ready;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s <unmounted-ntfs-device-or-image>\n",
			argv[0]);
		return 2;
	}
	setlinebuf(stdout);
	ntfs_log_set_handler(ntfs_log_handler_outerr);
	printf("MODE=READ_ONLY_FORENSIC\nDEVICE=%s\n", argv[1]);
	vol = ntfs_mount(argv[1], NTFS_MNT_RDONLY | NTFS_MNT_FORENSIC |
		NTFS_MNT_RECOVER);
	if (!vol) {
		fprintf(stderr, "MOUNT_FAILED=%s\n", strerror(errno));
		return 3;
	}
	printf("VOLUME_BYTES=%" PRIu64 "\n",
		(u64)vol->nr_clusters << vol->cluster_size_bits);
	printf("CLUSTER_SIZE=%u\nMFT_RECORD_SIZE=%u\n",
		vol->cluster_size, vol->mft_record_size);
	reparse_inode = ntfs_inode_open(vol, 26);
	if (!reparse_inode) {
		fprintf(stderr, "REPARSE_INODE_OPEN_FAILED=%s\n", strerror(errno));
		ntfs_umount(vol, FALSE);
		return 4;
	}
	ctx = ntfs_index_ctx_get(reparse_inode, index_name, 2);
	if (!ctx) {
		fprintf(stderr, "R_INDEX_OPEN_FAILED=%s\n", strerror(errno));
		ntfs_inode_close(reparse_inode);
		ntfs_umount(vol, FALSE);
		return 5;
	}
	memset(&zero_key, 0, sizeof(zero_key));
	errno = 0;
	if (ntfs_index_lookup(&zero_key, sizeof(zero_key), ctx) == 0) {
		entry = ctx->entry;
	} else if (errno == ENOENT) {
		entry = ctx->entry;
		if (entry && (entry->ie_flags & INDEX_ENTRY_END))
			entry = ntfs_index_next(entry, ctx);
	} else {
		fprintf(stderr, "R_INDEX_INITIAL_LOOKUP_FAILED=%s\n", strerror(errno));
		ntfs_index_ctx_put(ctx);
		ntfs_inode_close(reparse_inode);
		ntfs_umount(vol, FALSE);
		return 6;
	}
	while (entry) {
		REPARSE_INDEX_KEY key;
		u64 ref;
		u32 tag;
		int is_valid;
		char reason[192];

		if (entry->ie_flags & INDEX_ENTRY_END)
			break;
		if (le16_to_cpu(entry->key_length) != sizeof(REPARSE_INDEX_KEY)) {
			fprintf(stderr, "INVALID_KEY_LENGTH_AT_ENTRY_%d=%u\n",
				total + 1, le16_to_cpu(entry->key_length));
			iteration_error = 1;
			break;
		}
		memcpy(&key, &entry->key.reparse, sizeof(key));
		if (have_previous && !memcmp(&key, &previous_key, sizeof(key)))
			++duplicates;
		memcpy(&previous_key, &key, sizeof(key));
		have_previous = 1;
		tag = le32_to_cpu(key.reparse_tag);
		ref = le64_to_cpu(key.file_id);
		is_valid = validate_target(vol, ref, tag, reason, sizeof(reason));
		++total;
		if (is_valid)
			++valid;
		else
			++stale;
		printf("ENTRY=%03d TAG=0x%08x REF=0x%016" PRIx64
			" MFT=%" PRIu64 " SEQ=%u STATE=%s REASON=%s\n",
			total, tag, ref, MREF(ref), MSEQNO(ref),
			is_valid ? "VALID" : "STALE", reason);
		entry = ntfs_index_next(entry, ctx);
	}
	if (!entry && errno != ENODATA && errno != ENOENT && errno != 0) {
		fprintf(stderr, "INDEX_ITERATION_ENDED_WITH_ERRNO=%d:%s\n",
			errno, strerror(errno));
		iteration_error = 1;
	}
	printf("SUMMARY_TOTAL=%d\nSUMMARY_VALID=%d\nSUMMARY_STALE=%d\n",
		total, valid, stale);
	printf("SUMMARY_DUPLICATES=%d\n", duplicates);
	printf("=== MFT TO $R SEMANTIC COMPARISON ===\n");
	if (scan_mft_reparse_records(vol, ctx, &mft_stats))
		iteration_error = 1;
	ready = !iteration_error && total > 0 && valid == total && stale == 0 &&
		duplicates == 0 && mft_stats.reparse_count == total &&
		mft_stats.indexed_count == total && mft_stats.missing_count == 0 &&
		mft_stats.zero_links_count == 0 && mft_stats.parse_errors == 0;
	printf("AUDIT_SAFE_TO_REBUILD=%s\n", ready ? "YES" : "NO");
	ntfs_index_ctx_put(ctx);
	ntfs_inode_close(reparse_inode);
	if (ntfs_umount(vol, FALSE)) {
		fprintf(stderr, "UNMOUNT_FAILED=%s\n", strerror(errno));
		return 7;
	}
	return ready ? 0 : 8;
}
