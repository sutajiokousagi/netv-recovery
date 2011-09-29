/* $Id: esd_config_area.h 21557 2009-10-09 14:05:10Z henry $
 * esd_config_area.h - definition of eSD config area in partition 0
 * Copyright (C) 2009 Chumby Industries. All rights reserved
 */

#ifndef _CONFIG_AREA_H_INCLUDED_
#define _CONFIG_AREA_H_INCLUDED_

/* Current config area version in array format */
#define CONFIG_AREA_VER	1,0,0,1

/* Fixed offset within partition 1 for start of config area */
#define CONFIG_AREA_PART1_OFFSET	0xc000

/* Length of config area */
#define CONFIG_AREA_LENGTH			0x4000

/*
 * First unused area in config - used as a temporary storage in RAM
 * for boot-time debugging
 */
#define CONFIG_UNUSED	unused2

/*
 * pragma pack() not supported on all platforms, so we make everything
 * dword-aligned using arrays
 * WARNING: we're being lazy here and assuming that the platform any utility
 * using this is running on little-endian! Otherwise you'll need to convert
 * u32 values
 */
typedef union {
	char name[4];
	unsigned int uname;
} block_def_name;


struct block_def {
	/*
	 * Offset from start of partition 1.
	 * if 0xffffffff, end of block table
	 */
	unsigned int offset;

	/* Length of block in bytes */
	unsigned int length;

	/* Version of this block data, e.g. 1,0,0,0 */
	unsigned char block_ver[4];

	/*
	 * Name of block, e.g. "krnA" (not NULL-terminated, a-z, A-Z,
	 * 0-9 and non-escape symbols allowed)
	 */
	block_def_name n;
};


struct config_area {
	/* 'C','f','g','*' */
	char sig[4];

	/* 1,0,0,0 */
	unsigned char area_version[4];

	/* element 0 is 0 if krnA active, 1 if krnB; elements 1-3 are padding */
	unsigned char active_index[4];

	/* element 0 is 1 if update in progress; elements 1-3 are padding */
	unsigned char updating[4];

	/* NULL-terminated version of last successful update, e.g. "1.7.1892" */
	char last_update[16];

	/* Offset in bytes from start of device to start of partition 1 */
	unsigned int p1_offset;

	/* Data recorded in manufacturing in format KEY=VALUE<newline>... */
	char factory_data[220];

	/* NULL-terminated CONFIGNAME of current build, e.g. "silvermoon_sd" */
	char configname[128];

	unsigned char unused2[128];

	/* Backup copy of MBR */
	unsigned char mbr_backup[512];

	/* Block table entries ending with offset==0xffffffff */
	struct block_def block_table[64];

	unsigned char unused3[0];
};

#endif
