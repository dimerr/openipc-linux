/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2020-2023. All rights reserved.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/kthread.h>

#include <asm/io.h>

#if ((defined CONFIG_ARCH_BSP) && (defined CONFIG_CMD_TIMESTAMP))
#define TIMESTAMP_MAGIC_VALUE  0x55aa55aa
#define TIMESTAMP_MAGIC_OFFSET 0
#define TIMESTAMP_COUNT_OFFSET 4
#define TIMESTAMP_ITEM_OFFSET  8
#define TIMESTAMP_NAME_LEN 64
#define TIMESTAMP_COUNT_MAX	100
typedef struct {
	char name[TIMESTAMP_NAME_LEN];
	unsigned int line;
	unsigned int stamp;
} timestamp_item;

static phys_addr_t timestamp_record_start;
static unsigned long timestamp_record_size;
static int __init early_timestamp(char *p)
{
	phys_addr_t start;
	unsigned long size;
	char *endp;

	start = memparse(p, &endp);
	if (*endp == ',') {
		size = memparse(endp + 1, NULL);

		timestamp_record_start = start;
		timestamp_record_size = size;
	}
	return 0;
}
early_param("timestamp", early_timestamp);

void boot_timestamp_print(void)
{
	void __iomem *base;
	unsigned int i;
	unsigned int value;
	unsigned int count;
	timestamp_item *item;

#ifdef CONFIG_PHYS_ADDR_T_64BIT
	printk(KERN_INFO "timestamp_record_start:0x%llx timestamp_record_size:0x%lx\n",
		timestamp_record_start, timestamp_record_size);
#else
	printk(KERN_INFO "timestamp_record_start:0x%x timestamp_record_size:0x%lx\n",
		timestamp_record_start, timestamp_record_size);

#endif
	if (timestamp_record_start == 0 || timestamp_record_size == 0) {
		printk(KERN_INFO "boot args not pass timestamp params to kernel, please check the bootargs");
		return;
	}

	base = ioremap(timestamp_record_start, timestamp_record_size);
	value = *((volatile unsigned int *)(base + TIMESTAMP_MAGIC_OFFSET));
	if (value != TIMESTAMP_MAGIC_VALUE) {
		iounmap(base);
		printk(KERN_INFO "error: timestamp area maybe overlay:0x%x\n", value);
		return;
	}
	count = *((volatile unsigned int *)(base + TIMESTAMP_COUNT_OFFSET));
	item = (timestamp_item *)(base + TIMESTAMP_ITEM_OFFSET);
	printk(KERN_INFO "count:%d\n", count);
	for (i = 0; i < count && count < TIMESTAMP_COUNT_MAX; i++) {
		printk_deferred(" -%d- boot timestamp %s @ %u\n", i, item[i].name, item[i].stamp);
	}
	iounmap(base);
}
#endif

#if ((defined CONFIG_ARCH_BSP) && (defined CONFIG_VENDOR_TIMER_TRIGGER_RCU))
/* fast rcu */
static int rcu_wake_thread_func(void *fn)
{
	int i;
	int rs_times = 20;
	int wait_usecs = 1000;

	for (i = 0; i < rs_times; i++) {
		raise_softirq(RCU_SOFTIRQ);
		usleep_range(wait_usecs, wait_usecs);
	}

	return 0;
}

void do_fast_rcu(void)
{
	unsigned i;
	int cpu_nums = num_online_cpus();

	for (i = 0; i < cpu_nums; ++i) {
		struct task_struct *task = kthread_create_on_cpu(rcu_wake_thread_func, NULL, i, "fast_rcu_thread");
		if (IS_ERR_OR_NULL(task)) {
			pr_err("Create fast_rcu_thread thread failed\n");
			continue;
		} else {
			wake_up_process(task);
		}
	}
}
#endif

