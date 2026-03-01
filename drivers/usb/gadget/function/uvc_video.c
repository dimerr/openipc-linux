// SPDX-License-Identifier: GPL-2.0+
/*
 *	uvc_video.c  --  USB Video Class Gadget driver
 *
 *	Copyright (C) 2009-2010
 *	    Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/errno.h>
#ifdef CONFIG_ARCH_BSP
#include <linux/securec.h>
#endif
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/video.h>
#ifdef CONFIG_ARCH_BSP
#include <linux/usb/g_uvc.h>
#include <asm/unaligned.h>
#endif
#include <media/v4l2-dev.h>

#ifdef CONFIG_ARCH_BSP
#include <linux/iprec.h>
#endif
#include "uvc.h"
#include "uvc_queue.h"
#include "uvc_video.h"

#ifdef CONFIG_ARCH_BSP
#include <linux/scatterlist.h>
#include <linux/io.h>
#include <linux/delay.h>
#endif

/*************************************************************/
#if defined(CONFIG_ARCH_BSP)
#define MAX_FRAME 1
#define MAX_VB_FRAME 2
#define DEVICE_NUM 255
static struct uvc_pack_trans *g_uvc_pack[DEVICE_NUM];

typedef enum {
	OT_UVC_FORMAT_YUYV = 0,
	OT_UVC_FORMAT_NV21,
	OT_UVC_FORMAT_NV12,
	OT_UVC_FORMAT_MJPEG,
	OT_UVC_FORMAT_H264,
	OT_UVC_FORMAT_H265,
	OT_UVC_FORMAT_BUTT
} ot_uvc_format;

typedef enum {
	OT_UVC_BUF_TYPE_LIFO = 0, /* Support last in first out */
	OT_UVC_BUF_TYPE_FIFO, /* Support first in first out */
} ot_uvc_buf_type;

typedef struct {
	u64 phys_addr;  /* yuv frame physical addr */
	u64 pts;
	u32 unique_id; /* for venc */
	s32 uvc_chn;
	s32 src_chn; /* data source, venc channel */
	ot_uvc_format uvc_format;
	ot_uvc_buf_type buf_type;
} uvc_pack_priv;

/* the caller shoulud hold g_uvc_pack.lock */
static void clean_untrans_frame(bool need_clean_all, bool force, u32 dev_id)
{
	struct uvc_pack_trans *p;

	int rem = MAX_FRAME;
	if (need_clean_all == 1)
		rem = 0;

	while (!list_empty(&(g_uvc_pack[dev_id]->list))) {
		p = list_first_entry(&(g_uvc_pack[dev_id]->list), struct uvc_pack_trans, list);
		if (p->is_transferring && force == false)
			return;

		list_del(&p->list);

		if (p->need_free)
			p->pack->callback_func(p->pack);
		if (p->is_frame_end)
			g_uvc_pack[dev_id]->frame_cnts--;

		kfree(p->pack);
		kfree(p);

#ifdef UVC_SLICE_MODE
		if (!need_clean_all)
			break;
#else
		if (g_uvc_pack[dev_id]->frame_cnts <= rem)
			break;
#endif
	}
}

/* the caller shoulud hold g_uvc_pack.lock */
static void clean_complete_trans_frame(u32 dev_id)
{
	struct uvc_pack_trans *p;

	bool exit_flag = false;

	while (!list_empty(&(g_uvc_pack[dev_id]->list))) {
		p = list_first_entry(&(g_uvc_pack[dev_id]->list), struct uvc_pack_trans, list);

		list_del(&p->list);

		if (p->need_free)
			p->pack->callback_func(p->pack);
		if (p->is_frame_end) {
			g_uvc_pack[dev_id]->frame_cnts--;
			exit_flag = true;
		}

		kfree(p->pack);
		kfree(p);
		if (exit_flag)
			break;
#ifdef UVC_SLICE_MODE
		break;
#endif
	}
}

/* the caller shoulud hold g_uvc_pack.lock */
static void pack_save_to_list(struct uvc_pack_trans *ptr,
		struct uvc_pack *pack, bool is_frame_end, bool free)
{
	struct uvc_pack_trans *p = NULL;
	uvc_pack_priv *priv = (uvc_pack_priv *)pack->private_data;

	ptr->pack = (struct uvc_pack *)kmalloc(sizeof(struct uvc_pack), GFP_KERNEL);
	if (ptr->pack == NULL)
		return;
	if (memcpy_s(ptr->pack, sizeof(struct uvc_pack), pack, sizeof(struct uvc_pack)) != 0) {
		kfree(ptr->pack);
		ptr->pack = NULL;
		return;
	}

	ptr->is_frame_end = is_frame_end;
	ptr->need_free = free;
	ptr->buf_used = 0;
	ptr->is_transferring = false;

	if (!list_empty(&(g_uvc_pack[pack->dev_id]->list))) {
		p = list_first_entry(&(g_uvc_pack[pack->dev_id]->list), struct uvc_pack_trans, list);
		if (g_uvc_pack[pack->dev_id]->frame_cnts == MAX_VB_FRAME && p->is_transferring &&
				is_frame_end && priv->buf_type == OT_UVC_BUF_TYPE_LIFO) {
			pack->callback_func(pack);
			return;
		}
	}

	list_add_tail(&ptr->list, &(g_uvc_pack[pack->dev_id]->list));

	if (is_frame_end) {
		g_uvc_pack[pack->dev_id]->frame_cnts++;
		if (g_uvc_pack[pack->dev_id]->frame_cnts > MAX_FRAME) {
			clean_untrans_frame(0, false, pack->dev_id);
		}
		if (g_uvc_pack[pack->dev_id]->frame_cnts > MAX_VB_FRAME) {
			clean_untrans_frame(0, false, pack->dev_id);
		}
		if (unlikely(g_uvc_pack[pack->dev_id]->video->is_streaming == false)) {
			clean_untrans_frame(1, true, pack->dev_id);
		}
	}
}

