/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2024-2024. All rights reserved.
 */

#include <linux/securec.h>
#include <linux/iprec.h>
#include "uvc.h"

static struct usb_endpoint_descriptor uvc_fs_streaming_still_ep = {
	.bLength		= USB_DT_ENDPOINT_SIZE,
	.bDescriptorType	= USB_DT_ENDPOINT,
	.bEndpointAddress	= USB_DIR_IN,
	.bmAttributes		= USB_ENDPOINT_XFER_BULK,
};

static struct usb_endpoint_descriptor uvc_hs_streaming_still_ep = {
	.bLength		= USB_DT_ENDPOINT_SIZE,
	.bDescriptorType	= USB_DT_ENDPOINT,
	.bEndpointAddress	= USB_DIR_IN,
	.bmAttributes		= USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =   cpu_to_le16(512),
};

static struct usb_endpoint_descriptor uvc_ss_streaming_still_ep = {
	.bLength		= USB_DT_ENDPOINT_SIZE,
	.bDescriptorType	= USB_DT_ENDPOINT,
	.bEndpointAddress	= USB_DIR_IN,
	.bmAttributes		= USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize =   cpu_to_le16(1024),
};

static struct usb_ss_ep_comp_descriptor uvc_ss_streaming_still_comp = {
	.bLength		= sizeof(uvc_ss_streaming_still_comp),
	.bDescriptorType	= USB_DT_SS_ENDPOINT_COMP,
	.bMaxBurst =	0,
	.bmAttributes =	0,
	.wBytesPerInterval =	0,
};

struct usb_interface_descriptor uvc_streaming_intf_alt1;
struct usb_interface_descriptor uvc_streaming_intf_alt2;
struct usb_interface_descriptor uvc_streaming_intf_alt3;
struct usb_interface_descriptor uvc_streaming_intf_alt4;

struct usb_endpoint_descriptor uvc_fs_streaming_ep;
struct usb_endpoint_descriptor uvc_fs_streaming_ep1;
struct usb_endpoint_descriptor uvc_fs_streaming_ep2;
struct usb_endpoint_descriptor uvc_fs_streaming_ep3;

struct usb_endpoint_descriptor uvc_hs_streaming_ep;
struct usb_endpoint_descriptor uvc_hs_streaming_ep1;
struct usb_endpoint_descriptor uvc_hs_streaming_ep2;
struct usb_endpoint_descriptor uvc_hs_streaming_ep3;

struct usb_endpoint_descriptor uvc_ss_streaming_ep;
struct usb_endpoint_descriptor uvc_ss_streaming_ep1;
struct usb_endpoint_descriptor uvc_ss_streaming_ep2;
struct usb_endpoint_descriptor uvc_ss_streaming_ep3;

struct usb_ss_ep_comp_descriptor uvc_ss_streaming_comp;
struct usb_ss_ep_comp_descriptor uvc_ss_streaming_comp1;
struct usb_ss_ep_comp_descriptor uvc_ss_streaming_comp2;
struct usb_ss_ep_comp_descriptor uvc_ss_streaming_comp3;

static const struct usb_descriptor_header * const uvc_fs_streaming_md3[] = {
	(struct usb_descriptor_header *)&uvc_streaming_intf_alt1,
	(struct usb_descriptor_header *)&uvc_fs_streaming_ep,
	(struct usb_descriptor_header *)&uvc_fs_streaming_still_ep,
	(struct usb_descriptor_header *)&uvc_streaming_intf_alt2,
	(struct usb_descriptor_header *)&uvc_fs_streaming_ep1,
	(struct usb_descriptor_header *)&uvc_fs_streaming_still_ep,
	(struct usb_descriptor_header *)&uvc_streaming_intf_alt3,
	(struct usb_descriptor_header *)&uvc_fs_streaming_ep2,
	(struct usb_descriptor_header *)&uvc_fs_streaming_still_ep,
	(struct usb_descriptor_header *)&uvc_streaming_intf_alt4,
	(struct usb_descriptor_header *)&uvc_fs_streaming_ep3,
	(struct usb_descriptor_header *)&uvc_fs_streaming_still_ep,
	NULL,
};

static const struct usb_descriptor_header * const uvc_hs_streaming_md3[] = {
	(struct usb_descriptor_header *)&uvc_streaming_intf_alt1,
	(struct usb_descriptor_header *)&uvc_hs_streaming_ep,
	(struct usb_descriptor_header *)&uvc_hs_streaming_still_ep,
	(struct usb_descriptor_header *)&uvc_streaming_intf_alt2,
	(struct usb_descriptor_header *)&uvc_hs_streaming_ep1,
	(struct usb_descriptor_header *)&uvc_hs_streaming_still_ep,
	(struct usb_descriptor_header *)&uvc_streaming_intf_alt3,
	(struct usb_descriptor_header *)&uvc_hs_streaming_ep2,
	(struct usb_descriptor_header *)&uvc_hs_streaming_still_ep,
	(struct usb_descriptor_header *)&uvc_streaming_intf_alt4,
	(struct usb_descriptor_header *)&uvc_hs_streaming_ep3,
	(struct usb_descriptor_header *)&uvc_hs_streaming_still_ep,
	NULL,
};

