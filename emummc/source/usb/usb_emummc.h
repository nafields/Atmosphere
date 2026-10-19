/*
 * USB mass storage backed emuMMC (emuMMC_USB_Raw)
 *
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

#ifndef __USB_EMUMMC_H__
#define __USB_EMUMMC_H__

#include "../utils/types.h"

/*
 * The raw emuMMC image lives on the first USB mass storage device reachable
 * from the console (OTG adapter, or the dock's hub), at
 * emuMMC_ctx.EMMC_StoragePartitionOffset (512-byte sectors), laid out like
 * a raw SD partition emuMMC: BOOT0, BOOT1 (0x2000 sectors each), then GPP.
 *
 * The XUSB host firmware is read from the SD card on first use
 * (atmosphere/xusb_firmware.bin, else bootloader/sys/xusbfw.bin) and stays
 * resident, so the host can be restarted after sleep.
 */

/* Bring the drive up if it is not already. Aborts with a Fatal_Usb* reason
 * when the drive cannot be used. Returns true when ready. */
bool usb_emummc_initialize(void);

/* Stop the host (FS closes the eMMC controller on sleep/shutdown). The next
 * access brings it back up. */
void usb_emummc_finalize(void);

/* Whether the drive is up right now (no side effects). */
bool usb_emummc_is_ready(void);

/* Whether the XUSB firmware has been read from the SD card. Until then the
 * SD card must stay with emummc's own driver. */
bool usb_emummc_firmware_loaded(void);

/* Capacity of the drive in 512-byte sectors (0 when not ready). */
u32  usb_emummc_get_sector_count(void);

/* Absolute drive sectors. Return 1 on success, 0 on failure (sdmmc_storage_* convention). */
int  usb_emummc_read(u32 sector, u32 num_sectors, void *buf);
int  usb_emummc_write(u32 sector, u32 num_sectors, const void *buf);

#endif /* __USB_EMUMMC_H__ */
