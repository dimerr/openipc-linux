/*
 * Copyright (c) CompanyNameMagicTag 2023-2023. All rights reserved.
 */
#ifndef _LINUX_ANNOTATED_H
#define _LINUX_ANNOTATED_H
#include <linux/drv_def.h>
typedef int (*threadfn)(void *);

struct device_driver;

struct _annotated_initcall {
	const initcall_t initcall;
	const unsigned id; /* from driver_ids.h */
	const unsigned *dependencies;
	const struct device_driver *driver;
};
extern const struct _annotated_initcall __annotated_initcall_start[],
				  __annotated_initcall_end[];


extern initcall_entry_t __security_initcall_start[], __security_initcall_end[];

static int const level3_dep[2] = {0};
static int const level4_dep[2] = {LEVEL3_START, 0};
static int const level5_dep[2] = {LEVEL4_START, 0};
static int const level6_dep[2] = {LEVEL5_START, 0};
static int const level7_dep[2] = {LEVEL6_START, 0};

#define __define_annotated_initcall_dep_level(fn, _dep_level_, deps) \
	static struct _annotated_initcall __annotated_initcall_##fn __used \
	__attribute__((__section__(".annotated_initcall.init"))) = \
		{ .initcall = (fn), .id = fn ## _drv_id, \
		  .dependencies = level##_dep_level_##_dep};

#define __define_annotated_initcall(fn, __id, deps) \
	static struct _annotated_initcall __annotated_initcall_##fn __used \
	__attribute__((__section__(".annotated_initcall.init"))) = \
		{ .initcall = (fn), .id = (__id), .dependencies = (deps), \
		  .driver = NULL }

#define __define_annotated_initcall_drv(fn, __id, deps, drv) \
	static struct _annotated_initcall __annotated_initcall_##fn __used \
	__attribute__((__section__(".annotated_initcall.init"))) = \
		{ .initcall = (fn), .id = (__id), .dependencies = (deps), \
		  .driver = &(drv) }

extern bool overflow_enabled;

void __init load_default_modules(void);

/* Defined in init/dependencies.c */
void __init do_annotated_initcalls(void);

/* id_dependency will be initialized before id */
int __init add_initcall_dependency(unsigned id, unsigned id_dependency);

extern struct async_domain  populate_rootfs_domain;

/*
 * Annotated initcalls are accompanied by a struct device_driver.
 * This makes initcalls identifiable and is used to order initcalls.
 *
 * If disabled, nothing is changed and the classic level based
 * initialization sequence is in use.
 */
#ifdef CONFIG_BSP_FAST_STARTUP
#define annotated_module_init(fn, id, deps) \
	__define_annotated_initcall(fn, id, deps)
#define annotated_module_init_drv(fn, id, deps, drv) \
	__define_annotated_initcall_drv(fn, id, deps, drv)
#define annotated_initcall(level, fn, id, deps) \
	__define_annotated_initcall(fn, id, deps)
#define annotated_initcall_sync(level, fn, id, deps) \
	__define_annotated_initcall(fn, id, deps)
#define annotated_initcall_drv(level, fn, id, deps, drv) \
	__define_annotated_initcall_drv(fn, id, deps, drv)
#define annotated_initcall_drv_sync(level, fn, id, deps, drv) \
	__define_annotated_initcall_drv(fn, id, deps, drv)
#else
#define annotated_module_init(fn, id, deps)	module_init(fn)
#define annotated_module_init_drv(fn, id, deps, drv)	module_init(fn)
#define annotated_initcall(level, fn, id, deps)	level ## _initcall(fn)
#define annotated_initcall_sync(level, fn, id, deps) \
	level ## _initcall_sync(fn)
#define annotated_initcall_drv(level, fn, id, deps, drv) \
	level ## _initcall(fn)
#define annotated_initcall_drv_sync(level, fn, id, deps, drv) \
	level ## _initcall_sync(fn)
#endif


#ifdef CONFIG_BSP_FAST_STARTUP
#define pure_initcall(fn)		__define_initcall(fn, 0)
#define core_initcall(fn)		__define_initcall(fn, 1)
#define core_initcall_sync(fn)		__define_initcall(fn, 1s)
#define postcore_initcall(fn)		__define_initcall(fn, 2)
#define postcore_initcall_sync(fn)	__define_initcall(fn, 2s)

// start annotated
#define arch_initcall(fn)		__define_annotated_initcall_dep_level(fn, 3, level3_dep)
#define arch_initcall_sync(fn)		__define_annotated_initcall_dep_level(fn, 3, level3_dep)
#define subsys_initcall(fn)		__define_annotated_initcall_dep_level(fn, 4, level4_dep)
#define subsys_initcall_sync(fn)	__define_annotated_initcall_dep_level(fn, 4, level4_dep)
#define fs_initcall(fn)			__define_annotated_initcall_dep_level(fn, 5, level5_dep)
#define fs_initcall_sync(fn)		__define_annotated_initcall_dep_level(fn, 5, level5_dep)
#define rootfs_initcall(fn)		__define_annotated_initcall_dep_level(fn, 5, level5_dep)
#define device_initcall(fn)		__define_annotated_initcall_dep_level(fn, 6, level6_dep)
#define device_initcall_sync(fn)	__define_annotated_initcall_dep_level(fn, 6, level6_dep)
#define late_initcall(fn)		__define_annotated_initcall_dep_level(fn, 7, level7_dep)
#define late_initcall_sync(fn)		__define_annotated_initcall_dep_level(fn, 7, level7_dep)
#endif
#endif /* _LINUX_ANNOTATED_H */
