/*
 * Copyright (C) 2025, Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*****************************************************************************
 * irq_shmem_demo.c - shared memory with IRQ demo
 * This demo will:
 *  1. Open the shared memory device.
 *  2. Open the IRQ device.
 *  3. Register IRQ interrupt handler.
 *  4. Write message to the shared memory.
 *  5. Kick IRQ to notify there is a message written to the shared memory
 *  6. Wait until the remote has kicked the IRQ to notify the remote
 *     has echoed back the message.
 *  7. Read the message from shared memory.
 *  8. Verify the message
 *  9. Repeat step 4 to 8 for 100 times.
 * 10. Clean up: deregister the IRQ interrupt handler, close the IRQ device
 *     , close the shared memory device.
 *
 * Here is the Shared memory structure of this demo:
 * |0x0   - 0x03        | number of APU to RPU buffers available to RPU |
 * |0x04  - 0x07        | number of APU to RPU buffers consumed by RPU |
 * |0x08  - 0x1FFC      | address array for shared buffers from APU to RPU |
 * |0x2000 - 0x2003     | number of RPU to APU buffers available to APU |
 * |0x2004 - 0x2007     | number of RPU to APU buffers consumed by APU |
 * |0x2008 - 0x3FFC     | address array for shared buffers from RPU to APU |
 * |0x04000 - 0x103FFC  | APU to RPU buffers |
 * |0x104000 - 0x203FFC | RPU to APU buffers |
 */

#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <metal/sys.h>
#include <metal/io.h>
#include <metal/alloc.h>
#include <sys/time.h>
#include <sys/types.h>
#include <metal/device.h>
#include <metal/irq.h>
#include <metal/errno.h>

#include "common.h"
#include "sys_init.h"

/* Shared memory offsets */
#define SHM_DESC_OFFSET_TX 0x0
#define SHM_BUFF_OFFSET_TX 0x04000
#define SHM_DESC_OFFSET_RX 0x02000
#define SHM_BUFF_OFFSET_RX 0x104000

/* Shared memory descriptors offset */
#define SHM_DESC_AVAIL_OFFSET 0x00
#define SHM_DESC_USED_OFFSET  0x04
#define SHM_DESC_ADDR_ARRAY_OFFSET 0x08

#define PKGS_TOTAL 1024

#define BUF_SIZE_MAX 512
#define SHUTDOWN "shutdown"

#define NS_PER_S  (1000 * 1000 * 1000)

struct msg_hdr_s {
	uint32_t index;
	uint32_t len;
};

/**
 * @brief get_timestamp() - Get the timestamp
 *        IT gets the timestamp and return nanoseconds.
 *
 * @return nano seconds.
 */
static unsigned long long get_timestamp(void)
{
	unsigned long long t = 0;
	struct timespec tp;
	int r;

	r = clock_gettime(CLOCK_MONOTONIC, &tp);
	if (r == -1) {
		LPERROR("Bad clock_gettime!\n");
		return t;
	}

	return tp.tv_sec * (NS_PER_S) + tp.tv_nsec;
}

/**
 * @brief   irq_shmem_echo() - shared memory IRQ demo
 *          This task will:
 *          * Get the timestamp and put it into the ping shared memory
 *          * Update the shared memory descriptor for the new available
 *            ping buffer.
 *          * Trigger IRQ to notifty the remote.
 *          * Repeat the above steps until it sends out all the packages.
 *          * Monitor IRQ interrupt, verify every received package.
 *          * After all the packages are received, it sends out shutdown
 *            message to the remote.
 *
 * @param[in] ch - communication channel used
 * @return - return 0 on success, otherwise return error number indicating
 *           type of error.
 */
