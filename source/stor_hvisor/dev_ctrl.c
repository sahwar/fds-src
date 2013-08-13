/*
 *
 * Copyright (C) 2011 Citrix Systems Inc.
 *
 * This file is part of Blktap2.
 *
 * Blktap2 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * Blktap2 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License version 2 for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with Blktap2.  If not, see 
 * <http://www.gnu.org/licenses/>.
 *
 *
 */

#include <net/sock.h>
#include <linux/net.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/slab.h>
#include <linux/cdrom.h>
#include <linux/hdreg.h>
#include <linux/log2.h>
//#include <scsi/scsi.h>
//#include <scsi/scsi_ioctl.h>
// #include "vvclib.h"
// #include "../include/fds_commons.h"
// #include "../include/fdsp.h"
#include "blktap.h"
#include "fbd.h"
#include "fds.h"


int blktap_device_major;

#define dev_to_blktap(_dev) container_of(_dev, struct fbd_device, device)


#if 0
static int
blktap_device_release(struct gendisk *disk, fmode_t mode)
{
	struct device *tapdev = disk->private_data;
	struct block_device *bdev = bdget_disk(disk, 0);
	struct blktap *tap = dev_to_blktap(tapdev);

	bdput(bdev);

	if (!bdev->bd_openers) {
		set_bit(BLKTAP_DEVICE_CLOSED, &tap->dev_inuse);
		blktap_ring_kick_user(tap);
	}

	return 0;
}

static int
blktap_device_getgeo(struct block_device *bd, struct hd_geometry *hg)
{
	/* We don't have real geometry info, but let's at least return
	   values consistent with the size of the device */
	sector_t nsect = get_capacity(bd->bd_disk);
	sector_t cylinders = nsect;

	hg->heads = 0xff;
	hg->sectors = 0x3f;
	sector_div(cylinders, hg->heads * hg->sectors);
	hg->cylinders = cylinders;
	if ((sector_t)(hg->cylinders + 1) * hg->heads * hg->sectors < nsect)
		hg->cylinders = 0xffff;
	return 0;
}
#endif

/* NB. __blktap holding the queue lock; blktap where unlocked */

static inline struct request*
__blktap_next_queued_rq(struct request_queue *q)
{
	return blk_peek_request(q);
}

static inline void
__blktap_dequeue_rq(struct request *rq)
{
	blk_start_request(rq);
}

/* NB. err == 0 indicates success, failures < 0 */

static inline void
__blktap_end_queued_rq(struct request *rq, int err)
{
	blk_start_request(rq);
	__blk_end_request(rq, err, blk_rq_bytes(rq));
}

static inline void
__blktap_end_rq(struct request *rq, int err)
{
	__blk_end_request(rq, err, blk_rq_bytes(rq));
}

static inline void
blktap_end_rq(struct request *rq, int err)
{
	struct request_queue *q = rq->q;

	spin_lock_irq(q->queue_lock);
	__blktap_end_rq(rq, err);
	spin_unlock_irq(q->queue_lock);
}

void
blktap_device_end_request(struct fbd_device *tap,
			  struct blktap_request *request,
			  int error)
{
	struct request *rq = request->rq;

	blktap_ring_unmap_request(tap, request);

	blktap_ring_free_request(tap, request);

	printk(	"end_request: op=%d error=%d bytes=%d\n",
		rq_data_dir(rq), error, blk_rq_bytes(rq));

	blktap_end_rq(rq, error);
}

int
blktap_device_make_request(struct fbd_device *tap, struct request *rq,
			   struct file *ring)
{
	struct gendisk *disk = &tap->disk;
	struct blktap_request *request;
	int nsegs;
	int err;

printk(" inside the device  make request  %p\n",tap);
	request = blktap_ring_make_request(tap);
	if (IS_ERR(request)) {
printk(" make ring request failed \n");
		err = PTR_ERR(request);
		request = NULL;

		if (err == -ENOSPC || err == -ENOMEM)
			goto stop;

		goto fail;
	}

	if (rq->cmd_type != REQ_TYPE_FS) {
		err = -EOPNOTSUPP;
		goto fail;
	}

	if (rq->cmd_flags & REQ_FLUSH) {
		request->operation = BLKTAP_OP_FLUSH;
		request->nr_pages  = 0;
		goto submit;
	}

	if (rq->cmd_flags & REQ_DISCARD) {
		request->operation = BLKTAP_OP_TRIM;
		request->nr_pages  = 0;
		goto submit;
	}
printk(" map the sg request \n");

	nsegs = blk_rq_map_sg(rq->q, rq, request->sg_table);

