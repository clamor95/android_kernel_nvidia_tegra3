/*
 * drivers/video/tegra/dc/dc_config.c
 * * Copyright (c) 2012, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __DRIVERS_VIDEO_TEGRA_DC_DC_CONFIG_H
#define __DRIVERS_VIDEO_TEGRA_DC_DC_CONFIG_H

#include <linux/errno.h>
#include <mach/dc.h>

#include "dc_priv.h"

#define ENTRY_SIZE	4	/* Size of feature entry args */

/* These macros are defined based on T20/T30 formats. */
#define TEGRA_WIN_FMT_BASE_CNT	(TEGRA_WIN_FMT_YUV422RA + 1)
#define TEGRA_WIN_FMT_BASE	((1 << TEGRA_WIN_FMT_P8) | \
				(1 << TEGRA_WIN_FMT_B4G4R4A4) | \
				(1 << TEGRA_WIN_FMT_B5G5R5A) | \
				(1 << TEGRA_WIN_FMT_B5G6R5) | \
				(1 << TEGRA_WIN_FMT_AB5G5R5) | \
				(1 << TEGRA_WIN_FMT_B8G8R8A8) | \
				(1 << TEGRA_WIN_FMT_R8G8B8A8) | \
				(1 << TEGRA_WIN_FMT_B6x2G6x2R6x2A8) | \
				(1 << TEGRA_WIN_FMT_R6x2G6x2B6x2A8) | \
				(1 << TEGRA_WIN_FMT_YCbCr422) | \
				(1 << TEGRA_WIN_FMT_YUV422) | \
				(1 << TEGRA_WIN_FMT_YCbCr420P) | \
				(1 << TEGRA_WIN_FMT_YUV420P) | \
				(1 << TEGRA_WIN_FMT_YCbCr422P) | \
				(1 << TEGRA_WIN_FMT_YUV422P) | \
				(1 << TEGRA_WIN_FMT_YCbCr422R) | \
				(1 << TEGRA_WIN_FMT_YUV422R) | \
				(1 << TEGRA_WIN_FMT_YCbCr422RA) | \
				(1 << TEGRA_WIN_FMT_YUV422RA))

#define TEGRA_WIN_FMT_WIN_A	((1 << TEGRA_WIN_FMT_P1) | \
				(1 << TEGRA_WIN_FMT_P2) | \
				(1 << TEGRA_WIN_FMT_P4) | \
				(1 << TEGRA_WIN_FMT_P8) | \
				(1 << TEGRA_WIN_FMT_B4G4R4A4) | \
				(1 << TEGRA_WIN_FMT_B5G5R5A) | \
				(1 << TEGRA_WIN_FMT_B5G6R5) | \
				(1 << TEGRA_WIN_FMT_AB5G5R5) | \
				(1 << TEGRA_WIN_FMT_B8G8R8A8) | \
				(1 << TEGRA_WIN_FMT_R8G8B8A8) | \
				(1 << TEGRA_WIN_FMT_B6x2G6x2R6x2A8) | \
				(1 << TEGRA_WIN_FMT_R6x2G6x2B6x2A8))

#define TEGRA_WIN_FMT_WIN_B	(TEGRA_WIN_FMT_BASE & \
				~(1 << TEGRA_WIN_FMT_B8G8R8A8) & \
				~(1 << TEGRA_WIN_FMT_R8G8B8A8))

#define TEGRA_WIN_FMT_WIN_C	TEGRA_WIN_FMT_BASE

#define UNDEFINED	-1
#define MAX_WIDTH	0
#define MIN_WIDTH	1
#define MAX_HEIGHT	2
#define MIN_HEIGHT	3
#define CHECK_SIZE(val, min, max)	( \
		((val) < (min) || (val) > (max)) ? -EINVAL : 0)

/* Available operations of feature table. */
#define HAS_SCALE		1
#define HAS_TILED		2
#define HAS_V_FILTER		3
#define HAS_H_FILTER		4
#define HAS_GEN2_BLEND		5
#define GET_WIN_FORMATS		6
#define GET_WIN_SIZE		7

enum tegra_dc_feature_option {
	TEGRA_DC_FEATURE_FORMATS,
	TEGRA_DC_FEATURE_BLEND_TYPE,
	TEGRA_DC_FEATURE_MAXIMUM_SIZE,
	TEGRA_DC_FEATURE_MAXIMUM_SCALE,
	TEGRA_DC_FEATURE_FILTER_TYPE,
	TEGRA_DC_FEATURE_LAYOUT_TYPE,
	TEGRA_DC_FEATURE_INVERT_TYPE,
};

struct tegra_dc_feature_entry {
	int window_index;
	enum tegra_dc_feature_option option;
	long arg[ENTRY_SIZE];
};

struct tegra_dc_feature {
	unsigned num_entries;
	struct tegra_dc_feature_entry *entries;
};

int tegra_dc_feature_has_scaling(struct tegra_dc *dc, int win_idx);
int tegra_dc_feature_has_tiling(struct tegra_dc *dc, int win_idx);
int tegra_dc_feature_has_filter(struct tegra_dc *dc, int win_idx, int operation);

long *tegra_dc_parse_feature(struct tegra_dc *dc, int win_idx, int operation);
void tegra_dc_feature_register(struct tegra_dc *dc);
#endif