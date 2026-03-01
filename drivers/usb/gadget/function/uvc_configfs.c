// SPDX-License-Identifier: GPL-2.0
/*
 * uvc_configfs.c
 *
 * Configfs support for the uvc function.
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Andrzej Pietrasiewicz <andrzejtp2010@gmail.com>
 */

#include <linux/sort.h>
#ifdef CONFIG_ARCH_BSP
#include <linux/securec.h>
#endif

#include "u_uvc.h"
#include "uvc_configfs.h"

/* -----------------------------------------------------------------------------
 * Global Utility Structures and Macros
 */

#define UVCG_STREAMING_CONTROL_SIZE	1

#define UVC_ATTR(prefix, cname, aname) \
static struct configfs_attribute prefix##attr_##cname = { \
	.ca_name	= __stringify(aname),				\
	.ca_mode	= S_IRUGO | S_IWUGO,				\
	.ca_owner	= THIS_MODULE,					\
	.show		= prefix##cname##_show,				\
	.store		= prefix##cname##_store,			\
}

#define UVC_ATTR_RO(prefix, cname, aname) \
static struct configfs_attribute prefix##attr_##cname = { \
	.ca_name	= __stringify(aname),				\
	.ca_mode	= S_IRUGO,					\
	.ca_owner	= THIS_MODULE,					\
	.show		= prefix##cname##_show,				\
}

#define le8_to_cpu(x)	(x)
#define cpu_to_le8(x)	(x)

static int uvcg_config_compare_u32(const void *l, const void *r)
{
	u32 li = *(const u32 *)l;
	u32 ri = *(const u32 *)r;

	return li < ri ? -1 : li == ri ? 0 : 1;
}

static inline struct f_uvc_opts *to_f_uvc_opts(struct config_item *item)
{
	return container_of(to_config_group(item), struct f_uvc_opts,
			    func_inst.group);
}

struct uvcg_config_group_type {
	struct config_item_type type;
	const char *name;
	const struct uvcg_config_group_type **children;
	int (*create_children)(struct config_group *group);
};

static void uvcg_config_item_release(struct config_item *item)
{
	struct config_group *group = to_config_group(item);

	kfree(group);
}

static struct configfs_item_operations uvcg_config_item_ops = {
	.release	= uvcg_config_item_release,
};

static int uvcg_config_create_group(struct config_group *parent,
				    const struct uvcg_config_group_type *type);

static int uvcg_config_create_children(struct config_group *group,
				const struct uvcg_config_group_type *type)
{
	const struct uvcg_config_group_type **child;
	int ret;

	if (type->create_children)
		return type->create_children(group);

	for (child = type->children; child && *child; ++child) {
		ret = uvcg_config_create_group(group, *child);
		if (ret < 0)
			return ret;
	}

	return 0;
}

static int uvcg_config_create_group(struct config_group *parent,
				    const struct uvcg_config_group_type *type)
{
	struct config_group *group;

	group = kzalloc(sizeof(*group), GFP_KERNEL);
	if (!group)
		return -ENOMEM;

	config_group_init_type_name(group, type->name, &type->type);
	configfs_add_default_group(group, parent);

	return uvcg_config_create_children(group, type);
}

static void uvcg_config_remove_children(struct config_group *group)
{
	struct config_group *child, *n;

	list_for_each_entry_safe(child, n, &group->default_groups, group_entry) {
		list_del(&child->group_entry);
		uvcg_config_remove_children(child);
		config_item_put(&child->cg_item);
	}
}

/* -----------------------------------------------------------------------------
 * control/header/<NAME>
 * control/header
 */

DECLARE_UVC_HEADER_DESCRIPTOR(1);

struct uvcg_control_header {
	struct config_item		item;
	struct UVC_HEADER_DESCRIPTOR(1)	desc;
	unsigned			linked;
};

static struct uvcg_control_header *to_uvcg_control_header(struct config_item *item)
{
	return container_of(item, struct uvcg_control_header, item);
}

#define UVCG_CTRL_HDR_ATTR(cname, aname, bits, limit)			\
static ssize_t uvcg_control_header_##cname##_show(			\
	struct config_item *item, char *page)				\
{									\
	struct uvcg_control_header *ch = to_uvcg_control_header(item);	\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &ch->item.ci_group->cg_subsys->su_mutex;\
	int result;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = ch->item.ci_parent->ci_parent->ci_parent;		\
	opts = to_f_uvc_opts(opts_item);				\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf(page, "%u\n", le##bits##_to_cpu(ch->desc.aname));\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
	return result;							\
}									\
									\
static ssize_t								\
uvcg_control_header_##cname##_store(struct config_item *item,		\
			   const char *page, size_t len)		\
{									\
	struct uvcg_control_header *ch = to_uvcg_control_header(item);	\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &ch->item.ci_group->cg_subsys->su_mutex;\
	int ret;							\
	u##bits num;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = ch->item.ci_parent->ci_parent->ci_parent;		\
	opts = to_f_uvc_opts(opts_item);				\
									\
	mutex_lock(&opts->lock);					\
	if (ch->linked || opts->refcnt) {				\
		ret = -EBUSY;						\
		goto end;						\
	}								\
									\
	ret = kstrtou##bits(page, 0, &num);				\
	if (ret)							\
		goto end;						\
									\
	if (num > limit) {						\
		ret = -EINVAL;						\
		goto end;						\
	}								\
	ch->desc.aname = cpu_to_le##bits(num);				\
	ret = len;							\
end:									\
	mutex_unlock(&opts->lock);					\
	mutex_unlock(su_mutex);						\
	return ret;							\
}									\
									\
UVC_ATTR(uvcg_control_header_, cname, aname)

UVCG_CTRL_HDR_ATTR(bcd_uvc, bcdUVC, 16, 0xffff);

UVCG_CTRL_HDR_ATTR(dw_clock_frequency, dwClockFrequency, 32, 0x7fffffff);

#undef UVCG_CTRL_HDR_ATTR

static struct configfs_attribute *uvcg_control_header_attrs[] = {
	&uvcg_control_header_attr_bcd_uvc,
	&uvcg_control_header_attr_dw_clock_frequency,
	NULL,
};

static const struct config_item_type uvcg_control_header_type = {
	.ct_item_ops	= &uvcg_config_item_ops,
	.ct_attrs	= uvcg_control_header_attrs,
	.ct_owner	= THIS_MODULE,
};

static struct config_item *uvcg_control_header_make(struct config_group *group,
						    const char *name)
{
	struct uvcg_control_header *h;

	h = kzalloc(sizeof(*h), GFP_KERNEL);
	if (!h)
		return ERR_PTR(-ENOMEM);

	h->desc.bLength			= UVC_DT_HEADER_SIZE(1);
	h->desc.bDescriptorType		= USB_DT_CS_INTERFACE;
	h->desc.bDescriptorSubType	= UVC_VC_HEADER;
#ifdef CONFIG_ARCH_BSP
	h->desc.bcdUVC			= cpu_to_le16(UVC_VERSION_DEFAULT);
#else
	h->desc.bcdUVC			= cpu_to_le16(0x0100);
#endif
	h->desc.dwClockFrequency	= cpu_to_le32(48000000);

	config_item_init_type_name(&h->item, name, &uvcg_control_header_type);

	return &h->item;
}

static struct configfs_group_operations uvcg_control_header_grp_ops = {
	.make_item		= uvcg_control_header_make,
};

static const struct uvcg_config_group_type uvcg_control_header_grp_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_group_ops	= &uvcg_control_header_grp_ops,
		.ct_owner	= THIS_MODULE,
	},
	.name = "header",
};

/* -----------------------------------------------------------------------------
 * control/processing/default
 */

#define UVCG_DEFAULT_PROCESSING_ATTR(cname, aname, bits)		\
static ssize_t uvcg_default_processing_##cname##_show(			\
	struct config_item *item, char *page)				\
{									\
	struct config_group *group = to_config_group(item);		\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &group->cg_subsys->su_mutex;		\
	struct uvc_processing_unit_descriptor *pd;			\
	int result;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = group->cg_item.ci_parent->ci_parent->ci_parent;	\
	opts = to_f_uvc_opts(opts_item);				\
	pd = &opts->uvc_processing;					\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf(page, "%u\n", le##bits##_to_cpu(pd->aname));	\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
	return result;							\
}									\
									\
UVC_ATTR_RO(uvcg_default_processing_, cname, aname)

UVCG_DEFAULT_PROCESSING_ATTR(b_unit_id, bUnitID, 8);
UVCG_DEFAULT_PROCESSING_ATTR(b_source_id, bSourceID, 8);
UVCG_DEFAULT_PROCESSING_ATTR(w_max_multiplier, wMaxMultiplier, 16);
UVCG_DEFAULT_PROCESSING_ATTR(i_processing, iProcessing, 8);

#undef UVCG_DEFAULT_PROCESSING_ATTR

static ssize_t uvcg_default_processing_bm_controls_show(
	struct config_item *item, char *page)
{
	struct config_group *group = to_config_group(item);
	struct f_uvc_opts *opts;
	struct config_item *opts_item;
	struct mutex *su_mutex = &group->cg_subsys->su_mutex;
	struct uvc_processing_unit_descriptor *pd;
	int result, i;
	char *pg = page;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = group->cg_item.ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);
	pd = &opts->uvc_processing;

	mutex_lock(&opts->lock);
	for (result = 0, i = 0; i < pd->bControlSize; ++i) {
		result += sprintf(pg, "%u\n", pd->bmControls[i]);
		pg = page + result;
	}
	mutex_unlock(&opts->lock);

	mutex_unlock(su_mutex);

	return result;
}

#ifdef CONFIG_ARCH_BSP
static ssize_t uvcg_default_processing_bm_controls_store(
	struct config_item *item, const char *page, size_t len)
{
	struct config_group *group = to_config_group(item);
	struct f_uvc_opts *opts;
	struct config_item *opts_item;
	struct mutex *su_mutex = &group->cg_subsys->su_mutex;
	struct uvc_processing_unit_descriptor *pd;
	int ret, i;
	const char *pg = page;
	/* sign, base 2 representation, newline, terminator */
	char buf[1 + sizeof(u8) * 8 + 1 + 1];
	int idx;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = group->cg_item.ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);
	pd = &opts->uvc_processing;

	idx = 0;
	while (pg - page < len) {
		i = 0;
		while (i < sizeof(buf) && (pg - page < len) &&
				*pg != '\0' && *pg != '\n')
			buf[i++] = *pg++;
		while ((pg - page < len) && (*pg == '\0' || *pg == '\n'))
			++pg;
		buf[i] = '\0';
		ret = kstrtou8(buf, 0, &pd->bmControls[idx++]);
		if (ret < 0)
			goto end;
		if (idx >= pd->bControlSize)
			break;
	}
	ret = len;
end:
	mutex_unlock(&opts->lock);
	mutex_unlock(su_mutex);
	return ret;
}

UVC_ATTR(uvcg_default_processing_, bm_controls, bmControls);
#else // CONFIG_ARCH_BSP
UVC_ATTR_RO(uvcg_default_processing_, bm_controls, bmControls);
#endif // CONFIG_ARCH_BSP

static struct configfs_attribute *uvcg_default_processing_attrs[] = {
	&uvcg_default_processing_attr_b_unit_id,
	&uvcg_default_processing_attr_b_source_id,
	&uvcg_default_processing_attr_w_max_multiplier,
	&uvcg_default_processing_attr_bm_controls,
	&uvcg_default_processing_attr_i_processing,
	NULL,
};

static const struct uvcg_config_group_type uvcg_default_processing_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_attrs	= uvcg_default_processing_attrs,
		.ct_owner	= THIS_MODULE,
	},
	.name = "default",
};

/* -----------------------------------------------------------------------------
 * control/processing
 */

static const struct uvcg_config_group_type uvcg_processing_grp_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_owner	= THIS_MODULE,
	},
	.name = "processing",
	.children = (const struct uvcg_config_group_type*[]) {
		&uvcg_default_processing_type,
		NULL,
	},
};

#ifdef CONFIG_ARCH_BSP
/* control/encoding/default */
#define UVCG_DEFAULT_ENCODING_ATTR(cname, aname, bits)		\
static ssize_t uvcg_default_encoding_##cname##_show(			\
	struct config_item *item, char *page)				\
{									\
	struct config_group *group = to_config_group(item); \
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &group->cg_subsys->su_mutex;	\
	struct uvc_encoding_unit_descriptor *ed;			\
	int result;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = group->cg_item.ci_parent->ci_parent->ci_parent;	\
	opts = to_f_uvc_opts(opts_item);				\
	ed = &opts->uvc_encoding;					\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf_s(page, PAGE_SIZE, "%d\n", le##bits##_to_cpu(ed->aname));	\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
	return result;							\
}									\
									\
UVC_ATTR_RO(uvcg_default_encoding_, cname, aname)

UVCG_DEFAULT_ENCODING_ATTR(b_unit_id, bUnitID, 8);
UVCG_DEFAULT_ENCODING_ATTR(b_source_id, bSourceID, 8);
UVCG_DEFAULT_ENCODING_ATTR(i_encoding, iEncoding, 8);

#undef UVCG_DEFAULT_ENCODING_ATTR

static ssize_t uvcg_default_encoding_bm_controls_show(
	struct config_item *item, char *page)
{
	struct config_group *group = to_config_group(item);
	struct f_uvc_opts *opts;
	struct config_item *opts_item;
	struct mutex *su_mutex = &group->cg_subsys->su_mutex;
	struct uvc_encoding_unit_descriptor *ed;
	int result, i;
	char *pg = page;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = group->cg_item.ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);
	ed = &opts->uvc_encoding;

	mutex_lock(&opts->lock);
	for (result = 0, i = 0; i < ed->bControlSize; ++i) {
		result += sprintf_s(pg, PAGE_SIZE, "%d\n", ed->bmControls[i]);
		pg = page + result;
	}
	mutex_unlock(&opts->lock);

	mutex_unlock(su_mutex);

	return result;
}

static ssize_t uvcg_default_encoding_bm_controls_store(
	struct config_item *item, const char *page, size_t len)
{
	struct config_group *group = to_config_group(item);
	struct f_uvc_opts *opts;
	struct config_item *opts_item;
	struct mutex *su_mutex = &group->cg_subsys->su_mutex;
	struct uvc_encoding_unit_descriptor *ed;
	int ret, i;
	const char *pg = page;
	/* sign, base 2 representation, newline, terminator */
	char buf[1 + sizeof(u8) * 8 + 1 + 1];
	int idx;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = group->cg_item.ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);
	ed = &opts->uvc_encoding;

	idx = 0;
	while (pg - page < len) {
		i = 0;
		while (i < sizeof(buf) && (pg - page < len) &&
				*pg != '\0' && *pg != '\n')
			buf[i++] = *pg++;
		while ((pg - page < len) && (*pg == '\0' || *pg == '\n'))
			++pg;
		buf[i] = '\0';
		ret = kstrtou8(buf, 0, &ed->bmControls[idx++]);
		if (ret < 0)
			goto end;
		if (idx >= ed->bControlSize)
			break;
	}
	ret = len;
end:
	mutex_unlock(&opts->lock);
	mutex_unlock(su_mutex);
	return ret;
}

static ssize_t uvcg_default_encoding_bm_controlsruntime_show(
	struct config_item *item, char *page)
{
	struct config_group *group = to_config_group(item);
	struct f_uvc_opts *opts;
	struct config_item *opts_item;
	struct mutex *su_mutex = &group->cg_subsys->su_mutex;
	struct uvc_encoding_unit_descriptor *ed;
	int result, i;
	char *pg = page;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = group->cg_item.ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);
	ed = &opts->uvc_encoding;

	mutex_lock(&opts->lock);
	for (result = 0, i = 0; i < ed->bControlSize; ++i) {
		result += sprintf_s(pg, PAGE_SIZE, "%d\n", ed->bmControlsRuntime[i]);
		pg = page + result;
	}
	mutex_unlock(&opts->lock);

	mutex_unlock(su_mutex);

	return result;
}

static ssize_t uvcg_default_encoding_bm_controlsruntime_store(
	struct config_item *item, const char *page, size_t len)
{
	struct config_group *group = to_config_group(item);
	struct f_uvc_opts *opts;
	struct config_item *opts_item;
	struct mutex *su_mutex = &group->cg_subsys->su_mutex;
	struct uvc_encoding_unit_descriptor *ed;
	int ret, i;
	const char *pg = page;
	/* sign, base 2 representation, newline, terminator */
	char buf[1 + sizeof(u8) * 8 + 1 + 1];
	int idx;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = group->cg_item.ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);
	ed = &opts->uvc_encoding;

	idx = 0;
	while (pg - page < len) {
		i = 0;
		while (i < sizeof(buf) && (pg - page < len) &&
				*pg != '\0' && *pg != '\n')
			buf[i++] = *pg++;
		while ((pg - page < len) && (*pg == '\0' || *pg == '\n'))
			++pg;
		buf[i] = '\0';
		ret = kstrtou8(buf, 0, &ed->bmControlsRuntime[idx++]);
		if (ret < 0)
			goto end;
		if (idx >= ed->bControlSize)
			break;
	}
	ret = len;
end:
	mutex_unlock(&opts->lock);
	mutex_unlock(su_mutex);
	return ret;
}

UVC_ATTR(uvcg_default_encoding_, bm_controls, bmControls);
UVC_ATTR(uvcg_default_encoding_, bm_controlsruntime, bmControlsRuntime);

static struct configfs_attribute *uvcg_default_encoding_attrs[] = {
	&uvcg_default_encoding_attr_b_unit_id,
	&uvcg_default_encoding_attr_b_source_id,
	&uvcg_default_encoding_attr_i_encoding,
	&uvcg_default_encoding_attr_bm_controls,
	&uvcg_default_encoding_attr_bm_controlsruntime,
	NULL,
};

static const struct uvcg_config_group_type uvcg_default_encoding_types = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_attrs	= uvcg_default_encoding_attrs,
		.ct_owner	= THIS_MODULE,
	},
	.name = "default",
};

/* -----------------------------------------------------------------------------
 * control/encoding
 */

static const struct uvcg_config_group_type uvcg_encoding_grp_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_owner	= THIS_MODULE,
	},
	.name = "encoding",
	.children = (const struct uvcg_config_group_type*[]) {
		&uvcg_default_encoding_types,
		NULL,
	},
};

/* -----------------------------------------------------------------------------
 * control/extension/default
 */

#define UVCG_DEFAULT_EXTENSION_ATTR(cname, aname, bits)			\
static ssize_t uvcg_default_extension_##cname##_show(			\
	struct config_item *item, char *page)				\
{									\
	struct config_group *group = to_config_group(item);		\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &group->cg_subsys->su_mutex;		\
	struct UVC_EXTENSION_UNIT_DESCRIPTOR(1, 2) *xd;			\
	int result;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = group->cg_item.ci_parent->ci_parent->ci_parent;	\
	opts = to_f_uvc_opts(opts_item);				\
	xd = &opts->uvc_extension;				\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf_s(page, PAGE_SIZE, "%u\n", le##bits##_to_cpu(xd->aname));	\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
	return result;							\
}											\
											\
UVC_ATTR_RO(uvcg_default_extension_, cname, aname)

UVCG_DEFAULT_EXTENSION_ATTR(b_unit_id, bUnitID, 8);
UVCG_DEFAULT_EXTENSION_ATTR(b_num_input_pins, bNrInPins, 8);
UVCG_DEFAULT_EXTENSION_ATTR(i_extension, iExtension, 8);

#undef UVCG_DEFAULT_EXTENSION_ATTR

static ssize_t uvcg_default_extension_bm_controls_show(
	struct config_item *item, char *page)
{
	struct config_group *group = to_config_group(item);
	struct f_uvc_opts *opts;
	struct config_item *opts_item;
	struct mutex *su_mutex = &group->cg_subsys->su_mutex;
	struct UVC_EXTENSION_UNIT_DESCRIPTOR(1, 2) *xd;
	int result, pg_state, i;
	int max_size = PAGE_SIZE;
	char *pg = page;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = group->cg_item.ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);
	xd = &opts->uvc_extension;

