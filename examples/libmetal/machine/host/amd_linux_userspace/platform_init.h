/*
 * Copyright (C) 2025, Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __PLATFORM_INIT_H__
#define __PLATFORM_INIT_H__

struct channel_s;
struct app_platform_options {
	const char *shm_dev_name;
	const char *desc0_dev_name;
	const char *desc1_dev_name;
	const char *ipi_dev_name;
	const char *ttc_dev_name;
	const char *ipi_remote_mask_property;
};

void platform_get_default_options(struct app_platform_options *options);
int platform_init(struct channel_s *ch,
		  const struct app_platform_options *options);
void platform_cleanup(struct channel_s *ch);

#endif /* __PLATFORM_INIT_H__ */