static const struct usb_descriptor_header * const uvc_ss_streaming_md3[] = {
	(struct usb_descriptor_header *)&uvc_streaming_intf_alt1,
	(struct usb_descriptor_header *)&uvc_ss_streaming_ep,
	(struct usb_descriptor_header *)&uvc_ss_streaming_comp,
	(struct usb_descriptor_header *)&uvc_ss_streaming_still_ep,
	(struct usb_descriptor_header *)&uvc_ss_streaming_still_comp,
	(struct usb_descriptor_header *)&uvc_streaming_intf_alt2,
	(struct usb_descriptor_header *)&uvc_ss_streaming_ep1,
	(struct usb_descriptor_header *)&uvc_ss_streaming_comp1,
	(struct usb_descriptor_header *)&uvc_ss_streaming_still_ep,
	(struct usb_descriptor_header *)&uvc_ss_streaming_still_comp,
	(struct usb_descriptor_header *)&uvc_streaming_intf_alt3,
	(struct usb_descriptor_header *)&uvc_ss_streaming_ep2,
	(struct usb_descriptor_header *)&uvc_ss_streaming_comp2,
	(struct usb_descriptor_header *)&uvc_ss_streaming_still_ep,
	(struct usb_descriptor_header *)&uvc_ss_streaming_still_comp,
	(struct usb_descriptor_header *)&uvc_streaming_intf_alt4,
	(struct usb_descriptor_header *)&uvc_ss_streaming_ep3,
	(struct usb_descriptor_header *)&uvc_ss_streaming_comp3,
	(struct usb_descriptor_header *)&uvc_ss_streaming_still_ep,
	(struct usb_descriptor_header *)&uvc_ss_streaming_still_comp,
	NULL,
};

static const struct usb_descriptor_header * const uvc_fs_streaming_bulk_md3[] = {
	(struct usb_descriptor_header *)&uvc_fs_streaming_ep,
	(struct usb_descriptor_header *)&uvc_fs_streaming_still_ep,
	NULL,
};

static const struct usb_descriptor_header * const uvc_hs_streaming_bulk_md3[] = {
	(struct usb_descriptor_header *)&uvc_hs_streaming_ep,
	(struct usb_descriptor_header *)&uvc_hs_streaming_still_ep,
	NULL,
};

static const struct usb_descriptor_header * const uvc_ss_streaming_bulk_md3[] = {
	(struct usb_descriptor_header *)&uvc_ss_streaming_ep,
	(struct usb_descriptor_header *)&uvc_ss_streaming_comp,
	(struct usb_descriptor_header *)&uvc_ss_streaming_still_ep,
	(struct usb_descriptor_header *)&uvc_ss_streaming_still_comp,
	NULL,
};

void uvc_still_copy_ep_desc(void *desc, void *desc1, void *desc2, void *desc3, char name)
{
	switch (name) {
	case 'f':
		memcpy_s(&uvc_fs_streaming_ep, sizeof(uvc_fs_streaming_ep), desc, sizeof(uvc_fs_streaming_ep));
		memcpy_s(&uvc_fs_streaming_ep1, sizeof(uvc_fs_streaming_ep1), desc1, sizeof(uvc_fs_streaming_ep1));
		memcpy_s(&uvc_fs_streaming_ep2, sizeof(uvc_fs_streaming_ep2), desc2, sizeof(uvc_fs_streaming_ep2));
		memcpy_s(&uvc_fs_streaming_ep3, sizeof(uvc_fs_streaming_ep3), desc3, sizeof(uvc_fs_streaming_ep3));
		break;
	case 'h':
		memcpy_s(&uvc_hs_streaming_ep, sizeof(uvc_hs_streaming_ep), desc, sizeof(uvc_hs_streaming_ep));
		memcpy_s(&uvc_hs_streaming_ep1, sizeof(uvc_hs_streaming_ep1), desc1, sizeof(uvc_hs_streaming_ep1));
		memcpy_s(&uvc_hs_streaming_ep2, sizeof(uvc_hs_streaming_ep2), desc2, sizeof(uvc_hs_streaming_ep2));
		memcpy_s(&uvc_hs_streaming_ep3, sizeof(uvc_hs_streaming_ep3), desc3, sizeof(uvc_hs_streaming_ep3));
		break;
	case 's':
		memcpy_s(&uvc_ss_streaming_ep, sizeof(uvc_ss_streaming_ep), desc, sizeof(uvc_ss_streaming_ep));
		memcpy_s(&uvc_ss_streaming_ep1, sizeof(uvc_ss_streaming_ep1), desc1, sizeof(uvc_ss_streaming_ep1));
		memcpy_s(&uvc_ss_streaming_ep2, sizeof(uvc_ss_streaming_ep2), desc2, sizeof(uvc_ss_streaming_ep2));
		memcpy_s(&uvc_ss_streaming_ep3, sizeof(uvc_ss_streaming_ep3), desc3, sizeof(uvc_ss_streaming_ep3));
		break;
	case 'c':
		memcpy_s(&uvc_ss_streaming_comp, sizeof(uvc_ss_streaming_comp), desc, sizeof(uvc_ss_streaming_comp));
		memcpy_s(&uvc_ss_streaming_comp1, sizeof(uvc_ss_streaming_comp1), desc1, sizeof(uvc_ss_streaming_comp1));
		memcpy_s(&uvc_ss_streaming_comp2, sizeof(uvc_ss_streaming_comp2), desc2, sizeof(uvc_ss_streaming_comp2));
		memcpy_s(&uvc_ss_streaming_comp3, sizeof(uvc_ss_streaming_comp3), desc3, sizeof(uvc_ss_streaming_comp3));
		break;
	case 'a':
		memcpy_s(&uvc_streaming_intf_alt1, sizeof(uvc_streaming_intf_alt1), desc, sizeof(uvc_streaming_intf_alt1));
		memcpy_s(&uvc_streaming_intf_alt2, sizeof(uvc_streaming_intf_alt2), desc1, sizeof(uvc_streaming_intf_alt2));
		memcpy_s(&uvc_streaming_intf_alt3, sizeof(uvc_streaming_intf_alt3), desc2, sizeof(uvc_streaming_intf_alt3));
		memcpy_s(&uvc_streaming_intf_alt4, sizeof(uvc_streaming_intf_alt4), desc3, sizeof(uvc_streaming_intf_alt4));
		break;
	default:
		break;
	}
}