	mutex_lock(&opts->lock);
	for (result = 0, i = 0; (i < xd->bControlSize) && (max_size > 0); ++i) {
		pg_state = sprintf_s(pg, max_size, "%d\n", xd->bmControls[i]);
		if (pg_state < 0)
			break;
		result += pg_state;
		pg = page + result;
		max_size = PAGE_SIZE - result;
	}
	mutex_unlock(&opts->lock);

	mutex_unlock(su_mutex);

	return result;
}

static ssize_t uvcg_default_extension_bm_controls_store(
	struct config_item *item, const char *page, size_t len)
{
	struct config_group *group = to_config_group(item);
	struct f_uvc_opts *opts;
	struct config_item *opts_item;
	struct mutex *su_mutex = &group->cg_subsys->su_mutex;
	struct UVC_EXTENSION_UNIT_DESCRIPTOR(1, 2) *xd;
	int ret, i;
	const char *pg = page;
	/* sign, base 2 representation, newline, terminator */
	char buf[1 + sizeof(u8) * 8 + 1 + 1];
	int idx;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = group->cg_item.ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);
	xd = &opts->uvc_extension;

	idx = 0;
	while (pg - page < len) {
		i = 0;
		while (i < sizeof(buf) && (pg - page < len) &&
				*pg != '\0' && *pg != '\n')
			buf[i++] = *pg++;
		while ((pg - page < len) && (*pg == '\0' || *pg == '\n'))
			++pg;
		buf[i] = '\0';
		ret = kstrtou8(buf, 0, &xd->bmControls[idx++]);
		if (ret < 0)
			goto end;
		if (idx >= xd->bControlSize)
			break;
	}
	ret = len;
end:
	mutex_unlock(&opts->lock);
	mutex_unlock(su_mutex);
	return ret;
}

UVC_ATTR(uvcg_default_extension_, bm_controls, bmControls);

static struct configfs_attribute *uvcg_default_extension_attrs[] = {
	&uvcg_default_extension_attr_b_unit_id,
	&uvcg_default_extension_attr_b_num_input_pins,
	&uvcg_default_extension_attr_bm_controls,
	&uvcg_default_extension_attr_i_extension,
	NULL,
};

static const struct uvcg_config_group_type uvcg_default_extension_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_attrs	= uvcg_default_extension_attrs,
		.ct_owner	= THIS_MODULE,
	},
	.name = "default",
};

/* -----------------------------------------------------------------------------
 * control/extension
 */
static const struct uvcg_config_group_type uvcg_extension_grp_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_owner	= THIS_MODULE,
	},
	.name = "extension",
	.children = (const struct uvcg_config_group_type*[]) {
		&uvcg_default_extension_type,
		NULL,
	},
};
#endif  // CONFIG_ARCH_BSP
/* -----------------------------------------------------------------------------
 * control/terminal/camera/default
 */

#define UVCG_DEFAULT_CAMERA_ATTR(cname, aname, bits)			\
static ssize_t uvcg_default_camera_##cname##_show(			\
	struct config_item *item, char *page)				\
{									\
	struct config_group *group = to_config_group(item);		\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &group->cg_subsys->su_mutex;		\
	struct uvc_camera_terminal_descriptor *cd;			\
	int result;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = group->cg_item.ci_parent->ci_parent->ci_parent->	\
			ci_parent;					\
	opts = to_f_uvc_opts(opts_item);				\
	cd = &opts->uvc_camera_terminal;				\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf(page, "%u\n", le##bits##_to_cpu(cd->aname));	\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
									\
	return result;							\
}									\
									\
UVC_ATTR_RO(uvcg_default_camera_, cname, aname)

UVCG_DEFAULT_CAMERA_ATTR(b_terminal_id, bTerminalID, 8);
UVCG_DEFAULT_CAMERA_ATTR(w_terminal_type, wTerminalType, 16);
UVCG_DEFAULT_CAMERA_ATTR(b_assoc_terminal, bAssocTerminal, 8);
UVCG_DEFAULT_CAMERA_ATTR(i_terminal, iTerminal, 8);
UVCG_DEFAULT_CAMERA_ATTR(w_objective_focal_length_min, wObjectiveFocalLengthMin,
			 16);
UVCG_DEFAULT_CAMERA_ATTR(w_objective_focal_length_max, wObjectiveFocalLengthMax,
			 16);
UVCG_DEFAULT_CAMERA_ATTR(w_ocular_focal_length, wOcularFocalLength,
			 16);

#undef UVCG_DEFAULT_CAMERA_ATTR

static ssize_t uvcg_default_camera_bm_controls_show(
	struct config_item *item, char *page)
{
	struct config_group *group = to_config_group(item);
	struct f_uvc_opts *opts;
	struct config_item *opts_item;
	struct mutex *su_mutex = &group->cg_subsys->su_mutex;
	struct uvc_camera_terminal_descriptor *cd;
	int result, i;
	char *pg = page;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = group->cg_item.ci_parent->ci_parent->ci_parent->
			ci_parent;
	opts = to_f_uvc_opts(opts_item);
	cd = &opts->uvc_camera_terminal;

	mutex_lock(&opts->lock);
	for (result = 0, i = 0; i < cd->bControlSize; ++i) {
		result += sprintf(pg, "%u\n", cd->bmControls[i]);
		pg = page + result;
	}
	mutex_unlock(&opts->lock);

	mutex_unlock(su_mutex);
	return result;
}

#ifdef CONFIG_ARCH_BSP
static ssize_t uvcg_default_camera_bm_controls_store(
	struct config_item *item, const char *page, size_t len)
{
	struct config_group *group = to_config_group(item);
	struct f_uvc_opts *opts;
	struct config_item *opts_item;
	struct mutex *su_mutex = &group->cg_subsys->su_mutex;
	struct uvc_camera_terminal_descriptor *cd;
	int ret, i;
	const char *pg = page;
	/* sign, base 2 representation, newline, terminator */
	char buf[1 + sizeof(u8) * 8 + 1 + 1];
	int idx;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = group->cg_item.ci_parent->ci_parent->ci_parent->
			ci_parent;
	opts = to_f_uvc_opts(opts_item);
	cd = &opts->uvc_camera_terminal;

	idx = 0;
	while (pg - page < len) {
		i = 0;
		while (i < sizeof(buf) && (pg - page < len) &&
				*pg != '\0' && *pg != '\n')
			buf[i++] = *pg++;
		while ((pg - page < len) && (*pg == '\0' || *pg == '\n'))
			++pg;
		buf[i] = '\0';
		ret = kstrtou8(buf, 0, &cd->bmControls[idx++]);
		if (ret < 0)
			goto end;
		if (idx >= cd->bControlSize)
			break;
	}
	ret = len;
end:
	mutex_unlock(&opts->lock);
	mutex_unlock(su_mutex);
	return ret;
}

UVC_ATTR(uvcg_default_camera_, bm_controls, bmControls);
#else
UVC_ATTR_RO(uvcg_default_camera_, bm_controls, bmControls);
#endif

static struct configfs_attribute *uvcg_default_camera_attrs[] = {
	&uvcg_default_camera_attr_b_terminal_id,
	&uvcg_default_camera_attr_w_terminal_type,
	&uvcg_default_camera_attr_b_assoc_terminal,
	&uvcg_default_camera_attr_i_terminal,
	&uvcg_default_camera_attr_w_objective_focal_length_min,
	&uvcg_default_camera_attr_w_objective_focal_length_max,
	&uvcg_default_camera_attr_w_ocular_focal_length,
	&uvcg_default_camera_attr_bm_controls,
	NULL,
};

static const struct uvcg_config_group_type uvcg_default_camera_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_attrs	= uvcg_default_camera_attrs,
		.ct_owner	= THIS_MODULE,
	},
	.name = "default",
};

/* -----------------------------------------------------------------------------
 * control/terminal/camera
 */

static const struct uvcg_config_group_type uvcg_camera_grp_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_owner	= THIS_MODULE,
	},
	.name = "camera",
	.children = (const struct uvcg_config_group_type*[]) {
		&uvcg_default_camera_type,
		NULL,
	},
};

/* -----------------------------------------------------------------------------
 * control/terminal/output/default
 */

#define UVCG_DEFAULT_OUTPUT_ATTR(cname, aname, bits)			\
static ssize_t uvcg_default_output_##cname##_show(			\
	struct config_item *item, char *page)				\
{									\
	struct config_group *group = to_config_group(item);		\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &group->cg_subsys->su_mutex;		\
	struct uvc_output_terminal_descriptor *cd;			\
	int result;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = group->cg_item.ci_parent->ci_parent->		\
			ci_parent->ci_parent;				\
	opts = to_f_uvc_opts(opts_item);				\
	cd = &opts->uvc_output_terminal;				\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf(page, "%u\n", le##bits##_to_cpu(cd->aname));	\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
									\
	return result;							\
}									\
									\
UVC_ATTR_RO(uvcg_default_output_, cname, aname)

UVCG_DEFAULT_OUTPUT_ATTR(b_terminal_id, bTerminalID, 8);
UVCG_DEFAULT_OUTPUT_ATTR(w_terminal_type, wTerminalType, 16);
UVCG_DEFAULT_OUTPUT_ATTR(b_assoc_terminal, bAssocTerminal, 8);
UVCG_DEFAULT_OUTPUT_ATTR(i_terminal, iTerminal, 8);

#undef UVCG_DEFAULT_OUTPUT_ATTR

static ssize_t uvcg_default_output_b_source_id_show(struct config_item *item,
						    char *page)
{
	struct config_group *group = to_config_group(item);
	struct f_uvc_opts *opts;
	struct config_item *opts_item;
	struct mutex *su_mutex = &group->cg_subsys->su_mutex;
	struct uvc_output_terminal_descriptor *cd;
	int result;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = group->cg_item.ci_parent->ci_parent->
			ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);
	cd = &opts->uvc_output_terminal;

	mutex_lock(&opts->lock);
	result = sprintf(page, "%u\n", le8_to_cpu(cd->bSourceID));
	mutex_unlock(&opts->lock);

	mutex_unlock(su_mutex);

	return result;
}

static ssize_t uvcg_default_output_b_source_id_store(struct config_item *item,
						     const char *page, size_t len)
{
	struct config_group *group = to_config_group(item);
	struct f_uvc_opts *opts;
	struct config_item *opts_item;
	struct mutex *su_mutex = &group->cg_subsys->su_mutex;
	struct uvc_output_terminal_descriptor *cd;
	int result;
	u8 num;

	result = kstrtou8(page, 0, &num);
	if (result)
		return result;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = group->cg_item.ci_parent->ci_parent->
			ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);
	cd = &opts->uvc_output_terminal;

	mutex_lock(&opts->lock);
	cd->bSourceID = num;
	mutex_unlock(&opts->lock);

	mutex_unlock(su_mutex);

	return len;
}
UVC_ATTR(uvcg_default_output_, b_source_id, bSourceID);

static struct configfs_attribute *uvcg_default_output_attrs[] = {
	&uvcg_default_output_attr_b_terminal_id,
	&uvcg_default_output_attr_w_terminal_type,
	&uvcg_default_output_attr_b_assoc_terminal,
	&uvcg_default_output_attr_b_source_id,
	&uvcg_default_output_attr_i_terminal,
	NULL,
};

static const struct uvcg_config_group_type uvcg_default_output_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_attrs	= uvcg_default_output_attrs,
		.ct_owner	= THIS_MODULE,
	},
	.name = "default",
};

/* -----------------------------------------------------------------------------
 * control/terminal/output
 */

static const struct uvcg_config_group_type uvcg_output_grp_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_owner	= THIS_MODULE,
	},
	.name = "output",
	.children = (const struct uvcg_config_group_type*[]) {
		&uvcg_default_output_type,
		NULL,
	},
};

/* -----------------------------------------------------------------------------
 * control/terminal
 */

static const struct uvcg_config_group_type uvcg_terminal_grp_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_owner	= THIS_MODULE,
	},
	.name = "terminal",
	.children = (const struct uvcg_config_group_type*[]) {
		&uvcg_camera_grp_type,
		&uvcg_output_grp_type,
		NULL,
	},
};

/* -----------------------------------------------------------------------------
 * control/class/{fs|ss}
 */

struct uvcg_control_class_group {
	struct config_group group;
	const char *name;
};

static inline struct uvc_descriptor_header
**uvcg_get_ctl_class_arr(struct config_item *i, struct f_uvc_opts *o)
{
	struct uvcg_control_class_group *group =
		container_of(i, struct uvcg_control_class_group,
			     group.cg_item);

	if (!strcmp(group->name, "fs"))
		return o->uvc_fs_control_cls;

	if (!strcmp(group->name, "ss"))
		return o->uvc_ss_control_cls;

	return NULL;
}

static int uvcg_control_class_allow_link(struct config_item *src,
					 struct config_item *target)
{
	struct config_item *control, *header;
	struct f_uvc_opts *opts;
	struct mutex *su_mutex = &src->ci_group->cg_subsys->su_mutex;
	struct uvc_descriptor_header **class_array;
	struct uvcg_control_header *target_hdr;
	int ret = -EINVAL;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	control = src->ci_parent->ci_parent;
	header = config_group_find_item(to_config_group(control), "header");
	if (!header || target->ci_parent != header)
		goto out;

	opts = to_f_uvc_opts(control->ci_parent);

	mutex_lock(&opts->lock);

	class_array = uvcg_get_ctl_class_arr(src, opts);
	if (!class_array)
		goto unlock;
	if (opts->refcnt || class_array[0]) {
		ret = -EBUSY;
		goto unlock;
	}

	target_hdr = to_uvcg_control_header(target);
	++target_hdr->linked;
	class_array[0] = (struct uvc_descriptor_header *)&target_hdr->desc;
	ret = 0;

unlock:
	mutex_unlock(&opts->lock);
out:
	config_item_put(header);
	mutex_unlock(su_mutex);
	return ret;
}

static void uvcg_control_class_drop_link(struct config_item *src,
					struct config_item *target)
{
	struct config_item *control, *header;
	struct f_uvc_opts *opts;
	struct mutex *su_mutex = &src->ci_group->cg_subsys->su_mutex;
	struct uvc_descriptor_header **class_array;
	struct uvcg_control_header *target_hdr;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	control = src->ci_parent->ci_parent;
	header = config_group_find_item(to_config_group(control), "header");
	if (!header || target->ci_parent != header)
		goto out;

	opts = to_f_uvc_opts(control->ci_parent);

	mutex_lock(&opts->lock);

	class_array = uvcg_get_ctl_class_arr(src, opts);
	if (!class_array || opts->refcnt)
		goto unlock;

	target_hdr = to_uvcg_control_header(target);
	--target_hdr->linked;
	class_array[0] = NULL;

unlock:
	mutex_unlock(&opts->lock);
out:
	config_item_put(header);
	mutex_unlock(su_mutex);
}

static struct configfs_item_operations uvcg_control_class_item_ops = {
	.release	= uvcg_config_item_release,
	.allow_link	= uvcg_control_class_allow_link,
	.drop_link	= uvcg_control_class_drop_link,
};

static const struct config_item_type uvcg_control_class_type = {
	.ct_item_ops	= &uvcg_control_class_item_ops,
	.ct_owner	= THIS_MODULE,
};

/* -----------------------------------------------------------------------------
 * control/class
 */

static int uvcg_control_class_create_children(struct config_group *parent)
{
	static const char * const names[] = { "fs", "ss" };
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(names); ++i) {
		struct uvcg_control_class_group *group;

		group = kzalloc(sizeof(*group), GFP_KERNEL);
		if (!group)
			return -ENOMEM;

		group->name = names[i];

		config_group_init_type_name(&group->group, group->name,
					    &uvcg_control_class_type);
		configfs_add_default_group(&group->group, parent);
	}

	return 0;
}

static const struct uvcg_config_group_type uvcg_control_class_grp_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_owner	= THIS_MODULE,
	},
	.name = "class",
	.create_children = uvcg_control_class_create_children,
};

/* -----------------------------------------------------------------------------
 * control
 */

static ssize_t uvcg_default_control_b_interface_number_show(
	struct config_item *item, char *page)
{
	struct config_group *group = to_config_group(item);
	struct mutex *su_mutex = &group->cg_subsys->su_mutex;
	struct config_item *opts_item;
	struct f_uvc_opts *opts;
	int result = 0;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = item->ci_parent;
	opts = to_f_uvc_opts(opts_item);

	mutex_lock(&opts->lock);
	result += sprintf(page, "%u\n", opts->control_interface);
	mutex_unlock(&opts->lock);

	mutex_unlock(su_mutex);

	return result;
}

UVC_ATTR_RO(uvcg_default_control_, b_interface_number, bInterfaceNumber);

static struct configfs_attribute *uvcg_default_control_attrs[] = {
	&uvcg_default_control_attr_b_interface_number,
	NULL,
};

static const struct uvcg_config_group_type uvcg_control_grp_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_attrs	= uvcg_default_control_attrs,
		.ct_owner	= THIS_MODULE,
	},
	.name = "control",
	.children = (const struct uvcg_config_group_type*[]) {
		&uvcg_control_header_grp_type,
		&uvcg_processing_grp_type,
#ifdef CONFIG_ARCH_BSP
		&uvcg_encoding_grp_type,
		&uvcg_extension_grp_type,
#endif
		&uvcg_terminal_grp_type,
		&uvcg_control_class_grp_type,
		NULL,
	},
};

/* -----------------------------------------------------------------------------
 * streaming/uncompressed
 * streaming/mjpeg
 * streaming/h264
 */

static const char * const uvcg_format_names[] = {
	"uncompressed",
	"mjpeg",
#ifdef CONFIG_ARCH_BSP
	"framebased",
	"h264"
#endif
};

enum uvcg_format_type {
	UVCG_UNCOMPRESSED = 0,
	UVCG_MJPEG,
#ifdef CONFIG_ARCH_BSP
	UVCG_FRAME_FRAME_BASED,
	UVCG_H264,
#endif
};

struct uvcg_format {
	struct config_group	group;
	enum uvcg_format_type	type;
	unsigned		linked;
	unsigned		num_frames;
	__u8			bmaControls[UVCG_STREAMING_CONTROL_SIZE];
#if defined(CONFIG_ARCH_BSP) && defined(CONFIG_USB_F_UVC_STILL_IMAGE) && !defined(CONFIG_UVC_NO_STATUS_INT_EP)
	void			*still_image;
#endif
};

static struct uvcg_format *to_uvcg_format(struct config_item *item)
{
	return container_of(to_config_group(item), struct uvcg_format, group);
}

