# usb sysmodule patches for USB emuMMC boots

On a USB emuMMC boot (see `docs/features/usb_emummc.md`) the `emummc` module drives the
XUSB host controller from inside `FS`. The `usb` sysmodule (`0100000000000006`) normally
resets that controller and takes it over when it starts, which ends the boot in a fatal
error. loader therefore applies IPS patches to `usb`'s main NSO from

```
/atmosphere/usb_emummc_patches/<patchset name>/<nso build id>.ips
```

on USB emuMMC boots only. One patch is needed per Horizon version (per build id of the
`usb` main NSO), and it has to be authored from that version's NSO. Nintendo's NSOs are
not distributed with Atmosphère, so no patches are shipped in this repository until
they have been authored and tried on hardware; `usb_emummc_patch.py` is the tool for
authoring them.

## Getting the NSO

Extract the `usb` program NCA of the firmware you run from your own console (it is in the
`SYSTEM` partition's `Contents/registered/`), then its exefs:

```
hactool -k prod.keys -t nca --exefsdir usb_exefs <usb program nca>
```

`usb_exefs/main` is the NSO. `usb_emummc_patch.py info usb_exefs/main` prints its build
id and segment layout.

## Finding what to patch

```
usb_emummc_patch.py decompress usb_exefs/main -o usb_main_plain.nso
usb_emummc_patch.py find usb_exefs/main
```

`decompress` writes the NSO uncompressed in the layout loader patches against (segment
file offset = `0x100` + memory offset), ready for a disassembler loaded at address 0 (the
ReSwitched IDA loaders or Ghidra's NSO loader handle it directly).

`find` lists every place the code builds an address in the XUSB register blocks
(`XUSB_HOST 0x70090000`, its FPCI/IPFS windows, `XUSB_PADCTL 0x7009F000`,
`XUSB_DEV 0x700D0000`) with `MOVZ`/`MOVK`, the enclosing function, and every data word
holding such an address (the IO mapping tables the sysmodule passes to
`svcQueryIoMapping`). Those functions are the register-level drivers; follow their
callers up to the host (`usb:hs`) and device (`usb:ds`) controller initialisation that
the sysmodule runs at start, before it registers its services. The functions to stub
are the outermost ones whose callers accept a success return without any hardware
having been touched: the controller initialisation must not run, but the service
threads, `usb:pd` (USB-PD / charging through the BM92T36) and `usb:pm` must keep running.
The clocks and resets for the XUSB blocks go through `pcv`, so a stubbed function must
also be above those calls.

## Writing the patch

```
usb_emummc_patch.py make usb_exefs/main -o usb_xusb_off --stub 0x12340 --stub 0x15678
```

writes `usb_xusb_off/<build id>.ips` replacing the first two instructions of each listed
function (a `.text` offset) with `mov w0, #0; ret`. `--stub <offset>=<value>` returns
another value, `--patch <offset>:<hex bytes>` writes arbitrary bytes. The tool refuses an
offset that does not start with a function prologue unless `--force` is given, and only
patches `.text`.

Check the result with `usb_emummc_patch.py apply usb_exefs/main usb_xusb_off/<id>.ips -o
patched.nso` and a disassembler, then copy the directory to
`/atmosphere/usb_emummc_patches/usb_xusb_off/` on the SD card. The file name is the
build id with trailing zero bytes trimmed, as for `exefs_patches`; patches are only read
on a USB emuMMC boot, so the same SD card keeps a working USB connection on sysMMC and
SD card emuMMC boots.

Patch sets that have been verified on hardware belong in
`config_templates/usb_emummc_patches/<patchset name>/`, from where `make dist` copies
them into the release zip.

## Expected result

With the right functions stubbed, the console boots with `usb` running but never
touching the controller: no PC connection (MTP, USB homebrew tools) and no USB
peripherals in the dock, while charging, USB-PD, HDMI, Joy-Cons, Wi-Fi and Bluetooth are
unaffected. If `usb` still aborts, the stubbed functions are not the outermost ones, or
a caller checks for state the stub did not set up; if the emuMMC stops responding after
boot, something below the stubs still resets the XUSB blocks or the pads.
