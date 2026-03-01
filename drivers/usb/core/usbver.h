/*
 * Copyright (c) CompanyNameMagicTag 2024. All rights reserved.
 * Description: USB Driver Version Description
 * Author: AuthorNameMagicTag
 * Create: 2024.12.01
 */
#ifndef _USB_VER_H
#define _USB_VER_H
#include <linux/version.h>

#define USB_VERSION "2.3"

static inline void usb_version_report(void)
{
	pr_info("usb_ver: %d.%d - %s\n",
		(u8)((LINUX_VERSION_CODE >> 16) & 0xff), (u8)((LINUX_VERSION_CODE >> 8) & 0xff), USB_VERSION);
}
#endif
