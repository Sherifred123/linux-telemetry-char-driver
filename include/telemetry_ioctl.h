/* SPDX-License-Identifier: GPL-2.0 */
/**
 * @file    telemetry_ioctl.h
 * @brief   IOCTL command definitions and shared structures for the
 *          Linux Telemetry Character Device Driver (/dev/telemetry_dev).
 *
 * This header is shared between kernel-space driver and user-space
 * application to maintain a unified binary interface (ABI).
 */

#ifndef _TELEMETRY_IOCTL_H_
#define _TELEMETRY_IOCTL_H_

#ifdef __KERNEL__
#include <linux/ioctl.h>
#include <linux/types.h>
#else
#include <sys/ioctl.h>
#include <stdint.h>
#endif

/*
 * Magic number chosen for Telemetry subsystem ('T' = 0x54).
 * Must be unique to prevent accidental execution of commands intended
 * for other drivers.
 */
#define TELEMETRY_IOC_MAGIC    'T'

/**
 * struct telemetry_stats - Telemetry driver runtime statistics.
 * @bytes_written:  Total bytes written from user space into kfifo.
 * @bytes_read:     Total bytes read from kfifo by user space.
 * @fifo_used:      Current number of unread bytes in kfifo.
 * @fifo_total:     Total allocated capacity of kfifo (power-of-2).
 * @dropped_bytes:  Bytes discarded due to FIFO buffer overflow.
 * @mode:           Current telemetry acquisition operating mode.
 */
struct telemetry_stats {
	uint32_t bytes_written;
	uint32_t bytes_read;
	uint32_t fifo_used;
	uint32_t fifo_total;
	uint32_t dropped_bytes;
	uint32_t mode;
};

/*
 * Supported telemetry operating modes
 */
#define TELEMETRY_MODE_NORMAL     0  /* Standard 10Hz sampling mode */
#define TELEMETRY_MODE_BURST      1  /* High-frequency burst sampling */
#define TELEMETRY_MODE_DIAGNOSTIC 2  /* Diagnostic loopback mode */

/*
 * IOCTL Command Definitions using standard Linux encoding:
 *   _IO   : Command with no data payload
 *   _IOR  : Driver reads data from hardware / kernel to user space
 *   _IOW  : User space writes configuration parameter to driver
 *   _IOWR : Bidirectional transfer
 */

/* Flush all telemetry bytes in kernel FIFO and reset statistics */
#define TELEMETRY_IOC_RESET         _IO(TELEMETRY_IOC_MAGIC, 0)

/* Retrieve runtime statistics struct from kernel */
#define TELEMETRY_IOC_GET_STATS     _IOR(TELEMETRY_IOC_MAGIC, 1, struct telemetry_stats)

/* Set operating acquisition mode (NORMAL, BURST, DIAGNOSTIC) */
#define TELEMETRY_IOC_SET_MODE      _IOW(TELEMETRY_IOC_MAGIC, 2, uint32_t)

/* Max sequential command index for validity checking */
#define TELEMETRY_IOC_MAXNR         2

#endif /* _TELEMETRY_IOCTL_H_ */