static ssize_t uvcg_format_bma_controls_show(struct uvcg_format *f, char *page)
{
	struct f_uvc_opts *opts;
	struct config_item *opts_item;
	struct mutex *su_mutex = &f->group.cg_subsys->su_mutex;
	int result, i;
	char *pg = page;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = f->group.cg_item.ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);

	mutex_lock(&opts->lock);
	result = sprintf(pg, "0x");
	pg += result;
	for (i = 0; i < UVCG_STREAMING_CONTROL_SIZE; ++i) {
		result += sprintf(pg, "%x\n", f->bmaControls[i]);
		pg = page + result;
	}
	mutex_unlock(&opts->lock);

	mutex_unlock(su_mutex);
	return result;
}

static ssize_t uvcg_format_bma_controls_store(struct uvcg_format *ch,
					      const char *page, size_t len)
{
	struct f_uvc_opts *opts;
	struct config_item *opts_item;
	struct mutex *su_mutex = &ch->group.cg_subsys->su_mutex;
	int ret = -EINVAL;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = ch->group.cg_item.ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);

	mutex_lock(&opts->lock);
	if (ch->linked || opts->refcnt) {
		ret = -EBUSY;
		goto end;
	}

	if (len < 4 || *page != '0' ||
	    (*(page + 1) != 'x' && *(page + 1) != 'X'))
		goto end;
	ret = hex2bin(ch->bmaControls, page + 2, 1);
	if (ret < 0)
		goto end;
	ret = len;
end:
	mutex_unlock(&opts->lock);
	mutex_unlock(su_mutex);
	return ret;
}

struct uvcg_format_ptr {
	struct uvcg_format	*fmt;
	struct list_head	entry;
};

/* -----------------------------------------------------------------------------
 * streaming/header/<NAME>
 * streaming/header
 */

struct uvcg_streaming_header {
	struct config_item				item;
	struct uvc_input_header_descriptor		desc;
	unsigned					linked;
	struct list_head				formats;
	unsigned					num_fmt;
};

static struct uvcg_streaming_header *to_uvcg_streaming_header(struct config_item *item)
{
	return container_of(item, struct uvcg_streaming_header, item);
}

static void uvcg_format_set_indices(struct config_group *fmt);

static int uvcg_streaming_header_allow_link(struct config_item *src,
					    struct config_item *target)
{
	struct mutex *su_mutex = &src->ci_group->cg_subsys->su_mutex;
	struct config_item *opts_item;
	struct f_uvc_opts *opts;
	struct uvcg_streaming_header *src_hdr;
	struct uvcg_format *target_fmt = NULL;
	struct uvcg_format_ptr *format_ptr;
	int i, ret = -EINVAL;

	src_hdr = to_uvcg_streaming_header(src);
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = src->ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);

	mutex_lock(&opts->lock);

	if (src_hdr->linked) {
		ret = -EBUSY;
		goto out;
	}

	/*
	 * Linking is only allowed to direct children of the format nodes
	 * (streaming/uncompressed or streaming/mjpeg nodes). First check that
	 * the grand-parent of the target matches the grand-parent of the source
	 * (the streaming node), and then verify that the target parent is a
	 * format node.
	 */
	if (src->ci_parent->ci_parent != target->ci_parent->ci_parent)
		goto out;

	for (i = 0; i < ARRAY_SIZE(uvcg_format_names); ++i) {
		if (!strcmp(target->ci_parent->ci_name, uvcg_format_names[i]))
			break;
	}

	if (i == ARRAY_SIZE(uvcg_format_names))
		goto out;

	target_fmt = container_of(to_config_group(target), struct uvcg_format,
				  group);
	if (!target_fmt)
		goto out;

	uvcg_format_set_indices(to_config_group(target));

	format_ptr = kzalloc(sizeof(*format_ptr), GFP_KERNEL);
	if (!format_ptr) {
		ret = -ENOMEM;
		goto out;
	}
	ret = 0;
	format_ptr->fmt = target_fmt;
	list_add_tail(&format_ptr->entry, &src_hdr->formats);
	++src_hdr->num_fmt;
	++target_fmt->linked;

out:
	mutex_unlock(&opts->lock);
	mutex_unlock(su_mutex);
	return ret;
}

static void uvcg_streaming_header_drop_link(struct config_item *src,
					   struct config_item *target)
{
	struct mutex *su_mutex = &src->ci_group->cg_subsys->su_mutex;
	struct config_item *opts_item;
	struct f_uvc_opts *opts;
	struct uvcg_streaming_header *src_hdr;
	struct uvcg_format *target_fmt = NULL;
	struct uvcg_format_ptr *format_ptr, *tmp;

	src_hdr = to_uvcg_streaming_header(src);
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = src->ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);

	mutex_lock(&opts->lock);
	target_fmt = container_of(to_config_group(target), struct uvcg_format,
				  group);
	if (!target_fmt)
		goto out;

	list_for_each_entry_safe(format_ptr, tmp, &src_hdr->formats, entry)
		if (format_ptr->fmt == target_fmt) {
			list_del(&format_ptr->entry);
			kfree(format_ptr);
			--src_hdr->num_fmt;
			break;
		}

	--target_fmt->linked;

out:
	mutex_unlock(&opts->lock);
	mutex_unlock(su_mutex);
}

static struct configfs_item_operations uvcg_streaming_header_item_ops = {
	.release	= uvcg_config_item_release,
	.allow_link	= uvcg_streaming_header_allow_link,
	.drop_link	= uvcg_streaming_header_drop_link,
};

#define UVCG_STREAMING_HEADER_ATTR(cname, aname, bits)			\
static ssize_t uvcg_streaming_header_##cname##_show(			\
	struct config_item *item, char *page)				\
{									\
	struct uvcg_streaming_header *sh = to_uvcg_streaming_header(item); \
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &sh->item.ci_group->cg_subsys->su_mutex;\
	int result;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = sh->item.ci_parent->ci_parent->ci_parent;		\
	opts = to_f_uvc_opts(opts_item);				\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf(page, "%u\n", le##bits##_to_cpu(sh->desc.aname));\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
	return result;							\
}									\
									\
UVC_ATTR_RO(uvcg_streaming_header_, cname, aname)

#if defined(CONFIG_ARCH_BSP) && defined(CONFIG_USB_F_UVC_STILL_IMAGE) && !defined(CONFIG_UVC_NO_STATUS_INT_EP)
#define UVCG_STREAMING_HEADER_RW_ATTR(cname, aname, bits)			\
static ssize_t uvcg_streaming_header_##cname##_show(			\
	struct config_item *item, char *page)				\
{									\
	struct uvcg_streaming_header *sh = to_uvcg_streaming_header(item);	\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &sh->item.ci_group->cg_subsys->su_mutex;  \
	int result;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = sh->item.ci_parent->ci_parent->ci_parent; \
	opts = to_f_uvc_opts(opts_item);				\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf_s(page, PAGE_SIZE, "%u\n", le##bits##_to_cpu(sh->desc.aname));		\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
	return result;							\
}									\
									\
static ssize_t								\
uvcg_streaming_header_##cname##_store(struct config_item *item,		\
				    const char *page, size_t len)	\
{									\
	struct uvcg_streaming_header *sh = to_uvcg_streaming_header(item);	\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &sh->item.ci_group->cg_subsys->su_mutex;	\
	int ret;							\
	u8 num;								\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = sh->item.ci_parent->ci_parent->ci_parent; \
	opts = to_f_uvc_opts(opts_item);				\
									\
	mutex_lock(&opts->lock);					\
	if (sh->linked || opts->refcnt) {				\
		ret = -EBUSY;						\
		goto end;						\
	}								\
									\
	ret = kstrtou8(page, 0, &num);					\
	if (ret)							\
		goto end;						\
									\
	if (num > 3) {						\
		ret = -EINVAL;						\
		goto end;						\
	}								\
	sh->desc.aname = le##bits##_to_cpu(num);						\
	ret = len;							\
end:									\
	mutex_unlock(&opts->lock);					\
	mutex_unlock(su_mutex);						\
	return ret;							\
}									\
									\
UVC_ATTR(uvcg_streaming_header_, cname, aname)
#endif

UVCG_STREAMING_HEADER_ATTR(bm_info, bmInfo, 8);
UVCG_STREAMING_HEADER_ATTR(b_terminal_link, bTerminalLink, 8);
#if defined(CONFIG_ARCH_BSP) && defined(CONFIG_USB_F_UVC_STILL_IMAGE) && !defined(CONFIG_UVC_NO_STATUS_INT_EP)
UVCG_STREAMING_HEADER_RW_ATTR(b_still_capture_method, bStillCaptureMethod, 8);
#else
UVCG_STREAMING_HEADER_ATTR(b_still_capture_method, bStillCaptureMethod, 8);
#endif
UVCG_STREAMING_HEADER_ATTR(b_trigger_support, bTriggerSupport, 8);
UVCG_STREAMING_HEADER_ATTR(b_trigger_usage, bTriggerUsage, 8);

#undef UVCG_STREAMING_HEADER_ATTR

static struct configfs_attribute *uvcg_streaming_header_attrs[] = {
	&uvcg_streaming_header_attr_bm_info,
	&uvcg_streaming_header_attr_b_terminal_link,
	&uvcg_streaming_header_attr_b_still_capture_method,
	&uvcg_streaming_header_attr_b_trigger_support,
	&uvcg_streaming_header_attr_b_trigger_usage,
	NULL,
};

static const struct config_item_type uvcg_streaming_header_type = {
	.ct_item_ops	= &uvcg_streaming_header_item_ops,
	.ct_attrs	= uvcg_streaming_header_attrs,
	.ct_owner	= THIS_MODULE,
};

static struct config_item
*uvcg_streaming_header_make(struct config_group *group, const char *name)
{
	struct uvcg_streaming_header *h;

	h = kzalloc(sizeof(*h), GFP_KERNEL);
	if (!h)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&h->formats);
	h->desc.bDescriptorType		= USB_DT_CS_INTERFACE;
	h->desc.bDescriptorSubType	= UVC_VS_INPUT_HEADER;
	h->desc.bTerminalLink		= 3;
	h->desc.bControlSize		= UVCG_STREAMING_CONTROL_SIZE;

	config_item_init_type_name(&h->item, name, &uvcg_streaming_header_type);

	return &h->item;
}

static struct configfs_group_operations uvcg_streaming_header_grp_ops = {
	.make_item		= uvcg_streaming_header_make,
};

static const struct uvcg_config_group_type uvcg_streaming_header_grp_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_group_ops	= &uvcg_streaming_header_grp_ops,
		.ct_owner	= THIS_MODULE,
	},
	.name = "header",
};

/* -----------------------------------------------------------------------------
 * streaming/<mode>/<format>/<NAME>
 */

struct uvcg_frame {
	struct config_item	item;
	enum uvcg_format_type	fmt_type;
	struct {
		u8	b_length;
		u8	b_descriptor_type;
		u8	b_descriptor_subtype;
		u8	b_frame_index;
		u8	bm_capabilities;
		u16	w_width;
		u16	w_height;
		u32	dw_min_bit_rate;
		u32	dw_max_bit_rate;
		u32	dw_max_video_frame_buffer_size;
		u32	dw_default_frame_interval;
		u8	b_frame_interval_type;
	} __attribute__((packed)) frame;
	u32 *dw_frame_interval;
};

static struct uvcg_frame *to_uvcg_frame(struct config_item *item)
{
	return container_of(item, struct uvcg_frame, item);
}

#define UVCG_FRAME_ATTR(cname, aname, bits) \
static ssize_t uvcg_frame_##cname##_show(struct config_item *item, char *page)\
{									\
	struct uvcg_frame *f = to_uvcg_frame(item);			\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &f->item.ci_group->cg_subsys->su_mutex;\
	int result;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = f->item.ci_parent->ci_parent->ci_parent->ci_parent;	\
	opts = to_f_uvc_opts(opts_item);				\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf(page, "%u\n", f->frame.cname);			\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
	return result;							\
}									\
									\
static ssize_t  uvcg_frame_##cname##_store(struct config_item *item,	\
					   const char *page, size_t len)\
{									\
	struct uvcg_frame *f = to_uvcg_frame(item);			\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct uvcg_format *fmt;					\
	struct mutex *su_mutex = &f->item.ci_group->cg_subsys->su_mutex;\
	typeof(f->frame.cname) num;					\
	int ret;							\
									\
	ret = kstrtou##bits(page, 0, &num);				\
	if (ret)							\
		return ret;						\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = f->item.ci_parent->ci_parent->ci_parent->ci_parent;	\
	opts = to_f_uvc_opts(opts_item);				\
	fmt = to_uvcg_format(f->item.ci_parent);			\
									\
	mutex_lock(&opts->lock);					\
	if (fmt->linked || opts->refcnt) {				\
		ret = -EBUSY;						\
		goto end;						\
	}								\
									\
	f->frame.cname = num;						\
	ret = len;							\
end:									\
	mutex_unlock(&opts->lock);					\
	mutex_unlock(su_mutex);						\
	return ret;							\
}									\
									\
UVC_ATTR(uvcg_frame_, cname, aname);

static ssize_t uvcg_frame_b_frame_index_show(struct config_item *item,
					     char *page)
{
	struct uvcg_frame *f = to_uvcg_frame(item);
	struct uvcg_format *fmt;
	struct f_uvc_opts *opts;
	struct config_item *opts_item;
	struct config_item *fmt_item;
	struct mutex *su_mutex = &f->item.ci_group->cg_subsys->su_mutex;
	int result;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	fmt_item = f->item.ci_parent;
	fmt = to_uvcg_format(fmt_item);

	if (!fmt->linked) {
		result = -EBUSY;
		goto out;
	}

	opts_item = fmt_item->ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);

	mutex_lock(&opts->lock);
	result = sprintf(page, "%u\n", f->frame.b_frame_index);
	mutex_unlock(&opts->lock);

out:
	mutex_unlock(su_mutex);
	return result;
}

UVC_ATTR_RO(uvcg_frame_, b_frame_index, bFrameIndex);

UVCG_FRAME_ATTR(bm_capabilities, bmCapabilities, 8);
UVCG_FRAME_ATTR(w_width, wWidth, 16);
UVCG_FRAME_ATTR(w_height, wHeight, 16);
UVCG_FRAME_ATTR(dw_min_bit_rate, dwMinBitRate, 32);
UVCG_FRAME_ATTR(dw_max_bit_rate, dwMaxBitRate, 32);
UVCG_FRAME_ATTR(dw_max_video_frame_buffer_size, dwMaxVideoFrameBufferSize, 32);
UVCG_FRAME_ATTR(dw_default_frame_interval, dwDefaultFrameInterval, 32);

#undef UVCG_FRAME_ATTR

static ssize_t uvcg_frame_dw_frame_interval_show(struct config_item *item,
						 char *page)
{
	struct uvcg_frame *frm = to_uvcg_frame(item);
	struct f_uvc_opts *opts;
	struct config_item *opts_item;
	struct mutex *su_mutex = &frm->item.ci_group->cg_subsys->su_mutex;
	int result, i;
	char *pg = page;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = frm->item.ci_parent->ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);

	mutex_lock(&opts->lock);
	for (result = 0, i = 0; i < frm->frame.b_frame_interval_type; ++i) {
		result += sprintf(pg, "%u\n", frm->dw_frame_interval[i]);
		pg = page + result;
	}
	mutex_unlock(&opts->lock);

	mutex_unlock(su_mutex);
	return result;
}

static inline int __uvcg_count_frm_intrv(char *buf, void *priv)
{
	++*((int *)priv);
	return 0;
}

static inline int __uvcg_fill_frm_intrv(char *buf, void *priv)
{
	u32 num, **interv;
	int ret;

	ret = kstrtou32(buf, 0, &num);
	if (ret)
		return ret;

	interv = priv;
	**interv = num;
	++*interv;

	return 0;
}

static int __uvcg_iter_frm_intrv(const char *page, size_t len,
				 int (*fun)(char *, void *), void *priv)
{
	/* sign, base 2 representation, newline, terminator */
	char buf[1 + sizeof(u32) * 8 + 1 + 1];
	const char *pg = page;
	int i, ret;

	if (!fun)
		return -EINVAL;

	while (pg - page < len) {
		i = 0;
		while (i < sizeof(buf) && (pg - page < len) &&
				*pg != '\0' && *pg != '\n')
			buf[i++] = *pg++;
		if (i == sizeof(buf))
			return -EINVAL;
		while ((pg - page < len) && (*pg == '\0' || *pg == '\n'))
			++pg;
		buf[i] = '\0';
		ret = fun(buf, priv);
		if (ret)
			return ret;
	}

	return 0;
}

static ssize_t uvcg_frame_dw_frame_interval_store(struct config_item *item,
						  const char *page, size_t len)
{
	struct uvcg_frame *ch = to_uvcg_frame(item);
	struct f_uvc_opts *opts;
	struct config_item *opts_item;
	struct uvcg_format *fmt;
	struct mutex *su_mutex = &ch->item.ci_group->cg_subsys->su_mutex;
	int ret = 0, n = 0;
	u32 *frm_intrv, *tmp;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = ch->item.ci_parent->ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);
	fmt = to_uvcg_format(ch->item.ci_parent);

	mutex_lock(&opts->lock);
	if (fmt->linked || opts->refcnt) {
		ret = -EBUSY;
		goto end;
	}

	ret = __uvcg_iter_frm_intrv(page, len, __uvcg_count_frm_intrv, &n);
	if (ret)
		goto end;

	tmp = frm_intrv = kcalloc(n, sizeof(u32), GFP_KERNEL);
	if (!frm_intrv) {
		ret = -ENOMEM;
		goto end;
	}

	ret = __uvcg_iter_frm_intrv(page, len, __uvcg_fill_frm_intrv, &tmp);
	if (ret) {
		kfree(frm_intrv);
		goto end;
	}

	kfree(ch->dw_frame_interval);
	ch->dw_frame_interval = frm_intrv;
	ch->frame.b_frame_interval_type = n;
	sort(ch->dw_frame_interval, n, sizeof(*ch->dw_frame_interval),
	     uvcg_config_compare_u32, NULL);
	ret = len;

end:
	mutex_unlock(&opts->lock);
	mutex_unlock(su_mutex);
	return ret;
}

UVC_ATTR(uvcg_frame_, dw_frame_interval, dwFrameInterval);

static struct configfs_attribute *uvcg_frame_attrs[] = {
	&uvcg_frame_attr_b_frame_index,
	&uvcg_frame_attr_bm_capabilities,
	&uvcg_frame_attr_w_width,
	&uvcg_frame_attr_w_height,
	&uvcg_frame_attr_dw_min_bit_rate,
	&uvcg_frame_attr_dw_max_bit_rate,
	&uvcg_frame_attr_dw_max_video_frame_buffer_size,
	&uvcg_frame_attr_dw_default_frame_interval,
	&uvcg_frame_attr_dw_frame_interval,
	NULL,
};

static const struct config_item_type uvcg_frame_type = {
	.ct_item_ops	= &uvcg_config_item_ops,
	.ct_attrs	= uvcg_frame_attrs,
	.ct_owner	= THIS_MODULE,
};

