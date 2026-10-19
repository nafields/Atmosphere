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

/*
 * Glue between FS's eMMC accessor (emummc.c) and the XHCI / mass storage
 * drivers (usbh.c, usbh_msc.c):
 *
 *  - DMA memory.  Mesosphere keeps XUSB_HOST behind the SMMU, so the block
 *    the controller works on (rings, scratchpad, firmware image, BOT
 *    staging) is mapped into a device address space attached to
 *    DeviceName_XUSB_HOST.  Device addresses are 32-bit and congruent to
 *    the process addresses modulo 4 MiB, as svcMapDeviceAddressSpaceAligned
 *    requires.
 *  - Firmware.  The Falcon image is read from the SD card with emummc's own
 *    SD driver and FatFS, once, into the DMA block.
 *  - VBUS policy.  5V is sourced only when the charger reports no external
 *    VBUS (handheld with an OTG adapter), never against a dock or charger.
 *  - Lifetime.  FS closes the eMMC controller on sleep and shutdown; the
 *    host is stopped then and brought back up on the next access.
 */

#include <string.h>

#include "usb_emummc.h"
#include "usbh.h"
#include "usbh_msc.h"

#include "../emuMMC/emummc.h"
#include "../libs/fatfs/ff.h"
#include "../nx/cache.h"
#include "../nx/svc.h"
#include "../soc/clock.h"
#include "../soc/gpio.h"
#include "../soc/i2c.h"
#include "../soc/pinmux.h"
#include "../soc/pmc.h"
#include "../soc/t210.h"
#include "../utils/fatal.h"
#include "../utils/util.h"

#define USB_EMUMMC_FW_PATH     "atmosphere/xusb_firmware.bin"
#define USB_EMUMMC_FW_PATH_ALT "bootloader/sys/xusbfw.bin"

/* How long to wait for the drive to show up, per attempt. */
#define USB_EMUMMC_CONNECT_TIMEOUT_MS 10000
#define USB_EMUMMC_INIT_ATTEMPTS      3

/* BQ24193 charger: VBUS / power good status. */
#define BQ24193_I2C_ADDR          0x6B
#define BQ24193_REG_STATUS        0x08
#define BQ24193_STATUS_PG_MASK    (1 << 2)
#define BQ24193_STATUS_VBUS_MASK  (3 << 6)

/*
 * DMA block layout (offsets from the 64 KiB aligned block):
 *   0x00000  rings and contexts (usbh_rings_t, < 64 KiB)
 *   0x10000  BOT staging: CBW/CSW page, data at +0x10000 (64 KiB aligned)
 *   0x60000  XUSB Falcon firmware image (resident)
 *   0x88000  xHCI scratchpad pointer array (one page)
 *   0x89000  xHCI scratchpad pages
 */
#define POOL_RINGS_OFF     0
#define POOL_STAGING_OFF   0x10000
#define POOL_FW_OFF        (POOL_STAGING_OFF + USBH_MSC_STAGING_SZ)
#define POOL_FW_SZ         0x28000
#define POOL_SP_ARRAY_OFF  (POOL_FW_OFF + POOL_FW_SZ)
#define POOL_SP_PAGES_OFF  (POOL_SP_ARRAY_OFF + 0x1000)
#define POOL_SP_PAGES      64
#define POOL_SIZE          (POOL_SP_PAGES_OFF + POOL_SP_PAGES * 0x1000)

_Static_assert(sizeof(usbh_rings_t) <= POOL_STAGING_OFF, "rings must fit below the staging area");
_Static_assert((POOL_STAGING_OFF % 0x10000) == 0, "staging must be 64 KiB aligned");
_Static_assert((POOL_SP_ARRAY_OFF % 0x1000) == 0 && (POOL_SIZE % 0x1000) == 0, "page alignment");

/* Device address window: 32-bit, well away from the SMMU's identity ranges. */
#define DMA_DEV_WINDOW_BASE 0x80000000ull
#define DMA_DEV_WINDOW_SIZE 0x01000000ull

static u8 g_dma_pool[POOL_SIZE] __attribute__((aligned(0x10000)));

static Handle g_das = INVALID_HANDLE;
static u32    g_dma_dev_base;
static bool   g_dma_mapped;
static u32    g_fw_size;
static bool   g_i2c1_ready;
static bool   g_ready;

/* ---------- DMA services for the drivers --------------------------------- */

u32 usbh_dma_addr(const void *p)
{
	uintptr_t off = (uintptr_t)p - (uintptr_t)g_dma_pool;
	if (off >= POOL_SIZE)
		fatal_abort(Fatal_UsbDma);
	return g_dma_dev_base + (u32)off;
}

void usbh_dma_clean(const void *p, u32 len)
{
	armDCacheClean((void *)p, len);
}