int uvc_recv_pack(struct uvc_pack *pack)
{
	struct uvc_pack_trans *p = NULL;
	struct uvc_pack_trans *q = NULL;
	uint64_t end_virt_addr;
	unsigned long flags;

	if (g_uvc_pack[pack->dev_id]->list.next == NULL || g_uvc_pack[pack->dev_id]->list.prev == NULL) {
		printk(KERN_EMERG"[Error][uvc_recv_pack] UVC video has not been initialized!");
		return -1;
	}

	/* uvcg_uvc_pack_init() has initialized g_uvc_pack[pack->dev_id]->video */
	if (g_uvc_pack[pack->dev_id]->video == NULL) {
		printk(KERN_EMERG"[Error][uvc_recv_pack] video is NULL!\n");
		return -1;
	}
	if (g_uvc_pack[pack->dev_id]->video->performance_mode != UVC_PERFORMANCE_MODE) {
		printk(KERN_EMERG"[Error][uvc_recv_pack] Not in high performance mode!\n");
		return -1;
	}

	if (pack->buf_vir_addr == 0 || pack->pack_vir_addr == 0 || pack->pack_len == 0) {
		printk(KERN_EMERG"[Error][uvc_recv_pack] Get NULL pointer addr or illegal length!\n");
		goto err;
	}

	end_virt_addr = pack->buf_vir_addr + pack->buf_size;

	if ((pack->pack_vir_addr < pack->buf_vir_addr) || (pack->pack_vir_addr > end_virt_addr)) {
		printk(KERN_EMERG"[Error][uvc_recv_pack] Get illegal pack_vir_addr!\n");
		goto err;
	}

	p = (struct uvc_pack_trans *)kmalloc(sizeof(struct uvc_pack_trans), GFP_KERNEL);
	if (unlikely(p == NULL)) {
		printk("[Warning][uvc_recv_pack]Can not alloc uvc_pack_trans!\n");
		goto err;
	}

	if (pack->pack_vir_addr + pack->pack_len > end_virt_addr) {
		iprec("[%s] addr ring back", __func__);
		q = (struct uvc_pack_trans *)kmalloc(sizeof(struct uvc_pack_trans), GFP_KERNEL);
		if (unlikely(q == NULL)) {
			printk("[Warning][uvc_recv_pack]Can not alloc uvc_pack_trans!");
			goto err;
		}
		spin_lock_irqsave(&(g_uvc_pack[pack->dev_id]->lock), flags);
		p->len = end_virt_addr - pack->pack_vir_addr;
		p->addr = pack->pack_vir_addr;
		if (p->len != 0) {
			pack_save_to_list(p, pack, false, false);
		} else {
			printk(KERN_EMERG"[Error] p->len == 0, free +++++++\n");
			printk(KERN_EMERG"buf_vir_addr: 0x%llx, buf_size: %d, pack_vir_addr: 0x%llx, pack_len:%d, "
						"is_frame_end:%d\n", pack->buf_vir_addr, pack->buf_size, pack->pack_vir_addr, pack->pack_len,
						pack->is_frame_end);
			kfree(p);
		}
		q->len = pack->pack_len - (end_virt_addr - pack->pack_vir_addr);
		q->addr = pack->buf_vir_addr;
		if (q->len != 0) {
			pack_save_to_list(q, pack, pack->is_frame_end, true);
		} else {
			printk(KERN_EMERG"[Error len ==0]buf_vir_addr: 0x%llx, buf_size: %d, pack_vir_addr: 0x%llx, pack_len:%d, "
						"is_frame_end:%d\n", pack->buf_vir_addr, pack->buf_size,
						pack->pack_vir_addr, pack->pack_len, pack->is_frame_end);
			kfree(q);
		}
		spin_unlock_irqrestore(&(g_uvc_pack[pack->dev_id]->lock), flags);
	} else {
		spin_lock_irqsave(&(g_uvc_pack[pack->dev_id]->lock), flags);
		p->len = pack->pack_len;
		p->addr = pack->pack_vir_addr;
		if (p->len != 0) {
			pack_save_to_list(p, pack, pack->is_frame_end, true);
		} else {
			printk(KERN_EMERG"[Error len ==0]buf_vir_addr: 0x%llx, buf_size: %d, pack_vir_addr: 0x%llx, pack_len:%d, "
						"is_frame_end:%d\n", pack->buf_vir_addr, pack->buf_size, pack->pack_vir_addr, pack->pack_len,
						pack->is_frame_end);
			kfree(p);
		}
		spin_unlock_irqrestore(&(g_uvc_pack[pack->dev_id]->lock), flags);
	}

	if (g_uvc_pack[pack->dev_id]->video != NULL)
			schedule_work(&(g_uvc_pack[pack->dev_id]->video->pump));
	return 0;

err:
	spin_lock_irqsave(&(g_uvc_pack[pack->dev_id]->lock), flags);
	clean_untrans_frame(1, true, pack->dev_id);
	spin_unlock_irqrestore(&(g_uvc_pack[pack->dev_id]->lock), flags);

	if (pack->callback_func != NULL)
		pack->callback_func(pack);

	return 0;
}
EXPORT_SYMBOL(uvc_recv_pack);
#endif /* CONFIG_ARCH_BSP */

/* --------------------------------------------------------------------------
 * Video codecs
 */
#if defined(CONFIG_ARCH_BSP)
static int
uvc_video_encode_header_for_high_perf_mode(struct uvc_video *video, struct uvc_pack_trans *pack,
		u8 *data, int len, bool is_first_packet)
{
	struct uvc_device *uvc = container_of(video, struct uvc_device, video);
	struct usb_composite_dev *cdev = uvc->func.config->cdev;
	static struct timespec64 ts = {0};
	static u32 pts = 0;
	static u32 sof = 0;
	static u32 stc = 0;
	uvc_pack_priv *priv_data = (uvc_pack_priv *)pack->pack->private_data;
	int pos = 0x2;

	u64 current_pts = priv_data->pts;
	data[1] = video->fid;

	if (is_first_packet == true) {
		pts = (u32)current_pts;

		if (cdev->gadget->ops->get_frame) {
			sof = usb_gadget_frame_number(cdev->gadget);
		}
		ktime_get_ts64(&ts);
		stc = ((u64)ts.tv_sec * USEC_PER_SEC + ts.tv_nsec / NSEC_PER_USEC) * 48; // 48 for 48KHz kernel frequency
	}

	data[1] |= UVC_STREAM_PTS;
	put_unaligned_le32(pts, &data[pos]);
	pos += 4; // u32 occupies 4 bytes
	data[1] |= UVC_STREAM_SCR;
	put_unaligned_le32(stc, &data[pos]);
	put_unaligned_le16(sof, &data[pos + 4]); // 4 for sof offset
	pos += 6; // u32 + u16 occupies 6 bytes

	data[0] = pos;

#ifdef UVC_SLICE_MODE
	if (pack->len - pack->buf_used <= len - UVC_HEADER_SIZE)
		data[1] |= UVC_STREAM_RES;
#endif
	if ((pack->len - pack->buf_used <= len - UVC_HEADER_SIZE) && pack->is_frame_end)
		data[1] |= UVC_STREAM_EOF;

	return UVC_HEADER_SIZE;
}