static struct config_item *uvcg_frame_make(struct config_group *group,
					   const char *name)
{
	struct uvcg_frame *h;
	struct uvcg_format *fmt;
	struct f_uvc_opts *opts;
	struct config_item *opts_item;

	h = kzalloc(sizeof(*h), GFP_KERNEL);
	if (!h)
		return ERR_PTR(-ENOMEM);

	h->frame.b_descriptor_type		= USB_DT_CS_INTERFACE;
	h->frame.b_frame_index			= 1;
	h->frame.w_width			= 640;
	h->frame.w_height			= 360;
	h->frame.dw_min_bit_rate		= 18432000;
	h->frame.dw_max_bit_rate		= 55296000;
	h->frame.dw_max_video_frame_buffer_size	= 460800;
	h->frame.dw_default_frame_interval	= 666666;

	opts_item = group->cg_item.ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);

	mutex_lock(&opts->lock);
	fmt = to_uvcg_format(&group->cg_item);
	if (fmt->type == UVCG_UNCOMPRESSED) {
		h->frame.b_descriptor_subtype = UVC_VS_FRAME_UNCOMPRESSED;
		h->fmt_type = UVCG_UNCOMPRESSED;
	} else if (fmt->type == UVCG_MJPEG) {
		h->frame.b_descriptor_subtype = UVC_VS_FRAME_MJPEG;
		h->fmt_type = UVCG_MJPEG;
	} else {
		mutex_unlock(&opts->lock);
		kfree(h);
		return ERR_PTR(-EINVAL);
	}
	++fmt->num_frames;
#ifdef CONFIG_ARCH_BSP
	h->frame.b_frame_index	= fmt->num_frames;
#endif
	mutex_unlock(&opts->lock);

	config_item_init_type_name(&h->item, name, &uvcg_frame_type);

	return &h->item;
}

static void uvcg_frame_drop(struct config_group *group, struct config_item *item)
{
	struct uvcg_format *fmt;
	struct f_uvc_opts *opts;
	struct config_item *opts_item;

	opts_item = group->cg_item.ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);

	mutex_lock(&opts->lock);
	fmt = to_uvcg_format(&group->cg_item);
	--fmt->num_frames;
	mutex_unlock(&opts->lock);

	config_item_put(item);
}

static void uvcg_format_set_indices(struct config_group *fmt)
{
	struct config_item *ci;
	unsigned int i = 1;

	list_for_each_entry(ci, &fmt->cg_children, ci_entry) {
		struct uvcg_frame *frm;

		if (ci->ci_type != &uvcg_frame_type)
			continue;

		frm = to_uvcg_frame(ci);
		frm->frame.b_frame_index = i++;
	}
}

#ifdef CONFIG_ARCH_BSP
/* -----------------------------------------------------------------------------
 * frame_based
 */
struct uvcg_frame_based_frame {
	struct config_item	item;
	enum uvcg_format_type   fmt_type;
	struct {
		u8	b_length;
		u8	b_descriptor_type;
		u8	b_descriptor_subtype;
		u8	b_frame_index;
		u8	bm_capabilities;
		u16	w_width;
		u16	w_height;
		u32	dw_min_bit_rate;
		u32	dw_max_bit_rate;
		u32	dw_default_frame_interval;
		u8	b_frame_interval_type;
		u32  dw_bytes_per_line;
	} __attribute__((packed)) frame;
	u32 *dw_frame_interval;
};

static struct uvcg_frame_based_frame *to_uvcg_frame_based_frame(struct config_item *item)
{
	return container_of(item, struct uvcg_frame_based_frame, item);
}

#define UVCG_FRAME_BASED_FRAME_ATTR(cname, aname, bits) \
static ssize_t uvcg_frame_based_frame_##cname##_show(struct config_item *item, char *page)\
{									\
	struct uvcg_frame_based_frame *f = to_uvcg_frame_based_frame(item);			\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &f->item.ci_group->cg_subsys->su_mutex;\
	int result;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = f->item.ci_parent->ci_parent->ci_parent->ci_parent;	\
	opts = to_f_uvc_opts(opts_item);				\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf_s(page, PAGE_SIZE, "%d\n", f->frame.cname);	\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
	return result;							\
}									\
									\
static ssize_t  uvcg_frame_based_frame_##cname##_store(struct config_item *item,	\
					   const char *page, size_t len)\
{									\
	struct uvcg_frame_based_frame *f = to_uvcg_frame_based_frame(item);			\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct uvcg_format *fmt;					\
	struct mutex *su_mutex = &f->item.ci_group->cg_subsys->su_mutex;\
	int ret;							\
	typeof(f->frame.cname) num;								\
									\
	ret = kstrtou##bits(page, 0, &num);				\
	if (ret)							\
		return ret;						\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = f->item.ci_parent->ci_parent->ci_parent->ci_parent;	\
	opts = to_f_uvc_opts(opts_item);				\
	fmt = to_uvcg_format(f->item.ci_parent);			\
									\
	mutex_lock(&opts->lock);					\
	if (fmt->linked || opts->refcnt) {				\
		ret = -EBUSY;						\
		goto end;						\
	}								\
									\
	f->frame.cname = num;				\
	ret = len;							\
end:									\
	mutex_unlock(&opts->lock);					\
	mutex_unlock(su_mutex);						\
	return ret;							\
}									\
									\
UVC_ATTR(uvcg_frame_based_frame_, cname, aname);

static ssize_t uvcg_frame_based_frame_b_frame_index_show(struct config_item *item,
					     char *page)
{
	struct uvcg_frame_based_frame *f = to_uvcg_frame_based_frame(item);
	struct uvcg_format *fmt;
	struct f_uvc_opts *opts;
	struct config_item *opts_item;
	struct config_item *fmt_item;
	struct mutex *su_mutex = &f->item.ci_group->cg_subsys->su_mutex;
	int result;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	fmt_item = f->item.ci_parent;
	fmt = to_uvcg_format(fmt_item);

	if (!fmt->linked) {
		result = -EBUSY;
		goto out;
	}

	opts_item = fmt_item->ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);

	mutex_lock(&opts->lock);
	result = sprintf_s(page, PAGE_SIZE, "%u\n", f->frame.b_frame_index);
	mutex_unlock(&opts->lock);

out:
	mutex_unlock(su_mutex);
	return result;
}

UVC_ATTR_RO(uvcg_frame_based_frame_, b_frame_index, bFrameIndex);

UVCG_FRAME_BASED_FRAME_ATTR(bm_capabilities, bmCapabilities, 8);
UVCG_FRAME_BASED_FRAME_ATTR(w_width, wWidth, 16);
UVCG_FRAME_BASED_FRAME_ATTR(w_height, wHeight, 16);
UVCG_FRAME_BASED_FRAME_ATTR(dw_min_bit_rate, dwMinBitRate, 32);
UVCG_FRAME_BASED_FRAME_ATTR(dw_max_bit_rate, dwMaxBitRate, 32);
UVCG_FRAME_BASED_FRAME_ATTR(dw_default_frame_interval, dwDefaultFrameInterval, 32);
UVCG_FRAME_BASED_FRAME_ATTR(dw_bytes_per_line, dwBytesPerLine, 32);

#undef UVCG_FRAME_BASED_FRAME_ATTR

static ssize_t uvcg_frame_based_frame_dw_frame_interval_show(struct config_item *item,
						 char *page)
{
	struct uvcg_frame_based_frame *frm = to_uvcg_frame_based_frame(item);
	struct f_uvc_opts *opts;
	struct config_item *opts_item;
	struct mutex *su_mutex = &frm->item.ci_group->cg_subsys->su_mutex;
	int result, pg_state, i;
	int max_size = PAGE_SIZE;
	char *pg = page;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = frm->item.ci_parent->ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);

	mutex_lock(&opts->lock);
	for (result = 0, i = 0; (i < frm->frame.b_frame_interval_type) && (max_size > 0); ++i) {
		pg_state = sprintf_s(pg, max_size, "%u\n", frm->dw_frame_interval[i]);
		if (pg_state < 0)
			break;
		result += pg_state;
		pg = page + result;
		max_size = PAGE_SIZE - result;
	}
	mutex_unlock(&opts->lock);

	mutex_unlock(su_mutex);
	return result;
}

static ssize_t uvcg_frame_based_frame_dw_frame_interval_store(struct config_item *item,
						  const char *page, size_t len)
{
	struct uvcg_frame_based_frame *ch = to_uvcg_frame_based_frame(item);
	struct f_uvc_opts *opts;
	struct config_item *opts_item;
	struct uvcg_format *fmt;
	struct mutex *su_mutex = &ch->item.ci_group->cg_subsys->su_mutex;
	int ret = 0, n = 0;
	u32 *frm_intrv, *tmp;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = ch->item.ci_parent->ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);
	fmt = to_uvcg_format(ch->item.ci_parent);

	mutex_lock(&opts->lock);
	if (fmt->linked || opts->refcnt) {
		ret = -EBUSY;
		goto end;
	}

	ret = __uvcg_iter_frm_intrv(page, len, __uvcg_count_frm_intrv, &n);
	if (ret)
		goto end;

	tmp = frm_intrv = kcalloc(n, sizeof(u32), GFP_KERNEL);
	if (!frm_intrv) {
		ret = -ENOMEM;
		goto end;
	}

	ret = __uvcg_iter_frm_intrv(page, len, __uvcg_fill_frm_intrv, &tmp);
	if (ret) {
		kfree(frm_intrv);
		goto end;
	}

	kfree(ch->dw_frame_interval);
	ch->dw_frame_interval = frm_intrv;
	ch->frame.b_frame_interval_type = n;
	sort(ch->dw_frame_interval, n, sizeof(*ch->dw_frame_interval),
	     uvcg_config_compare_u32, NULL);
	ret = len;

end:
	mutex_unlock(&opts->lock);
	mutex_unlock(su_mutex);
	return ret;
}

UVC_ATTR(uvcg_frame_based_frame_, dw_frame_interval, dwFrameInterval);

static struct configfs_attribute *uvcg_frame_based_frame_attrs[] = {
	&uvcg_frame_based_frame_attr_b_frame_index,
	&uvcg_frame_based_frame_attr_bm_capabilities,
	&uvcg_frame_based_frame_attr_w_width,
	&uvcg_frame_based_frame_attr_w_height,
	&uvcg_frame_based_frame_attr_dw_min_bit_rate,
	&uvcg_frame_based_frame_attr_dw_max_bit_rate,
	&uvcg_frame_based_frame_attr_dw_default_frame_interval,
	&uvcg_frame_based_frame_attr_dw_frame_interval,
	&uvcg_frame_based_frame_attr_dw_bytes_per_line,
	NULL,
};

static const struct config_item_type uvcg_frame_based_frame_type = {
	.ct_item_ops	= &uvcg_config_item_ops,
	.ct_attrs	= uvcg_frame_based_frame_attrs,
	.ct_owner	= THIS_MODULE,
};

static struct config_item *uvcg_frame_based_frame_make(struct config_group *group,
					   const char *name)
{
	struct uvcg_frame_based_frame *h;
	struct uvcg_format *fmt;
	struct f_uvc_opts *opts;
	struct config_item *opts_item;

	h = kzalloc(sizeof(*h), GFP_KERNEL);
	if (!h)
		return ERR_PTR(-ENOMEM);

	h->frame.b_descriptor_type		= USB_DT_CS_INTERFACE;
	h->frame.b_frame_index			= 1;
	h->frame.w_width			= 640;
	h->frame.w_height			= 360;
	h->frame.dw_min_bit_rate		= 18432000;
	h->frame.dw_max_bit_rate		= 55296000;
	h->frame.dw_default_frame_interval	= 333333;
	h->frame.dw_bytes_per_line	= 0;

	opts_item = group->cg_item.ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);

	mutex_lock(&opts->lock);
	fmt = to_uvcg_format(&group->cg_item);
	if (fmt->type == UVCG_FRAME_FRAME_BASED) {
		h->frame.b_descriptor_subtype = UVC_VS_FRAME_FRAME_BASED;
		h->fmt_type = UVCG_FRAME_FRAME_BASED;
	} else {
		mutex_unlock(&opts->lock);
		kfree(h);
		return ERR_PTR(-EINVAL);
	}
	++fmt->num_frames;
	h->frame.b_frame_index	= fmt->num_frames;
	mutex_unlock(&opts->lock);

	config_item_init_type_name(&h->item, name, &uvcg_frame_based_frame_type);

	return &h->item;
}

static void uvcg_frame_based_frame_drop(struct config_group *group, struct config_item *item)
{
	struct uvcg_frame_based_frame *h = to_uvcg_frame_based_frame(item);
	struct uvcg_format *fmt;
	struct f_uvc_opts *opts;
	struct config_item *opts_item;

	opts_item = group->cg_item.ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);

	mutex_lock(&opts->lock);
	fmt = to_uvcg_format(&group->cg_item);
	--fmt->num_frames;
	kfree(h);
	mutex_unlock(&opts->lock);
}

/* -----------------------------------------------------------------------------
 * h264
 */

struct uvcg_h264_frame {
	struct config_item	item;
	enum uvcg_format_type	fmt_type;
	struct {
		u8	b_length;
		u8	b_descriptor_type;
		u8	b_descriptor_subtype;
		u8	b_frame_index;
		u16	w_width;
		u16	w_height;
		u16	w_sar_width;
		u16	w_sar_height;
		u16 w_profile;
		u8 b_level_idc;
		u16 w_constrained_toolset;
		u32 bm_supported_usages;
		u16 bm_capabilities;
		u32 bm_svcc_apabilities;
		u32 bm_mvcc_apabilities;
		u32	dw_min_bit_rate;
		u32	dw_max_bit_rate;
		u32	dw_default_frame_interval;
		u8 b_num_frameintervals;
	} __attribute__((packed)) frame;
	u32 *dw_frame_interval;
};

static struct uvcg_h264_frame *to_uvcg_h264_frame(struct config_item *item)
{
	return container_of(item, struct uvcg_h264_frame, item);
}

#define UVCG_H264_FRAME_ATTR_RO(cname, aname, bits) \
static ssize_t uvcg_h264_frame_##cname##_show(struct config_item *item, char *page) \
{									\
	struct uvcg_h264_frame *h = to_uvcg_h264_frame(item);			\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &h->item.ci_group->cg_subsys->su_mutex; \
	int result;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = h->item.ci_parent->ci_parent->ci_parent->ci_parent;	\
	opts = to_f_uvc_opts(opts_item);				\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf_s(page, PAGE_SIZE, "%d\n", h->frame.cname);	\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
	return result;							\
}									\
									\
UVC_ATTR_RO(uvcg_h264_frame_, cname, aname)

#define UVCG_H264_FRAME_ATTR(cname, aname, bits) \
static ssize_t uvcg_h264_frame_##cname##_show(struct config_item *item, char *page) \
{									\
	struct uvcg_h264_frame *h = to_uvcg_h264_frame(item);			\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &h->item.ci_group->cg_subsys->su_mutex; \
	int result;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = h->item.ci_parent->ci_parent->ci_parent->ci_parent;	\
	opts = to_f_uvc_opts(opts_item);				\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf_s(page, PAGE_SIZE, "%d\n", h->frame.cname);	\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
	return result;							\
}									\
									\
static ssize_t  uvcg_h264_frame_##cname##_store(struct config_item *item,	\
					   const char *page, size_t len)\
{									\
	struct uvcg_h264_frame *h = to_uvcg_h264_frame(item);			\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct uvcg_format *fmt;					\
	struct mutex *su_mutex = &h->item.ci_group->cg_subsys->su_mutex; \
	int ret;							\
	typeof(h->frame.cname) num;							\
									\
	ret = kstrtou##bits(page, 0, &num);				\
	if (ret)							\
		return ret;						\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = h->item.ci_parent->ci_parent->ci_parent->ci_parent;	\
	opts = to_f_uvc_opts(opts_item);				\
	fmt = to_uvcg_format(h->item.ci_parent);			\
									\
	mutex_lock(&opts->lock);					\
	if (fmt->linked || opts->refcnt) {				\
		ret = -EBUSY;						\
		goto end;						\
	}								\
									\
	h->frame.cname = num;				\
	ret = len;							\
end:									\
	mutex_unlock(&opts->lock);					\
	mutex_unlock(su_mutex);						\
	return ret;							\
}									\
									\
UVC_ATTR(uvcg_h264_frame_, cname, aname)

UVCG_H264_FRAME_ATTR(w_width, wWidth, 16);
UVCG_H264_FRAME_ATTR(w_height, wHeight, 16);
UVCG_H264_FRAME_ATTR(w_sar_width, wSARwidth, 16);
UVCG_H264_FRAME_ATTR(w_sar_height, wSARheight, 16);
UVCG_H264_FRAME_ATTR(w_profile, wProfile, 16);
UVCG_H264_FRAME_ATTR(b_level_idc, bLevelIDC, 8);
UVCG_H264_FRAME_ATTR_RO(w_constrained_toolset, wConstrainedToolset, 16);
UVCG_H264_FRAME_ATTR(bm_supported_usages, bmSupportedUsages, 32);
UVCG_H264_FRAME_ATTR(bm_capabilities, bmCapabilities, 16);
UVCG_H264_FRAME_ATTR(bm_svcc_apabilities, bmSVCCapabilities, 32);
UVCG_H264_FRAME_ATTR(bm_mvcc_apabilities, bmMVCCapabilities, 32);
UVCG_H264_FRAME_ATTR(dw_min_bit_rate, dwMinBitRate, 32);
UVCG_H264_FRAME_ATTR(dw_max_bit_rate, dwMaxBitRate, 32);
UVCG_H264_FRAME_ATTR(dw_default_frame_interval, dwDefaultFrameInterval, 32);

#undef UVCG_H264_FRAME_ATTR_RO
#undef UVCG_H264_FRAME_ATTR

static ssize_t uvcg_h264_frame_dw_frame_interval_show(struct config_item *item,
						 char *page)
{
	struct uvcg_h264_frame *frm = to_uvcg_h264_frame(item);
	struct f_uvc_opts *opts;
	struct config_item *opts_item;
	struct mutex *su_mutex = &frm->item.ci_group->cg_subsys->su_mutex;
	int result, i;
	char *pg = page;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = frm->item.ci_parent->ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);

	mutex_lock(&opts->lock);
	for (result = 0, i = 0; i < frm->frame.b_num_frameintervals; ++i) {
		result += sprintf_s(pg, PAGE_SIZE, "%d\n",
				  le32_to_cpu(frm->dw_frame_interval[i]));
		pg = page + result;
	}
	mutex_unlock(&opts->lock);

	mutex_unlock(su_mutex);
	return result;
}

static ssize_t uvcg_h264_frame_dw_frame_interval_store(struct config_item *item,
						  const char *page, size_t len)
{
	struct uvcg_h264_frame *ch = to_uvcg_h264_frame(item);
	struct f_uvc_opts *opts;
	struct config_item *opts_item;
	struct uvcg_format *fmt;
	struct mutex *su_mutex = &ch->item.ci_group->cg_subsys->su_mutex;
	int ret = 0, n = 0;
	u32 *frm_intrv, *tmp;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = ch->item.ci_parent->ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);
	fmt = to_uvcg_format(ch->item.ci_parent);

	mutex_lock(&opts->lock);
	if (fmt->linked || opts->refcnt) {
		ret = -EBUSY;
		goto end;
	}

	ret = __uvcg_iter_frm_intrv(page, len, __uvcg_count_frm_intrv, &n);
	if (ret)
		goto end;

	tmp = frm_intrv = kcalloc(n, sizeof(u32), GFP_KERNEL);
	if (!frm_intrv) {
		ret = -ENOMEM;
		goto end;
	}

	ret = __uvcg_iter_frm_intrv(page, len, __uvcg_fill_frm_intrv, &tmp);
	if (ret) {
		kfree(frm_intrv);
		goto end;
	}

	kfree(ch->dw_frame_interval);
	ch->dw_frame_interval = frm_intrv;
	ch->frame.b_num_frameintervals = n;
	sort(ch->dw_frame_interval, n, sizeof(*ch->dw_frame_interval),
		uvcg_config_compare_u32, NULL);
	ret = len;