void uvc_still_add_alt_endpoint_num(int method, struct usb_interface_descriptor *alt0)
{
	if (method == UVC_STILL_IMAGE_METHOD3) {	/*  Only still image method3 need add endpoint nums.  */
		uvc_streaming_intf_alt1.bNumEndpoints += 1;
		uvc_streaming_intf_alt2.bNumEndpoints += 1;
		uvc_streaming_intf_alt3.bNumEndpoints += 1;
		uvc_streaming_intf_alt4.bNumEndpoints += 1;
		if (alt0->bNumEndpoints > 0)	 /* alt0->bNumEndpoints > 0 means bulk transfer */
			alt0->bNumEndpoints += 1;	/* add 1 endpoint for still transfer */
	}
}

void uvc_still_fill_still_desc(struct still_image *still, const struct usb_descriptor_header * const **uvc_std,
	enum usb_device_speed speed, int is_bulk_mode)
{
	if (!uvc_still_is_md3(still))	/*  Only still image method3 need add endpoint desc.  */
		return;

	if (is_bulk_mode > 0) {
		switch (speed) {
		case USB_SPEED_SUPER:
			*uvc_std = uvc_ss_streaming_bulk_md3;
			break;

		case USB_SPEED_HIGH:
			*uvc_std = uvc_hs_streaming_bulk_md3;
			break;

		case USB_SPEED_FULL:
		default:
			*uvc_std = uvc_fs_streaming_bulk_md3;
			break;
		}
	} else {
		switch (speed) {
		case USB_SPEED_SUPER:
			*uvc_std = uvc_ss_streaming_md3;
			break;

		case USB_SPEED_HIGH:
			*uvc_std = uvc_hs_streaming_md3;
			break;

		case USB_SPEED_FULL:
		default:
			*uvc_std = uvc_fs_streaming_md3;
			break;
		}
	}
}

void uvc_still_set_appropriate_still_ep(struct still_image *still, int method, int speed, int max_ss_ep_pack, int mask)
{
	if (method == UVC_STILL_IMAGE_METHOD3) {	/*  Only still image method3 use still endpoint.  */
		if (speed == USB_SPEED_SUPER) {
			still->still_ep->maxpacket = max_ss_ep_pack;
			still->still_ep->desc = &uvc_ss_streaming_still_ep;
			still->still_ep->mult = 0;
			still->still_ep->maxburst = 0;
			still->still_ep->comp_desc = &uvc_ss_streaming_still_comp;
		} else {
			still->still_ep->desc = &uvc_hs_streaming_still_ep;
			still->still_ep->maxpacket = still->still_ep->desc->wMaxPacketSize & mask;
			still->still_ep->mult = 0;
		}
	}
}

/**
 * uvc_still_set_video_method_attr - set still image attr from struct &f_uvc_opts.
 * @still: Pointer to struct &still_image
 * @method: Method of still image
 * @still_maxpacket: Max payloadsize of still endpoint
 *
 * Return: If endpoint config err, return < 0, else return 0.
 */
int uvc_still_set_video_method_attr(struct still_image *still, int method,
	struct usb_configuration *c, int still_maxpacket)
{
	struct usb_composite_dev *cdev = c->cdev;
	struct usb_ep *ep;

