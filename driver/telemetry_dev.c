/* SPDX-License-Identifier: GPL-2.0 */
/**
 * @file    telemetry_dev.c
 * @brief   Production-Grade Linux Character Device Driver for Industrial
 *          Telemetry Acquisition with kfifo, wait queues, and IOCTL.
 *
 * Exposes device node: /dev/telemetry_dev
 *
 * Key Capabilities:
 *  - Dynamic registration via Linux miscdevice subsystem
 *  - Mutex-synchronized circular buffering via struct kfifo
 *  - Blocking & non-blocking I/O using wait_queue_head_t
 *  - Custom hardware configuration interface via unlocked_ioctl
 *  - Thread-safe statistics tracking protected by struct mutex
 *  - Clean user-kernel boundary enforcement via copy_to_user / copy_from_user
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/kfifo.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <linux/slab.h>

#include "telemetry_ioctl.h"

#define DEV_NAME            "telemetry_dev"
#define FIFO_BUFFER_SIZE    1024  /* Must be a power of 2 for kfifo */

/**
 * struct telemetry_device - Driver private state container.
 * @fifo:       Kernel circular FIFO instance.
 * @lock:       Mutex protecting statistics and FIFO state transitions.
 * @read_wait:  Wait queue for blocking read operations.
 * @stats:      Runtime statistics tracking payload transfer and drops.
 * @misc:       Miscdevice registration descriptor.
 */
struct telemetry_device {
	struct kfifo fifo;
	struct mutex lock;
	wait_queue_head_t read_wait;
	struct telemetry_stats stats;
	struct miscdevice misc;
};

static struct telemetry_device telem_dev;

/* =========================================================================
 * File Operations: open & release
 * ========================================================================= */

static int telemetry_open(struct inode *inode, struct file *file)
{
	pr_info("Device opened by process %s [PID %d]\n",
		current->comm, current->pid);
	return 0;
}

static int telemetry_release(struct inode *inode, struct file *file)
{
	pr_info("Device closed by process %s [PID %d]\n",
		current->comm, current->pid);
	return 0;
}

/* =========================================================================
 * File Operations: read
 * ========================================================================= */

static ssize_t telemetry_read(struct file *file, char __user *buf,
			      size_t count, loff_t *ppos)
{
	unsigned int copied = 0;
	int ret;

	if (!buf || count == 0)
		return -EINVAL;

	/*
	 * Re-entrant read loop:
	 * Protects against multi-reader race conditions. If multiple threads
	 * wake up concurrently, the first acquires the lock and consumes bytes;
	 * subsequent threads will observe an empty FIFO and safely re-enter
	 * the wait queue instead of returning 0 (premature EOF).
	 */
	while (1) {
		if (mutex_lock_interruptible(&telem_dev.lock))
			return -ERESTARTSYS;

		if (!kfifo_is_empty(&telem_dev.fifo)) {
			/* Safely copy bytes from kernel kfifo directly to user space */
			ret = kfifo_to_user(&telem_dev.fifo, buf, count, &copied);
			if (ret == 0) {
				telem_dev.stats.bytes_read += copied;
				telem_dev.stats.fifo_used = kfifo_len(&telem_dev.fifo);
			}
			mutex_unlock(&telem_dev.lock);

			if (ret)
				return -EFAULT; /* User pointer memory fault */

			return copied;
		}

		/* FIFO is empty, release lock before deciding to sleep or return */
		mutex_unlock(&telem_dev.lock);

		if (file->f_flags & O_NONBLOCK)
			return -EAGAIN;

		pr_debug("FIFO empty. Process %d entering interruptible sleep\n",
			 current->pid);

		ret = wait_event_interruptible(telem_dev.read_wait,
					       !kfifo_is_empty(&telem_dev.fifo));
		if (ret)
			return -ERESTARTSYS; /* Interrupted by signal */
	}
}

/* =========================================================================
 * File Operations: write
 * ========================================================================= */

static ssize_t telemetry_write(struct file *file, const char __user *buf,
			       size_t count, loff_t *ppos)
{
	unsigned int copied = 0;
	int ret;

	if (!buf || count == 0)
		return -EINVAL;

	if (mutex_lock_interruptible(&telem_dev.lock))
		return -ERESTARTSYS;

	/* Safely stream bytes from user space buffer into kernel kfifo */
	ret = kfifo_from_user(&telem_dev.fifo, buf, count, &copied);

	if (ret == 0) {
		telem_dev.stats.bytes_written += copied;

		/* Track dropped bytes if user wrote more than available capacity */
		if (copied < count) {
			unsigned int dropped = count - copied;
			telem_dev.stats.dropped_bytes += dropped;
			pr_warn("FIFO full! %u bytes dropped\n", dropped);
		}

		telem_dev.stats.fifo_used = kfifo_len(&telem_dev.fifo);
	}

	mutex_unlock(&telem_dev.lock);

	if (ret)
		return -EFAULT;

	/* Wake up any sleeping read() tasks */
	wake_up_interruptible(&telem_dev.read_wait);

	return copied;
}

/* =========================================================================
 * File Operations: unlocked_ioctl
 * ========================================================================= */