end:
	mutex_unlock(&opts->lock);
	mutex_unlock(su_mutex);
	return ret;
}

UVC_ATTR(uvcg_h264_frame_, dw_frame_interval, dwFrameInterval);

static struct configfs_attribute *uvcg_h264_frame_attrs[] = {
	&uvcg_h264_frame_attr_w_width,
	&uvcg_h264_frame_attr_w_height,
	&uvcg_h264_frame_attr_w_sar_width,
	&uvcg_h264_frame_attr_w_sar_height,
	&uvcg_h264_frame_attr_w_profile,
	&uvcg_h264_frame_attr_b_level_idc,
	&uvcg_h264_frame_attr_w_constrained_toolset,
	&uvcg_h264_frame_attr_bm_supported_usages,
	&uvcg_h264_frame_attr_bm_capabilities,
	&uvcg_h264_frame_attr_bm_svcc_apabilities,
	&uvcg_h264_frame_attr_bm_mvcc_apabilities,
	&uvcg_h264_frame_attr_dw_min_bit_rate,
	&uvcg_h264_frame_attr_dw_max_bit_rate,
	&uvcg_h264_frame_attr_dw_default_frame_interval,
	&uvcg_h264_frame_attr_dw_frame_interval,
	NULL,
};

static struct config_item_type uvcg_h264_frame_type = {
	.ct_item_ops	= &uvcg_config_item_ops,
	.ct_attrs	= uvcg_h264_frame_attrs,
	.ct_owner	= THIS_MODULE,
};

static struct config_item *uvcg_h264_frame_make(struct config_group *group,
					   const char *name)
{
	struct uvcg_h264_frame *h;
	struct uvcg_format *fmt;
	struct f_uvc_opts *opts;
	struct config_item *opts_item;

	h = kzalloc(sizeof(*h), GFP_KERNEL);
	if (!h)
		return ERR_PTR(-ENOMEM);

	h->frame.b_descriptor_type		= USB_DT_CS_INTERFACE;
	h->frame.b_frame_index			= 1;
	h->frame.w_width			= cpu_to_le16(640);
	h->frame.w_height			= cpu_to_le16(360);
	h->frame.w_sar_width		= cpu_to_le16(1);
	h->frame.w_sar_height		= cpu_to_le16(1);
	h->frame.w_profile			= 0x4D00;
	h->frame.b_level_idc		= 0x1F;
	h->frame.w_constrained_toolset		= 0;
	h->frame.bm_supported_usages		= 0x50000;
	h->frame.bm_capabilities		= 0x21;
	h->frame.bm_mvcc_apabilities		= 0;
	h->frame.bm_svcc_apabilities		= 0;
	h->frame.dw_min_bit_rate		= cpu_to_le32(2048);
	h->frame.dw_max_bit_rate		= cpu_to_le32(167772160);
	h->frame.dw_default_frame_interval	= cpu_to_le32(333333);

	opts_item = group->cg_item.ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);

	mutex_lock(&opts->lock);
	fmt = to_uvcg_format(&group->cg_item);
	if (fmt->type == UVCG_H264) {
		h->frame.b_descriptor_subtype = UVC_VS_FRAME_H264;
		h->fmt_type = UVCG_H264;
	} else {
		mutex_unlock(&opts->lock);
		kfree(h);
		return ERR_PTR(-EINVAL);
	}
	++fmt->num_frames;
	h->frame.b_frame_index	= fmt->num_frames;
	mutex_unlock(&opts->lock);

	config_item_init_type_name(&h->item, name, &uvcg_h264_frame_type);

	return &h->item;
}

static void uvcg_h264_frame_drop(struct config_group *group, struct config_item *item)
{
	struct uvcg_h264_frame *h = to_uvcg_h264_frame(item);
	struct uvcg_format *fmt;
	struct f_uvc_opts *opts;
	struct config_item *opts_item;

	opts_item = group->cg_item.ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);

	mutex_lock(&opts->lock);
	fmt = to_uvcg_format(&group->cg_item);
	--fmt->num_frames;
	kfree(h);
	mutex_unlock(&opts->lock);
}
#endif

/* -----------------------------------------------------------------------------
 * streaming/uncompressed/<NAME>
 */

struct uvcg_uncompressed {
	struct uvcg_format		fmt;
	struct uvc_format_uncompressed	desc;
};

static struct uvcg_uncompressed *to_uvcg_uncompressed(struct config_item *item)
{
	return container_of(
		container_of(to_config_group(item), struct uvcg_format, group),
		struct uvcg_uncompressed, fmt);
}

static struct configfs_group_operations uvcg_uncompressed_group_ops = {
	.make_item		= uvcg_frame_make,
	.drop_item		= uvcg_frame_drop,
};

static ssize_t uvcg_uncompressed_guid_format_show(struct config_item *item,
							char *page)
{
	struct uvcg_uncompressed *ch = to_uvcg_uncompressed(item);
	struct f_uvc_opts *opts;
	struct config_item *opts_item;
	struct mutex *su_mutex = &ch->fmt.group.cg_subsys->su_mutex;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = ch->fmt.group.cg_item.ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);

	mutex_lock(&opts->lock);
	memcpy(page, ch->desc.guidFormat, sizeof(ch->desc.guidFormat));
	mutex_unlock(&opts->lock);

	mutex_unlock(su_mutex);

	return sizeof(ch->desc.guidFormat);
}

static ssize_t uvcg_uncompressed_guid_format_store(struct config_item *item,
						   const char *page, size_t len)
{
	struct uvcg_uncompressed *ch = to_uvcg_uncompressed(item);
	struct f_uvc_opts *opts;
	struct config_item *opts_item;
	struct mutex *su_mutex = &ch->fmt.group.cg_subsys->su_mutex;
	int ret;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = ch->fmt.group.cg_item.ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);

	mutex_lock(&opts->lock);
	if (ch->fmt.linked || opts->refcnt) {
		ret = -EBUSY;
		goto end;
	}

	memcpy(ch->desc.guidFormat, page,
	       min(sizeof(ch->desc.guidFormat), len));
	ret = sizeof(ch->desc.guidFormat);

end:
	mutex_unlock(&opts->lock);
	mutex_unlock(su_mutex);
	return ret;
}

UVC_ATTR(uvcg_uncompressed_, guid_format, guidFormat);

#define UVCG_UNCOMPRESSED_ATTR_RO(cname, aname, bits)			\
static ssize_t uvcg_uncompressed_##cname##_show(			\
	struct config_item *item, char *page)				\
{									\
	struct uvcg_uncompressed *u = to_uvcg_uncompressed(item);	\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &u->fmt.group.cg_subsys->su_mutex;	\
	int result;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = u->fmt.group.cg_item.ci_parent->ci_parent->ci_parent;\
	opts = to_f_uvc_opts(opts_item);				\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf(page, "%u\n", le##bits##_to_cpu(u->desc.aname));\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
	return result;							\
}									\
									\
UVC_ATTR_RO(uvcg_uncompressed_, cname, aname);

#define UVCG_UNCOMPRESSED_ATTR(cname, aname, bits)			\
static ssize_t uvcg_uncompressed_##cname##_show(			\
	struct config_item *item, char *page)				\
{									\
	struct uvcg_uncompressed *u = to_uvcg_uncompressed(item);	\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &u->fmt.group.cg_subsys->su_mutex;	\
	int result;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = u->fmt.group.cg_item.ci_parent->ci_parent->ci_parent;\
	opts = to_f_uvc_opts(opts_item);				\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf(page, "%u\n", le##bits##_to_cpu(u->desc.aname));\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
	return result;							\
}									\
									\
static ssize_t								\
uvcg_uncompressed_##cname##_store(struct config_item *item,		\
				    const char *page, size_t len)	\
{									\
	struct uvcg_uncompressed *u = to_uvcg_uncompressed(item);	\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &u->fmt.group.cg_subsys->su_mutex;	\
	int ret;							\
	u8 num;								\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = u->fmt.group.cg_item.ci_parent->ci_parent->ci_parent;\
	opts = to_f_uvc_opts(opts_item);				\
									\
	mutex_lock(&opts->lock);					\
	if (u->fmt.linked || opts->refcnt) {				\
		ret = -EBUSY;						\
		goto end;						\
	}								\
									\
	ret = kstrtou8(page, 0, &num);					\
	if (ret)							\
		goto end;						\
									\
	u->desc.aname = num;						\
	ret = len;							\
end:									\
	mutex_unlock(&opts->lock);					\
	mutex_unlock(su_mutex);						\
	return ret;							\
}									\
									\
UVC_ATTR(uvcg_uncompressed_, cname, aname);

UVCG_UNCOMPRESSED_ATTR_RO(b_format_index, bFormatIndex, 8);
UVCG_UNCOMPRESSED_ATTR(b_bits_per_pixel, bBitsPerPixel, 8);
UVCG_UNCOMPRESSED_ATTR(b_default_frame_index, bDefaultFrameIndex, 8);
UVCG_UNCOMPRESSED_ATTR_RO(b_aspect_ratio_x, bAspectRatioX, 8);
UVCG_UNCOMPRESSED_ATTR_RO(b_aspect_ratio_y, bAspectRatioY, 8);
UVCG_UNCOMPRESSED_ATTR_RO(bm_interface_flags, bmInterfaceFlags, 8);

#undef UVCG_UNCOMPRESSED_ATTR
#undef UVCG_UNCOMPRESSED_ATTR_RO

static inline ssize_t
uvcg_uncompressed_bma_controls_show(struct config_item *item, char *page)
{
	struct uvcg_uncompressed *unc = to_uvcg_uncompressed(item);
	return uvcg_format_bma_controls_show(&unc->fmt, page);
}

static inline ssize_t
uvcg_uncompressed_bma_controls_store(struct config_item *item,
				     const char *page, size_t len)
{
	struct uvcg_uncompressed *unc = to_uvcg_uncompressed(item);
	return uvcg_format_bma_controls_store(&unc->fmt, page, len);
}

UVC_ATTR(uvcg_uncompressed_, bma_controls, bmaControls);

static struct configfs_attribute *uvcg_uncompressed_attrs[] = {
	&uvcg_uncompressed_attr_b_format_index,
	&uvcg_uncompressed_attr_guid_format,
	&uvcg_uncompressed_attr_b_bits_per_pixel,
	&uvcg_uncompressed_attr_b_default_frame_index,
	&uvcg_uncompressed_attr_b_aspect_ratio_x,
	&uvcg_uncompressed_attr_b_aspect_ratio_y,
	&uvcg_uncompressed_attr_bm_interface_flags,
	&uvcg_uncompressed_attr_bma_controls,
	NULL,
};

static const struct config_item_type uvcg_uncompressed_type = {
	.ct_item_ops	= &uvcg_config_item_ops,
	.ct_group_ops	= &uvcg_uncompressed_group_ops,
	.ct_attrs	= uvcg_uncompressed_attrs,
	.ct_owner	= THIS_MODULE,
};

static struct config_group *uvcg_uncompressed_make(struct config_group *group,
						   const char *name)
{
	static char guid[] = {
		'Y',  'U',  'Y',  '2', 0x00, 0x00, 0x10, 0x00,
		 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71
	};
	struct uvcg_uncompressed *h;

	h = kzalloc(sizeof(*h), GFP_KERNEL);
	if (!h)
		return ERR_PTR(-ENOMEM);

	h->desc.bLength			= UVC_DT_FORMAT_UNCOMPRESSED_SIZE;
	h->desc.bDescriptorType		= USB_DT_CS_INTERFACE;
	h->desc.bDescriptorSubType	= UVC_VS_FORMAT_UNCOMPRESSED;
	memcpy(h->desc.guidFormat, guid, sizeof(guid));
	h->desc.bBitsPerPixel		= 16;
	h->desc.bDefaultFrameIndex	= 1;
	h->desc.bAspectRatioX		= 0;
	h->desc.bAspectRatioY		= 0;
	h->desc.bmInterfaceFlags	= 0;
	h->desc.bCopyProtect		= 0;

#if defined(CONFIG_ARCH_BSP) && defined(CONFIG_USB_F_UVC_STILL_IMAGE) && !defined(CONFIG_UVC_NO_STATUS_INT_EP)
	h->fmt.still_image = NULL;
#endif

	h->fmt.type = UVCG_UNCOMPRESSED;
	config_group_init_type_name(&h->fmt.group, name,
				    &uvcg_uncompressed_type);

	return &h->fmt.group;
}

static struct configfs_group_operations uvcg_uncompressed_grp_ops = {
	.make_group		= uvcg_uncompressed_make,
};

static const struct uvcg_config_group_type uvcg_uncompressed_grp_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_group_ops	= &uvcg_uncompressed_grp_ops,
		.ct_owner	= THIS_MODULE,
	},
	.name = "uncompressed",
};

/* -----------------------------------------------------------------------------
 * streaming/mjpeg/<NAME>
 */

struct uvcg_mjpeg {
	struct uvcg_format		fmt;
	struct uvc_format_mjpeg		desc;
};

static struct uvcg_mjpeg *to_uvcg_mjpeg(struct config_item *item)
{
	return container_of(
		container_of(to_config_group(item), struct uvcg_format, group),
		struct uvcg_mjpeg, fmt);
}

static struct configfs_group_operations uvcg_mjpeg_group_ops = {
	.make_item		= uvcg_frame_make,
	.drop_item		= uvcg_frame_drop,
};

#define UVCG_MJPEG_ATTR_RO(cname, aname, bits)				\
static ssize_t uvcg_mjpeg_##cname##_show(struct config_item *item, char *page)\
{									\
	struct uvcg_mjpeg *u = to_uvcg_mjpeg(item);			\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &u->fmt.group.cg_subsys->su_mutex;	\
	int result;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = u->fmt.group.cg_item.ci_parent->ci_parent->ci_parent;\
	opts = to_f_uvc_opts(opts_item);				\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf(page, "%u\n", le##bits##_to_cpu(u->desc.aname));\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
	return result;							\
}									\
									\
UVC_ATTR_RO(uvcg_mjpeg_, cname, aname)

#define UVCG_MJPEG_ATTR(cname, aname, bits)				\
static ssize_t uvcg_mjpeg_##cname##_show(struct config_item *item, char *page)\
{									\
	struct uvcg_mjpeg *u = to_uvcg_mjpeg(item);			\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &u->fmt.group.cg_subsys->su_mutex;	\
	int result;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = u->fmt.group.cg_item.ci_parent->ci_parent->ci_parent;\
	opts = to_f_uvc_opts(opts_item);				\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf(page, "%u\n", le##bits##_to_cpu(u->desc.aname));\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
	return result;							\
}									\
									\
static ssize_t								\
uvcg_mjpeg_##cname##_store(struct config_item *item,			\
			   const char *page, size_t len)		\
{									\
	struct uvcg_mjpeg *u = to_uvcg_mjpeg(item);			\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &u->fmt.group.cg_subsys->su_mutex;	\
	int ret;							\
	u8 num;								\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = u->fmt.group.cg_item.ci_parent->ci_parent->ci_parent;\
	opts = to_f_uvc_opts(opts_item);				\
									\
	mutex_lock(&opts->lock);					\
	if (u->fmt.linked || opts->refcnt) {				\
		ret = -EBUSY;						\
		goto end;						\
	}								\
									\
	ret = kstrtou8(page, 0, &num);					\
	if (ret)							\
		goto end;						\
									\
	u->desc.aname = num;						\
	ret = len;							\
end:									\
	mutex_unlock(&opts->lock);					\
	mutex_unlock(su_mutex);						\
	return ret;							\
}									\
									\
UVC_ATTR(uvcg_mjpeg_, cname, aname)

UVCG_MJPEG_ATTR_RO(b_format_index, bFormatIndex, 8);
UVCG_MJPEG_ATTR(b_default_frame_index, bDefaultFrameIndex, 8);
UVCG_MJPEG_ATTR_RO(bm_flags, bmFlags, 8);
UVCG_MJPEG_ATTR_RO(b_aspect_ratio_x, bAspectRatioX, 8);
UVCG_MJPEG_ATTR_RO(b_aspect_ratio_y, bAspectRatioY, 8);
UVCG_MJPEG_ATTR_RO(bm_interface_flags, bmInterfaceFlags, 8);

#undef UVCG_MJPEG_ATTR
#undef UVCG_MJPEG_ATTR_RO

static inline ssize_t
uvcg_mjpeg_bma_controls_show(struct config_item *item, char *page)
{
	struct uvcg_mjpeg *u = to_uvcg_mjpeg(item);
	return uvcg_format_bma_controls_show(&u->fmt, page);
}

static inline ssize_t
uvcg_mjpeg_bma_controls_store(struct config_item *item,
				     const char *page, size_t len)
{
	struct uvcg_mjpeg *u = to_uvcg_mjpeg(item);
	return uvcg_format_bma_controls_store(&u->fmt, page, len);
}

UVC_ATTR(uvcg_mjpeg_, bma_controls, bmaControls);

static struct configfs_attribute *uvcg_mjpeg_attrs[] = {
	&uvcg_mjpeg_attr_b_format_index,
	&uvcg_mjpeg_attr_b_default_frame_index,
	&uvcg_mjpeg_attr_bm_flags,
	&uvcg_mjpeg_attr_b_aspect_ratio_x,
	&uvcg_mjpeg_attr_b_aspect_ratio_y,
	&uvcg_mjpeg_attr_bm_interface_flags,
	&uvcg_mjpeg_attr_bma_controls,
	NULL,
};

static const struct config_item_type uvcg_mjpeg_type = {
	.ct_item_ops	= &uvcg_config_item_ops,
	.ct_group_ops	= &uvcg_mjpeg_group_ops,
	.ct_attrs	= uvcg_mjpeg_attrs,
	.ct_owner	= THIS_MODULE,
};

static struct config_group *uvcg_mjpeg_make(struct config_group *group,
						   const char *name)
{
	struct uvcg_mjpeg *h;

	h = kzalloc(sizeof(*h), GFP_KERNEL);
	if (!h)
		return ERR_PTR(-ENOMEM);

	h->desc.bLength			= UVC_DT_FORMAT_MJPEG_SIZE;
	h->desc.bDescriptorType		= USB_DT_CS_INTERFACE;
	h->desc.bDescriptorSubType	= UVC_VS_FORMAT_MJPEG;
	h->desc.bDefaultFrameIndex	= 1;
	h->desc.bAspectRatioX		= 0;
	h->desc.bAspectRatioY		= 0;
	h->desc.bmInterfaceFlags	= 0;
	h->desc.bCopyProtect		= 0;

#if defined(CONFIG_ARCH_BSP) && defined(CONFIG_USB_F_UVC_STILL_IMAGE) && !defined(CONFIG_UVC_NO_STATUS_INT_EP)
	h->fmt.still_image = NULL;
#endif

	h->fmt.type = UVCG_MJPEG;
	config_group_init_type_name(&h->fmt.group, name,
				    &uvcg_mjpeg_type);

	return &h->fmt.group;
}

static struct configfs_group_operations uvcg_mjpeg_grp_ops = {
	.make_group		= uvcg_mjpeg_make,
};

static const struct uvcg_config_group_type uvcg_mjpeg_grp_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_group_ops	= &uvcg_mjpeg_grp_ops,
		.ct_owner	= THIS_MODULE,
	},
	.name = "mjpeg",
};