static int
uvc_video_encode_data_for_high_perf_mode(struct uvc_video *video, struct uvc_pack_trans *pack,
		u8 *data, int len)
{
	unsigned int nbytes;
	int i;
	u64 current_addr = pack->addr + pack->buf_used;

	nbytes = min((unsigned int)len, pack->len - pack->buf_used);
	if (nbytes == 0) {
		printk(KERN_EMERG"nbytes == 0; len: %d, pack->len:%d, pack->buf_used: %d", len, pack->len, pack->buf_used);
	}
	/* data[0~7] for video data memory address. 64-bit */
	for (i = 0; i < 8; i++)
		data[i] = (current_addr >> (8 * (7 - i))) & 0xFF;

	/* data[8~11] for video data size. byte */
	for (i = 0; i < 4; i++)
		data[i + 8] = (nbytes >> (8 * (3 - i))) & 0xFF;

	pack->buf_used += nbytes;

	return nbytes;
}

static void
uvc_video_encode_bulk_for_high_perf_mode(struct usb_request *req, struct uvc_video *video,
		struct uvc_pack_trans *g_pack)
{
	struct uvc_pack_trans *pack = NULL;
	int ret;
	int len;
	int ttllen = 0;
	unsigned int sg_idx;
	u8 *mem = NULL;
	int last_len = 0;

	if (unlikely(list_empty(&g_pack->list) || (g_pack->frame_cnts == 0)))
		return;

	pack = list_first_entry(&g_pack->list, struct uvc_pack_trans, list);
	for (sg_idx = 0; sg_idx < video->num_sgs; sg_idx++) {
		mem = sg_virt(&req->sg[sg_idx]);

		/* Add the header. */
		if ((sg_idx % 2) == 0) {
			len = video->req_size;
			ret = uvc_video_encode_header_for_high_perf_mode(video, pack, mem, len, (sg_idx == 0));
			mem += ret;
			len -= ret;
			sg_set_buf(&req->sg[sg_idx], sg_virt(&req->sg[sg_idx]),
						UVC_HEADER_SIZE);
			continue;
		}

		len = video->req_size - UVC_HEADER_SIZE;
		len = min((int)(video->max_payload_size - video->payload_size), len);
		/* Process video data. */
		ret = uvc_video_encode_data_for_high_perf_mode(video, pack, mem, len);
		video->payload_size += ret;
		len -= ret;

		/* Sync sg buffer len , default is 1024 or 3072 */
		sg_set_buf(&req->sg[sg_idx], sg_virt(&req->sg[sg_idx]),
				video->req_size - UVC_HEADER_SIZE - len);
		ttllen += video->req_size - len;
		last_len = video->req_size - len;

		video->payload_size = 0;

		if (pack->len == pack->buf_used) {
			pack->buf_used = 0;
			pack->is_transferring = true;

			if (pack->is_frame_end) {
				video->fid ^= UVC_STREAM_FID;
				break;
			} else {
				pack = list_next_entry(pack, list);
				if (pack == NULL)
					break;
			}
		}
	}
	req->num_sgs = sg_idx + 1;
	sg_mark_end(&req->sg[sg_idx]);
	req->length = ttllen;
	req->zero = (last_len % video->ep->maxpacket == 0);
}

#ifdef UVC_SLICE_MODE
static struct uvc_pack_trans* fetch_next_unxfer_pack(struct uvc_video *video, struct uvc_pack_trans *g_pack)
{
	struct uvc_pack_trans *pack = NULL;

	pack = list_first_entry(&g_pack->list, struct uvc_pack_trans, list);
	while (pack && pack->is_transferring) {
		pack = list_next_entry(pack, list);
		if (pack == NULL)
			break;
		if (pack == g_uvc_pack[video->dev_id])
			return NULL;
	}
	return pack;
}
#endif

/* the caller shoulud hold g_uvc_pack.lock */
static void
uvc_video_encode_isoc_for_high_perf_mode(struct usb_request *req, struct uvc_video *video,
		struct uvc_pack_trans *g_pack)
{
	int ret;
	struct uvc_pack_trans *pack = NULL;
	int len;
	int ttllen = 0;
	unsigned int sg_idx;
	u8 *mem = NULL;

#ifdef UVC_SLICE_MODE
	if (unlikely(list_empty(&g_pack->list)))
		return;

	pack = fetch_next_unxfer_pack(video, g_pack);
	if (pack == NULL)
		return ;
#else
	if (unlikely(list_empty(&g_pack->list) || (g_pack->frame_cnts == 0)))
		return;

	pack = list_first_entry(&g_pack->list, struct uvc_pack_trans, list);
#endif

	for (sg_idx = 0; sg_idx < video->num_sgs; sg_idx++) {
		mem = sg_virt(&req->sg[sg_idx]);

		/* Add the header. */
		if ((sg_idx % 2) == 0) {
			len = video->req_size;
			ret = uvc_video_encode_header_for_high_perf_mode(video, pack, mem, len, (sg_idx == 0));
			mem += ret;
			len -= ret;
			sg_set_buf(&req->sg[sg_idx], sg_virt(&req->sg[sg_idx]),
						UVC_HEADER_SIZE);
			continue;
		}
		len = video->req_size - UVC_HEADER_SIZE;

		/* Process video data. */
		ret = uvc_video_encode_data_for_high_perf_mode(video, pack, mem, len);
		len -= ret;

		/* Sync sg buffer len , default is 1024 or 3072 */
		sg_set_buf(&req->sg[sg_idx], sg_virt(&req->sg[sg_idx]),
				video->req_size - UVC_HEADER_SIZE - len);
		ttllen += video->req_size - len;

		if (pack->len == pack->buf_used) {
			pack->buf_used = 0;
			pack->is_transferring = true;

			if (pack->is_frame_end) {
				video->fid ^= UVC_STREAM_FID;
				break;
			} else {
#ifdef UVC_SLICE_MODE
				break;
#else
				pack = list_next_entry(pack, list);
				if (pack == NULL)
					break;
#endif
			}
		}
	}
	req->num_sgs = sg_idx + 1;
	if (req->num_sgs > video->num_sgs)
		req->num_sgs = video->num_sgs;
	sg_mark_end(&req->sg[sg_idx]);
	req->length = ttllen;
}

static int
uvc_video_encode_header_for_v4l2_mode(struct uvc_video *video, struct uvc_buffer *buf,
		u8 *data, int len)
{
	data[0] = 2; // uvc 1.1 protocol header
	data[1] = UVC_STREAM_EOH | video->fid;

	/* 2 : uvc1.1 protocol header length */
	if (buf->bytesused - video->queue.buf_used <= len - 2)  // 2 for uvc 1.1 protocol header size
		data[1] |= UVC_STREAM_EOF;

	return 2; // uvc 1.1 protocol header size
}