	if (rq_data_dir(rq) == WRITE)
		request->operation = BLKTAP_OP_WRITE;
	else
		request->operation = BLKTAP_OP_READ;

printk(" get the  page allocated  \n");
	err = blktap_request_get_pages(tap, request, nsegs);
	if (err)
		goto stop;

printk(" map the request to  ring \n");
	err = blktap_ring_map_request(tap, ring, request);
	if (err)
		goto fail;

submit:
	request->rq = rq;
	blktap_ring_submit_request(tap, request);

	return 0;

stop:
	tap->stats.st_oo_req++;
	err = -EBUSY;

_out:
	if (request)
		blktap_ring_free_request(tap, request);

	return err;
fail:
	// if (printk_ratelimit())
	//	dev_warn(disk_to_dev(disk),
	//		 "make request: %d, failing\n", err);
	printk("Make request failed with error %d\n", err);
	goto _out;
}

/*
 * called from tapdisk context
 */
void
blktap_device_run_queue(struct request_queue *q)
{
	struct request *req;
    struct fbd_device *fbd;
	int err;

	spin_lock_irq(q->queue_lock);
	
	while ((req = blk_fetch_request(q)) != NULL) {

//		printk( "%s: request %p: dequeued (flags=%x)\n",
//				req->rq_disk->disk_name, req, req->cmd_type);

		fbd = req->rq_disk->private_data;
		if (req->cmd_type != REQ_TYPE_FS) {
			printk (KERN_NOTICE " Non valid  command request is skipped \n");
			if(req)
			__blk_end_request_all(req, 0);
			continue;
		}

		if (fbd->filp == NULL)
		{
			printk (KERN_NOTICE "Error:  Ring is not open yet \n");
			if(req)
			__blk_end_request_all(req, 0);
			continue;
		}


		if ((rq_data_dir(req) == WRITE) &&  (fbd->flags & FBD_READ_ONLY))
		 {
			printk("Error: writting read only disk \n");
			if(req)
			__blk_end_request_all(req, -EROFS);
			continue;
         }

		 spin_unlock_irq(q->queue_lock);

		/*
		 * queue  the packets to the  waiting queue 		
		 */

		spin_lock_irq(&fbd->queue_lock);
		/* we can  maintain per dev queue  for high performance and consistency  */
printk(" device make request fbd: %p  flip:%p \n ", fbd,fbd->filp);
		err = blktap_device_make_request(fbd, req, fbd->filp);
		if (err)
		{
			 printk(" Ending the request \n");
			__blk_end_request_all(req, 0);
		}
	//	__blk_end_request_all(req, 0); /* for testing only */ 
        spin_unlock_irq(&fbd->queue_lock);

        wake_up(&fbd->waiting_wq);
		spin_lock_irq(q->queue_lock);

	}

	spin_unlock_irq(q->queue_lock);

}

void
blktap_device_do_request(struct request_queue *rq)
{
	struct fbd_device *tap = rq->queuedata;

printk("send a message to  user \n");
	blktap_ring_kick_user(tap);
}



static void
blktap_device_fail_queue(struct fbd_device *tap)
{
	struct request_queue *q = tap->disk->queue;

	spin_lock_irq(&tap->queue_lock);
	queue_flag_clear(QUEUE_FLAG_STOPPED, q);

	do {
		struct request *rq = __blktap_next_queued_rq(q);
		if (!rq)
			break;

		__blktap_end_queued_rq(rq, -EIO);
	} while (1);

	spin_unlock_irq(&tap->queue_lock);
}


size_t
blktap_device_debug(struct fbd_device *tap, char *buf, size_t size)
{
	struct gendisk *disk = tap->disk;
	struct request_queue *q;
	struct block_device *bdev;
	char *s = buf, *end = buf + size;

	if (!disk)
		return 0;

	q = disk->queue;

	s += snprintf(s, end - s,
		      "disk capacity:%llu sector size:%u\n",
		      (unsigned long long)get_capacity(disk),
		      queue_logical_block_size(q));

	s += snprintf(s, end - s,
		      "queue flags:%#lx stopped:%d\n",
		      q->queue_flags, blk_queue_stopped(q));

	bdev = bdget_disk(disk, 0);
	if (bdev) {
		s += snprintf(s, end - s,
			      "bdev openers:%d closed:%d\n",
			      bdev->bd_openers,
			      test_bit(BLKTAP_DEVICE_CLOSED, &tap->dev_inuse));
		bdput(bdev);
	}

	return s - buf;
}

int __init
blktap_device_init()
{
	int major;

	/* Dynamically allocate a major for this device */
	major = register_blkdev(0, "tapdev");
	if (major < 0) {
		BTERR("Couldn't register blktap device\n");
		return -ENOMEM;
	}

	blktap_device_major = major;
	printk("blktap device major %d\n", major);
printk(" registered tap device successfully \n");

	return 0;
}

void
blktap_device_exit(void)
{
	if (blktap_device_major)
		unregister_blkdev(blktap_device_major, "tapdev");
}
