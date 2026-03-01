/*
 * Copyright (c) CompanyNameMagicTag 2023-2023. All rights reserved.
 */

#include <asm/setup.h>
#include <linux/kthread.h>
#include <linux/sort.h>
#include <linux/device.h>
#include <linux/mod_devicetable.h>
#include <linux/init.h>
#include <linux/delay.h>

#define MAX_VERTICES DRVID_MAX /* maximum number of vertices */
#define MAX_EDGES (MAX_VERTICES * 5) /* maximum number of edges (dependencies) */
struct edgenode {
	unsigned y; /* initcall ID */
#ifdef CONFIG_DEPENDENCIES_PARALLEL
	unsigned x;
#endif
	struct edgenode *next; /* next edge in list */
};

/* Vertex numbers correspond to initcall IDs. */
static struct edgenode edge_slots[MAX_EDGES] __initdata; /* avoid kmalloc */
static struct edgenode *edges[MAX_VERTICES] __initdata; /* adjacency info */
static unsigned nedges __initdata; /* number of edges */
static unsigned nvertices __initdata; /* number of vertices */
static bool processed[MAX_VERTICES] __initdata;
static bool include_node[MAX_VERTICES] __initdata;
static bool discovered[MAX_VERTICES] __initdata;

static unsigned order[MAX_VERTICES] __initdata;
static unsigned norder __initdata;
static const struct _annotated_initcall
	*annotated_initcall_by_drvid[MAX_VERTICES] __initdata;
#ifdef CONFIG_LOAD_VI_ONLY
#if defined (CONFIG_MPPQUICKKO_LOAD_FROM_XXX_SUPPORT_PM)
#define WHITELIST_COUNT (15)
#else
#define WHITELIST_COUNT (15)
#endif
#else // CONFIG_LOAD_VI_ONLY
#ifdef CONFIG_MPPQUICKKO_LOAD_FROM_XXX_SUPPORT_VO
#if defined (CONFIG_MPPQUICKKO_LOAD_FROM_XXX_SUPPORT_PM)
#define WHITELIST_COUNT (26)
#else
#define WHITELIST_COUNT (25)
#endif
#else // CONFIG_MPPQUICKKO_LOAD_FROM_XXX_SUPPORT_VO
#if defined (CONFIG_MPPQUICKKO_LOAD_FROM_XXX_SUPPORT_PM)
#define WHITELIST_COUNT (23)
#else
#define WHITELIST_COUNT (22)
#endif
#endif
static const struct _annotated_initcall
	*annotated_initcall_mppko_whitelist_by_drvid[WHITELIST_COUNT] __initdata;
static const unsigned annotated_initcall_mppko_whitelist_drvid[WHITELIST_COUNT] = {
		initcall_id(ot_sysconfig_init),
		initcall_id(osal_module_init),
		initcall_id(mmz_init),
		initcall_id(base_mod_init),
		initcall_id(vb_mod_init),
		initcall_id(vca_mod_init),
		initcall_id(g_ot_sys_driver_init),
		initcall_id(rgn_mod_init),
		initcall_id(vpp_mod_init),
		initcall_id(g_ot_vgs_device_driver_init),
		initcall_id(g_ot_vi_driver_init),
		initcall_id(isp_mod_init),
		initcall_id(g_ot_vpss_driver_init),
		initcall_id(chnl_mod_init),
		initcall_id(rc_mod_init),
		initcall_id(venc_mod_init),
		initcall_id(h264e_mod_init),
		initcall_id(h265e_mod_init),
		initcall_id(jpege_mod_init),
		initcall_id(g_mipirx_driver_init),
		initcall_id(i2c_module_init),
		initcall_id(pm_mod_init),
};
#endif

int __init add_initcall_dependency(unsigned id, unsigned id_dependency)
{
	struct edgenode *p;

	if (!id || !id_dependency)
		return 0; /* ignore root */
	if (unlikely(nedges >= MAX_EDGES)) {
		pr_err("init: maximum number of edges (%u) reached!\n",
		       MAX_EDGES);
		return -EINVAL;
	}
	if (unlikely(id == id_dependency))
		return 0;
	if (!include_node[id] || !include_node[id_dependency])
		return 0; /* ignore edges for initcalls not included */
	p = &edge_slots[nedges++];
	p->y = id_dependency;
#ifdef CONFIG_DEPENDENCIES_PARALLEL
	p->x = id;
#endif
	/* insert at head of list */
	p->next = edges[id];
	edges[id] = p;

	return 0;
}

static int __init depth_first_search(unsigned v)
{
	struct edgenode *p;
	unsigned y; /* successor vertex */

	discovered[v] = 1;
	p = edges[v];
	while (p) {
		y = p->y;
		if (unlikely(discovered[y] && !processed[y])) {
			pr_err("init: cycle found %u <-> %u!\n", v, y);
			return -EINVAL;
		}
		if (!discovered[y] && depth_first_search(y))
			return -EINVAL;
		p = p->next;
	}
	order[norder++] = v;
	processed[v] = 1;
	return 0;
}