static int
uvc_video_encode_data_for_v4l2_mode(struct uvc_video *video, struct uvc_buffer *buf,
		u8 *data, int len)
{
	struct uvc_video_queue *queue = &video->queue;
	unsigned int nbytes;
	void *mem;

	/* Copy video data to the USB buffer. */
	mem = buf->mem + queue->buf_used;
	nbytes = min((unsigned int)len, buf->bytesused - queue->buf_used);

	memcpy_s(data, len, mem, nbytes);
	queue->buf_used += nbytes;

	return nbytes;
}

static void
uvc_video_encode_bulk_for_v4l2_mode(struct usb_request *req, struct uvc_video *video,
		struct uvc_buffer *buf)
{
	void *mem = req->buf;
	int len = video->req_size;
	int ret;

	/* Add a header at the beginning of the payload. */
	if (video->payload_size == 0) {
		ret = uvc_video_encode_header_for_v4l2_mode(video, buf, mem, len);
		video->payload_size += ret;
		mem += ret;
		len -= ret;
	}

	/* Process video data. */
	len = min((int)(video->max_payload_size - video->payload_size), len);
	ret = uvc_video_encode_data_for_v4l2_mode(video, buf, mem, len);

	video->payload_size += ret;
	len -= ret;

	req->length = video->req_size - len;

	if (buf->bytesused == video->queue.buf_used) {
		video->queue.buf_used = 0;
		buf->state = UVC_BUF_STATE_DONE;
		uvcg_queue_next_buffer(&video->queue, buf);
		video->fid ^= UVC_STREAM_FID;

		req->zero = (video->payload_size % video->ep->maxpacket == 0);
		video->payload_size = 0;
	}

	if (video->payload_size == video->max_payload_size ||
	    buf->bytesused == video->queue.buf_used)
		video->payload_size = 0;
}

static void
uvc_video_encode_isoc_for_v4l2_mode(struct usb_request *req, struct uvc_video *video,
		struct uvc_buffer *buf)
{
	int ret;
	int len;
	int ttllen = 0;
	unsigned int sg_idx;
	u8 *mem = NULL;

	for (sg_idx = 0; sg_idx < video->num_sgs; sg_idx++) {
		mem = sg_virt(&req->sg[sg_idx]);
		len = video->req_size;

		/* Add the header. */
		ret = uvc_video_encode_header_for_v4l2_mode(video, buf, mem, len);
		mem += ret;
		len -= ret;

		/* Process video data. */
		ret = uvc_video_encode_data_for_v4l2_mode(video, buf, mem, len);
		len -= ret;

		/* Sync sg buffer len , default is 1024 or 3072 */
		sg_set_buf(&req->sg[sg_idx], sg_virt(&req->sg[sg_idx]),
				video->req_size - len);
		ttllen += video->req_size - len;

		if (buf->bytesused == video->queue.buf_used) {
			video->queue.buf_used = 0;
			buf->state = UVC_BUF_STATE_DONE;
			uvcg_queue_next_buffer(&video->queue, buf);
			video->fid ^= UVC_STREAM_FID;
			break;
		}
	}
	req->num_sgs = sg_idx + 1;
	sg_mark_end(&req->sg[sg_idx]);
	req->length = ttllen;
}

static void
uvc_video_encode(struct usb_request *req, struct uvc_video *video,
			struct uvc_pack_trans *pack, struct uvc_buffer *buf)
{
	if (video->performance_mode == UVC_PERFORMANCE_MODE) {
		if (video->max_payload_size) { // bulk mode
			return uvc_video_encode_bulk_for_high_perf_mode(req, video, pack);
		} else {
			return uvc_video_encode_isoc_for_high_perf_mode(req, video, pack);
		}
	} else {
		if (video->max_payload_size) { // bulk mode
			return uvc_video_encode_bulk_for_v4l2_mode(req, video, buf);
		} else {
			return uvc_video_encode_isoc_for_v4l2_mode(req, video, buf);
		}
	}
}

#else /* IS_ENABLED(CONFIG_ARCH_BSP) */

static int
uvc_video_encode_header(struct uvc_video *video, struct uvc_buffer *buf,
		u8 *data, int len)
{
	data[0] = 2; // uvc1.1 protocol header
	data[1] = UVC_STREAM_EOH | video->fid;

	if (buf->bytesused - video->queue.buf_used <= len - 2)
		data[1] |= UVC_STREAM_EOF;

	return 2; // uvc1.1 protocol header length
}

static int
uvc_video_encode_data(struct uvc_video *video, struct uvc_buffer *buf,
		u8 *data, int len)
{
	struct uvc_video_queue *queue = &video->queue;
	unsigned int nbytes;
	void *mem;

	/* Copy video data to the USB buffer. */
	mem = buf->mem + queue->buf_used;
	nbytes = min((unsigned int)len, buf->bytesused - queue->buf_used);

	memcpy(data, mem, nbytes);
	queue->buf_used += nbytes;

	return nbytes;
}

static void
uvc_video_encode_bulk(struct usb_request *req, struct uvc_video *video,
		struct uvc_buffer *buf)
{
	void *mem = req->buf;
	int len = video->req_size;
	int ret;

	/* Add a header at the beginning of the payload. */
	if (video->payload_size == 0) {
		ret = uvc_video_encode_header(video, buf, mem, len);
		video->payload_size += ret;
		mem += ret;
		len -= ret;
	}

	/* Process video data. */
	len = min((int)(video->max_payload_size - video->payload_size), len);
	ret = uvc_video_encode_data(video, buf, mem, len);

	video->payload_size += ret;
	len -= ret;

	req->length = video->req_size - len;
	req->zero = video->payload_size == video->max_payload_size;

	if (buf->bytesused == video->queue.buf_used) {
		video->queue.buf_used = 0;
		buf->state = UVC_BUF_STATE_DONE;
		uvcg_queue_next_buffer(&video->queue, buf);
		video->fid ^= UVC_STREAM_FID;

		video->payload_size = 0;
	}

	if (video->payload_size == video->max_payload_size ||
	    buf->bytesused == video->queue.buf_used)
		video->payload_size = 0;
}

static void
uvc_video_encode_isoc(struct usb_request *req, struct uvc_video *video,
		struct uvc_buffer *buf)
{
	int ret;
	void *mem = req->buf;
	int len = video->req_size;
	int ret;

	/* Add the header. */
	ret = uvc_video_encode_header(video, buf, mem, len);
	mem += ret;
	len -= ret;

	/* Process video data. */
	ret = uvc_video_encode_data(video, buf, mem, len);
	len -= ret;

	req->length = video->req_size - len;

	if (buf->bytesused == video->queue.buf_used) {
		video->queue.buf_used = 0;
		buf->state = UVC_BUF_STATE_DONE;
		uvcg_queue_next_buffer(&video->queue, buf);
		video->fid ^= UVC_STREAM_FID;
	}
}
#endif /* IS_ENABLED(CONFIG_ARCH_BSP) */