void usbh_dma_inval(void *p, u32 len)
{
	/* Clean + invalidate: never discard a dirty line, never keep a stale one. */
	armDCacheFlush(p, len);
}

static void _dma_map(void)
{
	if (g_dma_mapped)
		return;

	/* Congruent modulo 4 MiB with the process address (MapAligned rule). */
	g_dma_dev_base = (u32)DMA_DEV_WINDOW_BASE | ((u32)(uintptr_t)g_dma_pool & 0x3FFFFF);

	if (svcCreateDeviceAddressSpace(&g_das, DMA_DEV_WINDOW_BASE, DMA_DEV_WINDOW_SIZE) != 0)
		fatal_abort(Fatal_UsbDma);

	/* Claims the XUSB host for this process. Fails with Busy if the usb
	 * sysmodule already owns it, which means it was not kept off XUSB. */
	if (svcAttachDeviceAddressSpace(DeviceName_XUSB_HOST, g_das) != 0)
		fatal_abort(Fatal_UsbDma);

	if (svcMapDeviceAddressSpaceAligned(g_das, CUR_PROCESS_HANDLE, (u64)(uintptr_t)g_dma_pool,
	                                    POOL_SIZE, g_dma_dev_base, 3 /* ReadWrite */) != 0)
		fatal_abort(Fatal_UsbDma);

	g_dma_mapped = true;
}

/* ---------- board: VBUS ---------------------------------------------------- */

static bool _is_erista(void)
{
	return ((APB_MISC(APB_MISC_GP_HIDREV) >> 4) & 0xF) < GP_HIDREV_MAJOR_T210B01;
}

/* External VBUS (dock, charger) on the connector, per the charger IC. */
static bool _vbus_present(void)
{
	u8 status = 0;

	if (!g_i2c1_ready)
	{
		clock_enable_i2c1();
		i2c_init_bus(I2C_1);
		g_i2c1_ready = true;
	}

	/* Unknown is treated as present: never drive 5V against an unknown source. */
	if (!i2c_recv_buf_small(&status, 1, I2C_1, BQ24193_I2C_ADDR, BQ24193_REG_STATUS))
		return true;

	return (status & (BQ24193_STATUS_VBUS_MASK | BQ24193_STATUS_PG_MASK)) != 0;
}

/*
 * Source 5V on VBUS for a bus powered drive on an OTG adapter.  Mirrors
 * hekate's regulator_5v: the 5V regulator enable on GPIO PA5 (also feeds the
 * fan and Joy-Con rails) and, on Erista only, the USB VBUS enable on GPIO
 * PCC4.  Mariko has no VBUS source rail of its own.
 */
void usbh_board_vbus_source(bool enable)
{
	bool erista = _is_erista();

	if (enable)
	{
		PINMUX_AUX(PINMUX_AUX_SATA_LED_ACTIVE) = 1;
		gpio_config(GPIO_PORT_A, GPIO_PIN_5, GPIO_MODE_GPIO);
		gpio_output_enable(GPIO_PORT_A, GPIO_PIN_5, GPIO_OUTPUT_ENABLE);
		gpio_write(GPIO_PORT_A, GPIO_PIN_5, GPIO_HIGH);

		if (erista)
		{
			PINMUX_AUX(PINMUX_AUX_USB_VBUS_EN0) = PINMUX_LPDR | 1;
			gpio_config(GPIO_PORT_CC, GPIO_PIN_4, GPIO_MODE_GPIO);
			gpio_output_enable(GPIO_PORT_CC, GPIO_PIN_4, GPIO_OUTPUT_ENABLE);
			gpio_write(GPIO_PORT_CC, GPIO_PIN_4, GPIO_LOW);
		}

		/* GPIO IO power on, pads at 1.8V. */
		pmc_write(APBDEV_PMC_NO_IOPOWER, 0, PMC_NO_IOPOWER_GPIO);
		(void)pmc_read(APBDEV_PMC_NO_IOPOWER);
		pmc_write(APBDEV_PMC_PWR_DET_VAL, 0, PMC_PWR_DET_33V_GPIO);
		(void)pmc_read(APBDEV_PMC_PWR_DET_VAL);

		if (erista)
			gpio_write(GPIO_PORT_CC, GPIO_PIN_4, GPIO_HIGH);
	}
	else
	{
		if (erista)
			gpio_write(GPIO_PORT_CC, GPIO_PIN_4, GPIO_LOW);
		gpio_write(GPIO_PORT_A, GPIO_PIN_5, GPIO_LOW);
	}
}

/* ---------- firmware --------------------------------------------------- */

extern file_based_ctxt f_emu;

