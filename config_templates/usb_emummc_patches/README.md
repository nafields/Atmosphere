Patch sets for the `usb` sysmodule on USB emuMMC boots.

Each subdirectory is a patch set: `<patchset name>/<usb main NSO build id>.ips`, applied by
loader from `/atmosphere/usb_emummc_patches/` only when the active emuMMC is on a USB
drive. `make dist` copies every subdirectory here into the release zip. No patch set has
been authored and verified on hardware yet; `utilities/usb_emummc_patches/` has the tool
and the procedure.