/* --------------------------------------------------------------------------
 * Request handling
 */

static int uvcg_video_ep_queue(struct uvc_video *video, struct usb_request *req)
{
	int ret;
#ifdef CONFIG_ARCH_BSP
	/*
	 * Fixme, this is just to workaround the warning by udc core when the ep
	 * is disabled, this may happens when the uvc application is still
	 * streaming new data while the uvc gadget driver has already recieved
	 * the streamoff but the streamoff event is not yet received by the app
	 */
	if (!video->ep->enabled)
		return -EINVAL;
#endif

	ret = usb_ep_queue(video->ep, req, GFP_ATOMIC);
	if (ret < 0) {
		uvcg_err(&video->uvc->func, "Failed to queue request (%d).\n",
			 ret);

#ifdef CONFIG_ARCH_BSP
		/* If the endpoint is disabled the descriptor may be NULL. */
		if (video->ep->desc) {
			/* Isochronous endpoints can't be halted. */
			if (usb_endpoint_xfer_bulk(video->ep->desc))
				usb_ep_set_halt(video->ep);
		}
#else
		/* Isochronous endpoints can't be halted. */
		if (usb_endpoint_xfer_bulk(video->ep->desc))
			usb_ep_set_halt(video->ep);
#endif
	}

	return ret;
}

static void
uvc_video_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct uvc_video *video = req->context;
#if defined(CONFIG_ARCH_BSP)
	struct uvc_device *uvc = video->uvc;
	struct uvc_video_queue *queue = &video->queue;
#endif
	unsigned long flags;

	switch (req->status) {
	case 0:
		break;

	case -ESHUTDOWN:	/* disconnect from host. */
		uvcg_dbg(&video->uvc->func, "VS request cancelled.\n");
#if defined(CONFIG_ARCH_BSP)
		if (video->performance_mode != UVC_PERFORMANCE_MODE)
			uvcg_queue_cancel(queue, 1);
#endif
		break;

	default:
#if defined(CONFIG_ARCH_BSP)
		if (uvc->state == UVC_STATE_BULK_WAITING ||
			uvc->state == UVC_STATE_BULK_SETTING)
			break;
#endif
		uvcg_warn(&video->uvc->func,
			  "VS request completed with status %d.\n",
			  req->status);
#if defined(CONFIG_ARCH_BSP)
		if (video->performance_mode != UVC_PERFORMANCE_MODE)
			uvcg_queue_cancel(queue, 0);
#endif
	}

#if defined(CONFIG_ARCH_BSP)
	if (video->performance_mode == UVC_PERFORMANCE_MODE) {
		spin_lock_irqsave(&(g_uvc_pack[video->dev_id]->lock), flags);
		clean_complete_trans_frame(video->dev_id);
		spin_unlock_irqrestore(&(g_uvc_pack[video->dev_id]->lock), flags);
	}
#endif

	spin_lock_irqsave(&video->req_lock, flags);
	list_add_tail(&req->list, &video->req_free);
	spin_unlock_irqrestore(&video->req_lock, flags);

#if defined(CONFIG_USB_F_UVC_STILL_IMAGE) && !defined(CONFIG_UVC_NO_STATUS_INT_EP)
	/*  Only still image method2 need schedule from video complete.  */
	if (uvc_still_is_md2(video->still) &&
		(uvc_still_is_trigger_status(video->still) || uvc_still_is_prepare_status(video->still))) {
		return uvc_still_start_schedule(video->still);
	}
#endif
	schedule_work(&video->pump);
}

#if defined(CONFIG_ARCH_BSP)
static int
uvc_video_free_requests_for_perf_mode(struct uvc_video *video)
{
	unsigned int i;
	unsigned int sg_idx;

	for (i = 0; i < UVC_NUM_REQUESTS_SG; ++i) {
		if (video->req[i]) {
			for (sg_idx = 0; sg_idx < video->num_sgs; sg_idx++)
				if (sg_page(&video->req[i]->sg[sg_idx])) {
					kfree(sg_virt(&video->req[i]->sg[sg_idx]));
					video->req[i]->num_mapped_sgs = 0;
				}

			if (video->req[i]->sg) {
				kfree(video->req[i]->sg);
				video->req[i]->sg = NULL;
			}
			usb_ep_free_request(video->ep, video->req[i]);
			video->req[i] = NULL;
		}
	}

	return 0;
}

static int
uvc_video_free_requests_for_v4l2_mode(struct uvc_video *video)
{
	unsigned int i;
	unsigned int sg_idx;
	/* zero max_payload_size means isoc mode, fill req by sg */
	if (video->max_payload_size == 0) {
		for (i = 0; i < UVC_NUM_REQUESTS_SG; ++i) {
			if (video->req[i]) {
				for (sg_idx = 0; sg_idx < video->num_sgs; sg_idx++)
					if (sg_page(&video->req[i]->sg[sg_idx])) {
						kfree(sg_virt(&video->req[i]->sg[sg_idx]));
						video->req[i]->num_mapped_sgs = 0;
					}

				if (video->req[i]->sg) {
					kfree(video->req[i]->sg);
					video->req[i]->sg = NULL;
				}
				usb_ep_free_request(video->ep, video->req[i]);
				video->req[i] = NULL;
			}
		}
	} else { // non-zero max_payload_size means bulk mode
		for (i = 0; i < UVC_NUM_REQUESTS; ++i) {
			if (video->req[i]) {
				usb_ep_free_request(video->ep, video->req[i]);
				video->req[i] = NULL;
			}
			if (video->req_buffer[i]) {
				kfree(video->req_buffer[i]);
				video->req_buffer[i] = NULL;
			}
		}
	}
#if defined(CONFIG_USB_F_UVC_STILL_IMAGE) && !defined(CONFIG_UVC_NO_STATUS_INT_EP)
	uvc_still_free_image(video->still);
#endif

	return 0;
}

static int
uvc_video_free_requests(struct uvc_video *video)
{
	unsigned long flags;
	int ret = 0;

	if (video->performance_mode == UVC_PERFORMANCE_MODE) {
		ret = uvc_video_free_requests_for_perf_mode(video);
		spin_lock_irqsave(&(g_uvc_pack[video->dev_id]->lock), flags);
		clean_untrans_frame(1, true, video->dev_id);
		spin_unlock_irqrestore(&(g_uvc_pack[video->dev_id]->lock), flags);
	} else {
		ret = uvc_video_free_requests_for_v4l2_mode(video);
	}

	INIT_LIST_HEAD(&video->req_free);
	video->req_size = 0;
	return ret;
}