static long telemetry_ioctl(struct file *file, unsigned int cmd,
			    unsigned long arg)
{
	int ret = 0;

	/* Guard: Verify magic number */
	if (_IOC_TYPE(cmd) != TELEMETRY_IOC_MAGIC) {
		pr_err("Invalid IOCTL magic: expected '%c', got 0x%02X\n",
		       TELEMETRY_IOC_MAGIC, _IOC_TYPE(cmd));
		return -ENOTTY;
	}

	/* Guard: Verify command sequence range */
	if (_IOC_NR(cmd) > TELEMETRY_IOC_MAXNR) {
		pr_err("Invalid IOCTL command number: %u\n", _IOC_NR(cmd));
		return -ENOTTY;
	}

	switch (cmd) {
	case TELEMETRY_IOC_RESET:
		if (mutex_lock_interruptible(&telem_dev.lock))
			return -ERESTARTSYS;

		kfifo_reset(&telem_dev.fifo);
		telem_dev.stats.bytes_written = 0;
		telem_dev.stats.bytes_read = 0;
		telem_dev.stats.dropped_bytes = 0;
		telem_dev.stats.fifo_used = 0;

		mutex_unlock(&telem_dev.lock);
		pr_info("Telemetry FIFO buffer and statistics reset via IOCTL\n");
		break;

	case TELEMETRY_IOC_GET_STATS: {
		struct telemetry_stats snapshot;

		if (mutex_lock_interruptible(&telem_dev.lock))
			return -ERESTARTSYS;

		telem_dev.stats.fifo_used = kfifo_len(&telem_dev.fifo);
		snapshot = telem_dev.stats;

		mutex_unlock(&telem_dev.lock);

		if (copy_to_user((void __user *)arg, &snapshot,
				 sizeof(struct telemetry_stats)))
			return -EFAULT;

		pr_debug("Exported telemetry stats to process %d\n", current->pid);
		break;
	}

	case TELEMETRY_IOC_SET_MODE: {
		uint32_t new_mode;

		if (copy_from_user(&new_mode, (void __user *)arg,
				   sizeof(new_mode)))
			return -EFAULT;

		if (new_mode > TELEMETRY_MODE_DIAGNOSTIC)
			return -EINVAL;

		if (mutex_lock_interruptible(&telem_dev.lock))
			return -ERESTARTSYS;

		telem_dev.stats.mode = new_mode;

		mutex_unlock(&telem_dev.lock);
		pr_info("Telemetry acquisition mode changed to: %u\n", new_mode);
		break;
	}

	default:
		return -ENOTTY;
	}

	return ret;
}

/* =========================================================================
 * VFS file_operations dispatch table
 * ========================================================================= */

static const struct file_operations telemetry_fops = {
	.owner          = THIS_MODULE,
	.open           = telemetry_open,
	.release        = telemetry_release,
	.read           = telemetry_read,
	.write          = telemetry_write,
	.unlocked_ioctl = telemetry_ioctl,
};

/* =========================================================================
 * Module Initialization & Teardown
 * ========================================================================= */

static int __init telemetry_init(void)
{
	int ret;

	pr_info("Initializing Industrial Telemetry Driver...\n");

	/* Initialize concurrency primitives */
	mutex_init(&telem_dev.lock);
	init_waitqueue_head(&telem_dev.read_wait);

	/* Allocate circular kfifo buffer */
	ret = kfifo_alloc(&telem_dev.fifo, FIFO_BUFFER_SIZE, GFP_KERNEL);
	if (ret) {
		pr_err("Failed to allocate kfifo buffer (size %d)\n",
		       FIFO_BUFFER_SIZE);
		return ret;
	}

	/* Initialize telemetry statistics */
	telem_dev.stats.bytes_written = 0;
	telem_dev.stats.bytes_read    = 0;
	telem_dev.stats.dropped_bytes = 0;
	telem_dev.stats.fifo_used     = 0;
	telem_dev.stats.fifo_total    = FIFO_BUFFER_SIZE;
	telem_dev.stats.mode          = TELEMETRY_MODE_NORMAL;

	/* Setup miscdevice descriptor */
	telem_dev.misc.minor = MISC_DYNAMIC_MINOR;
	telem_dev.misc.name  = DEV_NAME;
	telem_dev.misc.fops  = &telemetry_fops;
	telem_dev.misc.mode  = 0666; /* Read/write for all users */

	/* Register character device node /dev/telemetry_dev */
	ret = misc_register(&telem_dev.misc);
	if (ret) {
		pr_err("misc_register failed for %s (error %d)\n",
		       DEV_NAME, ret);
		kfifo_free(&telem_dev.fifo);
		return ret;
	}

	pr_info("Driver successfully registered! Node created at /dev/%s\n",
		DEV_NAME);
	pr_info("kfifo initialized with %u bytes capacity\n", FIFO_BUFFER_SIZE);

	return 0;
}

static void __exit telemetry_exit(void)
{
	pr_info("Unloading Industrial Telemetry Driver...\n");

	/* Deregister character device from VFS */
	misc_deregister(&telem_dev.misc);

	/* Free allocated kernel circular buffer */
	kfifo_free(&telem_dev.fifo);

	pr_info("Clean teardown complete. Device /dev/%s removed\n", DEV_NAME);
}

module_init(telemetry_init);
module_exit(telemetry_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Industrial Embedded Systems Engineer");
MODULE_DESCRIPTION("Linux Character Device Driver with kfifo & IOCTL for Telemetry");
MODULE_VERSION("1.0.0");