#ifdef CONFIG_ARCH_BSP
/* -----------------------------------------------------------------------------
 * streaming/frame_based/<NAME>
 */

struct uvcg_frame_based_format {
	struct uvcg_format		fmt;
	struct uvc_frame_based_format_desc		desc;
};

static struct uvcg_frame_based_format *to_uvcg_frame_based_format(struct config_item *item)
{
	return container_of(
		container_of(to_config_group(item), struct uvcg_format, group),
		struct uvcg_frame_based_format, fmt);
}

static struct configfs_group_operations uvcg_frame_based_format_group_ops = {
	.make_item		= uvcg_frame_based_frame_make,
	.drop_item		= uvcg_frame_based_frame_drop,
};

static ssize_t uvcg_frame_based_guid_format_show(struct config_item *item,
							char *page)
{
	struct uvcg_frame_based_format *ch = to_uvcg_frame_based_format(item);
	struct f_uvc_opts *opts;
	struct config_item *opts_item;
	struct mutex *su_mutex = &ch->fmt.group.cg_subsys->su_mutex;
	int result;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = ch->fmt.group.cg_item.ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);

	mutex_lock(&opts->lock);
	result = memcpy_s(page, PAGE_SIZE, ch->desc.guidFormat, sizeof(ch->desc.guidFormat));
	mutex_unlock(&opts->lock);

	mutex_unlock(su_mutex);

	if (result != 0)
		return 0;
	return sizeof(ch->desc.guidFormat);
}

static ssize_t uvcg_frame_based_guid_format_store(struct config_item *item,
						   const char *page, size_t len)
{
	struct uvcg_frame_based_format *ch = to_uvcg_frame_based_format(item);
	struct f_uvc_opts *opts;
	struct config_item *opts_item;
	struct mutex *su_mutex = &ch->fmt.group.cg_subsys->su_mutex;
	int ret, rc;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = ch->fmt.group.cg_item.ci_parent->ci_parent->ci_parent;
	opts = to_f_uvc_opts(opts_item);

	mutex_lock(&opts->lock);
	if (ch->fmt.linked || opts->refcnt) {
		ret = -EBUSY;
		goto end;
	}

	rc = memcpy_s(ch->desc.guidFormat, sizeof(ch->desc.guidFormat), page,
	       min(sizeof(ch->desc.guidFormat), len));
	if (rc != 0) {
		ret = 0;
		goto end;
	}
	ret = sizeof(ch->desc.guidFormat);

end:
	mutex_unlock(&opts->lock);
	mutex_unlock(su_mutex);
	return ret;
}

UVC_ATTR(uvcg_frame_based_, guid_format, guidFormat);

#define UVCG_FRAME_BASED_FORMAT_ATTR_RO(cname, aname, bits)				\
static ssize_t uvcg_frame_based_format_##cname##_show(struct config_item *item, char *page)\
{									\
	struct uvcg_frame_based_format *u = to_uvcg_frame_based_format(item);			\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &u->fmt.group.cg_subsys->su_mutex;	\
	int result;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = u->fmt.group.cg_item.ci_parent->ci_parent->ci_parent;\
	opts = to_f_uvc_opts(opts_item);				\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf_s(page, PAGE_SIZE, "%u\n", le##bits##_to_cpu(u->desc.aname));		\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
	return result;							\
}									\
									\
UVC_ATTR_RO(uvcg_frame_based_format_, cname, aname)

#define UVCG_FRAME_BASED_FORMAT_ATTR(cname, aname, bits)				\
static ssize_t uvcg_frame_based_format_##cname##_show(struct config_item *item, char *page)\
{									\
	struct uvcg_frame_based_format *u = to_uvcg_frame_based_format(item);			\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &u->fmt.group.cg_subsys->su_mutex;	\
	int result;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = u->fmt.group.cg_item.ci_parent->ci_parent->ci_parent;\
	opts = to_f_uvc_opts(opts_item);				\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf_s(page, PAGE_SIZE, "%u\n", le##bits##_to_cpu(u->desc.aname));		\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
	return result;							\
}									\
									\
static ssize_t								\
uvcg_frame_based_format_##cname##_store(struct config_item *item,			\
			   const char *page, size_t len)		\
{									\
	struct uvcg_frame_based_format *u = to_uvcg_frame_based_format(item);			\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &u->fmt.group.cg_subsys->su_mutex;	\
	int ret;							\
	u8 num;								\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = u->fmt.group.cg_item.ci_parent->ci_parent->ci_parent;\
	opts = to_f_uvc_opts(opts_item);				\
									\
	mutex_lock(&opts->lock);					\
	if (u->fmt.linked || opts->refcnt) {				\
		ret = -EBUSY;						\
		goto end;						\
	}								\
									\
	ret = kstrtou8(page, 0, &num);					\
	if (ret)							\
		goto end;						\
									\
	u->desc.aname = num;						\
	ret = len;							\
end:									\
	mutex_unlock(&opts->lock);					\
	mutex_unlock(su_mutex);						\
	return ret;							\
}									\
									\
UVC_ATTR(uvcg_frame_based_format_, cname, aname)

UVCG_FRAME_BASED_FORMAT_ATTR_RO(b_format_index, bFormatIndex, 8);
UVCG_FRAME_BASED_FORMAT_ATTR(b_default_frame_index, bDefaultFrameIndex, 8);
UVCG_FRAME_BASED_FORMAT_ATTR_RO(b_aspect_ratio_x, bAspectRatioX, 8);
UVCG_FRAME_BASED_FORMAT_ATTR_RO(b_aspect_ratio_y, bAspectRatioY, 8);
UVCG_FRAME_BASED_FORMAT_ATTR_RO(bm_interface_flags, bmInterfaceFlags, 8);

#undef UVCG_FRAME_BASED_FORMAT_ATTR
#undef UVCG_FRAME_BASED_FORMAT_ATTR_RO

static inline ssize_t
uvcg_frame_based_format_bma_controls_show(struct config_item *item, char *page)
{
	struct uvcg_frame_based_format *u = to_uvcg_frame_based_format(item);
	return uvcg_format_bma_controls_show(&u->fmt, page);
}

static inline ssize_t
uvcg_frame_based_format_bma_controls_store(struct config_item *item,
				     const char *page, size_t len)
{
	struct uvcg_frame_based_format *u = to_uvcg_frame_based_format(item);
	return uvcg_format_bma_controls_store(&u->fmt, page, len);
}

UVC_ATTR(uvcg_frame_based_format_, bma_controls, bmaControls);

static struct configfs_attribute *uvcg_frame_based_format_attrs[] = {
	&uvcg_frame_based_format_attr_b_format_index,
	&uvcg_frame_based_attr_guid_format,
	&uvcg_frame_based_format_attr_b_default_frame_index,
	&uvcg_frame_based_format_attr_b_aspect_ratio_x,
	&uvcg_frame_based_format_attr_b_aspect_ratio_y,
	&uvcg_frame_based_format_attr_bm_interface_flags,
	&uvcg_frame_based_format_attr_bma_controls,
	NULL,
};

static struct config_item_type uvcg_frame_based_format_type = {
	.ct_item_ops	= &uvcg_config_item_ops,
	.ct_group_ops	= &uvcg_frame_based_format_group_ops,
	.ct_attrs	= uvcg_frame_based_format_attrs,
	.ct_owner	= THIS_MODULE,
};

static struct config_group *uvcg_frame_based_format_make(struct config_group *group,
						   const char *name)
{
	static char guid[] = { /*Declear frame frame based as H264*/
		'H',  '2',  '6',  '4', 0x00, 0x00, 0x10, 0x00,
		 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71
	};
	struct uvcg_frame_based_format *h;
	int result;

	h = kzalloc(sizeof(*h), GFP_KERNEL);
	if (!h)
		return ERR_PTR(-ENOMEM);

	h->desc.bLength			= UVC_DT_FRAME_BASED_FORMAT_SIZE;
	h->desc.bDescriptorType		= USB_DT_CS_INTERFACE;
	h->desc.bDescriptorSubType	= UVC_VS_FORMAT_FRAME_BASED;
	result = memcpy_s(h->desc.guidFormat, sizeof(h->desc.guidFormat), guid, sizeof(guid));
	if (result != 0)
		return NULL;
	h->desc.bBitsPerPixel		= 16;
	h->desc.bDefaultFrameIndex	= 1;
	h->desc.bAspectRatioX	= 0;
	h->desc.bAspectRatioY	= 0;
	h->desc.bmInterfaceFlags	= 0;
	h->desc.bCopyProtect		= 0;
	h->desc.bVariableSize		= 1;

#if defined(CONFIG_ARCH_BSP) && defined(CONFIG_USB_F_UVC_STILL_IMAGE) && !defined(CONFIG_UVC_NO_STATUS_INT_EP)
	h->fmt.still_image = NULL;
#endif

	h->fmt.type = UVCG_FRAME_FRAME_BASED;
	config_group_init_type_name(&h->fmt.group, name,
				    &uvcg_frame_based_format_type);

	return &h->fmt.group;
}

static struct configfs_group_operations uvcg_frame_based_format_grp_ops = {
	.make_group		= uvcg_frame_based_format_make,
};

static const struct uvcg_config_group_type uvcg_frame_based_format_grp_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_group_ops	= &uvcg_frame_based_format_grp_ops,
		.ct_owner	= THIS_MODULE,
	},
	.name = "framebased",
};

/* streaming/h264<NAME> */
struct uvcg_h264_format {
	struct uvcg_format		fmt;
	struct uvc_h264_format_desc		desc;
};

static struct uvcg_h264_format *to_uvcg_h264_format(struct config_item *item)
{
	return container_of(
		container_of(to_config_group(item), struct uvcg_format, group),
		struct uvcg_h264_format, fmt);
}

static struct configfs_group_operations uvcg_h264_format_group_ops = {
	.make_item		= uvcg_h264_frame_make,
	.drop_item		= uvcg_h264_frame_drop,
};

#define UVCG_H264_FORMAT_ATTR_RO(cname, aname, bits) \
static ssize_t uvcg_h264_format_##cname##_show(struct config_item *item, char *page) \
{									\
	struct uvcg_h264_format *u = to_uvcg_h264_format(item);			\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &u->fmt.group.cg_subsys->su_mutex;	\
	int result;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = u->fmt.group.cg_item.ci_parent->ci_parent->ci_parent;\
	opts = to_f_uvc_opts(opts_item);				\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf_s(page, PAGE_SIZE, "%d\n", le##bits##_to_cpu(u->desc.aname));		\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
	return result;							\
}									\
									\
UVC_ATTR_RO(uvcg_h264_format_, cname, aname)

#define UVCG_H264_FORMAT_ATTR(cname, aname, bits) \
static ssize_t uvcg_h264_format_##cname##_show(struct config_item *item, char *page) \
{									\
	struct uvcg_h264_format *u = to_uvcg_h264_format(item);			\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &u->fmt.group.cg_subsys->su_mutex;	\
	int result;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = u->fmt.group.cg_item.ci_parent->ci_parent->ci_parent;\
	opts = to_f_uvc_opts(opts_item);				\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf_s(page, PAGE_SIZE, "%d\n", le##bits##_to_cpu(u->desc.aname));		\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
	return result;							\
}									\
									\
static ssize_t								\
uvcg_h264_format_##cname##_store(struct config_item *item,			\
			   const char *page, size_t len)		\
{									\
	struct uvcg_h264_format *u = to_uvcg_h264_format(item);			\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &u->fmt.group.cg_subsys->su_mutex;	\
	int ret;							\
	u##bits num;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = u->fmt.group.cg_item.ci_parent->ci_parent->ci_parent;\
	opts = to_f_uvc_opts(opts_item);				\
									\
	mutex_lock(&opts->lock);					\
	if (u->fmt.linked || opts->refcnt) {				\
		ret = -EBUSY;						\
		goto end;						\
	}								\
									\
	ret = kstrtou##bits(page, 0, &num);				\
	if (ret)							\
		return ret;						\
									\
	u->desc.aname = num;						\
	ret = len;							\
end:									\
	mutex_unlock(&opts->lock);					\
	mutex_unlock(su_mutex);						\
	return ret;							\
}									\
									\
UVC_ATTR(uvcg_h264_format_, cname, aname)

UVCG_H264_FORMAT_ATTR(b_max_codec_config_delay, bMaxCodecConfigDelay, 8);
UVCG_H264_FORMAT_ATTR(bm_supported_slice_modes, bmSupportedSliceModes, 8);
UVCG_H264_FORMAT_ATTR(bm_supported_sync_frame_types, bmSupportedSyncFrameTypes, 8);
UVCG_H264_FORMAT_ATTR(b_resolution_scaling, bResolutionScaling, 8);
UVCG_H264_FORMAT_ATTR(bm_supported_rate_control_modes, bmSupportedRateControlModes, 8);
UVCG_H264_FORMAT_ATTR_RO(w_max_mbpersec_one_resolution_no_scalability,
			   wMaxMBperSecOneResolutionNoScalability, 16);
UVCG_H264_FORMAT_ATTR_RO(w_max_mbpersec_two_resolution_no_scalability,
			   wMaxMBperSecTwoResolutionsNoScalability, 16);
UVCG_H264_FORMAT_ATTR_RO(w_max_mbpersec_three_resolution_no_scalability,
			   wMaxMBperSecThreeResolutionsNoScalability, 16);
UVCG_H264_FORMAT_ATTR_RO(w_max_mbpersec_four_resolution_no_scalability,
			   wMaxMBperSecFourResolutionsNoScalability, 16);
UVCG_H264_FORMAT_ATTR_RO(w_max_mbpersec_one_resolution_temporalscal_ability,
			   wMaxMBperSecOneResolutionTemporalScalability, 16);
UVCG_H264_FORMAT_ATTR_RO(w_max_mbpersec_two_resolution_temporalscal_ability,
			   wMaxMBperSecTwoResolutionsTemporalScalability, 16);
UVCG_H264_FORMAT_ATTR_RO(w_max_mbpersec_three_resolution_temporalscal_ability,
			   wMaxMBperSecThreeResolutionsTemporalScalability, 16);
UVCG_H264_FORMAT_ATTR_RO(w_max_mbpersec_four_resolution_temporalscal_ability,
			   wMaxMBperSecFourResolutionsTemporalScalability, 16);
UVCG_H264_FORMAT_ATTR_RO(w_max_mbpersec_one_resolution_temporal_spatialscal_ability,
			   wMaxMBperSecOneResolutionTemporalSpatialScalability, 16);
UVCG_H264_FORMAT_ATTR_RO(w_max_mbpersec_two_resolution_temporal_spatialscal_ability,
			   wMaxMBperSecTwoResolutionsTemporalSpatialScalability, 16);
UVCG_H264_FORMAT_ATTR_RO(w_max_mbpersec_three_resolution_temporal_spatialscal_ability,
			   wMaxMBperSecThreeResolutionsTemporalSpatialScalability, 16);
UVCG_H264_FORMAT_ATTR_RO(w_max_mbpersec_four_resolution_temporal_spatialscal_ability,
			   wMaxMBperSecFourResolutionsTemporalSpatialScalability, 16);
UVCG_H264_FORMAT_ATTR_RO(w_max_mbpersec_one_resolution_full_scalability,
			   wMaxMBperSecOneResolutionFullScalability, 16);
UVCG_H264_FORMAT_ATTR_RO(w_max_mbpersec_two_resolution_full_scalability,
			   wMaxMBperSecTwoResolutionsFullScalability, 16);
UVCG_H264_FORMAT_ATTR_RO(w_max_mbpersec_three_resolution_full_scalability,
			   wMaxMBperSecThreeResolutionsFullScalability, 16);
UVCG_H264_FORMAT_ATTR_RO(w_max_mbpersec_four_resolution_full_scalability,
			   wMaxMBperSecFourResolutionsFullScalability, 16);

#undef UVCG_H264_FORMAT_ATTR
#undef UVCG_H264_FORMAT_ATTR_RO

static inline ssize_t
uvcg_h264_format_bma_controls_show(struct config_item *item, char *page)
{
	struct uvcg_h264_format *u = to_uvcg_h264_format(item);
	return uvcg_format_bma_controls_show(&u->fmt, page);
}

static inline ssize_t
uvcg_h264_format_bma_controls_store(struct config_item *item,
				     const char *page, size_t len)
{
	struct uvcg_h264_format *u = to_uvcg_h264_format(item);
	return uvcg_format_bma_controls_store(&u->fmt, page, len);
}

UVC_ATTR(uvcg_h264_format_, bma_controls, bmaControls);

static struct configfs_attribute *uvcg_h264_format_attrs[] = {
	&uvcg_h264_format_attr_b_max_codec_config_delay,
	&uvcg_h264_format_attr_bm_supported_slice_modes,
	&uvcg_h264_format_attr_bm_supported_sync_frame_types,
	&uvcg_h264_format_attr_b_resolution_scaling,
	&uvcg_h264_format_attr_bm_supported_rate_control_modes,
	&uvcg_h264_format_attr_w_max_mbpersec_one_resolution_no_scalability,
	&uvcg_h264_format_attr_w_max_mbpersec_two_resolution_no_scalability,
	&uvcg_h264_format_attr_w_max_mbpersec_three_resolution_no_scalability,
	&uvcg_h264_format_attr_w_max_mbpersec_four_resolution_no_scalability,
	&uvcg_h264_format_attr_w_max_mbpersec_one_resolution_temporalscal_ability,
	&uvcg_h264_format_attr_w_max_mbpersec_two_resolution_temporalscal_ability,
	&uvcg_h264_format_attr_w_max_mbpersec_three_resolution_temporalscal_ability,
	&uvcg_h264_format_attr_w_max_mbpersec_four_resolution_temporalscal_ability,
	&uvcg_h264_format_attr_w_max_mbpersec_one_resolution_temporal_spatialscal_ability,
	&uvcg_h264_format_attr_w_max_mbpersec_two_resolution_temporal_spatialscal_ability,
	&uvcg_h264_format_attr_w_max_mbpersec_three_resolution_temporal_spatialscal_ability,
	&uvcg_h264_format_attr_w_max_mbpersec_four_resolution_temporal_spatialscal_ability,
	&uvcg_h264_format_attr_w_max_mbpersec_one_resolution_full_scalability,
	&uvcg_h264_format_attr_w_max_mbpersec_two_resolution_full_scalability,
	&uvcg_h264_format_attr_w_max_mbpersec_three_resolution_full_scalability,
	&uvcg_h264_format_attr_w_max_mbpersec_four_resolution_full_scalability,
	&uvcg_h264_format_attr_bma_controls,
	NULL,
};

static struct config_item_type uvcg_h264_format_type = {
	.ct_item_ops	= &uvcg_config_item_ops,
	.ct_group_ops	= &uvcg_h264_format_group_ops,
	.ct_attrs	= uvcg_h264_format_attrs,
	.ct_owner	= THIS_MODULE,
};

static struct config_group *uvcg_h264_format_make(struct config_group *group,
						   const char *name)
{
	struct uvcg_h264_format *h;

	h = kzalloc(sizeof(*h), GFP_KERNEL);
	if (!h)
		return ERR_PTR(-ENOMEM);

	h->desc.bLength			= UVC_DT_H264_FORMAT_SIZE;
	h->desc.bDescriptorType		= USB_DT_CS_INTERFACE;
	h->desc.bDescriptorSubType	= UVC_VS_FORMAT_H264;
	h->desc.bDefaultFrameIndex	= 1;
	h->desc.bMaxCodecConfigDelay	= 0x8;
	h->desc.bmSupportedSliceModes	= 0xE;
	h->desc.bmSupportedSyncFrameTypes	= 0xA;
	h->desc.bResolutionScaling	= 0x3;
	h->desc.Reserved1		= 0;
	h->desc.bmSupportedRateControlModes	= 0x3;

	h->desc.wMaxMBperSecOneResolutionNoScalability		= 0xF5;
	h->desc.wMaxMBperSecTwoResolutionsNoScalability		= 0;
	h->desc.wMaxMBperSecThreeResolutionsNoScalability	= 0;
	h->desc.wMaxMBperSecFourResolutionsNoScalability	= 0;

	h->desc.wMaxMBperSecOneResolutionTemporalScalability	= 0xF5;
	h->desc.wMaxMBperSecTwoResolutionsTemporalScalability	= 0;
	h->desc.wMaxMBperSecThreeResolutionsTemporalScalability	= 0;
	h->desc.wMaxMBperSecFourResolutionsTemporalScalability	= 0;

	h->desc.wMaxMBperSecOneResolutionTemporalQualityScalability	= 0;
	h->desc.wMaxMBperSecTwoResolutionsTemporalQualityScalability	= 0;
	h->desc.wMaxMBperSecThreeResolutionsTemporalQualityScalability	= 0;
	h->desc.wMaxMBperSecFourResolutionsTemporalQualityScalability	= 0;

	h->desc.wMaxMBperSecOneResolutionTemporalSpatialScalability	= 0;
	h->desc.wMaxMBperSecTwoResolutionsTemporalSpatialScalability	= 0;
	h->desc.wMaxMBperSecThreeResolutionsTemporalSpatialScalability	= 0;
	h->desc.wMaxMBperSecFourResolutionsTemporalSpatialScalability	= 0;

	h->desc.wMaxMBperSecOneResolutionFullScalability	= 0;
	h->desc.wMaxMBperSecTwoResolutionsFullScalability	= 0;
	h->desc.wMaxMBperSecThreeResolutionsFullScalability	= 0;
	h->desc.wMaxMBperSecFourResolutionsFullScalability	= 0;

#if defined(CONFIG_ARCH_BSP) && defined(CONFIG_USB_F_UVC_STILL_IMAGE) && !defined(CONFIG_UVC_NO_STATUS_INT_EP)
	h->fmt.still_image = NULL;
#endif

	h->fmt.type = UVCG_H264;
	config_group_init_type_name(&h->fmt.group, name,
				    &uvcg_h264_format_type);

	return &h->fmt.group;
}

static struct configfs_group_operations uvcg_h264_format_grp_ops = {
	.make_group		= uvcg_h264_format_make,
};

static const struct uvcg_config_group_type uvcg_h264_format_grp_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_group_ops	= &uvcg_h264_format_grp_ops,
		.ct_owner	= THIS_MODULE,
	},
	.name = "h264",
};
#endif  // CONFIG_ARCH_BSP