static int irq_shmem_echo(struct channel_s *ch)
{
	int ret;
	uint32_t i;
	struct metal_io_region *shm_io = ch->shm_io;
	uint32_t rx_avail;
	unsigned long tx_avail_offset, rx_avail_offset;
	unsigned long rx_used_offset;
	unsigned long tx_addr_offset, rx_addr_offset;
	unsigned long tx_data_offset, rx_data_offset;
	unsigned long long tstart, tend;
	long long tdiff;
	long long tdiff_avg_s = 0, tdiff_avg_ns = 0;
	void *txbuf = NULL, *rxbuf = NULL, *tmpptr;
	struct msg_hdr_s *msg_hdr;
	uint32_t tx_phy_addr_32;

	txbuf = metal_allocate_memory(BUF_SIZE_MAX);
	if (!txbuf) {
		LPERROR("Failed to allocate local tx buffer for msg.\n");
		ret = -ENOMEM;
		goto out;
	}
	rxbuf = metal_allocate_memory(BUF_SIZE_MAX);
	if (!rxbuf) {
		LPERROR("Failed to allocate local rx buffer for msg.\n");
		ret = -ENOMEM;
		goto out;
	}

	/* Clear shared memory */
	metal_io_block_set(shm_io, 0, 0, metal_io_region_size(shm_io));

	/* Set tx/rx buffer address offset */
	tx_avail_offset = SHM_DESC_OFFSET_TX + SHM_DESC_AVAIL_OFFSET;
	rx_avail_offset = SHM_DESC_OFFSET_RX + SHM_DESC_AVAIL_OFFSET;
	rx_used_offset = SHM_DESC_OFFSET_RX + SHM_DESC_USED_OFFSET;
	tx_addr_offset = SHM_DESC_OFFSET_TX + SHM_DESC_ADDR_ARRAY_OFFSET;
	rx_addr_offset = SHM_DESC_OFFSET_RX + SHM_DESC_ADDR_ARRAY_OFFSET;
	tx_data_offset = SHM_DESC_OFFSET_TX + SHM_BUFF_OFFSET_TX;
	rx_data_offset = SHM_DESC_OFFSET_RX + SHM_BUFF_OFFSET_RX;

	LPRINTF("Start echo flood testing....\n");
	LPRINTF("Sending msgs to the remote.\n");

	for (i = 0; i < PKGS_TOTAL; i++) {

		/* Construct a message to send */
		tmpptr = txbuf;
		msg_hdr = tmpptr;
		msg_hdr->index = i;
		msg_hdr->len = sizeof(tstart);
		tmpptr += sizeof(struct msg_hdr_s);
		tstart = get_timestamp();
		*(unsigned long long *)tmpptr = tstart;

		/* copy message to shared buffer */
		metal_io_block_write(shm_io, tx_data_offset, msg_hdr,
			sizeof(struct msg_hdr_s) + msg_hdr->len);

		/* Write to the address array to tell the other end
		 * the buffer address.
		 */
		tx_phy_addr_32 = (uint32_t)metal_io_phys(shm_io,
					tx_data_offset);
		metal_io_write32(shm_io, tx_addr_offset, tx_phy_addr_32);
		tx_data_offset += sizeof(struct msg_hdr_s) + msg_hdr->len;
		tx_addr_offset += sizeof(uint32_t);

		/* Increase number of available buffers */
		metal_io_write32(shm_io, tx_avail_offset, (i + 1));
		/* Kick IRQ to notify data has been put to shared buffer */
		irq_kick(ch);
	}

	LPRINTF("Waiting for messages to echo back and verify.\n");
	i = 0;
	tx_data_offset = SHM_DESC_OFFSET_TX + SHM_BUFF_OFFSET_TX;
	while (i != PKGS_TOTAL) {
		wait_for_notified(&ch->remote_nkicked);
		rx_avail = metal_io_read32(shm_io, rx_avail_offset);
		while (i != rx_avail) {
			uint32_t rx_phy_addr_32;

			/* Received pong from the other side */

			/* Get the buffer location from the shared memory
			 * rx address array.
			 */
			rx_phy_addr_32 = metal_io_read32(shm_io,
					rx_addr_offset);
			rx_data_offset = metal_io_phys_to_offset(shm_io,
					(metal_phys_addr_t)rx_phy_addr_32);
			if (rx_data_offset == METAL_BAD_OFFSET) {
				LPERROR("failed to get rx [%d] offset: 0x%x.\n",
					i, rx_phy_addr_32);
				ret = -EINVAL;
				goto out;
			}
			rx_addr_offset += sizeof(rx_phy_addr_32);

			/* Read message header from shared memory */
			metal_io_block_read(shm_io, rx_data_offset, rxbuf,
				sizeof(struct msg_hdr_s));
			msg_hdr = (struct msg_hdr_s *)rxbuf;

			/* Check if the message header is valid */
			if (msg_hdr->index != (uint32_t)i) {
				LPERROR("wrong msg: expected: %d, actual: %d\n",
					i, msg_hdr->index);
				ret = -EINVAL;
				goto out;
			}
			if (msg_hdr->len != sizeof(tstart)) {
				LPERROR("wrong msg: length invalid: %lu, %u.\n",
					sizeof(tstart), msg_hdr->len);
				ret = -EINVAL;
				goto out;
			}
			/* Read message */
			rx_data_offset += sizeof(*msg_hdr);
			metal_io_block_read(shm_io,
					rx_data_offset,
					rxbuf + sizeof(*msg_hdr), msg_hdr->len);
			rx_data_offset += msg_hdr->len;
			/* increase rx used count to indicate it has consumed
			 * the received data */
			metal_io_write32(shm_io, rx_used_offset, (i + 1));

			/* Verify message */
			/* Get tx message previously sent*/
			metal_io_block_read(shm_io, tx_data_offset, txbuf,
					sizeof(*msg_hdr) + sizeof(tstart));
			tx_data_offset += sizeof(*msg_hdr) + sizeof(tstart);
			/* Compare the received message and the sent message */
			ret = memcmp(rxbuf, txbuf,
				sizeof(*msg_hdr) + sizeof(tstart));
			if (ret) {
				LPERROR("data[%u] verification failed.\n", i);
				LPRINTF("Expected:");
				dump_buffer(txbuf,
					sizeof(*msg_hdr) + sizeof(tstart));
				LPRINTF("Actual:");
				dump_buffer(rxbuf,
					sizeof(*msg_hdr) + sizeof(tstart));
				ret = -EINVAL;
				goto out;
			}

			i++;
		}
	}
	tend = get_timestamp();
	tdiff = tend - tstart;

	/* Send shutdown message */
	tmpptr = txbuf;
	msg_hdr = tmpptr;
	msg_hdr->index = i;
	msg_hdr->len = strlen(SHUTDOWN);
	tmpptr += sizeof(struct msg_hdr_s);
	sprintf(tmpptr, SHUTDOWN);
	/* copy message to shared buffer */
	metal_io_block_write(shm_io,
		tx_data_offset,
		msg_hdr,
		sizeof(struct msg_hdr_s) + msg_hdr->len);

	tx_phy_addr_32 = (uint32_t)metal_io_phys(shm_io,
					tx_data_offset);
	metal_io_write32(shm_io, tx_addr_offset, tx_phy_addr_32);
	metal_io_write32(shm_io, tx_avail_offset, PKGS_TOTAL + 1);
	LPRINTF("Kick remote to notify shutdown message sent...\n");
	irq_kick(ch);

	tdiff /= PKGS_TOTAL;
	tdiff_avg_s = tdiff / NS_PER_S;
	tdiff_avg_ns = tdiff % NS_PER_S;
	LPRINTF("Total packages: %d, time_avg = %lds, %ldns\n",
		i, (long int)tdiff_avg_s, (long int)tdiff_avg_ns);

	ret = 0;
out:
	if (txbuf)
		metal_free_memory(txbuf);
	if (rxbuf)
		metal_free_memory(rxbuf);
	return ret;
}

int main(void)
{
	struct channel_s ch_s;
	struct channel_s *ch = &ch_s;
	int ret = 0;

	/* sys_init will set the OS agnostic channel information */
	ret = sys_init(&ch_s);
	if (ret) {
		LPERROR("Failed to initialize system.\n");
		return ret;
	}

	print_demo("IRQ and shared memory");
	/* Run atomic operation demo */
	ret = irq_shmem_echo(ch);

	sys_cleanup(ch);

	return ret;

}