static int
uvc_video_alloc_requests_for_perf_mode(struct uvc_video *video, unsigned int req_size)
{
	unsigned int i;
	int ret = -ENOMEM;
	struct scatterlist  *sg;
	unsigned int num_sgs;
	unsigned int sg_idx;

	BUILD_BUG_ON(UVC_NUM_REQUESTS_SG > UVC_NUM_REQUESTS);

	/* 2 : one for header and one for data; 2 : in the event of winding , */
	num_sgs = ((video->imagesize / (req_size - UVC_HEADER_SIZE)) + 1) * 2 + 2;
	video->num_sgs = num_sgs;

	iprec("[%s] imagesize: %d, req_size: %d, num_sgs: %d", __func__, video->imagesize, req_size, num_sgs);
	for (i = 0; i < UVC_NUM_REQUESTS_SG; ++i) {
		sg = kmalloc(num_sgs * sizeof(struct scatterlist), GFP_ATOMIC);
		if (sg == NULL)
			goto error;
		sg_init_table(sg, num_sgs);

		video->req[i] = usb_ep_alloc_request(video->ep, GFP_KERNEL);
		if (video->req[i] == NULL)
			goto error;

		for (sg_idx = 0 ; sg_idx < num_sgs ; sg_idx++) {
			if ((sg_idx % 2) == 0) { // Add the header
				video->sg_buf = kmalloc(UVC_HEADER_SIZE, GFP_KERNEL);
				if (video->sg_buf == NULL)
					goto error;
				sg_set_buf(&sg[sg_idx], video->sg_buf, UVC_HEADER_SIZE);
			} else { // Add the data.
				video->sg_buf = kmalloc(UVC_DATA_STRUCT_LENGTH, GFP_KERNEL);
				if (video->sg_buf == NULL)
					goto error;
				sg_set_buf(&sg[sg_idx], video->sg_buf, UVC_DATA_STRUCT_LENGTH);
			}
		}
		video->req[i]->sg = sg;
		video->req[i]->num_sgs = num_sgs;
		video->req[i]->length = 0;
		video->req[i]->complete = uvc_video_complete;
		video->req[i]->context = video;
		video->req[i]->uvc_performance_mode = 1;

		list_add_tail(&video->req[i]->list, &video->req_free);
	}

	video->req_size = req_size;

	return 0;

error:
	uvc_video_free_requests(video);
	return ret;
}

static int
uvc_video_alloc_requests_for_v4l2_mode(struct uvc_video *video, unsigned int req_size)
{
	unsigned int i;
	int ret = -ENOMEM;
	struct scatterlist  *sg;
	unsigned int num_sgs;
	unsigned int sg_idx;

	/* zero max_payload_size means isoc mode, fill req by sg */
	if (video->max_payload_size == 0) {
		num_sgs = ((video->imagesize / (req_size - UVC_HEADER_SIZE)) + 1);
#if defined(CONFIG_USB_F_UVC_STILL_IMAGE) && !defined(CONFIG_UVC_NO_STATUS_INT_EP)
		if (uvc_still_is_md2(video->still) && CONFIG_USB_STILL_MAX_IMAGE_SIZE > video->imagesize)
			num_sgs = ((CONFIG_USB_STILL_MAX_IMAGE_SIZE / (req_size - UVC_HEADER_SIZE)) + 1);
#endif
		video->num_sgs = num_sgs;

		iprec("[%s] imagesize: %d, req_size: %d, num_sgs: %d", __func__, video->imagesize, req_size, num_sgs);
		for (i = 0; i < UVC_NUM_REQUESTS_SG; ++i) {
			sg = kmalloc(num_sgs * sizeof(struct scatterlist), GFP_ATOMIC);
			if (sg == NULL)
				goto error;
			sg_init_table(sg, num_sgs);

			video->req[i] = usb_ep_alloc_request(video->ep, GFP_KERNEL);
			if (video->req[i] == NULL)
				goto error;

			for (sg_idx = 0 ; sg_idx < num_sgs ; sg_idx++) {
				video->sg_buf = kmalloc(req_size, GFP_KERNEL);
				if (video->sg_buf == NULL)
						goto error;
				sg_set_buf(&sg[sg_idx], video->sg_buf, req_size);
			}
			video->req[i]->sg = sg;
			video->req[i]->num_sgs = num_sgs;
			video->req[i]->length = 0;
			video->req[i]->complete = uvc_video_complete;
			video->req[i]->context = video;

			list_add_tail(&video->req[i]->list, &video->req_free);
		}
	} else { // non-zero max_payload_size means bulk mode
		for (i = 0; i < UVC_NUM_REQUESTS; ++i) {
			video->num_sgs = 0;
			video->req_buffer[i] = kmalloc(req_size, GFP_KERNEL);
			if (video->req_buffer[i] == NULL)
				goto error;

			video->req[i] = usb_ep_alloc_request(video->ep, GFP_KERNEL);
			if (video->req[i] == NULL)
				goto error;

			video->req[i]->buf = video->req_buffer[i];
			video->req[i]->length = 0;
			video->req[i]->complete = uvc_video_complete;
			video->req[i]->context = video;

			list_add_tail(&video->req[i]->list, &video->req_free);
		}
	}

#if defined(CONFIG_USB_F_UVC_STILL_IMAGE) && !defined(CONFIG_UVC_NO_STATUS_INT_EP)
	uvc_still_alloc_image(video->still);
#endif
	video->req_size = req_size;

	return 0;

error:
	uvc_video_free_requests(video);
	return ret;
}

static int
uvc_video_alloc_requests(struct uvc_video *video)
{
	unsigned int req_size;
	BUG_ON(video->req_size);

	/* Bulk mode uses max_payload_size as req_size */
	if (video->max_payload_size)
		req_size = video->max_payload_size;
	else
		req_size = video->ep->maxpacket
			* max_t(unsigned int, video->ep->maxburst, 1)
			* (video->ep->mult);

	if (video->performance_mode == UVC_PERFORMANCE_MODE)
		return uvc_video_alloc_requests_for_perf_mode(video, req_size);

	return uvc_video_alloc_requests_for_v4l2_mode(video, req_size);
}

#else  //  defined(CONFIG_ARCH_BSP)
static int
uvc_video_free_requests(struct uvc_video *video)
{
	unsigned int i;

	for (i = 0; i < UVC_NUM_REQUESTS; ++i) {
		if (video->req[i]) {
			usb_ep_free_request(video->ep, video->req[i]);
			video->req[i] = NULL;
		}

		if (video->req_buffer[i]) {
			kfree(video->req_buffer[i]);
			video->req_buffer[i] = NULL;
		}
	}

	INIT_LIST_HEAD(&video->req_free);
	video->req_size = 0;
	return 0;
}

