## lsirec - LSI SAS2008/2108 HBA low-level recovery tool for Linux

Currently supports reading and writing the SBR and booting the card in host
boot mode.

Use [lsiutil](https://github.com/exactassembly/meta-xa-stm/blob/master/recipes-support/lsiutil/files/)
to crossflash between IT/IR firmwares from Linux, without vendor/product ID
restrictions.

## Quick guide to cleanly crossflash between IT/IR firmwares

`# lsiutil -e`

Select your adapter.

`46.  Upload FLASH section` → `5.  Complete (all sections)`

Make a complete Flash backup to be safe.

`67.  Dump all port state`

`68.  Show port state summary`

Copy and paste these somewhere safe. Take special note of the SAS WWID.

`33.  Erase non-volatile adapter storage` → `3.  FLASH`, then also
`8.  Persistent manufacturing config pages`

Wipe the whole Flash. This will take a while. Option number 3 excludes the
manufacturing config pages, so you need both.

`2.  Download firmware (update the FLASH)`

Flash the new firmware. Optionally, use
`4.  Download/erase BIOS and/or FCode (update the FLASH)` to flash the BIOS/EFI
module (not necessary if you're not booting from the adapter).

Exit lsiutil.

`# ./lsirec 0000:01:00.0 readsbr sbr_backup.bin`

Where 0000:01:00.0 is your PCI device ID.

`# python3 sbrtool.py parse sbr_backup.bin sbr.cfg`

Edit sbr.cfg with your favorite text editor. You may want to add
`SASAddr = 0xYOUR_SAS_WWID` to make the SAS WWID persist in the SBR (I'm not
sure which firmwares use this, but I've seen it in some SBRs). You may want to
change the Subsystem VID/PID, or use another SBR as a template.

`# python3 sbrtool.py build sbr.cfg sbr_new.bin`

`# ./lsirec 0000:01:00.0 writesbr sbr_new.bin`

Reboot and cross your fingers.

When the system comes back up, if all went well, launch `lsiutil -e` again and
use `18.  Change SAS WWID` to update the WWID if necessary, then reboot again
(this writes it to the config section in Flash, not to the SBR).

*NEW*: instead of rebooting, you can use:

`# ./lsirec 0000:01:00.0 reset`

`# ./lsirec 0000:01:00.0 rescan`

Make sure your disks are not in use if you do this. `reset` might fail if you
have just flashed a new firmware. This is normal, as the adapter takes a while
to copy the firmware to the backup area on first boot. Wait a few seconds and
use `# ./lsirec 0000:01:00.0 info` until it returns `IOC is READY`.

## UNTESTED procedure to convert from MegaRAID to IT/IR firmware or recover a bricked card

WARNING: this is completely untested. Host boot support has only been tested
so far on a card that was already IT/IR. Please report back if you try this.
This process initially boots the IT/IR firwmare without touching Flash, and I'm
not sure if it might balk at whatever MegaRAID stuff was left there before we
have a chance to wipe it.

This procedure (obviously) resets the adapter, so make sure your disks are not
in use and any dm/md/lvm mappings have been removed!

This mode requires HugeTLB support enabled in your kernel:

`# echo 16 > /proc/sys/vm/nr_hugepages`

This process is also incompatible with IOMMUs. If you have one, make sure it
is not active (e.g. check that `/sys/kernel/iommu_groups` is an empty
directory).

Make note of your SAS WWID (e.g. using MegaCLI or the kernel interfaces).

`# ./lsirec 0000:01:00.0 unbind`

Where 0000:01:00.0 is your PCI device ID. Unbind the kernel driver (if any).

`# ./lsirec 0000:01:00.0 halt`

Halt the IOP, so that the firmware will not interfere with subsequent
operations.

`# ./lsirec 0000:01:00.0 readsbr sbr_backup.bin`

Back up your MegaRAID SBR.

`# python3 sbrtool.py parse sbr_backup.bin sbr.cfg`

Edit sbr.cfg with your favorite text editor. You'll probably want to use an IT
SBR as a template, such as
[sbr_sas9211-8i_itir.cfg](sample_sbr/sbr_sas9211-8i_itir.cfg). At the very
least you need to set `PCIPID` properly (`0x0072` for SAS2008-based cards) and
set `Interface` to `0x00` for IT/IR mode. You may want to add
`SASAddr = 0xYOUR_SAS_WWID` to make the SAS WWID persist in the SBR (I'm not
sure which firmwares use this, but I've seen it in some SBRs).

`# python3 sbrtool.py build sbr.cfg sbr_new.bin`

`# ./lsirec 0000:01:00.0 writesbr sbr_new.bin`

Write the new IT/IR mode SBR. This does not immediately take effect.

`# ./lsirec 0000:01:00.0 hostboot 2118it.bin`

Where 2118it.bin is your desired firmware. If all went well, you should see
something like this:

```
# ./lsirec 0000:01:00.0 hostboot 2118it.bin
Device in MPT mode
Resetting adapter in HCB mode...
Trying unlock in MPT mode...
Device in MPT mode
IOC is RESET
Setting up HCB...
HCDW virtual: 0x7fca79e00000
HCDW physical: 0x439a00000
Loading firmware...
Loaded 722708 bytes
Booting IOC...
IOC is READY
IOC Host Boot successful.
```

At this point the PCI VID/PID should've changed, but the kernel will not have
noticed. Check with lspci:

`# lspci -vns 0000:01:00.0 -A linux-sysfs | head -n 2`

`# lspci -vns 0000:01:00.0 -A intel-conf1 | head -n 2`

The first command should still show the old VID/PID, but the second one should
show the new (MPT mode) ones. To make the kernel notice:

`# ./lsirec 0000:01:00.0 rescan`

This removes the PCI device from the kernel and requests a rescan. At this point
the mpt3sas kernel driver should load. Check `dmesg` for any errors.

If all went well, you can use lsiutil to wipe Flash and flash your new firmware:

`# lsiutil -e`

Select your adapter.

`46.  Upload FLASH section` → `5.  Complete (all sections)`

Make a complete Flash backup to be safe.

`33.  Erase non-volatile adapter storage` → `3.  FLASH`, then also
`8.  Persistent manufacturing config pages`

Wipe the whole Flash. This will take a while. Option number 3 excludes the
manufacturing config pages, so you need both.

`2.  Download firmware (update the FLASH)`

Flash the new firmware. Optionally, use
`4.  Download/erase BIOS and/or FCode (update the FLASH)` to flash the BIOS/EFI
module (not necessary if you're not booting from the adapter).

Exit lsiutil.

Finally, if all went well, reset into normal mode:

`# ./lsirec 0000:01:00.0 reset`

This might complain about IOC not becoming ready, but this is normal, as the
first boot takes longer. Use `./lsirec 0000:01:00.0 info` after a few seconds
and check for `IOC is READY`.

`# ./lsirec 0000:01:00.0 rescan`

If all went well, `dmesg` should show the driver loading again successfully.
Launch `lsiutil -e` again and use `18.  Change SAS WWID` to update the WWID
if necessary, then `reset` and `rescan` again to make sure the kernel sees the
new WWID.

Enjoy your shiny new IT/IR-mode HBA.

## Disclaimer

This has barely been tested a couple of cards. Don't blame me if this bricks or
smokes your HBA.

DO NOT attempt to use this tool on non-SAS2x08 chipsets. It probably won't work
and may do horrible things. This tool deliberately does not check the VID/PID
so it can be used on cards with wacky SBRs, but that means it will happily
try to write the SBR into any random PCI device too.

I have tested this on an LSI SAS2108-based MegaRAID card (Fujitsu D2616) with
MegaRAID firmware and it successfully backed up the SBR, but the action
triggered a PCI error in syslog (though the controller kept working). Your
mileage may vary. I have not yet tried crossflashing it live to IT/IR mode.

## License

2-clause BSD. See the LICENSE file.
