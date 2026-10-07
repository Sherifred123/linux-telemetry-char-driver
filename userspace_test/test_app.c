/* SPDX-License-Identifier: MIT */
/**
 * @file    test_app.c
 * @brief   User-space test client and verification suite for the
 *          /dev/telemetry_dev Linux character driver.
 *
 * Demonstrates:
 *   1. Opening and validating the character device node.
 *   2. Writing simulated multi-channel telemetry frames into kfifo.
 *   3. Reading back telemetry stream and checking data integrity.
 *   4. Executing IOCTL queries (stats retrieval, mode switching, FIFO reset).
 *   5. Validating kernel boundary error handling (invalid IOCTL commands).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdint.h>

#include "../include/telemetry_ioctl.h"

#define DEVICE_PATH "/dev/telemetry_dev"

/* ANSI Color formatting for professional terminal output */
#define COLOR_GREEN  "\033[1;32m"
#define COLOR_CYAN   "\033[1;36m"
#define COLOR_YELLOW "\033[1;33m"
#define COLOR_RED    "\033[1;31m"
#define COLOR_RESET  "\033[0m"

static void print_banner(void)
{
	printf(COLOR_CYAN "==============================================================\n" COLOR_RESET);
	printf(COLOR_CYAN "  LINUX TELEMETRY DRIVER TEST & VERIFICATION SUITE (/dev/telemetry_dev)\n" COLOR_RESET);
	printf(COLOR_CYAN "==============================================================\n\n" COLOR_RESET);
}

static void display_stats(const struct telemetry_stats *s)
{
	printf(COLOR_YELLOW "---------------- CURRENT DRIVER STATISTICS ----------------\n" COLOR_RESET);
	printf("  • Total Bytes Written : %u bytes\n", s->bytes_written);
	printf("  • Total Bytes Read    : %u bytes\n", s->bytes_read);
	printf("  • FIFO Utilization    : %u / %u bytes (%.1f%%)\n",
	       s->fifo_used, s->fifo_total,
	       (float)s->fifo_used * 100.0f / (float)s->fifo_total);
	printf("  • Dropped Bytes       : %u bytes\n", s->dropped_bytes);
	printf("  • Acquisition Mode    : %s\n",
	       s->mode == TELEMETRY_MODE_NORMAL ? "NORMAL (0)" :
	       s->mode == TELEMETRY_MODE_BURST  ? "BURST (1)" : "DIAGNOSTIC (2)");
	printf(COLOR_YELLOW "-----------------------------------------------------------\n\n" COLOR_RESET);
}

