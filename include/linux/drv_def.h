/*
 * Copyright (c) CompanyNameMagicTag 2023-2023. All rights reserved.
 */
#ifndef _LINUX_DRV_DEF_H
#define _LINUX_DRV_DEF_H

/*
 * In fact, the IDs listed here are IDs for initcalls, and not for
 * drivers. But most of the time, a driver or subsystem has only one
 * initcall, and talking about IDs for drivers makes more sense than
 * talking about initcalls, something many people have no clear
 * understanding about.
 *
 * Please use the name of the module as the name for the ID if
 * something can be build as a module.
 */


/* define the enumeration of all driver */
#define initcall_id(_x) _x ## _drv_id,

enum drv_id {
	DRVID_UNUSED,
#include <linux/driver_ids.h>
	/* To be filled */
	DRVID_MAX,
};

#undef initcall_id
#define initcall_id(_x) _x ## _drv_id
enum level_start {
	LEVEL0_START = initcall_id(ipc_ns_init),
	LEVEL1_START = initcall_id(fpsimd_init),
	LEVEL2_START = initcall_id(debug_monitors_init),
	LEVEL3_START = initcall_id(reserve_memblock_reserved_regions),
	LEVEL4_START = initcall_id(topology_init),
	LEVEL5_START = initcall_id(create_debug_debugfs_entry),
	LEVEL6_START = initcall_id(register_arm64_panic_block),
	LEVEL7_START = initcall_id(init_oops_id),
	LEVEL8_START = initcall_id(populate_rootfs),
};

#endif /* _LINUX_DRV_DEF_H */
