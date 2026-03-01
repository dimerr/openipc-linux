 /*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2020-2023. All rights reserved.
 */

#include <core.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/usb.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/proc_fs.h>

#include "proc.h"

#define MODE_BUF_LEN 32

#define	WING_USB_PROC        "wing-usb"
static struct proc_dir_entry *wing_usb_proc_root;

static ssize_t wing_usb_mode_write(struct file *file,
	const char __user *ubuf, size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct wing_usb *wusb = s->private;
	struct wing_usb_event usb_event = {0};

	char buf[MODE_BUF_LEN] = {0};

	if (ubuf == NULL)
		return -EFAULT;

	if (copy_from_user(buf, ubuf, min_t(size_t, sizeof(buf) - 1, count)))
		return -EFAULT;

	if (strncmp(buf, "device", strlen("device")) == 0) {
		usb_event.type = SWITCH_TO_DEVICE;
	} else if (strncmp(buf, "host", strlen("host")) == 0) {
		usb_event.type = SWITCH_TO_HOST;
	} else {
		usb_event.type = NONE_EVENT;
		wing_usb_err("input event type error\n");
		return -EINVAL;
	}

	usb_event.ctrl_id = wusb->id;
	wing_usb_queue_event(&usb_event, wusb);

	wing_usb_dbg("write %s\n", buf);

	return count;
}

static int wing_usb_mode_show(struct seq_file *s, void *v)
{
	struct wing_usb *wusb = s->private;
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&wusb->event_lock, flags);
	reg = readl(wusb->ctrl_base + DWC3_GCTL);
	spin_unlock_irqrestore(&wusb->event_lock, flags);
	switch (DWC3_GCTL_PRTCAP(reg)) {
	case DWC3_GCTL_PRTCAP_HOST:
		seq_printf(s, "host\n");
		wusb->state = WING_USB_STATE_HOST;
		break;
	case DWC3_GCTL_PRTCAP_DEVICE:
		seq_printf(s, "device\n");
		wusb->state = WING_USB_STATE_DEVICE;
		break;
	case DWC3_GCTL_PRTCAP_OTG:
		seq_printf(s, "otg\n");
		break;
	default:
		seq_printf(s, "UNKNOWN %08x\n", DWC3_GCTL_PRTCAP(reg));
	}

	return 0;
}

static int wing_usb_mode_open(struct inode *inode, struct file *file)
{
	return single_open(file, wing_usb_mode_show, PDE_DATA(inode));
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0))
static const struct proc_ops g_wing_usb_proc_mode_ops = {
	.proc_open		= wing_usb_mode_open,
	.proc_write		= wing_usb_mode_write,
	.proc_read		= seq_read,
	.proc_release		= single_release,
};

#else
static const struct file_operations g_wing_usb_proc_mode_ops = {
	.open		= wing_usb_mode_open,
	.write		= wing_usb_mode_write,
	.read		= seq_read,
	.release	= single_release,
};

#endif

int wing_usb_create_proc_entry_for_ctrl(struct device *dev, struct wing_usb *wusb)
{
	struct proc_dir_entry *proc_entry = NULL;

	wing_usb_dbg("+\n");

	if (wusb == NULL)
		return -EINVAL;

	proc_entry = proc_mkdir(dev_name(dev), wing_usb_proc_root);
	if (proc_entry == NULL) {
		wing_usb_err("failed to create proc file\n");
		return -ENOMEM;
	}

	wusb->proc_entry = proc_entry;

	if (proc_create_data("mode", S_IRUGO | S_IWUSR, proc_entry,
		&g_wing_usb_proc_mode_ops, wusb) == NULL) {
		wing_usb_err("Failed to create proc file mode \n");
		goto remove_entry;
	}

	wing_usb_dbg("-\n");
	return 0;

remove_entry:
	remove_proc_entry(dev_name(dev), wing_usb_proc_root);
	wusb->proc_entry = NULL;

	return -ENOMEM;
}