/* Read the XUSB Falcon firmware from the SD card into the DMA block. */
static u32 _load_firmware(void)
{
	FIL  fp;
	UINT br = 0;
	u32  size = 0;

	/* emummc's SD driver must be up for its FatFS to read the card. */
	sdmmc_initialize();

	if (f_mount(&f_emu.sd_fs, "", 1) != FR_OK)
		fatal_abort(Fatal_UsbFirmware);

	FRESULT res = f_open(&fp, USB_EMUMMC_FW_PATH, FA_READ);
	if (res != FR_OK)
		res = f_open(&fp, USB_EMUMMC_FW_PATH_ALT, FA_READ);
	if (res != FR_OK)
		fatal_abort(Fatal_UsbFirmware);

	size = (u32)f_size(&fp);
	if (size <= 0x100 || size > POOL_FW_SZ ||
	    f_read(&fp, g_dma_pool + POOL_FW_OFF, size, &br) != FR_OK || br != size)
	{
		fatal_abort(Fatal_UsbFirmware);
	}

	f_close(&fp);
	f_mount(NULL, "", 1);

	return size;
}

/* ---------- lifetime --------------------------------------------------- */

bool usb_emummc_is_ready(void)
{
	return g_ready && usbh_is_alive() && usbh_msc_get()->ready;
}

bool usb_emummc_firmware_loaded(void)
{
	return g_fw_size != 0;
}

u32 usb_emummc_get_sector_count(void)
{
	return g_ready ? usbh_msc_get_sector_count() : 0;
}

bool usb_emummc_initialize(void)
{
	if (usb_emummc_is_ready())
		return true;
	g_ready = false;

	_dma_map();

	if (!g_fw_size)
		g_fw_size = _load_firmware();

	/* A stale controller (after sleep, or a failed attempt) is torn down first. */
	if (usbh_is_initialized())
		usbh_deinit();

	usbh_rings_t *rings    = (usbh_rings_t *)(g_dma_pool + POOL_RINGS_OFF);
	void         *staging  = g_dma_pool + POOL_STAGING_OFF;
	void         *sp_array = g_dma_pool + POOL_SP_ARRAY_OFF;
	void         *sp_pages = g_dma_pool + POOL_SP_PAGES_OFF;
	const void   *fw       = g_dma_pool + POOL_FW_OFF;

	int  res = USB_ERROR_INIT;
	bool msc_failed = false;
	for (u32 attempt = 0; attempt < USB_EMUMMC_INIT_ATTEMPTS; attempt++)
	{
		if (attempt)
		{
			usbh_deinit();
			svcSleepThread(500000000ull); /* 500 ms */
		}

		bool external = _vbus_present();
		res = usbh_init(rings, sp_array, sp_pages, POOL_SP_PAGES, fw, g_fw_size,
		                USB_EMUMMC_CONNECT_TIMEOUT_MS, !external);
		msc_failed = false;
		if (!res)
		{
			res = usbh_msc_init(staging);
			msc_failed = (res != 0);
		}
		if (!res)
			break;
	}

	if (res)
	{
		usbh_deinit();
		if (msc_failed)
			fatal_abort(Fatal_UsbDrive);
		fatal_abort(res == USB_ERROR_TIMEOUT ? Fatal_UsbNoDrive : Fatal_UsbHost);
	}

	/* The whole image (BOOT0, BOOT1 and some GPP) must be on the drive. */
	u64 start = emuMMC_ctx.EMMC_StoragePartitionOffset;
	if (!start || start + 2 * BOOT_PARTITION_SIZE >= usbh_msc_get_sector_count())
		fatal_abort(Fatal_UsbDrive);

	g_ready = true;
	return true;
}

void usb_emummc_finalize(void)
{
	if (usbh_is_initialized())
		usbh_deinit();
	g_ready = false;
	/* Sleep may follow: the partition state is verified on the next access. */
	usbh_recheck_power();
}

/* ---------- transfers -------------------------------------------------- */

static int _rw(u32 sector, u32 num_sectors, void *buf, bool write)
{
	if (!usb_emummc_initialize())
		return 0;

	int res = write ? usbh_msc_write(sector, num_sectors, buf)
	                : usbh_msc_read(sector, num_sectors, buf);
	if (!res)
		return 1;

	/* The drive reset or the bus glitched: bring it back once and retry. */
	g_ready = false;
	if (!usb_emummc_initialize())
		return 0;

	res = write ? usbh_msc_write(sector, num_sectors, buf)
	            : usbh_msc_read(sector, num_sectors, buf);
	return res ? 0 : 1;
}

int usb_emummc_read(u32 sector, u32 num_sectors, void *buf)
{
	return _rw(sector, num_sectors, buf, false);
}

int usb_emummc_write(u32 sector, u32 num_sectors, const void *buf)
{
	return _rw(sector, num_sectors, (void *)buf, true);
}