	if (method == UVC_STILL_IMAGE_METHOD3) {	/*  Only still image method3 use still endpoint and still_maxpacket.  */
		if (gadget_is_superspeed(c->cdev->gadget))
			ep = usb_ep_autoconfig_ss(cdev->gadget, &uvc_ss_streaming_still_ep,
						&uvc_ss_streaming_still_comp);
		else if (gadget_is_dualspeed(cdev->gadget))
			ep = usb_ep_autoconfig_ss(cdev->gadget, &uvc_hs_streaming_still_ep, NULL);
		else
			ep = usb_ep_autoconfig(cdev->gadget, &uvc_fs_streaming_still_ep);

		if (!ep) {
			printk("Unable to allocate streaming still EP\n");
			return -1;
		}
		still->still_ep = ep;
		uvc_fs_streaming_still_ep.bEndpointAddress = ep->address;
		uvc_hs_streaming_still_ep.bEndpointAddress = ep->address;
		uvc_ss_streaming_still_ep.bEndpointAddress = ep->address;

#define SUPER_SPEED_MAX_ENDPOINT_PACKET 1024
#define HIGH_SPEED_MAX_ENDPOINT_PACKET 512
		if (still_maxpacket % still->still_ep->maxpacket != 0) {
			still_maxpacket = roundup(still_maxpacket, HIGH_SPEED_MAX_ENDPOINT_PACKET);
			if (gadget_is_superspeed(c->cdev->gadget))
				still_maxpacket = roundup(still_maxpacket, SUPER_SPEED_MAX_ENDPOINT_PACKET);
			printk("overriding still_maxpacket to %d\n", still_maxpacket);
		}
		still->still_maxpacket = still_maxpacket;
	} else {
		still->still_ep = NULL;
	}

	still->still_capture_method = method;

	return 0;
}

void uvc_still_check_commit_status(struct still_image *still, unsigned char request, int select, int w_index)
{
	int intf = w_index & 0xff;
	int entry_id = w_index & 0xff00;
	if (uvc_still_is_md2(still) && request == UVC_SET_CUR && select == UVC_STILL_IMAGE_COMMIT &&
			intf != 0 && entry_id == 0) {
		iprec("still image method%d to STILL_COMMIT status", still->still_capture_method);
		still->still_status = STILL_COMMIT;	  /* Only still image method2 usb STILL_COMMIT state. */
	}
}

void uvc_still_to_commit_reset_status(struct still_image *still)
{
	iprec("still image method%d to STILL_COMMIT_RESET status", still->still_capture_method);
	still->still_status = STILL_COMMIT_RESET;
}

int uvc_still_is_commit_status(struct still_image *still)
{
	return still->still_status == STILL_COMMIT;
}

int uvc_still_is_commit_reset_status(struct still_image *still)
{
	return still->still_status == STILL_COMMIT_RESET;
}

void uvc_still_to_idle_status(struct still_image *still)
{
	iprec("still image method%d to STILL_IDLE status", still->still_capture_method);
	still->still_status = STILL_IDLE;
}

int uvc_still_is_active_status(struct still_image *still)
{
	return still->still_status != STILL_IDLE;
}

int uvc_still_is_restore_status(struct still_image *still)
{
	return still->still_status == STILL_RESTORE;
}

int uvc_still_is_reset_status(struct still_image *still)
{
	return still->still_status == STILL_RESTORE || still->still_status == STILL_COMMIT_RESET;
}

void uvc_still_disable_ep(struct still_image *still)
{
	if (uvc_still_is_md3(still))   /*  Only still image method3 use still endpoint.  */
		usb_ep_disable(still->still_ep);
}

void uvc_still_clean_status(struct still_image *still)
{
	still->still_len = 0;
	still->still_buf_used = 0;
	uvc_still_to_idle_status(still);
	still->status_req_buf[UVC_VS_STATUS_VALUE_INDEX] = UVC_VS_STATUS_BUTTON_DONE;
}

int uvc_still_is_trigger_status(struct still_image *still)
{
	return still->still_status == STILL_TRIGGER || still->still_status == STILL_TRIGGER2;
}

static void uvc_still_to_next_status(struct still_image *still)
{
	switch (still->still_status) {
	case STILL_COMMIT:
		iprec("still image method%d to STILL_PREPARE status", still->still_capture_method);
		still->still_status = STILL_PREPARE;
		break;
	case STILL_PREPARE:
		iprec("still image method%d to STILL_TRIGGER status", still->still_capture_method);
		still->still_status = STILL_TRIGGER;
		break;
	case STILL_TRIGGER:
		iprec("still image method%d to STILL_IDLE status", still->still_capture_method);
		still->still_status = STILL_IDLE;
		break;
	case STILL_COMMIT_RESET:
		iprec("still image method%d to STILL_PREPARE2 status", still->still_capture_method);
		still->still_status = STILL_PREPARE2;
		break;
	case STILL_PREPARE2:
		iprec("still image method%d to STILL_TRIGGER2 status", still->still_capture_method);
		still->still_status = STILL_TRIGGER2;
		break;
	case STILL_TRIGGER2:
		iprec("still image method%d to STILL_RESTORE status", still->still_capture_method);
		still->still_status = STILL_RESTORE;
		break;
	case STILL_RESTORE:
		iprec("still image method%d to STILL_IDLE status", still->still_capture_method);
		still->still_status = STILL_IDLE;
		break;
	default:
		break;
	}
}