static int
uvc_video_alloc_requests(struct uvc_video *video)
{
	unsigned int req_size;
	unsigned int i;
	int ret = -ENOMEM;

	BUG_ON(video->req_size);

	req_size = video->ep->maxpacket
		 * max_t(unsigned int, video->ep->maxburst, 1)
		 * (video->ep->mult);

	for (i = 0; i < UVC_NUM_REQUESTS; ++i) {
		video->req_buffer[i] = kmalloc(req_size, GFP_KERNEL);
		if (video->req_buffer[i] == NULL)
			goto error;

		video->req[i] = usb_ep_alloc_request(video->ep, GFP_KERNEL);
		if (video->req[i] == NULL)
			goto error;

		video->req[i]->buf = video->req_buffer[i];
		video->req[i]->length = 0;
		video->req[i]->complete = uvc_video_complete;
		video->req[i]->context = video;

		list_add_tail(&video->req[i]->list, &video->req_free);
	}

	video->req_size = req_size;

	return 0;

error:
	uvc_video_free_requests(video);
	return ret;
}
#endif  //  defined(CONFIG_ARCH_BSP)

/* --------------------------------------------------------------------------
 * Video streaming
 */

/*
 * uvcg_video_pump - Pump video data into the USB requests
 *
 * This function fills the available USB requests (listed in req_free) with
 * video data from the queued buffers.
 */
#if defined(CONFIG_ARCH_BSP)
static void uvcg_video_pump_for_high_perf_mode(struct uvc_video *video)
{
	struct usb_request *req;
	unsigned long flags;
	int ret;

	if (g_uvc_pack[video->dev_id]->video->is_streaming == false)
		return;

	while (1) {
		/* Retrieve the first available USB request, protected by the
		 * request lock.
		 */
		spin_lock_irqsave(&video->req_lock, flags);
		if (list_empty(&video->req_free)) {
			spin_unlock_irqrestore(&video->req_lock, flags);
			return;
		}
		req = list_first_entry(&video->req_free, struct usb_request,
					list);
		list_del(&req->list);
		spin_unlock_irqrestore(&video->req_lock, flags);

		/* Retrieve the first available video buffer and fill the
		 * request, protected by the video queue irqlock.
		 */
		spin_lock_irqsave(&(g_uvc_pack[video->dev_id]->lock), flags);
#ifdef UVC_SLICE_MODE
		if (list_empty(&(g_uvc_pack[video->dev_id]->list))) {
			spin_unlock_irqrestore(&(g_uvc_pack[video->dev_id]->lock), flags);
			break;
		}
#else
		if (list_empty(&(g_uvc_pack[video->dev_id]->list)) || (g_uvc_pack[video->dev_id]->frame_cnts == 0)) {
			spin_unlock_irqrestore(&(g_uvc_pack[video->dev_id]->lock), flags);
			break;
		}
#endif

		sg_unmark_end(&req->sg[req->num_sgs - 1]);

		video->encode(req, video, g_uvc_pack[video->dev_id], NULL);

		/* Queue the USB request */
		ret = uvcg_video_ep_queue(video, req);
		if (ret < 0) {
			spin_unlock_irqrestore(&(g_uvc_pack[video->dev_id]->lock), flags);
			break;
		}

		spin_unlock_irqrestore(&(g_uvc_pack[video->dev_id]->lock), flags);
	}

	spin_lock_irqsave(&video->req_lock, flags);
	sg_unmark_end(&req->sg[req->num_sgs - 1]);
	list_add_tail(&req->list, &video->req_free);
	spin_unlock_irqrestore(&video->req_lock, flags);
	return;
}

static void uvcg_video_pump_for_v4l2_mode(struct uvc_video *video)
{
	struct uvc_video_queue *queue = &video->queue;
	struct usb_request *req;
	struct uvc_buffer *buf;
	unsigned long flags;
	int ret;

	if (video->is_streaming == false)
		return;

#if defined(CONFIG_USB_F_UVC_STILL_IMAGE) && !defined(CONFIG_UVC_NO_STATUS_INT_EP)
	/*  Only still image method2 need stop video pump when transfer still image  */
	if (uvc_still_is_md2(video->still) && uvc_still_is_active_status(video->still))
		return;
#endif

	while (1) {
		/* Retrieve the first available USB request, protected by the
		 * request lock.
		 */
		spin_lock_irqsave(&video->req_lock, flags);
		if (list_empty(&video->req_free)) {
			spin_unlock_irqrestore(&video->req_lock, flags);
			return;
		}
		req = list_first_entry(&video->req_free, struct usb_request,
					list);
		list_del(&req->list);
		spin_unlock_irqrestore(&video->req_lock, flags);

		/* Retrieve the first available video buffer and fill the
		 * request, protected by the video queue irqlock.
		 */
		spin_lock_irqsave(&queue->irqlock, flags);
		buf = uvcg_queue_head(queue);
		if (buf == NULL) {
			spin_unlock_irqrestore(&queue->irqlock, flags);
			break;
		}

		if (video->max_payload_size == 0) { // zero max_payload_size means isoc mode
			sg_unmark_end(&req->sg[req->num_sgs - 1]);
		}

		video->encode(req, video, NULL, buf);

		/* Queue the USB request */
		ret = uvcg_video_ep_queue(video, req);
		spin_unlock_irqrestore(&queue->irqlock, flags);

		if (ret < 0) {
			uvcg_queue_cancel(queue, 0);
			break;
		}
	}

	spin_lock_irqsave(&video->req_lock, flags);
	if (video->max_payload_size == 0) {
		sg_unmark_end(&req->sg[req->num_sgs - 1]);
	}
	list_add_tail(&req->list, &video->req_free);
	spin_unlock_irqrestore(&video->req_lock, flags);
	return;
}

static void uvcg_video_pump(struct work_struct *work)
{
	struct uvc_video *video = container_of(work, struct uvc_video, pump);
	if (video->performance_mode == UVC_PERFORMANCE_MODE)
		return uvcg_video_pump_for_high_perf_mode(video);
	else
		return uvcg_video_pump_for_v4l2_mode(video);
}