static int __init topological_sort(void)
{
	unsigned i;

	for (i = 1; i <= nvertices; ++i)
		if (!discovered[i] && include_node[i])
			if (depth_first_search(i))
				return -EINVAL;
	return 0;
}

#ifdef CONFIG_DEPENDENCIES_PARALLEL
/*
 * The algorithm I've used below to calculate the max. distance for
 * nodes to the root node likely isn't the fasted. But based on the
 * already done implementation of the topological sort, this is an
 * easy way to achieve this. Instead of first doing an topological
 * sort and then using the stuff below to calculate the distances,
 * using an algorithm which does spit out distances directly would
 * be likely faster (also we are talking here about a few ms).
 * If you want to spend the time, you could have a look e.g. at the
 * topic 'layered graph drawing'.
 */
/* max. distance from a node to root */
static unsigned distance[MAX_VERTICES] __initdata;
static struct {
	unsigned start;
	unsigned length;
} tgroup[20] __initdata;
static unsigned count_groups __initdata;
static __initdata DECLARE_COMPLETION(initcall_thread_done);
static atomic_t shared_counter __initdata;
static atomic_t count_initcall_threads __initdata;
static atomic_t ostart __initdata;
static atomic_t ocount __initdata;
static atomic_t current_group __initdata;
static unsigned num_threads __initdata;
static __initdata DECLARE_WAIT_QUEUE_HEAD(group_waitqueue);

static void __init calc_max_distance(uint32_t v)
{
	unsigned i;
	unsigned max_dist = 0;
	pr_debug("edges num is %u +++++++++++++++++++++\n", nedges);
	for (i = 0; i < nedges; ++i)
		if (edge_slots[i].x == v)
			max_dist = max(max_dist, distance[edge_slots[i].y] + 1);
	distance[v] = max_dist;
}

static void __init calc_distances(void)
{
	unsigned i;

	for (i = 0; i < norder; ++i)
		calc_max_distance(order[i]);
}

static int __init compare_by_distance(const void *lhs, const void *rhs)
{
	if (distance[*(unsigned *)lhs] < distance[*(unsigned *)rhs])
		return -1;
	if (distance[*(unsigned *)lhs] > distance[*(unsigned *)rhs])
		return 1;
	if (*(unsigned *)lhs < *(unsigned *)rhs)
		return -1;
	if (*(unsigned *)lhs > *(unsigned *)rhs)
		return 1;
	return 0;
}

static void __init build_order_by_distance(void)
{
	calc_distances();
	sort(order, norder, sizeof(unsigned), &compare_by_distance, NULL);
}

static void __init build_tgroups(void)
{
	unsigned i;
	unsigned dist = 0;

	for (i = 0; i < norder; ++i) {
		if (distance[order[i]] != dist) {
			dist = distance[order[i]];
			count_groups++;
			tgroup[count_groups].start = i;
		}
		tgroup[count_groups].length++;
	}
	count_groups++;
#ifdef DEBUG
	for (i = 0; i < count_groups; ++i)
		pr_info("init: group %u length %u (start %u)\n", i,
			tgroup[i].length, tgroup[i].start);
#endif
}

static void __init init_drivers_non_threaded_for_mppko_whitelist(void)
{
	unsigned i;
	const struct _annotated_initcall *ac;

	for (i = 0; i < WHITELIST_COUNT; i++) {
		ac = annotated_initcall_mppko_whitelist_by_drvid[i];
		if (ac != NULL) {
			pr_debug("do_initcall_media: %ps\n", *ac->initcall);
			do_one_initcall(*ac->initcall);
		}
	}
}

static int __init initcall_thread(void *thread_nr)
{
	int i;
	unsigned group;
	int start, count;
	const struct _annotated_initcall *ac;
	DEFINE_WAIT(wait);
	while ((group = atomic_read(&current_group)) < count_groups) {
		start = atomic_read(&ostart);
		count = atomic_read(&ocount);
		while ((i = atomic_dec_return(&shared_counter)) >= 0) {
			ac = annotated_initcall_by_drvid[order[start + count -
							       1 - i]];
			pr_err("do_initcall: %ps\n", *ac->initcall);
			do_one_initcall(*ac->initcall);
		}
		prepare_to_wait(&group_waitqueue, &wait, TASK_UNINTERRUPTIBLE);
		if (!atomic_dec_and_test(&count_initcall_threads)) {
			/*
			 * The current group was processed, sleep until the
			 * last thread finished work on this group, changes
			 * the group and wakes up all threads.
			 */
			schedule();
			finish_wait(&group_waitqueue, &wait);
			continue;
		}
		atomic_inc(&current_group);
		atomic_set(&count_initcall_threads, num_threads);
		if (++group >= count_groups) {
			/*
			 * All groups processed and all threads finished.
			 * Prepare to process unordered annotated
			 * initcalls and wake up other threads to call
			 * them too.
			 */
			atomic_set(
				&shared_counter,
				__annotated_initcall_end -
					__annotated_initcall_start); // here changed by yu
			wake_up_all(&group_waitqueue);
			finish_wait(&group_waitqueue, &wait);
			break;
		}
		/*
		 * Finalize the switch to the next group and wake up other
		 * threads to process the new group too.
		 */
		atomic_set(&ostart, tgroup[group].start);
		atomic_set(&ocount, tgroup[group].length);
		atomic_set(&shared_counter, tgroup[group].length);
		wake_up_all(&group_waitqueue);
		finish_wait(&group_waitqueue, &wait);
	}
	if (atomic_dec_and_test(&count_initcall_threads))
		complete(&initcall_thread_done);
	do_exit(0);
	return 0;
}
#endif /* CONFIG_DEPENDENCIES_PARALLEL */