int uvc_still_is_prepare_status(struct still_image *still)
{
	return still->still_status == STILL_PREPARE || still->still_status == STILL_PREPARE2;
}

void uvc_still_start_schedule(struct still_image *still)
{
	if (uvc_still_is_prepare_status(still)) {
		uvc_still_to_next_status(still);
	}
	schedule_work(&still->pump);
}

int uvc_still_is_md3(struct still_image *still)
{
	return still->still_capture_method == UVC_STILL_IMAGE_METHOD3;
}

int uvc_still_is_md2(struct still_image *still)
{
	return still->still_capture_method == UVC_STILL_IMAGE_METHOD2;
}

void uvc_still_reset_imagesize(struct still_image *still, u32 *max_img_size)
{
	struct uvc_video *video = still->video;
	if (uvc_still_is_commit_status(still) && uvc_still_is_md2(still)) { /* only still image need reset video image size. */
		video->imagesize = *max_img_size;
	}
}

static int uvc_still_encode_header(struct still_image *still, u8 *data, int len)
{
	data[0] = 2; // uvc 1.1 protocol header
	data[1] = UVC_STREAM_EOH | UVC_STREAM_STI;

	if ((still->still_len - still->still_buf_used <= len - 2)) // 2 for uvc 1.1 protocol header size
		data[1] |= UVC_STREAM_EOF;

	return 2; // uvc 1.1 protocol header length
}

static int uvc_still_encode_data(struct still_image *still, u8 *data, unsigned int len)
{
	unsigned int nbytes, i, seg_one;
	u8 *mem = NULL;
	int in_board = (still->still_buf_used / PAGE_ONE_LEN == (still->still_buf_used + len - 1) / PAGE_ONE_LEN ? 0 : 1);

	i = still->still_buf_used / PAGE_ONE_LEN;
	if (!in_board) {
		/* Copy video data to the USB buffer. */
		mem = (void *)still->still_addr[i] + still->still_buf_used - i * PAGE_ONE_LEN;
		nbytes = min((unsigned int)len, still->still_len - still->still_buf_used);
		still->still_buf_used += nbytes;
		memcpy_s(data, len, mem, nbytes);
	} else  {
		seg_one = (i + 1) * PAGE_ONE_LEN - still->still_buf_used;
		mem = (void *)still->still_addr[i] + still->still_buf_used - i * PAGE_ONE_LEN;
		nbytes = min((unsigned int)len, seg_one);
		memcpy_s(data, len, mem, nbytes);

		if (len - seg_one > 0) {
			mem = (void *)still->still_addr[i + 1];
			memcpy_s((u8*)data + nbytes, len - seg_one, mem, len - seg_one);
			nbytes += len - seg_one;
		}
		still->still_buf_used += nbytes;
	}

	return nbytes;
}

static void uvc_still_encode_isoc(struct usb_request *req, struct still_image *still)
{
	int ret;
	int len;
	int ttllen = 0;
	unsigned int sg_idx;
	u8 *mem = NULL;
	struct uvc_video *video = still->video;

	sg_unmark_end(&req->sg[req->num_sgs - 1]);

	for (sg_idx = 0; sg_idx < video->num_sgs; sg_idx++) {
		mem = sg_virt(&req->sg[sg_idx]);
		len = video->req_size;

		/* Add the header. */
		ret = uvc_still_encode_header(still, mem, len);
		mem += ret;
		len -= ret;

		/* Process still data. */
		ret = uvc_still_encode_data(still, mem, len);
		len -= ret;

		/* Sync sg buffer len , default is 1024 or 3072 */
		sg_set_buf(&req->sg[sg_idx], sg_virt(&req->sg[sg_idx]),
				video->req_size - len);
		ttllen += video->req_size - len;

		if (still->still_len == still->still_buf_used) {
			still->still_buf_used = 0;
			uvc_still_to_next_status(still);
			break;
		}
	}
	req->num_sgs = sg_idx + 1;
	if (req->num_sgs > video->num_sgs)
		req->num_sgs = video->num_sgs;
	sg_mark_end(&req->sg[sg_idx]);

	req->length = ttllen;
}

static void uvc_still_encode_bulk(struct usb_request *req, struct still_image *still)
{
	int ret;
	void *mem = req->buf;
	struct uvc_video *video = still->video;
	int len = video->req_size;

	/* Add a header at the beginning of the payload. */
	ret = uvc_still_encode_header(still, mem, len);
	mem += ret;
	len -= ret;

	/* Process video data. */
	ret = uvc_still_encode_data(still, mem, len);
	len -= ret;

	req->length = video->req_size - len;
	req->zero = 0;

	if (still->still_len == still->still_buf_used) {
		still->still_buf_used = 0;
		uvc_still_to_next_status(still);
	}
}