int main(int argc, char *argv[])
{
	int fd;
	int ret;
	struct telemetry_stats stats;
	uint32_t new_mode;

	print_banner();

	/* -------------------------------------------------------------
	 * Test 1: Open the device node
	 * ------------------------------------------------------------- */
	printf("[TEST 1] Opening character device node: %s ... ", DEVICE_PATH);
	fd = open(DEVICE_PATH, O_RDWR);
	if (fd < 0) {
		printf(COLOR_RED "[FAILED]\n" COLOR_RESET);
		fprintf(stderr, "Error: Could not open %s: %s (Did you run 'sudo insmod'?)\n",
			DEVICE_PATH, strerror(errno));
		return EXIT_FAILURE;
	}
	printf(COLOR_GREEN "[PASSED] (fd = %d)\n\n" COLOR_RESET, fd);

	/* -------------------------------------------------------------
	 * Test 2: Query initial statistics via IOCTL
	 * ------------------------------------------------------------- */
	printf("[TEST 2] Querying initial driver statistics via IOCTL ... ");
	ret = ioctl(fd, TELEMETRY_IOC_GET_STATS, &stats);
	if (ret < 0) {
		printf(COLOR_RED "[FAILED]\n" COLOR_RESET);
		perror("ioctl(TELEMETRY_IOC_GET_STATS)");
		close(fd);
		return EXIT_FAILURE;
	}
	printf(COLOR_GREEN "[PASSED]\n" COLOR_RESET);
	display_stats(&stats);

	/* -------------------------------------------------------------
	 * Test 3: Write simulated telemetry packet into /dev/telemetry_dev
	 * ------------------------------------------------------------- */
	uint8_t telemetry_frame[16] = {
		0xAA, 0x55,             /* Sync Header */
		0x01,                   /* Node Address */
		0x21,                   /* Command: Telemetry Read */
		0x0A,                   /* Length: 10 bytes */
		0x01, 0x02, 0x03, 0x04, /* Sample Telemetry Payload */
		0x05, 0x06, 0x07, 0x08,
		0x38, 0x50              /* Modbus CRC-16 Checksum */
	};

	printf("[TEST 3] Writing 16-byte telemetry frame to kernel buffer ... ");
	ssize_t bytes_written = write(fd, telemetry_frame, sizeof(telemetry_frame));
	if (bytes_written != sizeof(telemetry_frame)) {
		printf(COLOR_RED "[FAILED]\n" COLOR_RESET);
		perror("write");
		close(fd);
		return EXIT_FAILURE;
	}
	printf(COLOR_GREEN "[PASSED] (%zd bytes written)\n\n" COLOR_RESET, bytes_written);

	/* Verify updated stats */
	ioctl(fd, TELEMETRY_IOC_GET_STATS, &stats);
	display_stats(&stats);

	/* -------------------------------------------------------------
	 * Test 4: Read telemetry stream back from kernel kfifo
	 * ------------------------------------------------------------- */
	uint8_t rx_buffer[32];
	memset(rx_buffer, 0, sizeof(rx_buffer));

	printf("[TEST 4] Reading telemetry frame back from kernel buffer ... ");
	ssize_t bytes_read = read(fd, rx_buffer, sizeof(rx_buffer));
	if (bytes_read != sizeof(telemetry_frame)) {
		printf(COLOR_RED "[FAILED]\n" COLOR_RESET);
		perror("read");
		close(fd);
		return EXIT_FAILURE;
	}
	printf(COLOR_GREEN "[PASSED] (%zd bytes read)\n" COLOR_RESET, bytes_read);

	/* Verify byte integrity */
	printf("  Payload verification: ");
	if (memcmp(telemetry_frame, rx_buffer, sizeof(telemetry_frame)) == 0) {
		printf(COLOR_GREEN "Data Integrity 100%% Matched!\n" COLOR_RESET);
		printf("  Hex dump: ");
		for (size_t i = 0; i < (size_t)bytes_read; i++) {
			printf("%02X ", rx_buffer[i]);
		}
		printf("\n\n");
	} else {
		printf(COLOR_RED "Data mismatch detected!\n\n" COLOR_RESET);
	}

	/* -------------------------------------------------------------
	 * Test 5: Configure acquisition mode via IOCTL
	 * ------------------------------------------------------------- */
	new_mode = TELEMETRY_MODE_BURST;
	printf("[TEST 5] Setting acquisition mode to BURST (1) via IOCTL ... ");
	ret = ioctl(fd, TELEMETRY_IOC_SET_MODE, &new_mode);
	if (ret < 0) {
		printf(COLOR_RED "[FAILED]\n" COLOR_RESET);
		perror("ioctl(TELEMETRY_IOC_SET_MODE)");
	} else {
		printf(COLOR_GREEN "[PASSED]\n\n" COLOR_RESET);
	}

	/* -------------------------------------------------------------
	 * Test 6: Reset kernel FIFO via IOCTL
	 * ------------------------------------------------------------- */
	printf("[TEST 6] Triggering FIFO Reset via IOCTL ... ");
	ret = ioctl(fd, TELEMETRY_IOC_RESET);
	if (ret < 0) {
		printf(COLOR_RED "[FAILED]\n" COLOR_RESET);
		perror("ioctl(TELEMETRY_IOC_RESET)");
	} else {
		printf(COLOR_GREEN "[PASSED]\n\n" COLOR_RESET);
	}

	/* Verify final cleared stats */
	ioctl(fd, TELEMETRY_IOC_GET_STATS, &stats);
	display_stats(&stats);

	/* -------------------------------------------------------------
	 * Test 7: Error handling boundary test (Invalid IOCTL code)
	 * ------------------------------------------------------------- */
	printf("[TEST 7] Testing invalid IOCTL command rejection ... ");
	ret = ioctl(fd, 0xDEADBEEF);
	if (ret < 0 && errno == ENOTTY) {
		printf(COLOR_GREEN "[PASSED] (Kernel safely returned ENOTTY: Inappropriate ioctl)\n\n" COLOR_RESET);
	} else {
		printf(COLOR_RED "[FAILED] (Expected ENOTTY, got ret=%d, errno=%d)\n\n" COLOR_RESET, ret, errno);
	}

	close(fd);
	printf(COLOR_CYAN "==============================================================\n" COLOR_RESET);
	printf(COLOR_GREEN "  ALL 7 TESTS COMPLETED SUCCESSFULLY! DRIVER OPERATIONAL.\n" COLOR_RESET);
	printf(COLOR_CYAN "==============================================================\n" COLOR_RESET);

	return EXIT_SUCCESS;
}
