# USB emuMMC

Atmosphère can run from an emuMMC that is a raw image on a USB mass storage drive (SSD, HDD or flash drive) instead of the SD card. The drive is connected directly with a USB-C OTG adapter or through the dock's USB hub.

**This feature is experimental and has not been verified on hardware yet.** Read the limitations before trying it.

## How it works

Nintendo's filesystem services (`FS`) read the eMMC through the `emummc` module, which Atmosphère injects into `FS`. For a USB emuMMC the module carries its own XHCI host and USB mass storage (bulk-only transport) driver and reads the image straight from the drive, inside the `FS` process, long before any USB system module exists.

The image has the same layout as a raw SD card partition emuMMC, starting at a sector on the drive:

| Offset from the start sector | Content                            |
| ---------------------------- | ---------------------------------- |
| `0x0`                        | BOOT0 (`0x2000` sectors, 4 MiB)    |
| `0x2000`                     | BOOT1 (`0x2000` sectors, 4 MiB)    |
| `0x4000`                     | GPP (the raw eMMC user partition)  |

The bootloader describes the emuMMC to exosphère like a raw partition one, with type `3` instead of `1`:

| Field                        | Value                                                 |
| ---------------------------- | ----------------------------------------------------- |
| `base_cfg.type`              | `3` (`EmummcType_Usb`)                                 |
| `partition_cfg.start_sector` | LBA of BOOT0 on the USB drive, in 512-byte sectors     |
| `base_cfg.id`, `nintendo_path` | as for any other emuMMC                              |

The emuMMC `Nintendo` folder, the `atmosphere` folder and everything else Atmosphère reads from the SD card stay on the SD card. Only the eMMC image is on the drive.

## Requirements

- **hekate** with USB drive support (`usb_enabled=1` in `emuMMC/emummc.ini`, or an emuMMC folder with a `usb_based` file), which creates the image, reads package1 from it and passes type `3` to exosphère. hekate refuses to launch a USB emuMMC until `usb_atmosphere=1` is set in `emummc.ini`, which tells it that the installed Atmosphère has this backend. fusee cannot read the drive and refuses to boot a USB emuMMC.
- An `emummc.kipm` built from this version of the module (hekate injects the module's kernel capabilities from that file, and the USB backend needs the XUSB host, pad control and fuse register mappings added here).
- The Tegra210 XUSB host firmware on the SD card, as `atmosphere/xusb_firmware.bin` or hekate's `bootloader/sys/xusbfw.bin`. This is the stock L4T `tegra21x_xusb_firmware` file (also found as `nvidia/tegra210/xusb.bin` in linux-firmware). It is not distributed with Atmosphère. Images up to 160 KiB are accepted.
- A drive with 512-byte sectors. Only the first 2 TiB of larger drives are used. Drives work in USB 2.0 High Speed.
- The `usb` system module (`0100000000000006`) must be kept away from the XUSB controller: see below.

## Keeping the `usb` system module off the host controller

The console has one usable USB controller. On a normal boot the `usb` system module resets it and takes it over for USB device mode (connecting to a PC) and for USB host mode in the dock. On a USB emuMMC boot the `emummc` module owns the controller instead, and the kernel keeps the controller's DMA attached to the `FS` process. If `usb` then tries to take it, `usb` fails to start and the system shows a fatal error.

`usb` therefore needs executable patches that make its XUSB host and device bring-up return early. The patches are specific to each Horizon version and are **not included yet**. They are applied by `loader` from

```
/atmosphere/usb_emummc_patches/<patchset name>/<nso build id>.ips
```

and only on a USB emuMMC boot, so the same SD card still boots sysMMC and SD card emuMMCs with a working USB connection. The format and the build id rules are the same as for `/atmosphere/exefs_patches/` (see the [loader documentation](../components/modules/loader.md#nso-patching)).

With `usb` patched out of the controller, the console loses the PC connection (MTP, USB homebrew tools) and USB peripherals in the dock. Charging, USB-PD negotiation, HDMI output, Joy-Cons, Wi-Fi and Bluetooth are not affected.

## What the module does on a USB emuMMC boot

1. On the first eMMC access by `FS`, the module maps its DMA memory into a device address space attached to the XUSB host (mesosphère keeps that device behind the SMMU), initialises the SD card with its own driver and reads the XUSB firmware file into that memory.
2. It powers the XUSB partitions, programs the pads and clocks, loads the firmware into the controller's Falcon microcontroller and starts the xHCI host.
3. It waits up to 10 seconds (3 attempts) for a device on the root port. A hub (the dock) is enumerated one tier deep and every connected port is tried until a mass storage device is found. The drive is then identified, spun up if needed and its capacity checked against the image.
4. Reads and writes go through a 256 KiB staging buffer per bulk-only transport command. `FS` serialises eMMC access, so no extra locking is needed.
5. When `FS` closes the eMMC controller (sleep, shutdown) the host is stopped. The next access brings it back up, firmware included, so the console can resume from sleep as long as the drive is still there.

5V on VBUS is sourced only when the charger reports no external VBUS (handheld with an OTG adapter), never against a dock or charger. On Mariko the console has no VBUS source rail, so a bus powered drive on an OTG adapter needs external power there.

If the drive cannot be used the module reboots to the fatal error screen with one of these error descriptions:

| Code (`error_desc`) | Meaning                                                             |
| ------------------- | ------------------------------------------------------------------- |
| `16`                | XUSB firmware file missing, too large, or not a XUSB image          |
| `17`                | The DMA memory could not be attached to the XUSB host (the `usb` system module took the controller?) |
| `18`                | The host controller did not come up                                 |
| `19`                | No USB mass storage drive was found in time                         |
| `20`                | The drive is not ready, has unsupported sectors, or is too small for the image |

## Limitations

- Untested on hardware. The driver was ported from hekate's, which was itself specified and reviewed but not yet exercised on a console.
- No `usb` system module patches ship yet. Until they exist for your Horizon version, the boot ends in a fatal error when `usb` starts.
- USB 2.0 High Speed only.
- The module reserves about 800 KiB of memory in `FS` for the DMA block, on every boot, whichever emuMMC type is active.
- A drive that disappears while the console runs is fatal, like a removed eMMC would be.