static void uvc_still_status_report(struct still_image *still, __u8 status);
static void uvc_still_status_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct still_image *still = req->context;
	switch (req->status) {
	case 0:
		break;

	case -ESHUTDOWN:	/* disconnect from host. */
		printk(KERN_DEBUG "still status request cancelled.\n");
		break;

	default:
		printk(KERN_INFO "still status request completed with status %d.\n",
			req->status);
	}

	if (still->status_req_buf[UVC_VS_STATUS_VALUE_INDEX] == UVC_VS_STATUS_BUTTON_RELEASED) {
		still->status_req_buf[UVC_VS_STATUS_VALUE_INDEX] = UVC_VS_STATUS_BUTTON_DONE;
	}

	/*  If still image is called by button pressed action, it responses a release event when sending the picture ends.  */
	if (still->status_req_buf[UVC_VS_STATUS_VALUE_INDEX] == UVC_VS_STATUS_BUTTON_PRESSED)
		uvc_still_status_report(still, UVC_VS_STATUS_BUTTON_RELEASED);
}

static void uvc_still_status_report(struct still_image *still, __u8 status)
{
	int ret;
	struct uvc_video *video = still->video;
	struct uvc_device *uvc = video->uvc;
	struct uvc_vs_status_packet_format* status_pformat = (struct uvc_vs_status_packet_format*)still->status_req_buf;

	if (!uvc->control_ep->enabled)
		return;

	if (status == UVC_VS_STATUS_BUTTON_PRESSED && status_pformat->bValue != UVC_VS_STATUS_BUTTON_DONE)
		return;

	status_pformat->bStatusType = UVC_STATUS_TYPE_STREAMING;

	/* Video Streaming Interface, 1 for single channel uvc, multi-channel uvc should usb actuality number */
	status_pformat->bOriginator = 1;
	status_pformat->bEvent = 0; /* 0: Button Press */
	status_pformat->bValue = status;

	still->status_req->buf = still->status_req_buf;
	still->status_req->length = sizeof(struct uvc_vs_status_packet_format);
	still->status_req->complete = uvc_still_status_complete;
	still->status_req->context = still;
	still->status_req->num_mapped_sgs = 0;
	still->status_req->num_sgs = 0;

	ret = usb_ep_queue(uvc->control_ep, still->status_req, GFP_ATOMIC);
	if (ret < 0) {
		printk(KERN_INFO "Failed to queue status request (%d).\n", ret);
	}

	return ;
}

static void uvc_still_complete_md3(struct usb_ep *ep, struct usb_request *req)
{
	unsigned long flags;
	struct still_image *still = req->context;

	switch (req->status) {
	case 0:
		break;

	case -ESHUTDOWN:	/* disconnect from host. */
		printk(KERN_DEBUG "VS request cancelled.\n");
		break;

	default:
		printk(KERN_INFO "VS still request completed with status %d.\n",
			req->status);
	}

	spin_lock_irqsave(&still->still_req_lock, flags);
	list_add_tail(&req->list, &still->still_req_free);
	spin_unlock_irqrestore(&still->still_req_lock, flags);

	if (uvc_still_is_trigger_status(still))
		schedule_work(&still->pump);
}

static int uvc_still_free_requests_md3(struct still_image *still)
{
	int i;

	if (still->still_ep->enabled) {
		usb_ep_disable(still->still_ep);
	}

	for (i = 0; i < UVC_NUM_REQUESTS; ++i) {
		if (still->still_req[i]) {
			usb_ep_free_request(still->still_ep, still->still_req[i]);
			still->still_req[i] = NULL;
		}
		if (still->still_req_buffer[i]) {
			kfree(still->still_req_buffer[i]);
			still->still_req_buffer[i] = NULL;
		}
	}
	INIT_LIST_HEAD(&still->still_req_free);
	return 0;
}

static int uvc_still_alloc_requests_md3(struct still_image *still)
{
	int i;
	int req_size = still->still_maxpacket;

	for (i = 0; i < UVC_NUM_REQUESTS; ++i) {
		still->still_req_buffer[i] = kmalloc(req_size, GFP_KERNEL);
		if (still->still_req_buffer[i] == NULL)
			goto error;

		still->still_req[i] = usb_ep_alloc_request(still->still_ep, GFP_KERNEL);
		if (still->still_req[i] == NULL)
			goto error;

		still->still_req[i]->buf = still->still_req_buffer[i];
		still->still_req[i]->length = 0;
		still->still_req[i]->complete = uvc_still_complete_md3;
		still->still_req[i]->context = still;
		still->still_req[i]->num_sgs = 0;
		still->still_req[i]->num_mapped_sgs = 0;

		list_add_tail(&still->still_req[i]->list, &still->still_req_free);
	}

	if (!still->still_ep->enabled) {
		usb_ep_enable(still->still_ep);
	}
	return 0;

error:
	uvc_still_free_requests_md3(still);
	return -1;
}