/* -----------------------------------------------------------------------------
 * streaming/color_matching/default
 */

#define UVCG_DEFAULT_COLOR_MATCHING_ATTR(cname, aname, bits)		\
static ssize_t uvcg_default_color_matching_##cname##_show(		\
	struct config_item *item, char *page)				\
{									\
	struct config_group *group = to_config_group(item);		\
	struct f_uvc_opts *opts;					\
	struct config_item *opts_item;					\
	struct mutex *su_mutex = &group->cg_subsys->su_mutex;		\
	struct uvc_color_matching_descriptor *cd;			\
	int result;							\
									\
	mutex_lock(su_mutex); /* for navigating configfs hierarchy */	\
									\
	opts_item = group->cg_item.ci_parent->ci_parent->ci_parent;	\
	opts = to_f_uvc_opts(opts_item);				\
	cd = &opts->uvc_color_matching;					\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf(page, "%u\n", le##bits##_to_cpu(cd->aname));	\
	mutex_unlock(&opts->lock);					\
									\
	mutex_unlock(su_mutex);						\
	return result;							\
}									\
									\
UVC_ATTR_RO(uvcg_default_color_matching_, cname, aname)

UVCG_DEFAULT_COLOR_MATCHING_ATTR(b_color_primaries, bColorPrimaries, 8);
UVCG_DEFAULT_COLOR_MATCHING_ATTR(b_transfer_characteristics,
				 bTransferCharacteristics, 8);
UVCG_DEFAULT_COLOR_MATCHING_ATTR(b_matrix_coefficients, bMatrixCoefficients, 8);

#undef UVCG_DEFAULT_COLOR_MATCHING_ATTR

static struct configfs_attribute *uvcg_default_color_matching_attrs[] = {
	&uvcg_default_color_matching_attr_b_color_primaries,
	&uvcg_default_color_matching_attr_b_transfer_characteristics,
	&uvcg_default_color_matching_attr_b_matrix_coefficients,
	NULL,
};

static const struct uvcg_config_group_type uvcg_default_color_matching_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_attrs	= uvcg_default_color_matching_attrs,
		.ct_owner	= THIS_MODULE,
	},
	.name = "default",
};

/* -----------------------------------------------------------------------------
 * streaming/color_matching
 */

static const struct uvcg_config_group_type uvcg_color_matching_grp_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_owner	= THIS_MODULE,
	},
	.name = "color_matching",
	.children = (const struct uvcg_config_group_type*[]) {
		&uvcg_default_color_matching_type,
		NULL,
	},
};

/* -----------------------------------------------------------------------------
 * streaming/class/{fs|hs|ss}
 */

struct uvcg_streaming_class_group {
	struct config_group group;
	const char *name;
};

static inline struct uvc_descriptor_header
***__uvcg_get_stream_class_arr(struct config_item *i, struct f_uvc_opts *o)
{
	struct uvcg_streaming_class_group *group =
		container_of(i, struct uvcg_streaming_class_group,
			     group.cg_item);

	if (!strcmp(group->name, "fs"))
		return &o->uvc_fs_streaming_cls;

	if (!strcmp(group->name, "hs"))
		return &o->uvc_hs_streaming_cls;

	if (!strcmp(group->name, "ss"))
		return &o->uvc_ss_streaming_cls;

	return NULL;
}

enum uvcg_strm_type {
	UVCG_HEADER = 0,
	UVCG_FORMAT,
	UVCG_FRAME,
#if defined(CONFIG_ARCH_BSP) && defined(CONFIG_USB_F_UVC_STILL_IMAGE) && !defined(CONFIG_UVC_NO_STATUS_INT_EP)
	UVCG_STILL
#endif
};

#if defined(CONFIG_ARCH_BSP) && defined(CONFIG_USB_F_UVC_STILL_IMAGE) && !defined(CONFIG_UVC_NO_STATUS_INT_EP)

DECLARE_STILL_IMAGE_FRAME(1);

const static int white_list[] = {UVCG_UNCOMPRESSED, UVCG_MJPEG};
const static u8 nv21_guid[] = {0x4E, 0x56, 0x32, 0x31, 0x00, 0x00, 0x10, 0x00,
								0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71};
const static u8 nv12_guid[] = {0x4E, 0x56, 0x31, 0x32, 0x00, 0x00, 0x10, 0x00,
								0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71};

static int frame_check(void *aframe, struct uvcg_frame *uframe, int nums)
{
	int i;
	struct uvc_still_frame_size *frame = aframe;

	if (uframe->frame.bm_capabilities == 0x0) {
		return 1;  // capability is not set, skip.
	}

	for (i = 0; i < nums; i++) {
		if (frame[i].wHeight == uframe->frame.w_height && frame[i].wWidth == uframe->frame.w_width) {
			return 1;
		}
	}
	return 0;
}

static int check_white_list(struct uvcg_format	*fmt)
{
	int i;
	int len = sizeof(white_list) / sizeof(int);
	for (i = 0; i < len; i++) {
		if (fmt->type == white_list[i])
			return 1;
	}
	return 0;
}

#define GUID_SIZE 16
static int check_guid_fmt(const u8 *a, const u8 *b)
{
	int i;
	for (i = 0; i < GUID_SIZE; i++) {
		if (a[i] != b[i]) {
			return 1;
		}
	}
	return 0;
}

static int uvcg_still_image_make(struct uvcg_streaming_header *h)
{
	int ret;
	struct uvcg_format_ptr *f;
	struct config_group *grp;
	struct config_item *item;
	struct uvcg_frame *frm;
	struct STILL_IMAGE_FRAME(1) *still_image = NULL;

	h->desc.bTriggerSupport     = 1;
	h->desc.bTriggerUsage       = 1;

	if (h->desc.bStillCaptureMethod != UVC_STILL_IMAGE_METHOD1 && h->desc.bStillCaptureMethod != UVC_STILL_IMAGE_METHOD2 &&
			h->desc.bStillCaptureMethod != UVC_STILL_IMAGE_METHOD3) {
		h->desc.bStillCaptureMethod = 0;
		h->desc.bTriggerSupport     = 0;
		h->desc.bTriggerUsage       = 0;
		return 0;
	}

	list_for_each_entry(f, &h->formats, entry) {
		grp = &f->fmt->group;
		if (check_white_list(f->fmt) == 0)
			continue;

		if (f->fmt->type == UVCG_UNCOMPRESSED) {
			struct uvcg_uncompressed *uncom = container_of(f->fmt, struct uvcg_uncompressed, fmt);
			if (!check_guid_fmt(uncom->desc.guidFormat, nv21_guid) || !check_guid_fmt(uncom->desc.guidFormat, nv12_guid))
				continue;  //  nv21 and v12 formate still image can't be showed in amcap3.0.9.exe, skip.
		}

		still_image = NULL;
		if (h->desc.bStillCaptureMethod != UVC_STILL_IMAGE_METHOD1 && f->fmt->still_image == NULL) {
			f->fmt->still_image = kzalloc(UVC_DT_STILL_IMAGE_FRAME_SIZE(f->fmt->num_frames, 0), GFP_KERNEL);
			if (f->fmt->still_image == NULL) {
				return -ENOMEM;
			}
			still_image = f->fmt->still_image;
			still_image->bLength = UVC_DT_STILL_IMAGE_FRAME_SIZE(f->fmt->num_frames, 0);
			still_image->bDescriptorType 	= USB_DT_CS_INTERFACE;
			still_image->bDescriptorSubType = UVC_VS_STILL_IMAGE_FRAME;
			still_image->bEndpointAddress	= 0;
			still_image->bNumImageSizePatterns	= 0;
		}

		still_image = f->fmt->still_image;
		list_for_each_entry(item, &grp->cg_children, ci_entry) {
			frm = to_uvcg_frame(item);
			frm->frame.bm_capabilities = 0x1;
			if (h->desc.bStillCaptureMethod != UVC_STILL_IMAGE_METHOD1 &&
				f->fmt->num_frames > still_image->bNumImageSizePatterns &&
				frame_check(still_image->frame, frm, f->fmt->num_frames) == 0) {
				struct uvc_still_frame_size *frame = still_image->frame;
				frame[still_image->bNumImageSizePatterns].wWidth = frm->frame.w_width;
				frame[still_image->bNumImageSizePatterns].wHeight = frm->frame.w_height;
				still_image->bNumImageSizePatterns++;
			}
		}
	}
	return ret;
}

static int uvcg_still_image_drop(struct uvcg_streaming_header *h)
{
	struct uvcg_format_ptr *f;
	list_for_each_entry(f, &h->formats, entry) {
		if (f->fmt->still_image != NULL) {
			kfree(f->fmt->still_image);
			f->fmt->still_image = NULL;
		}
	}
	return 0;
}
#endif

/*
 * Iterate over a hierarchy of streaming descriptors' config items.
 * The items are created by the user with configfs.
 *
 * It "processes" the header pointed to by @priv1, then for each format
 * that follows the header "processes" the format itself and then for
 * each frame inside a format "processes" the frame.
 *
 * As a "processing" function the @fun is used.
 *
 * __uvcg_iter_strm_cls() is used in two context: first, to calculate
 * the amount of memory needed for an array of streaming descriptors
 * and second, to actually fill the array.
 *
 * @h: streaming header pointer
 * @priv2: an "inout" parameter (the caller might want to see the changes to it)
 * @priv3: an "inout" parameter (the caller might want to see the changes to it)
 * @fun: callback function for processing each level of the hierarchy
 */
static int __uvcg_iter_strm_cls(struct uvcg_streaming_header *h,
	void *priv2, void *priv3,
	int (*fun)(void *, void *, void *, int, enum uvcg_strm_type type))
{
	struct uvcg_format_ptr *f;
	struct config_group *grp;
	struct config_item *item;
	struct uvcg_frame *frm;
	int ret, i, j;

	if (!fun)
		return -EINVAL;

	i = j = 0;
	ret = fun(h, priv2, priv3, 0, UVCG_HEADER);
	if (ret)
		return ret;
#if defined(CONFIG_ARCH_BSP) && defined(CONFIG_USB_F_UVC_STILL_IMAGE) && !defined(CONFIG_UVC_NO_STATUS_INT_EP)
	uvcg_still_image_make(h);
#endif
	list_for_each_entry(f, &h->formats, entry) {
		ret = fun(f->fmt, priv2, priv3, i++, UVCG_FORMAT);
		if (ret)
			return ret;
		grp = &f->fmt->group;
		list_for_each_entry(item, &grp->cg_children, ci_entry) {
			frm = to_uvcg_frame(item);
			ret = fun(frm, priv2, priv3, j++, UVCG_FRAME);
			if (ret)
				return ret;
		}
#if defined(CONFIG_ARCH_BSP) && defined(CONFIG_USB_F_UVC_STILL_IMAGE) && !defined(CONFIG_UVC_NO_STATUS_INT_EP)
		if (f->fmt->still_image != NULL) {
			ret = fun(f->fmt->still_image, priv2, priv3, f->fmt->num_frames, UVCG_STILL);
			if (ret)
				return ret;
		}
#endif
	}

	return ret;
}

/*
 * Count how many bytes are needed for an array of streaming descriptors.
 *
 * @priv1: pointer to a header, format or frame
 * @priv2: inout parameter, accumulated size of the array
 * @priv3: inout parameter, accumulated number of the array elements
 * @n: unused, this function's prototype must match @fun in __uvcg_iter_strm_cls
 */
static int __uvcg_cnt_strm(void *priv1, void *priv2, void *priv3, int n,
			   enum uvcg_strm_type type)
{
	size_t *size = priv2;
	size_t *count = priv3;

	switch (type) {
	case UVCG_HEADER: {
		struct uvcg_streaming_header *h = priv1;

		*size += sizeof(h->desc);
		/* bmaControls */
		*size += h->num_fmt * UVCG_STREAMING_CONTROL_SIZE;
	}
	break;
#if defined(CONFIG_ARCH_BSP) && defined(CONFIG_USB_F_UVC_STILL_IMAGE) && !defined(CONFIG_UVC_NO_STATUS_INT_EP)
	case UVCG_STILL: {
		struct STILL_IMAGE_FRAME(1) *h = priv1;
		*size += h->bLength;
	}
	break;
#endif
	case UVCG_FORMAT: {
		struct uvcg_format *fmt = priv1;

		if (fmt->type == UVCG_UNCOMPRESSED) {
			struct uvcg_uncompressed *u =
				container_of(fmt, struct uvcg_uncompressed,
					     fmt);

			*size += sizeof(u->desc);
		} else if (fmt->type == UVCG_MJPEG) {
			struct uvcg_mjpeg *m =
				container_of(fmt, struct uvcg_mjpeg, fmt);

			*size += sizeof(m->desc);
#ifdef CONFIG_ARCH_BSP
		} else if (fmt->type == UVCG_FRAME_FRAME_BASED) {
			struct uvcg_frame_based_format *f =
				container_of(fmt, struct uvcg_frame_based_format, fmt);

			*size += sizeof(f->desc);
		} else if (fmt->type == UVCG_H264) {
			struct uvcg_h264_format *h =
				container_of(fmt, struct uvcg_h264_format, fmt);

			*size += sizeof(h->desc);
#endif
		} else {
			return -EINVAL;
		}
	}
	break;
	case UVCG_FRAME: {
		struct uvcg_frame *frm = priv1;
		int sz = sizeof(frm->dw_frame_interval);
#ifdef CONFIG_ARCH_BSP
		if (frm->frame.b_descriptor_subtype == UVC_VS_FRAME_FRAME_BASED) {
			struct uvcg_frame_based_frame *fb_frm = priv1;
			*size += sizeof(fb_frm->frame);
			*size += fb_frm->frame.b_frame_interval_type * sizeof(fb_frm->dw_frame_interval);

			++*count;
			return 0;
		} else if (frm->frame.b_descriptor_subtype == UVC_VS_FRAME_H264) {
			struct uvcg_h264_frame *h_frm = priv1;
			*size += sizeof(h_frm->frame);
			*size += h_frm->frame.b_num_frameintervals * sizeof(h_frm->dw_frame_interval);

			++*count;
			return 0;
		}
#endif
		*size += sizeof(frm->frame);
		*size += frm->frame.b_frame_interval_type * sz;
	}
	break;
	}

	++*count;

	return 0;
}

/*
 * Fill an array of streaming descriptors.
 *
 * @priv1: pointer to a header, format or frame
 * @priv2: inout parameter, pointer into a block of memory
 * @priv3: inout parameter, pointer to a 2-dimensional array
 */
static int __uvcg_fill_strm(void *priv1, void *priv2, void *priv3, int n,
			    enum uvcg_strm_type type)
{
	void **dest = priv2;
	struct uvc_descriptor_header ***array = priv3;
	size_t sz;

	**array = *dest;
	++*array;