#else  //  defined(CONFIG_ARCH_BSP)
static void uvcg_video_pump(struct work_struct *work)
{
	struct uvc_video *video = container_of(work, struct uvc_video, pump);

	struct uvc_video_queue *queue = &video->queue;
	struct usb_request *req;
	struct uvc_buffer *buf;
	unsigned long flags;
	int ret;

	while (1) {
		/* Retrieve the first available USB request, protected by the
		 * request lock.
		 */
		spin_lock_irqsave(&video->req_lock, flags);
		if (list_empty(&video->req_free)) {
			spin_unlock_irqrestore(&video->req_lock, flags);
			return;
		}
		req = list_first_entry(&video->req_free, struct usb_request,
					list);
		list_del(&req->list);
		spin_unlock_irqrestore(&video->req_lock, flags);

		/* Retrieve the first available video buffer and fill the
		 * request, protected by the video queue irqlock.
		 */
		spin_lock_irqsave(&queue->irqlock, flags);
		buf = uvcg_queue_head(queue);
		if (buf == NULL) {
			spin_unlock_irqrestore(&queue->irqlock, flags);
			break;
		}

		video->encode(req, video, buf);

		/* Queue the USB request */
		ret = uvcg_video_ep_queue(video, req);
		spin_unlock_irqrestore(&queue->irqlock, flags);

		if (ret < 0) {
			uvcg_queue_cancel(queue, 0);
			break;
		}
	}

	spin_lock_irqsave(&video->req_lock, flags);
	list_add_tail(&req->list, &video->req_free);
	spin_unlock_irqrestore(&video->req_lock, flags);
	return;
}
#endif  //  defined(CONFIG_ARCH_BSP)

/*
 * Enable or disable the video stream.
 */
int uvcg_video_enable(struct uvc_video *video, int enable)
{
	unsigned int i;
	int ret;
#if defined(CONFIG_ARCH_BSP)
	unsigned long flags;

	iprec("[%s] %d", __func__, enable);
#endif

	if (video->ep == NULL) {
		uvcg_info(&video->uvc->func,
			  "Video enable failed, device is uninitialized.\n");
		return -ENODEV;
	}

	if (!enable) {
#if defined(CONFIG_ARCH_BSP)
		if (video->is_streaming && video->ep && video->ep->enabled)
			usb_ep_disable(video->ep);

		if (video->performance_mode == UVC_PERFORMANCE_MODE) {
			spin_lock_irqsave(&(g_uvc_pack[video->dev_id]->lock), flags);
			clean_untrans_frame(1, true, video->dev_id);
			g_uvc_pack[video->dev_id]->video->is_streaming = false;
			spin_unlock_irqrestore(&(g_uvc_pack[video->dev_id]->lock), flags);
		}
		video->is_streaming = false;
#endif
		cancel_work_sync(&video->pump);
		uvcg_queue_cancel(&video->queue, 0);
#if defined(CONFIG_ARCH_BSP) && defined(CONFIG_USB_F_UVC_STILL_IMAGE) && !defined(CONFIG_UVC_NO_STATUS_INT_EP)
		uvc_still_clean_status(video->still);
#endif

		for (i = 0; i < UVC_NUM_REQUESTS; ++i)
			if (video->req[i])
				usb_ep_dequeue(video->ep, video->req[i]);

		uvc_video_free_requests(video);
#if defined(CONFIG_ARCH_BSP)
		if (video->performance_mode != UVC_PERFORMANCE_MODE)
			uvcg_queue_enable(&video->queue, 0);
#endif
		return 0;
	}

#if defined(CONFIG_ARCH_BSP)
	if (video->performance_mode != UVC_PERFORMANCE_MODE) {
		if ((ret = uvcg_queue_enable(&video->queue, 1)) < 0)
			return ret;
	}
#endif

	if ((ret = uvc_video_alloc_requests(video)) < 0)
		return ret;

#if defined(CONFIG_ARCH_BSP)
	video->encode = uvc_video_encode;
	if (video->max_payload_size) {
		video->payload_size = 0;
	}
#else
	if (video->max_payload_size) {
		video->encode = uvc_video_encode_bulk;
		video->payload_size = 0;
	} else
		video->encode = uvc_video_encode_isoc;
#endif

#if defined(CONFIG_ARCH_BSP)
	if (video->performance_mode == UVC_PERFORMANCE_MODE) {
		spin_lock_irqsave(&(g_uvc_pack[video->dev_id]->lock), flags);
		g_uvc_pack[video->dev_id]->video->is_streaming = true;
		spin_unlock_irqrestore(&(g_uvc_pack[video->dev_id]->lock), flags);
	}
	video->is_streaming = true;
#endif
	schedule_work(&video->pump);

	return ret;
}

/*
 * Initialize the UVC video stream.
 */
int uvcg_video_init(struct uvc_video *video, struct uvc_device *uvc)
{
	INIT_LIST_HEAD(&video->req_free);
	spin_lock_init(&video->req_lock);
	INIT_WORK(&video->pump, uvcg_video_pump);

	video->uvc = uvc;
	video->fcc = V4L2_PIX_FMT_YUYV;
	video->bpp = 16;
	video->width = 320;
	video->height = 240;
	video->imagesize = 320 * 240 * 2;

	/* Initialize the video buffers queue. */
	uvcg_queue_init(&video->queue, V4L2_BUF_TYPE_VIDEO_OUTPUT,
			&video->mutex);
#if defined(CONFIG_USB_F_UVC_STILL_IMAGE) && !defined(CONFIG_UVC_NO_STATUS_INT_EP)
	uvc_still_image_init(&video->still, uvc_video_alloc_requests, uvc_video_free_requests);
#endif
	return 0;
}

/*
 * Initialize the uvc pack structure.
 */
#if defined(CONFIG_ARCH_BSP)
int uvcg_uvc_pack_init(struct uvc_video *video)
{
#ifdef DUMP_UVC_DATA_FROM_MPP
	struct uvc_pack_save *f;
#endif

	struct uvc_pack_trans *p;

	if (video->performance_mode != UVC_PERFORMANCE_MODE) {
		printk(KERN_INFO"[%s] UVC is not in high performance mode!\n", __func__);
		return 0;
	}

	p = (struct uvc_pack_trans *)kmalloc(sizeof(struct uvc_pack_trans), GFP_KERNEL);
	if (unlikely(p == NULL))
		return -1;
	INIT_LIST_HEAD(&(p->list));
	spin_lock_init(&(p->lock));
	p->frame_cnts = 0;
	video->is_streaming = false;
	p->video = video;
	g_uvc_pack[video->dev_id] = p;

#ifdef DUMP_UVC_DATA_FROM_MPP
	f = (struct uvc_pack_save *)kmalloc(sizeof(struct uvc_pack_save), GFP_KERNEL);
	if (unlikely(f == NULL))
		printk(KERN_ERR "Failed to malloc struct uvc_pack_save.\n");
	INIT_LIST_HEAD(&f->list);
	spin_lock_init(&f->lock);
	g_uvc_pack_save[video->dev_id] = f;
	save_file_worker[video->dev_id].dev_id = video->dev_id;
	INIT_WORK(&(save_file_worker[video->dev_id].sfworker), save_file_thread);
#endif

	return 0;
}
#endif
