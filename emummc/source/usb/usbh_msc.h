/*
 * USB Host Mass Storage Class driver (Bulk-Only Transport), emummc port
 *
 * Copyright (c) 2024-2025 Hekate contributors
 * Copyright (c) 2025 Atmosphere-NX
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _USBH_MSC_H_
#define _USBH_MSC_H_

#include "../utils/types.h"

#define USBH_MSC_SECTOR_SZ 512

/* Staging area layout inside the DMA block. Every BOT phase goes through it
 * so that FS buffers (not device mapped) can be passed to read/write. The
 * area is 64 KiB aligned so that no 64 KiB TRB crosses a 64 KiB boundary.
 *   +0:                CBW (31 bytes)
 *   +64:               CSW (13 bytes)
 *   +0x10000:          data (USBH_MSC_STAGING_DATA_SZ bytes)
 */
#define USBH_MSC_STAGING_DATA_OFF 0x10000
#define USBH_MSC_STAGING_DATA_SZ  0x40000  /* 256 KiB per BOT command. */
#define USBH_MSC_STAGING_SZ       (USBH_MSC_STAGING_DATA_OFF + USBH_MSC_STAGING_DATA_SZ)

typedef struct {
	u32  num_sectors;      /* Capacity in 512-byte sectors (clamped to 32 bit). */
	bool ready;
	bool removable;        /* INQUIRY RMB bit.                                   */
	bool capacity_clamped; /* Drive is larger than 2 TiB; only the first 2 TiB are usable. */
	char vendor[9];        /* INQUIRY T10 vendor identification.                 */
	char product[17];      /* INQUIRY product identification.                    */
	char revision[5];      /* INQUIRY product revision level.                    */
	u8   sense_key;        /* Last REQUEST SENSE key/ASC/ASCQ (diagnostics).     */
	u8   sense_asc;
	u8   sense_ascq;
} usbh_msc_t;

usbh_msc_t *usbh_msc_get(void);

/* Bring up LUN 0 of the enumerated device: TEST UNIT READY (with spin up),
 * INQUIRY and READ CAPACITY. staging: USBH_MSC_STAGING_SZ bytes inside the
 * DMA block, 64 KiB aligned. Returns 0 on success. */
int  usbh_msc_init(void *staging);

/* Transfer count 512-byte sectors. buf may be any CPU memory. Return 0 on success. */
int  usbh_msc_read(u32 sector, u32 count, void *buf);
int  usbh_msc_write(u32 sector, u32 count, const void *buf);

u32  usbh_msc_get_sector_count(void);

#endif /* _USBH_MSC_H_ */