static void __init init_drivers_non_threaded(void)
{
	unsigned i;
	const struct _annotated_initcall *ac;

	for (i = 0; i < norder; ++i) {
		ac = annotated_initcall_by_drvid[order[i]];
		do_one_initcall(*ac->initcall);
	}
}

static int __init add_dependencies(void)
{
	int rc;
	const struct _annotated_initcall *ac;
	const unsigned *dep;
	unsigned i;
	pr_debug("add_dependencies++++++++++\n");

	ac = __annotated_initcall_start;
	for (; ac < __annotated_initcall_end; ++ac) {
		dep = ac->dependencies;
		pr_debug(
			"for cnt ac : %p  dep = %p ---------\n",
			ac, dep);
		if (dep)
			for (i = 0; dep[i]; ++i) {
				pr_debug("ac->id = %d  dependency = %d", ac->id,
					 dep[i]);
				rc = add_initcall_dependency(ac->id, dep[i]);
				if (unlikely(rc))
					return rc;
			}
	}
	return 0;
}


static bool __init find_mppko_whitelist(unsigned int id, int* index)
{
	int i;
	for (i = 0; i < WHITELIST_COUNT; i++) {
		if (id == annotated_initcall_mppko_whitelist_drvid[i]) {
			*index = i;
			return true;
		}
	}
	return false;
}

static void __init build_inventory(void)
{
	const struct _annotated_initcall *ac;
	int index = 0;

	ac = __annotated_initcall_start;
	for (; ac < __annotated_initcall_end; ++ac) {
		if (find_mppko_whitelist(ac->id, &index)) {
			pr_info("find the mppko ac id=%d in whitelist, index=%d\n", ac->id, index);
			annotated_initcall_mppko_whitelist_by_drvid[index] = ac;
			continue;
		}
		include_node[ac->id] = true;
		annotated_initcall_by_drvid[ac->id] = ac;
		nvertices = max(nvertices, ac->id);
	}
}

static int __init build_order(void)
{
	int rc = 0;

	build_inventory();
	add_dependencies();
	if (topological_sort()) {
		return -EINVAL; /* cycle found */
	}
	pr_debug(
		"init: vertices: %u edges %u count %u  ++++++++++++++++++++++++++++++++++\n",
		nvertices, nedges, norder);
#ifdef CONFIG_DEPENDENCIES_PARALLEL
	build_order_by_distance();
	build_tgroups();
#endif
	return rc;
}

void __init do_annotated_initcalls(void)
{
	unsigned i;

	printk("config_dependencies is y\n");
	i = __annotated_initcall_end - __annotated_initcall_start;
	pr_debug(
		"i =  %u, __annotated_initcall_end = %px, __annotated_initcall_start = %px\n",
		i, __annotated_initcall_end, __annotated_initcall_start);
	pr_debug("__con_initcall_end = %px ,__con_initcall_start = %px\n",
		 __con_initcall_end, __con_initcall_start);
	if (!i)
		return;

	if (build_order()) {
		/*
		 * Building order failed (likely because of a dependency
		 * circle). Try to boot anyway by calling all annotated
		 * initcalls unordered.
		 */
		const struct _annotated_initcall *ac;

		ac = __annotated_initcall_start;
		for (; ac < __annotated_initcall_end; ++ac)
			do_one_initcall(*ac->initcall);
		return;
	}

#ifndef CONFIG_DEPENDENCIES_PARALLEL
	init_drivers_non_threaded();
#else
	if (CONFIG_DEPENDENCIES_THREADS == 0)
		num_threads = num_online_cpus();
	else
		num_threads = CONFIG_DEPENDENCIES_THREADS;
	if (num_threads < 2) {
		init_drivers_non_threaded();
		return;
	}
	pr_info("threadsnum = %u -----------------------------------\n",
		 num_threads);
	pr_info("init: using %u threads to call annotated initcalls\n",
		 num_threads);
	atomic_set(&count_initcall_threads, num_threads);
	atomic_set(&ostart, tgroup[0].start);
	atomic_set(&ocount, tgroup[0].length);
	atomic_set(&shared_counter, tgroup[0].length);
	atomic_set(&current_group, 0);

	for (i = 0; i < num_threads; ++i) {
	    	struct task_struct *p;
		p = kthread_create_on_cpu(initcall_thread, (void *)(unsigned long)i, i, "initcalls");
		if (!IS_ERR(p))
		    wake_up_process(p);
	}

	wait_for_completion(&initcall_thread_done);
	init_drivers_non_threaded_for_mppko_whitelist();
	pr_debug("init: all threads done\n");
#endif
}
