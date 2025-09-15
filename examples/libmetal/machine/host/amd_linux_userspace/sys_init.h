/******************************************************************************
 *
 * Copyright (C) 2025, Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 ******************************************************************************/

#ifndef __SYS_INIT_H__
#define __SYS_INIT_H__

#include "sys_init.h"

int sys_init(struct channel_s *ch);
void sys_cleanup(struct channel_s *ch);

#endif /* __SYS_INIT_H__ */

