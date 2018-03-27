#!/usr/bin/python3
import sys, struct

MFG_FIELDS = [
    ("Unk00", "I", "0x%08x"),
    ("Unk04", "I", "0x%08x"),
    ("Unk08", "I", "0x%08x"),
    ("PCIVID", "H", "0x%04x"),
    ("PCIPID", "H", "0x%04x"),
    ("Unk10", "H", "0x%04x"),
    ("HwConfig", "H", "0x%04x"),
    ("SubsysVID", "H", "0x%04x"),
    ("SubsysPID", "H", "0x%04x"),
    ("Unk18", "I", "0x%08x"),
    ("Unk1c", "I", "0x%08x"),
    ("Unk20", "I", "0x%08x"),
    ("Unk24", "I", "0x%08x"),
    ("Unk28", "I", "0x%08x"),
    ("Unk2c", "I", "0x%08x"),
    ("Unk30", "I", "0x%08x"),
    ("Unk34", "I", "0x%08x"),
    ("Unk38", "I", "0x%08x"),
    ("Unk3c", "I", "0x%08x"),
    ("Interface", "B", "0x%02x"),
    ("Unk41", "B", "0x%02x"),
    ("Unk42", "H", "0x%04x"),
    ("Unk44", "I", "0x%08x"),
    ("Unk48", "H", "0x%04x"),
    ("Unk4a", "B", "0x%02x"),
]
MFG_FORMAT = "<" + "".join(i[1] for i in MFG_FIELDS)
MFG_KEYS = {k: i for i, (k, _, _) in enumerate(MFG_FIELDS)}

def checksum(b):
    return (0x5b - sum(b)) & 0xff

def do_parse(fbin, fcfg):
    sbr = open(fbin, "rb").read()

    mfg = sbr[0:0x4c]
    mfg_2 = sbr[0x4c:0x98]

    if mfg != mfg_2:
        print("WARNING: Mfg data copies differ, using first")

    if mfg[-1] != checksum(mfg[:-1]):
        print("WARNING: Mfg data checksum error")

    fd = open(fcfg, "w")

    for (name, _, fmt), val in zip(MFG_FIELDS, struct.unpack(MFG_FORMAT, mfg[:-1])):
        fd.write("%s = %s\n" % (name, fmt % val))

    sas_addr = sbr[0xd8:0xe0]
    if sas_addr != b"\x00" * 8:
        if sbr[0xef] != checksum(sas_addr):
            print("WARNING: SAS address checksum error")
        fd.write("SASAddr = 0x%016x\n" % struct.unpack(">Q", sas_addr))

    fd.close()

def do_build(fcfg, fbin):
    fields = [0] * len(MFG_FIELDS)

    sas_addr = None

    for line in open(fcfg, "r"):
        line = line.strip()
        k, v = line.split("=", 1)
        k = k.strip()
        v = v.strip()
        if k == "SASAddr":
            sas_addr = struct.pack(">Q", int(v, 0))
        elif k in MFG_KEYS:
            fields[MFG_KEYS[k]] = int(v, 0)
        else:
            print("Unknown key %s" % k)
            sys.exit(1)

    mfg = struct.pack(MFG_FORMAT, *fields)
    mfg = mfg + bytes([checksum(mfg)])

    sbr = mfg + mfg + b"\x00" * 0x40

    if not sas_addr:
        sbr += b"\x00" * 0x18
    else:
        sbr += sas_addr + b"\x00" * 0xf + bytes([checksum(sas_addr)])

    sbr += b"\x00"* 16

    assert len(sbr) == 256
    with open(fbin, "wb") as fd:
        fd.write(sbr)

if __name__ == "__main__":
    if len(sys.argv) != 4 or sys.argv[1] not in ("parse", "build"):
        print("Usage:")
        print(" %s parse sbr.bin sbr.cfg" % sys.argv[0])
        print(" %s build sbr.cfg sbr.bin" % sys.argv[0])
        sys.exit(1)
    elif sys.argv[1] == "parse":
        do_parse(sys.argv[2], sys.argv[3])
    elif sys.argv[1] == "build":
        do_build(sys.argv[2], sys.argv[3])
