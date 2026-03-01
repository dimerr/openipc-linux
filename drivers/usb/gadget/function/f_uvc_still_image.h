/*
 * Copyright (c) HiSilicon (Shanghai) Technologies Co., Ltd. 2024-2024. All rights reserved.
 */

#ifndef U_UVC_STILL_IMAGE_H
#define U_UVC_STILL_IMAGE_H


#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/usb/composite.h>
#include <linux/videodev2.h>

#define UVC_STILL_BUTTON_TEST
#define UVC_VS_STATUS_BUTTON_RELEASED		0
#define UVC_VS_STATUS_BUTTON_PRESSED		1
#define UVC_VS_STATUS_BUTTON_DONE			2
#define UVC_VS_STATUS_BUF_LEN				4
#define UVC_VS_STATUS_VALUE_INDEX			3
#define UVC_STILL_IMAGE_METHOD1				1
#define UVC_STILL_IMAGE_METHOD2				2
#define UVC_STILL_IMAGE_METHOD3				3
#define PAGE_ONE_LEN						4194304
#define MAX_IMAGE_SIZE						(3840 * 2160 * 2)
#define UVC_STILL_IMAGE_PAGE_NUM			4
#define UVC_STILL_IMAGE_NUM_REQUESTS		32
#define UVC_STILL_IMAGE_INTERFACE			1
#define UVC_STILL_IMAGE_COMMIT				0x0400
#define UVC_STILL_IMAGE_TRIGGER				0x0500

/* Status Format: VideoStreaming Interface as the Originator */
struct uvc_vs_status_packet_format {
	__u8  bStatusType;
	__u8  bOriginator;
	__u8  bEvent;
	__u8  bValue;
} __attribute__((packed));

/* 3.9.2.5. Still Image Frame Descriptor */
struct uvc_still_frame_size {
	__u16 wWidth;
	__u16 wHeight;
};

struct uvc_still_image_frame_descriptor {
	__u8  bLength;
	__u8  bDescriptorType;
	__u8  bDescriptorSubType;
	__u8  bEndpointAddress;
	__u8  bNumImageSizePatterns;
	struct uvc_still_frame_size frame[4];
	__u8  bNumCompressionPattern;
} __attribute__((__packed__));

/*
METHOD2
	Normal Process:
	  STILL_IDLE -> STILL_COMMIT -> STILL_PREPARE -> STILL_TRIGGER -> STILL_IDLE;
	if need reset endpoint capability, process:
	  STILL_IDLE -> STILL_COMMIT -> STILL_COMMIT_RESET -> STILL_PREPARE2 -> STILL_TRIGGER2 -> STILL_RESTORE -> STILL_IDLE;

METHOD3
	STILL_IDLE -> STILL_TRIGGER -> STILL_IDLE
*/
enum still_image_status {
	STILL_IDLE = 0,   /* Still image is not used currently, for method2/method3. */
	STILL_COMMIT,   /* Still image commit state, for method2. */
	STILL_COMMIT_RESET,   /* Still image commit state and reset resource, for method2. */
	STILL_PREPARE,  /* Wait until a video transfer is complete and then start still image transfer, for method2 */
	STILL_PREPARE2,  /* Previous step is STILL_COMMIT_RESET, same as STILL_PREPARE, for method2. */
	STILL_TRIGGER,   /* Trigger state, for method2/method3. */
	STILL_TRIGGER2,   /* Trigger state, means that next step is restoring video resource, for method2. */
	STILL_RESTORE,   /* Meaning that video resource needs to be restored, for method2. */
};

#define UVC_DT_STILL_IMAGE_FRAME_SIZE(n, m)			(10+(4*n)-4+m)

#define STILL_IMAGE_FRAME(n) \
	uvc_frame_still_##n

