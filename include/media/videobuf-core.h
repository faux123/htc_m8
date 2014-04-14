/*
 * generic helper functions for handling video4linux capture buffers
 *
 * (c) 2007 Mauro Carvalho Chehab, <mchehab@infradead.org>
 *
 * Highly based on video-buf written originally by:
 * (c) 2001,02 Gerd Knorr <kraxel@bytesex.org>
 * (c) 2006 Mauro Carvalho Chehab, <mchehab@infradead.org>
 * (c) 2006 Ted Walther and John Sokol
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2
 */

#ifndef _VIDEOBUF_CORE_H
#define _VIDEOBUF_CORE_H

#include <linux/poll.h>
#include <linux/videodev2.h>

#define UNSET (-1U)


struct videobuf_buffer;
struct videobuf_queue;



struct videobuf_mapping {
	unsigned int count;
	struct videobuf_queue *q;
};

enum videobuf_state {
	VIDEOBUF_NEEDS_INIT = 0,
	VIDEOBUF_PREPARED   = 1,
	VIDEOBUF_QUEUED     = 2,
	VIDEOBUF_ACTIVE     = 3,
	VIDEOBUF_DONE       = 4,
	VIDEOBUF_ERROR      = 5,
	VIDEOBUF_IDLE       = 6,
};

struct videobuf_buffer {
	unsigned int            i;
	u32                     magic;

	
	unsigned int            width;
	unsigned int            height;
	unsigned int            bytesperline; 
	unsigned long           size;
	unsigned int            input;
	enum v4l2_field         field;
	enum videobuf_state     state;
	struct list_head        stream;  

	
	struct list_head        queue;
	wait_queue_head_t       done;
	unsigned int            field_count;
	struct timeval          ts;

	
	enum v4l2_memory        memory;

	
	size_t                  bsize;

	
	size_t                  boff;

	
	unsigned long           baddr;

	
	struct videobuf_mapping *map;

	
	int			privsize;
	void                    *priv;
};

struct videobuf_queue_ops {
	int (*buf_setup)(struct videobuf_queue *q,
			 unsigned int *count, unsigned int *size);
	int (*buf_prepare)(struct videobuf_queue *q,
			   struct videobuf_buffer *vb,
			   enum v4l2_field field);
	void (*buf_queue)(struct videobuf_queue *q,
			  struct videobuf_buffer *vb);
	void (*buf_release)(struct videobuf_queue *q,
			    struct videobuf_buffer *vb);
};

#define MAGIC_QTYPE_OPS	0x12261003

struct videobuf_qtype_ops {
	u32                     magic;

	struct videobuf_buffer *(*alloc_vb)(size_t size);
	void *(*vaddr)		(struct videobuf_buffer *buf);
	int (*iolock)		(struct videobuf_queue *q,
				 struct videobuf_buffer *vb,
				 struct v4l2_framebuffer *fbuf);
	int (*sync)		(struct videobuf_queue *q,
				 struct videobuf_buffer *buf);
	int (*mmap_mapper)	(struct videobuf_queue *q,
				 struct videobuf_buffer *buf,
				 struct vm_area_struct *vma);
};

struct videobuf_queue {
	struct mutex               vb_lock;
	struct mutex               *ext_lock;
	spinlock_t                 *irqlock;
	struct device		   *dev;

	wait_queue_head_t	   wait; 

	enum v4l2_buf_type         type;
	unsigned int               inputs; 
	unsigned int               msize;
	enum v4l2_field            field;
	enum v4l2_field            last;   
	struct videobuf_buffer     *bufs[VIDEO_MAX_FRAME];
	const struct videobuf_queue_ops  *ops;
	struct videobuf_qtype_ops  *int_ops;

	unsigned int               streaming:1;
	unsigned int               reading:1;

	
	struct list_head           stream;

	
	unsigned int               read_off;
	struct videobuf_buffer     *read_buf;

	
	void                       *priv_data;
};

static inline void videobuf_queue_lock(struct videobuf_queue *q)
{
	if (!q->ext_lock)
		mutex_lock(&q->vb_lock);
}

static inline void videobuf_queue_unlock(struct videobuf_queue *q)
{
	if (!q->ext_lock)
		mutex_unlock(&q->vb_lock);
}

int videobuf_waiton(struct videobuf_queue *q, struct videobuf_buffer *vb,
		int non_blocking, int intr);
int videobuf_iolock(struct videobuf_queue *q, struct videobuf_buffer *vb,
		struct v4l2_framebuffer *fbuf);

struct videobuf_buffer *videobuf_alloc_vb(struct videobuf_queue *q);

void *videobuf_queue_to_vaddr(struct videobuf_queue *q,
			      struct videobuf_buffer *buf);

void videobuf_queue_core_init(struct videobuf_queue *q,
			 const struct videobuf_queue_ops *ops,
			 struct device *dev,
			 spinlock_t *irqlock,
			 enum v4l2_buf_type type,
			 enum v4l2_field field,
			 unsigned int msize,
			 void *priv,
			 struct videobuf_qtype_ops *int_ops,
			 struct mutex *ext_lock);
int  videobuf_queue_is_busy(struct videobuf_queue *q);
void videobuf_queue_cancel(struct videobuf_queue *q);

enum v4l2_field videobuf_next_field(struct videobuf_queue *q);
int videobuf_reqbufs(struct videobuf_queue *q,
		     struct v4l2_requestbuffers *req);
int videobuf_querybuf(struct videobuf_queue *q, struct v4l2_buffer *b);
int videobuf_qbuf(struct videobuf_queue *q,
		  struct v4l2_buffer *b);
int videobuf_dqbuf(struct videobuf_queue *q,
		   struct v4l2_buffer *b, int nonblocking);
int videobuf_streamon(struct videobuf_queue *q);
int videobuf_streamoff(struct videobuf_queue *q);

void videobuf_stop(struct videobuf_queue *q);

int videobuf_read_start(struct videobuf_queue *q);
void videobuf_read_stop(struct videobuf_queue *q);
ssize_t videobuf_read_stream(struct videobuf_queue *q,
			     char __user *data, size_t count, loff_t *ppos,
			     int vbihack, int nonblocking);
ssize_t videobuf_read_one(struct videobuf_queue *q,
			  char __user *data, size_t count, loff_t *ppos,
			  int nonblocking);
unsigned int videobuf_poll_stream(struct file *file,
				  struct videobuf_queue *q,
				  poll_table *wait);

int videobuf_mmap_setup(struct videobuf_queue *q,
			unsigned int bcount, unsigned int bsize,
			enum v4l2_memory memory);
int __videobuf_mmap_setup(struct videobuf_queue *q,
			unsigned int bcount, unsigned int bsize,
			enum v4l2_memory memory);
int videobuf_mmap_free(struct videobuf_queue *q);
int videobuf_mmap_mapper(struct videobuf_queue *q,
			 struct vm_area_struct *vma);

#endif