static void uvc_still_free_image_buf(struct still_image *still)
{
	int i;

	for (i = 0; i < UVC_STILL_IMAGE_PAGE_NUM; i++) {
		if (still->still_addr[i] != NULL) {
			kfree(still->still_addr[i]);
			still->still_addr[i] = NULL;
		}
	}
	still->max_img_size = 0;
}

void uvc_still_free_image(struct still_image *still)
{
	if (uvc_still_is_reset_status(still))  /* it doesn't need free image when still image method2 reset resource. */
		return ;

	iprec("still image method%d free image buf", still->still_capture_method);

	uvc_still_free_image_buf(still);

	if (uvc_still_is_md3(still))	/*  Only still image method3 use independent request.  */
		uvc_still_free_requests_md3(still);
}

int uvc_still_alloc_image(struct still_image *still)
{
	if (uvc_still_is_reset_status(still))  /* it doesn't need alloc image when still image method2 reset resource. */
		return 0;

	iprec("still image method%d alloc image buf", still->still_capture_method);

	if (uvc_still_is_md3(still))	/*  Only still image method3 use independent request.  */
		return uvc_still_alloc_requests_md3(still);

	return 0;
}

static void uvc_still_encode_bulk_md3(struct usb_request *req, struct still_image *still)
{
	int len;
	int ret;
	void *mem;
	int max_payload_size = still->still_maxpacket;

	mem = req->buf;
	len = max_payload_size;

	/* Add a header at the beginning of the payload. */
	ret = uvc_still_encode_header(still, mem, len);
	mem += ret;
	len -= ret;

	/* Process video data. */
	ret = uvc_still_encode_data(still, mem, len);
	len -= ret;

	req->length = max_payload_size - len;
	req->zero = 0;

	if (still->still_len == still->still_buf_used) {
		still->still_buf_used = 0;
		uvc_still_to_idle_status(still);
	}
}

static void uvc_still_encode(struct still_image *still, struct usb_request *req)
{
	struct uvc_video *video = still->video;

	if (uvc_still_is_md3(still)) {
		uvc_still_encode_bulk_md3(req, still);
	} else if (uvc_still_is_md2(still)) {
		if (video->max_payload_size == 0) {
			uvc_still_encode_isoc(req, still);
		} else {
			uvc_still_encode_bulk(req, still);
		}
	}
}

static void uvc_still_pump(struct work_struct *work)
{
	int ret;
	spinlock_t *lock;
	struct usb_ep *ep;
	unsigned long flags;
	struct usb_request *req;
	struct list_head *req_free;
	struct still_image *still = container_of(work, struct still_image, pump);
	struct uvc_video *video = still->video;

	while (uvc_still_is_trigger_status(still)) {
		if (uvc_still_is_md3(still)) {
			req_free = &still->still_req_free;
			lock = &still->still_req_lock;
			ep = still->still_ep;
		} else if (uvc_still_is_md2(still)) {
			req_free = &video->req_free;
			lock = &video->req_lock;
			ep = video->ep;
		} else {
			uvc_still_to_idle_status(still);
			return;
		}

		if (!ep->enabled) {
			printk(KERN_INFO "Warning: ep is not enabled.\n");
			return;
		}

		spin_lock_irqsave(lock, flags);
		if (list_empty(req_free)) {
			spin_unlock_irqrestore(lock, flags);
			return;
		}
		req = list_first_entry(req_free, struct usb_request, list);
		list_del(&req->list);
		spin_unlock_irqrestore(lock, flags);

		uvc_still_encode(still, req);

		ret = usb_ep_queue(ep, req, GFP_ATOMIC);
		if (ret < 0) {
			printk(KERN_INFO "Failed to queue still request (%d).\n", ret);
			/* If the endpoint is disabled the descriptor may be NULL. */
			if (ep->desc) {
				if (usb_endpoint_xfer_bulk(ep->desc))
					usb_ep_set_halt(ep);
			}

			spin_lock_irqsave(lock, flags);
			list_add_tail(&req->list, req_free);
			spin_unlock_irqrestore(lock, flags);
			uvc_still_to_idle_status(still);
		}
	}
}

static int uvc_still_enable_resouce_md2(struct still_image *still, int enable)
{
	unsigned int i;
	int ret;
	struct uvc_video *video = still->video;

	if (video->ep == NULL) {
		printk("Video enable failed, device is uninitialized.\n");
		return -ENODEV;
	}

	if (!enable) {
		cancel_work_sync(&video->pump);

		for (i = 0; i < UVC_NUM_REQUESTS; ++i)
			if (video->req[i])
				usb_ep_dequeue(video->ep, video->req[i]);

		still->uvc_still_free_requests(video);

		return 0;
	}

	if ((ret = still->uvc_still_alloc_requests(video)) < 0)
		return ret;

	if (still->still_status == STILL_RESTORE)
		schedule_work(&video->pump);

	return 0;
}

void uvc_still_reset_resource(struct still_image *still)
{
	if (!uvc_still_is_md2(still))  /* Only still image method2 need reset resource. */
		return;

	iprec("still image method%d reset resource", still->still_capture_method);
	uvc_still_enable_resouce_md2(still, 0);
	uvc_still_enable_resouce_md2(still, 1);
}

