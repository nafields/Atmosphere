Patch sets for the `usb` sysmodule on USB emuMMC boots.

Each subdirectory is a patch set: `<patchset name>/<usb main NSO build id>.ips`, applied by
loader from `/atmosphere/usb_emummc_patches/` only when the active emuMMC is on a USB
drive. `make dist` copies every subdirectory here into the release zip. No patch set has
been authored and verified on hardware yet; `utilities/usb_emummc_patches/` has the tool
and the procedure.

## usb_xusb_off

| Build id | Firmware | Status |
| --- | --- | --- |
| `760E48C572F8192C9BC6DC8FEB3D6213F0551505` | 23.0.0 | **experimental, untested**: stubs the two functions `usb_emummc_patch.py find` reports as the XUSB host (`.text+0x49b00`) and device (`.text+0x2f4d0`) register-level routines with `mov w0, #0; ret`. Not confirmed to be the outermost start-up routines; expect to iterate on hardware. |