#define DECLARE_STILL_IMAGE_FRAME(n)			\
struct STILL_IMAGE_FRAME(n) {				\
	__u8  bLength;				\
	__u8  bDescriptorType;		\
	__u8  bDescriptorSubType;	\
	__u8  bEndpointAddress;		\
	__u8  bNumImageSizePatterns;		\
	struct uvc_still_frame_size frame[n];	\
	__u8  bNumCompressionPattern;	\
} __attribute__ ((packed))

#define DECLARE_STILL_IMAGE_FRAM_COMP(n, m)			\
struct STILL_IMAGE_FRAME_COMP(n, m) {				\
	__u8  bLength;				\
	__u8  bDescriptorType;		\
	__u8  bDescriptorSubType;	\
	__u8  bEndpointAddress;		\
	__u8  bNumImageSizePatterns;		\
	struct uvc_still_frame_size frame[n];	\
	__u8  bNumCompressionPattern;	\
	__u8  bCompression[m];		\
} __attribute__ ((packed))

struct uvc_video;
struct still_image {
	u8 *still_addr[UVC_STILL_IMAGE_PAGE_NUM];
	unsigned int max_img_size;
	struct usb_request *status_req;
	__u8 status_req_buf[UVC_VS_STATUS_BUF_LEN];
	struct usb_ep *still_ep;
	struct usb_request *still_req[UVC_STILL_IMAGE_NUM_REQUESTS];
	__u8 *still_req_buffer[UVC_STILL_IMAGE_NUM_REQUESTS];
	struct list_head still_req_free;
	spinlock_t still_req_lock;
	struct work_struct pump;

	unsigned int still_len;
	unsigned int still_buf_used;
	unsigned int still_maxpacket;
	int still_capture_method;	/* Indicates the current mode of the still image. */
	struct uvc_video *video;
	enum still_image_status still_status;
	int (*uvc_still_alloc_requests)(struct uvc_video *video);
	int (*uvc_still_free_requests)(struct uvc_video *video);
};


int uvc_still_is_ep_mark(struct still_image *still);
void uvc_still_disable_ep(struct still_image *still);
void uvc_still_copy_ep_desc(void *desc, void *desc1, void *desc2, void *desc3, char name);
void uvc_still_add_alt_endpoint_num(int method, struct usb_interface_descriptor *alt0);
void uvc_still_fill_still_desc(struct still_image *still, const struct usb_descriptor_header * const **uvc_std,
		enum usb_device_speed speed, int is_bulk_mode);
void uvc_still_set_appropriate_still_ep(struct still_image *still, int method,
	int speed, int max_ss_ep_pack, int mask);
int uvc_still_set_video_method_attr(struct still_image *still, int method,
	struct usb_configuration *c, int still_maxpacket);
void uvc_still_check_commit_status(struct still_image *still, unsigned char request, int select, int w_index);
void uvc_still_to_commit_reset_status(struct still_image *still);
int uvc_still_is_commit_status(struct still_image *still);
int uvc_still_is_commit_reset_status(struct still_image *still);

int uvc_still_is_md3(struct still_image *still);
int uvc_still_is_md2(struct still_image *still);
void uvc_still_clean_status(struct still_image *still);
int uvc_still_is_trigger_status(struct still_image *still);
int uvc_still_is_prepare_status(struct still_image *still);
void uvc_still_free_image(struct still_image *still);
int uvc_still_alloc_image(struct still_image *still);
int uvc_still_image_init(struct still_image **still_addr, void *alloc_req, void *free_req);
void uvc_still_start_schedule(struct still_image *still);
void uvc_still_reset_imagesize(struct still_image *still, u32 *max_img_size);
int uvc_still_is_reset_status(struct still_image *still);
void uvc_still_to_idle_status(struct still_image *still);
int uvc_still_is_active_status(struct still_image *still);
int uvc_still_is_restore_status(struct still_image *still);
void uvc_still_reset_resource(struct still_image *still);

int uvc_still_processes_still_image(struct file *file, void *fh, const struct v4l2_framebuffer *a);
int uvc_still_button_pressed(struct file *file, void *fh, struct v4l2_framebuffer *a);
#endif