	switch (type) {
	case UVCG_HEADER: {
		struct uvc_input_header_descriptor *ihdr = *dest;
		struct uvcg_streaming_header *h = priv1;
		struct uvcg_format_ptr *f;

		memcpy(*dest, &h->desc, sizeof(h->desc));
		*dest += sizeof(h->desc);
		sz = UVCG_STREAMING_CONTROL_SIZE;
		list_for_each_entry(f, &h->formats, entry) {
			memcpy(*dest, f->fmt->bmaControls, sz);
			*dest += sz;
		}
		ihdr->bLength = sizeof(h->desc) + h->num_fmt * sz;
		ihdr->bNumFormats = h->num_fmt;
	}
	break;
	case UVCG_FORMAT: {
		struct uvcg_format *fmt = priv1;

		if (fmt->type == UVCG_UNCOMPRESSED) {
			struct uvcg_uncompressed *u =
				container_of(fmt, struct uvcg_uncompressed,
					     fmt);

			u->desc.bFormatIndex = n + 1;
			u->desc.bNumFrameDescriptors = fmt->num_frames;
			memcpy(*dest, &u->desc, sizeof(u->desc));
			*dest += sizeof(u->desc);
		} else if (fmt->type == UVCG_MJPEG) {
			struct uvcg_mjpeg *m =
				container_of(fmt, struct uvcg_mjpeg, fmt);

			m->desc.bFormatIndex = n + 1;
			m->desc.bNumFrameDescriptors = fmt->num_frames;
			memcpy(*dest, &m->desc, sizeof(m->desc));
			*dest += sizeof(m->desc);
#ifdef CONFIG_ARCH_BSP
		} else if (fmt->type == UVCG_FRAME_FRAME_BASED) {
			struct uvc_frame_based_format_desc *ffb = *dest;
			struct uvcg_frame_based_format *f =
				container_of(fmt, struct uvcg_frame_based_format, fmt);

			if (memcpy_s(*dest, sizeof(f->desc), &f->desc, sizeof(f->desc)) != 0)
				return -EINVAL;
			*dest += sizeof(f->desc);
			ffb->bNumFrameDescriptors = fmt->num_frames;
			ffb->bFormatIndex = n + 1;
		} else if (fmt->type == UVCG_H264) {
			struct uvc_h264_format_desc *h264 = *dest;
			struct uvcg_h264_format *h =
				container_of(fmt, struct uvcg_h264_format, fmt);

			memcpy(*dest, &h->desc, sizeof(h->desc));
			*dest += sizeof(h->desc);
			h264->bNumFrameDescriptors = fmt->num_frames;
			h264->bFormatIndex = n + 1;
#endif
		} else {
			return -EINVAL;
		}
	}
	break;
	case UVCG_FRAME: {
		struct uvcg_frame *frm = priv1;
		struct uvc_descriptor_header *h = *dest;

#ifdef CONFIG_ARCH_BSP
		if (frm->frame.b_descriptor_subtype == UVC_VS_FRAME_FRAME_BASED) {
			struct uvcg_frame_based_frame *fb_frm = priv1;
			sz = sizeof(fb_frm->frame);
			if (memcpy_s(*dest, sz, &fb_frm->frame, sz) != 0)
				return -EINVAL;
			*dest += sz;
			sz = fb_frm->frame.b_frame_interval_type *
			sizeof(*fb_frm->dw_frame_interval);
			if (memcpy_s(*dest, sz, fb_frm->dw_frame_interval, sz) != 0)
				return -EINVAL;
			*dest += sz;
			h->bLength = UVC_DT_FRAME_BASED_FRAME_SIZE(
				fb_frm->frame.b_frame_interval_type);
			return 0;
		} else if (frm->frame.b_descriptor_subtype == UVC_VS_FRAME_H264) {
			struct uvcg_h264_frame *h_frm = priv1;
			sz = sizeof(h_frm->frame);
			memcpy_s(*dest, sz, &h_frm->frame, sz);
			*dest += sz;
			sz = h_frm->frame.b_num_frameintervals *
			sizeof(*h_frm->dw_frame_interval);
			memcpy_s(*dest, sz, h_frm->dw_frame_interval, sz);
			*dest += sz;
			h->bLength = UVC_DT_H264_FRAME_SIZE(
				h_frm->frame.b_num_frameintervals);
			return 0;
		}
#endif
		sz = sizeof(frm->frame);
#ifdef CONFIG_ARCH_BSP
		if (memcpy_s(*dest, sz, &frm->frame, sz) != 0)
			return -EINVAL;
#else
		memcpy(*dest, &frm->frame, sz);
#endif
		*dest += sz;
		sz = frm->frame.b_frame_interval_type *
			sizeof(*frm->dw_frame_interval);
#ifdef CONFIG_ARCH_BSP
		if (memcpy_s(*dest, sz, frm->dw_frame_interval, sz) != 0)
			return -EINVAL;
#else
		memcpy(*dest, frm->dw_frame_interval, sz);
#endif
		*dest += sz;
		if (frm->fmt_type == UVCG_UNCOMPRESSED)
			h->bLength = UVC_DT_FRAME_UNCOMPRESSED_SIZE(
				frm->frame.b_frame_interval_type);
		else if (frm->fmt_type == UVCG_MJPEG)
			h->bLength = UVC_DT_FRAME_MJPEG_SIZE(
				frm->frame.b_frame_interval_type);
	}
	break;
#if defined(CONFIG_ARCH_BSP) && defined(CONFIG_USB_F_UVC_STILL_IMAGE) && !defined(CONFIG_UVC_NO_STATUS_INT_EP)
	case UVCG_STILL: {
		struct STILL_IMAGE_FRAME(1) *still = priv1;
		struct uvc_descriptor_header *h = *dest;
		sz = still->bLength;
		memcpy_s(*dest, sz, still, sz);
		*dest += sz;
		h->bLength = sz;
	}
	break;
#endif
	}

	return 0;
}

static int uvcg_streaming_class_allow_link(struct config_item *src,
					   struct config_item *target)
{
	struct config_item *streaming, *header;
	struct f_uvc_opts *opts;
	struct mutex *su_mutex = &src->ci_group->cg_subsys->su_mutex;
	struct uvc_descriptor_header ***class_array, **cl_arr;
	struct uvcg_streaming_header *target_hdr;
	void *data, *data_save;
	size_t size = 0, count = 0;
	int ret = -EINVAL;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	streaming = src->ci_parent->ci_parent;
	header = config_group_find_item(to_config_group(streaming), "header");
	if (!header || target->ci_parent != header)
		goto out;

	opts = to_f_uvc_opts(streaming->ci_parent);

	mutex_lock(&opts->lock);

	class_array = __uvcg_get_stream_class_arr(src, opts);
	if (!class_array || *class_array || opts->refcnt) {
		ret = -EBUSY;
		goto unlock;
	}

	target_hdr = to_uvcg_streaming_header(target);
	ret = __uvcg_iter_strm_cls(target_hdr, &size, &count, __uvcg_cnt_strm);
	if (ret)
		goto unlock;

	count += 2; /* color_matching, NULL */
	*class_array = kcalloc(count, sizeof(void *), GFP_KERNEL);
	if (!*class_array) {
		ret = -ENOMEM;
		goto unlock;
	}

	data = data_save = kzalloc(size, GFP_KERNEL);
	if (!data) {
		kfree(*class_array);
		*class_array = NULL;
		ret = -ENOMEM;
		goto unlock;
	}
	cl_arr = *class_array;
	ret = __uvcg_iter_strm_cls(target_hdr, &data, &cl_arr,
				   __uvcg_fill_strm);
	if (ret) {
		kfree(*class_array);
		*class_array = NULL;
		/*
		 * __uvcg_fill_strm() called from __uvcg_iter_stream_cls()
		 * might have advanced the "data", so use a backup copy
		 */
		kfree(data_save);
		goto unlock;
	}
	*cl_arr = (struct uvc_descriptor_header *)&opts->uvc_color_matching;

	++target_hdr->linked;
	ret = 0;

unlock:
	mutex_unlock(&opts->lock);
out:
	config_item_put(header);
	mutex_unlock(su_mutex);
	return ret;
}

static void uvcg_streaming_class_drop_link(struct config_item *src,
					  struct config_item *target)
{
	struct config_item *streaming, *header;
	struct f_uvc_opts *opts;
	struct mutex *su_mutex = &src->ci_group->cg_subsys->su_mutex;
	struct uvc_descriptor_header ***class_array;
	struct uvcg_streaming_header *target_hdr;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	streaming = src->ci_parent->ci_parent;
	header = config_group_find_item(to_config_group(streaming), "header");
	if (!header || target->ci_parent != header)
		goto out;

	opts = to_f_uvc_opts(streaming->ci_parent);

	mutex_lock(&opts->lock);

	class_array = __uvcg_get_stream_class_arr(src, opts);
	if (!class_array || !*class_array)
		goto unlock;

	if (opts->refcnt)
		goto unlock;

	target_hdr = to_uvcg_streaming_header(target);
#if defined(CONFIG_ARCH_BSP) && defined(CONFIG_USB_F_UVC_STILL_IMAGE) && !defined(CONFIG_UVC_NO_STATUS_INT_EP)
	uvcg_still_image_drop(target_hdr);
#endif
	--target_hdr->linked;
	kfree(**class_array);
	kfree(*class_array);
	*class_array = NULL;

unlock:
	mutex_unlock(&opts->lock);
out:
	config_item_put(header);
	mutex_unlock(su_mutex);
}

static struct configfs_item_operations uvcg_streaming_class_item_ops = {
	.release	= uvcg_config_item_release,
	.allow_link	= uvcg_streaming_class_allow_link,
	.drop_link	= uvcg_streaming_class_drop_link,
};

static const struct config_item_type uvcg_streaming_class_type = {
	.ct_item_ops	= &uvcg_streaming_class_item_ops,
	.ct_owner	= THIS_MODULE,
};

/* -----------------------------------------------------------------------------
 * streaming/class
 */

static int uvcg_streaming_class_create_children(struct config_group *parent)
{
	static const char * const names[] = { "fs", "hs", "ss" };
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(names); ++i) {
		struct uvcg_streaming_class_group *group;

		group = kzalloc(sizeof(*group), GFP_KERNEL);
		if (!group)
			return -ENOMEM;

		group->name = names[i];

		config_group_init_type_name(&group->group, group->name,
					    &uvcg_streaming_class_type);
		configfs_add_default_group(&group->group, parent);
	}

	return 0;
}

static const struct uvcg_config_group_type uvcg_streaming_class_grp_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_owner	= THIS_MODULE,
	},
	.name = "class",
	.create_children = uvcg_streaming_class_create_children,
};

/* -----------------------------------------------------------------------------
 * streaming
 */

static ssize_t uvcg_default_streaming_b_interface_number_show(
	struct config_item *item, char *page)
{
	struct config_group *group = to_config_group(item);
	struct mutex *su_mutex = &group->cg_subsys->su_mutex;
	struct config_item *opts_item;
	struct f_uvc_opts *opts;
	int result = 0;

	mutex_lock(su_mutex); /* for navigating configfs hierarchy */

	opts_item = item->ci_parent;
	opts = to_f_uvc_opts(opts_item);

	mutex_lock(&opts->lock);
	result += sprintf(page, "%u\n", opts->streaming_interface);
	mutex_unlock(&opts->lock);

	mutex_unlock(su_mutex);

	return result;
}

UVC_ATTR_RO(uvcg_default_streaming_, b_interface_number, bInterfaceNumber);

static struct configfs_attribute *uvcg_default_streaming_attrs[] = {
	&uvcg_default_streaming_attr_b_interface_number,
	NULL,
};

static const struct uvcg_config_group_type uvcg_streaming_grp_type = {
	.type = {
		.ct_item_ops	= &uvcg_config_item_ops,
		.ct_attrs	= uvcg_default_streaming_attrs,
		.ct_owner	= THIS_MODULE,
	},
	.name = "streaming",
	.children = (const struct uvcg_config_group_type*[]) {
		&uvcg_streaming_header_grp_type,
		&uvcg_uncompressed_grp_type,
		&uvcg_mjpeg_grp_type,
#ifdef CONFIG_ARCH_BSP
		&uvcg_frame_based_format_grp_type,
		&uvcg_h264_format_grp_type,
#endif
		&uvcg_color_matching_grp_type,
		&uvcg_streaming_class_grp_type,
		NULL,
	},
};

/* -----------------------------------------------------------------------------
 * UVC function
 */

static void uvc_func_item_release(struct config_item *item)
{
	struct f_uvc_opts *opts = to_f_uvc_opts(item);

	uvcg_config_remove_children(to_config_group(item));
	usb_put_function_instance(&opts->func_inst);
}

static struct configfs_item_operations uvc_func_item_ops = {
	.release	= uvc_func_item_release,
};

#define UVCG_OPTS_ATTR(cname, aname, limit)				\
static ssize_t f_uvc_opts_##cname##_show(				\
	struct config_item *item, char *page)				\
{									\
	struct f_uvc_opts *opts = to_f_uvc_opts(item);			\
	int result;							\
									\
	mutex_lock(&opts->lock);					\
	result = sprintf(page, "%u\n", opts->cname);			\
	mutex_unlock(&opts->lock);					\
									\
	return result;							\
}									\
									\
static ssize_t								\
f_uvc_opts_##cname##_store(struct config_item *item,			\
			   const char *page, size_t len)		\
{									\
	struct f_uvc_opts *opts = to_f_uvc_opts(item);			\
	unsigned int num;						\
	int ret;							\
									\
	mutex_lock(&opts->lock);					\
	if (opts->refcnt) {						\
		ret = -EBUSY;						\
		goto end;						\
	}								\
									\
	ret = kstrtouint(page, 0, &num);				\
	if (ret)							\
		goto end;						\
									\
	if (num > limit) {						\
		ret = -EINVAL;						\
		goto end;						\
	}								\
	opts->cname = num;						\
	ret = len;							\
end:									\
	mutex_unlock(&opts->lock);					\
	return ret;							\
}									\
									\
UVC_ATTR(f_uvc_opts_, cname, cname)

UVCG_OPTS_ATTR(streaming_interval, streaming_interval, 16);
#ifdef CONFIG_ARCH_BSP
UVCG_OPTS_ATTR(streaming_maxpacket, streaming_maxpacket, 0x10000U);
#if defined(CONFIG_USB_F_UVC_STILL_IMAGE) && !defined(CONFIG_UVC_NO_STATUS_INT_EP)
UVCG_OPTS_ATTR(still_capture_method, still_capture_method, 3);
UVCG_OPTS_ATTR(still_maxpacket, still_maxpacket, 0x10000U);
#endif
#else
UVCG_OPTS_ATTR(streaming_maxpacket, streaming_maxpacket, 3072);
#endif
UVCG_OPTS_ATTR(streaming_maxburst, streaming_maxburst, 15);

#ifdef CONFIG_ARCH_BSP
#define UVCG_OPTS_ATTR_TRANSFER(cname, aname)				\
static ssize_t f_uvc_opts_##cname##_show(				\
	struct config_item *item, char *page)				\
{									\
	struct f_uvc_opts *opts = to_f_uvc_opts(item);			\
	int result;							\
	char *str;							\
									\
	mutex_lock(&opts->lock);					\
	switch (opts->cname) {						\
	case USB_ENDPOINT_XFER_BULK:					\
		str = "bulk";						\
		break;							\
	case USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_ASYNC:		\
		str = "isoc";						\
		break;							\
	default:							\
		str = "unknown";					\
		break;							\
	}								\
	result = sprintf_s(page, PAGE_SIZE, "%s\n", str);				\
	mutex_unlock(&opts->lock);					\
									\
	return result;							\
}									\
									\
static ssize_t								\
f_uvc_opts_##cname##_store(struct config_item *item,			\
			   const char *page, size_t len)		\
{									\
	struct f_uvc_opts *opts = to_f_uvc_opts(item);			\
	int ret = 0;							\
									\
	mutex_lock(&opts->lock);					\
	if (opts->refcnt) {						\
		ret = -EBUSY;						\
		goto end;						\
	}								\
									\
	if (!strncmp(page, "bulk", 4))					\
		opts->cname = USB_ENDPOINT_XFER_BULK;			\
	else if (!strncmp(page, "isoc", 4))				\
		opts->cname = USB_ENDPOINT_XFER_ISOC | USB_ENDPOINT_SYNC_ASYNC; \
	else {								\
		ret = -EINVAL;						\
		goto end;						\
	}								\
									\
	ret = len;							\
									\
end:									\
	mutex_unlock(&opts->lock);					\
	return ret;							\
}									\
									\
UVC_ATTR(f_uvc_opts_, cname, cname)

UVCG_OPTS_ATTR_TRANSFER(streaming_transfer, streaming_transfer);

#undef UVCG_OPTS_ATTR_TRANSFER

#define UVCG_OPTS_ATTR_PERF_MODE(cname, aname)				\
static ssize_t f_uvc_opts_##cname##_show(				\
	struct config_item *item, char *page)				\
{									\
	struct f_uvc_opts *opts = to_f_uvc_opts(item);			\
	int result;							\
	char *str;							\
									\
	mutex_lock(&opts->lock);					\
	switch (opts->cname) {						\
	case UVC_STANDARD_V4L2_MODE:					\
		str = "standard v4l2 mode";				\
		break;							\
	case UVC_PERFORMANCE_MODE:					\
		str = "high performance mode";				\
		break;							\
	default:							\
		str = "unknown";					\
		break;							\
	}								\
	result = sprintf_s(page, PAGE_SIZE, "%s\n", str);				\
	mutex_unlock(&opts->lock);					\
									\
	return result;							\
}									\
									\
static ssize_t								\
f_uvc_opts_##cname##_store(struct config_item *item,			\
			   const char *page, size_t len)		\
{									\
	struct f_uvc_opts *opts = to_f_uvc_opts(item);			\
	int ret = 0;							\
									\
	mutex_lock(&opts->lock);					\
	if (opts->refcnt) {						\
		ret = -EBUSY;						\
		goto end;						\
	}								\
									\
	if (!strncmp(page, "v4l2", 4))					\
		opts->cname = UVC_STANDARD_V4L2_MODE;			\
	else if (!strncmp(page, "performance", 11))			\
		opts->cname = UVC_PERFORMANCE_MODE;			\
	else {								\
		ret = -EINVAL;						\
		goto end;						\
	}								\
									\
	ret = len;							\
									\
end:									\
	mutex_unlock(&opts->lock);					\
	return ret;							\
}									\
									\
UVC_ATTR(f_uvc_opts_, cname, cname)

UVCG_OPTS_ATTR_PERF_MODE(performance_mode, performance_mode);

#undef UVCG_OPTS_ATTR_PERF_MODE
#endif  //  CONFIG_ARCH_BSP

#undef UVCG_OPTS_ATTR

static struct configfs_attribute *uvc_attrs[] = {
	&f_uvc_opts_attr_streaming_interval,
	&f_uvc_opts_attr_streaming_maxpacket,
	&f_uvc_opts_attr_streaming_maxburst,
#ifdef CONFIG_ARCH_BSP
	&f_uvc_opts_attr_streaming_transfer,
	&f_uvc_opts_attr_performance_mode,
#if defined(CONFIG_USB_F_UVC_STILL_IMAGE) && !defined(CONFIG_UVC_NO_STATUS_INT_EP)
	&f_uvc_opts_attr_still_capture_method,
	&f_uvc_opts_attr_still_maxpacket,
#endif
#endif
	NULL,
};

static const struct uvcg_config_group_type uvc_func_type = {
	.type = {
		.ct_item_ops	= &uvc_func_item_ops,
		.ct_attrs	= uvc_attrs,
		.ct_owner	= THIS_MODULE,
	},
	.name = "",
	.children = (const struct uvcg_config_group_type*[]) {
		&uvcg_control_grp_type,
		&uvcg_streaming_grp_type,
		NULL,
	},
};

int uvcg_attach_configfs(struct f_uvc_opts *opts)
{
	int ret;

	config_group_init_type_name(&opts->func_inst.group, uvc_func_type.name,
				    &uvc_func_type.type);

	ret = uvcg_config_create_children(&opts->func_inst.group,
					  &uvc_func_type);
	if (ret < 0)
		config_group_put(&opts->func_inst.group);

	return ret;
}
