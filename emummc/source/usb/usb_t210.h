/*
 * Tegra X1 XUSB host / pad control register definitions
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

#ifndef _USB_T210_H_
#define _USB_T210_H_

#include "../utils/types.h"

/* XUSB host FPCI config space (XUSB_HOST_BASE + 0x8000). */
#define XUSB_CFG_1                 0x4
#define  CFG_1_IO_SPACE            BIT(0)
#define  CFG_1_MEMORY_SPACE        BIT(1)
#define  CFG_1_BUS_MASTER          BIT(2)
#define XUSB_CFG_4                 0x10
#define  CFG_4_ADDRESS_TYPE_32_BIT (0 << 1)
#define  CFG_4_ADDRESS_TYPE_64_BIT (2 << 1)

/* XUSB host IPFS (XUSB_HOST_BASE + 0x9000). */
#define XUSB_HOST_CONFIGURATION    0x180
#define  HOST_CONFIGURATION_EN_FPCI BIT(0)

/* XUSB pad control. */
#define XUSB_PADCTL_USB2_PAD_MUX 0x4
#define  PADCTL_USB2_PAD_MUX_USB2_OTG_PAD_PORT0_USB2 (0 << 0)
#define  PADCTL_USB2_PAD_MUX_USB2_OTG_PAD_PORT0_XUSB (1 << 0)
#define  PADCTL_USB2_PAD_MUX_USB2_OTG_PAD_PORT0_MASK (3 << 0)
#define  PADCTL_USB2_PAD_MUX_USB2_BIAS_PAD_USB2      (0 << 18)
#define  PADCTL_USB2_PAD_MUX_USB2_BIAS_PAD_XUSB      (1 << 18)
#define  PADCTL_USB2_PAD_MUX_USB2_BIAS_PAD_MASK      (3 << 18)

#define XUSB_PADCTL_USB2_PORT_CAP 0x8
#define  PADCTL_USB2_PORT_CAP_PORT_0_CAP_DIS  (0 << 0)
#define  PADCTL_USB2_PORT_CAP_PORT_0_CAP_HOST (1 << 0)
#define  PADCTL_USB2_PORT_CAP_PORT_0_CAP_DEV  (2 << 0)
#define  PADCTL_USB2_PORT_CAP_PORT_0_CAP_OTG  (3 << 0)
#define  PADCTL_USB2_PORT_CAP_PORT_0_CAP_MASK (3 << 0)

#define XUSB_PADCTL_SS_PORT_MAP 0x14
#define  PADCTL_SS_PORT_MAP_PORT0_MASK (0xF << 0)

#define XUSB_PADCTL_USB2_BATTERY_CHRG_OTGPAD0_CTL0 0x80
#define XUSB_PADCTL_USB2_BATTERY_CHRG_OTGPAD0_CTL1 0x84
#define XUSB_PADCTL_USB2_OTG_PAD0_CTL_0 0x88
#define XUSB_PADCTL_USB2_OTG_PAD0_CTL_1 0x8C
#define XUSB_PADCTL_USB2_BIAS_PAD_CTL_0 0x284
#define XUSB_PADCTL_USB2_BIAS_PAD_CTL_1 0x288

#define XUSB_PADCTL_USB2_VBUS_ID 0xC60
#define  PADCTL_USB2_VBUS_ID_VBUS_OVR_EN   (1 << 12)
#define  PADCTL_USB2_VBUS_ID_VBUS_OVR_MASK (3 << 12)
#define  PADCTL_USB2_VBUS_ID_VBUS_ON       BIT(14)
#define  PADCTL_USB2_VBUS_ID_SRC_ID_OVR_EN (1 << 16)
#define  PADCTL_USB2_VBUS_ID_SRC_MASK      (3 << 16)
#define  PADCTL_USB2_VBUS_ID_OVR_GND       (0 << 18)
#define  PADCTL_USB2_VBUS_ID_OVR_FLOAT     (8 << 18)
#define  PADCTL_USB2_VBUS_ID_OVR_MASK      (0xF << 18)

#endif /* _USB_T210_H_ */