int uvc_still_image_init(struct still_image **still_addr, void *alloc_req, void *free_req)
{
	struct still_image *still = *still_addr;
	struct uvc_video *video = container_of(still_addr, struct uvc_video, still);
	struct uvc_device *uvc = video->uvc;
	still->video = video;

	still->status_req = usb_ep_alloc_request(uvc->control_ep, GFP_KERNEL);
	if (still->status_req == NULL) {
		printk("[UVC] alloc still image status req fail\n");
	}

	INIT_WORK(&still->pump, uvc_still_pump);
	if (uvc_still_is_md3(still)) {	/*  Only still image method3 use independent request.  */
		INIT_LIST_HEAD(&still->still_req_free);
		spin_lock_init(&still->still_req_lock);
	}

	still->status_req_buf[UVC_VS_STATUS_VALUE_INDEX] = UVC_VS_STATUS_BUTTON_DONE;
	still->uvc_still_alloc_requests = alloc_req;
	still->uvc_still_free_requests = free_req;
	still->max_img_size = 0;

	return 0;
}

static int uvc_still_reconfig_imagesize(struct still_image *still, int len)
{
	int i, cur_len, count, left;
	void *addr = NULL;

	uvc_still_free_image_buf(still);

	count = len / PAGE_ONE_LEN + (len % PAGE_ONE_LEN > 0 ? 1 : 0);
	for (i = 0 ; i < count; i++) {
		left = len - (i * PAGE_ONE_LEN);
		cur_len = (left >= PAGE_ONE_LEN) ? PAGE_ONE_LEN : left;
		addr = kmalloc(cur_len, GFP_KERNEL);
		if (addr == NULL) {
			uvc_still_free_image_buf(still);
			printk("[UVC] kmalloc failed, size:%d\n", cur_len);
			return -ENOMEM;
		}
		still->still_addr[i] = addr;
	}
	still->max_img_size = len;

	return 0;
}

/**
 * uvc_still_processes_still_image - get still image from user, and prepare to transfer.
 * @file: Pointer to struct &file
 * @a: Pointer to struct &v4l2_framebuffer
 *
 * Caller should fill &v4l2_framebuffer, &v4l2_framebuffer->fmt.sizeimage means the size of still image,
 * &v4l2_framebuffer->base means addr of still image.
 */
int uvc_still_processes_still_image(struct file *file, void *fh, const struct v4l2_framebuffer *a)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);
	struct uvc_video *video = &uvc->video;
	int ret, i, count, copy_len, len, last;

	len = a->fmt.sizeimage;
	if (len > MAX_IMAGE_SIZE || a->base == NULL) {
		ret = -EINVAL;
		goto out;
	}

	if (video->max_payload_size == 0 && uvc_still_is_md2(video->still) &&
			len > CONFIG_USB_STILL_MAX_IMAGE_SIZE && len > video->imagesize) {
		printk("[UVC] Error: image size of still is out of video image size!\n");
		ret = -EINVAL;
		goto out;
	}

	count = len / PAGE_ONE_LEN + (len % PAGE_ONE_LEN > 0 ? 1 : 0);

	if (video->still->max_img_size < len) {
		ret = uvc_still_reconfig_imagesize(video->still, len);
		if (ret < 0)
			goto out;
	}

	iprec("still image method%d start trans image[size:%d]", video->still->still_capture_method, len);
	for (i = 0; i < count; i++) {
		last = (len % PAGE_ONE_LEN == 0 ? PAGE_ONE_LEN : len % PAGE_ONE_LEN);
		copy_len = (i + 1 == count ? last : PAGE_ONE_LEN);

		ret = copy_from_user(video->still->still_addr[i], a->base + i * PAGE_ONE_LEN, copy_len);
		if (ret < 0)
			goto out;
	}

	video->still->still_len = a->fmt.sizeimage;

	if (uvc_still_is_md3(video->still)) {
		iprec("still image method%d to STILL_TRIGGER status", video->still->still_capture_method);
		video->still->still_status = STILL_TRIGGER;
		schedule_work(&video->still->pump);
	} else if (uvc_still_is_md2(video->still)) {
		uvc_still_to_next_status(video->still);
		if (!list_empty(&video->req_free)) {
			uvc_still_to_next_status(video->still);
			schedule_work(&video->still->pump);
		}
	}
	return 0;

out:
	uvc_still_clean_status(video->still);
	video->is_streaming = true;
	schedule_work(&video->pump);
	return ret;
}

#ifdef UVC_STILL_BUTTON_TEST
int uvc_still_button_pressed(struct file *file, void *fh, struct v4l2_framebuffer *a)
{
	struct video_device *vdev = video_devdata(file);
	struct uvc_device *uvc = video_get_drvdata(vdev);
	struct uvc_video *video = &uvc->video;

	uvc_still_status_report(video->still, UVC_VS_STATUS_BUTTON_PRESSED);
	return 0;
}
#endif
