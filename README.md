## lsirec - LSI SAS2008 HBA low-level recovery tool for Linux

Currently supports reading and writing the SBR. In the future it may support
cold-booting the adapter to recover bricked adapters or crossflash between
firmwares directly from Linux.

Use [lsiutil](https://github.com/exactassembly/meta-xa-stm/blob/master/recipes-support/lsiutil/files/)
to crossflash between IT/IR firmwares from Linux, without vendor/product ID
restrictions.

## Quick guide to cleanly crossflash between IT/IR firmwares

`# lsiutil -e`

Select your adapter.

`46.  Upload FLASH section` → `5.  Complete (all sections)`

Make a complete Flash backup to be safe.

```
67.  Dump all port state
68.  Show port state summary
```

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
`SASAddr = 0xYOUR_SAS_WWID` to make the SAS WWID is persist in the SBR (I'm not
sure how/when this is used, but I've seen it in some SBRs). You may want to
change the Subsystem VID/PID, or use another SBR as a template.

```
# python3 sbrtool.py build sbr.cfg sbr_new.bin
# ./lsirec 0000:01:00.0 writesbr sbr_new.bin
```

Reboot and cross your fingers.

When the system comes back up, if all went well, launch `lsiutil -e` again and
use `18.  Change SAS WWID` to update the WWID if necessary, then reboot again
(this writes it to the config section in Flash, not to the SBR).

## Disclaimer

This has barely been tested on one card. Don't blame me if this bricks or
smokes your HBA. MegaRAID mode has not been tested yet, and you'll still need
to flash from DOS/UEFI for now since lsiutil does not work with non-operational
adapters under Linux.

DO NOT attempt to use this tool on non-SAS2008 chipsets. It probably won't work
and may do horrible things. This tool deliberately does not check the VID/PID
so it can be used on cards with wacky SBRs, but that means it will happily
try to write the SBR into any random PCI device too.

## License

2-clause BSD. See the LICENSE file.