void wing_usb_remove_proc_entry_for_ctrl(struct device *dev, struct wing_usb *wusb)
{
	if (wusb->proc_entry == NULL)
		return;

	remove_proc_entry("mode", wusb->proc_entry);
	remove_proc_entry(dev_name(dev), wing_usb_proc_root);

	wusb->proc_entry = NULL;
}

/* for bug/feature list */
struct bug_feature_list {
	char *id;
	char *desc;
};

struct bug_feature_list feature_list[] = {
	{"20240001", "USB UVC support yuv420 4k30fps"},
	{"20240002", "USB UVC support multi_channel"},
	{"20240003", "USB support UVC1.5"},
	{"20240004", "USB UVC support bulk mode"},
	{"20240005", "USB UVC support v4l2 mode"},
	{"20240006", "USB UVC support Still Image"},
	{"20240007", "USB UVC support disable status endpoint"},
	{"20240008", "USB UAC microphone speaker configurable"},
};

struct bug_feature_list bug_list[] = {
	{"20240004", "USB fix UVC function crash when killing uvc_app"},
	{"20240005", "USB fix UVC device CV test (get alt) failure"},
	{"20240006", "USB fix a transmission failure caused by sending cmdioc"},
	{"20240007", "USB add support for filter_se0_fsls_eop"},
	{"20240008", "USB disable LPM feature"},
	{"20250001", "USB fix an unrecognized issue during switching between UVC and MTP"},
	{"20250002", "USB fix UVC video image freeze when playing h265 1440p"},
	{"20250003", "USB set zero length when send zero pack"},
	{"20250004", "USB UVC disable endpoints if it is enabled when closing video"},
};

static int wing_usb_bug_feature_list_show(struct seq_file *s, void *v)
{
	int index;

	seq_puts(s, "Feature List:\n");
	for (index = 0; index < ARRAY_SIZE(feature_list); index++) {
		seq_printf(s, "%-10s : %s\n",
				feature_list[index].id, feature_list[index].desc);
	}

	seq_puts(s, "\nBug List:\n");
	for (index = 0; index < ARRAY_SIZE(bug_list); index++) {
		seq_printf(s, "%-10s : %s\n",
				bug_list[index].id, bug_list[index].desc);
	}

	return 0;
}

static int wing_usb_bug_feature_list_open(struct inode *inode, struct file *file)
{
	return single_open(file, wing_usb_bug_feature_list_show, PDE_DATA(inode));
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0))
static const struct proc_ops g_wing_usb_proc_bug_feature_list_ops = {
	.proc_open		= wing_usb_bug_feature_list_open,
	.proc_read		= seq_read,
	.proc_release		= single_release,
};
#else
static const struct file_operations g_wing_usb_proc_bug_feature_list_ops = {
	.open		= wing_usb_bug_feature_list_open,
	.read		= seq_read,
	.release	= single_release,
};
#endif

static int wing_usb_bug_feature_list_create_proc_entry(void)
{
	if (proc_create("bug_feature_list", 0, wing_usb_proc_root,
		&g_wing_usb_proc_bug_feature_list_ops) == NULL) {
		wing_usb_err("Failed to create proc file bug_feature_list \n");
		return -ENOMEM;
	}

	return 0;
}

static void wing_usb_bug_feature_list_remove_proc_entry(void)
{
	remove_proc_entry("bug_feature_list", wing_usb_proc_root);
}

int wing_usb_create_proc_entry(void)
{
	struct proc_dir_entry *proc_entry = NULL;
	proc_entry = proc_mkdir(WING_USB_PROC, NULL);
	if (proc_entry == NULL) {
		wing_usb_err("Failed to create wing usb proc file \n");
		return -ENOMEM;
	}
	wing_usb_proc_root = proc_entry;

	if (wing_usb_bug_feature_list_create_proc_entry() != 0)
		goto remove_entry;

	return 0;

remove_entry:
	remove_proc_entry(WING_USB_PROC, NULL);

	return -ENOMEM;
}

void wing_usb_remove_proc_entry(void)
{
	wing_usb_bug_feature_list_remove_proc_entry();
	remove_proc_entry(WING_USB_PROC, NULL);
}
