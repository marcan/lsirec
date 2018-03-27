#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define MPI2_DOORBELL               0x00

#define MPI2_WRSEQ                  0x04

#define MPI2_DIAG                   0x08
#define MPI2_DIAG_WRITE_ENABLE      0x0080
#define MPI2_DIAG_RW_ENABLE         0x0010
#define MPI2_DIAG_RESET_ADAPTER     0x0004
#define MPI2_DIAG_HOLD_IOC_RESET    0x0002

#define MPI2_DIAG_RW_DATA           0x10
#define MPI2_DIAG_RW_ADDRESS_LOW    0x14
#define MPI2_DIAG_RW_ADDRESS_HIGH   0x18

#define MPI2_DCR_DATA               0x38
#define MPI2_DCR_ADDRESS            0x3c

#define MR_DIAG_RW_DATA             0x24
#define MR_DIAG_RW_ADDRESS_LOW      0x28
#define MR_DIAG_RW_ADDRESS_HIGH     0x2c
#define MR_DIAG                     0xf8
#define MR_WRSEQ                    0xfc

#define DCR_I2C_SELECT          0x307
#define DCR_SBR_CONFIG          0x340

#define CHIP_I2C_BASE           0xC2100000
#define CHIP_I2C_PINS           (CHIP_I2C_BASE + 0x20)
#define CHIP_I2C_SCL_RD         0x01
#define CHIP_I2C_SDA_RD         0x02
#define CHIP_I2C_SCL_DRV        0x04
#define CHIP_I2C_SDA_DRV        0x08
#define CHIP_I2C_RESET          (CHIP_I2C_BASE + 0x24)

#define EEPROM_TYPE_16BIT       0x01
#define EEPROM_TYPE_8BIT        0x02

typedef struct {
    void *bar1;

    uint8_t sbr_addr;
    int eep_type;

    uint32_t r_diag;
    uint32_t r_wrseq;
    uint32_t r_rw_data;
    uint32_t r_rw_addr_low;
    uint32_t r_rw_addr_high;
} lsi_dev_t;

static uint32_t read32(lsi_dev_t *d, uint32_t offset)
{
    return *(volatile uint32_t *)(d->bar1 + offset);
}

static void write32(lsi_dev_t *d, uint32_t offset, uint32_t data)
{
    *(volatile uint32_t *)(d->bar1 + offset) = data;
}

static uint32_t chip_read32(lsi_dev_t *d, uint32_t offset)
{
    write32(d, d->r_rw_addr_high, 0);
    write32(d, d->r_rw_addr_low, offset);
    return read32(d, d->r_rw_data);
}

static void chip_write32(lsi_dev_t *d, uint32_t offset, uint32_t data)
{
    write32(d, d->r_rw_addr_high, 0);
    write32(d, d->r_rw_addr_low, offset);
    write32(d, d->r_rw_data, data);
}

static uint32_t dcr_read32(lsi_dev_t *d, uint32_t offset)
{
    write32(d, MPI2_DCR_ADDRESS, offset);
    return read32(d, MPI2_DCR_DATA);
}

static void dcr_write32(lsi_dev_t *d, uint32_t offset, uint32_t data)
{
    write32(d, MPI2_DCR_ADDRESS, offset);
    write32(d, MPI2_DCR_DATA, data);
}

static void lsi_unlock(lsi_dev_t *d)
{
    write32(d, d->r_wrseq, 0x00);
    write32(d, d->r_wrseq, 0x04);
    write32(d, d->r_wrseq, 0x0b);
    write32(d, d->r_wrseq, 0x02);
    write32(d, d->r_wrseq, 0x07);
    write32(d, d->r_wrseq, 0x0d);
}

static int lsi_open(const char *pci_id, lsi_dev_t *d)
{
    char path[128];

    memset(d, 0, sizeof(*d));

    if (strlen(pci_id) > 16)
        return -1;

    sprintf(path, "/sys/bus/pci/devices/%s/resource1", pci_id);

    int fd = open(path, O_RDWR);
    if (fd < 0) {
        perror("open bar1");
        return fd;
    }

    d->bar1 = mmap(NULL, 0x1000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);

    if (d->bar1 == MAP_FAILED) {
        perror("mmap bar1");
        return -1;
    }

    close(fd);

    uint32_t val;

    d->r_diag = MPI2_DIAG;
    d->r_wrseq = MPI2_WRSEQ;
    d->r_rw_addr_high = MPI2_DIAG_RW_ADDRESS_HIGH;
    d->r_rw_addr_low = MPI2_DIAG_RW_ADDRESS_LOW;
    d->r_rw_data = MPI2_DIAG_RW_DATA;

    val = read32(d, d->r_diag);
    if (val & MPI2_DIAG_WRITE_ENABLE) {
        write32(d, d->r_diag, val | MPI2_DIAG_RW_ENABLE);
        printf("Device in MPT mode\n");
        return 0;
    }

    printf("Trying unlock in MPT mode...\n");
    lsi_unlock(d);

    val = read32(d, d->r_diag);
    if (val & MPI2_DIAG_WRITE_ENABLE) {
        write32(d, d->r_diag, val | MPI2_DIAG_RW_ENABLE);
        printf("Device in MPT mode\n");
        return 0;
    }

    d->r_diag = MR_DIAG;
    d->r_wrseq = MR_WRSEQ;
    d->r_rw_addr_high = MR_DIAG_RW_ADDRESS_HIGH;
    d->r_rw_addr_low = MR_DIAG_RW_ADDRESS_LOW;
    d->r_rw_data = MR_DIAG_RW_DATA;

    val = read32(d, d->r_diag);
    if (val & MPI2_DIAG_WRITE_ENABLE) {
        write32(d, d->r_diag, val | MPI2_DIAG_RW_ENABLE);
        printf("Device in MEGARAID mode\n");
        return 0;
    }

    printf("Trying unlock in MEGARAID mode...\n");
    lsi_unlock(d);

    val = read32(d, d->r_diag);
    if (val & MPI2_DIAG_WRITE_ENABLE) {
        write32(d, d->r_diag, val | MPI2_DIAG_RW_ENABLE);
        printf("Device in MEGARAID mode\n");
        return 0;
    }

    fprintf(stderr, "Failed to unlock device\n");

    return 0;
}

static void i2c_delay(lsi_dev_t *d)
{
    usleep(5);
}

static void set_sda(lsi_dev_t *d, int sda)
{
    uint32_t val = chip_read32(d, CHIP_I2C_PINS);
    if (sda)
        val &= ~CHIP_I2C_SDA_DRV;
    else
        val |= CHIP_I2C_SDA_DRV;
    chip_write32(d, CHIP_I2C_PINS, val);
}

static void set_scl(lsi_dev_t *d, int scl)
{
    uint32_t val = chip_read32(d, CHIP_I2C_PINS);
    if (scl)
        val &= ~CHIP_I2C_SCL_DRV;
    else
        val |= CHIP_I2C_SCL_DRV;
    chip_write32(d, CHIP_I2C_PINS, val);
}

static int get_sda(lsi_dev_t *d)
{
    return !!(chip_read32(d, CHIP_I2C_PINS) & CHIP_I2C_SDA_RD);
}

static int wait_scl(lsi_dev_t *d)
{
    for (int i = 0; i < 100; i++)
    {
        if (chip_read32(d, CHIP_I2C_PINS) & CHIP_I2C_SCL_RD)
            return 0;
        i2c_delay(d);
    }
    fprintf(stderr, "I2C: SCL timeout!\n");
    return -1;
}

void i2c_stop(lsi_dev_t *d)
{
    i2c_delay(d);
    set_sda(d, 0);
    i2c_delay(d);
    set_scl(d, 1);
    i2c_delay(d);
    set_sda(d, 1);
    i2c_delay(d);
}

void i2c_start(lsi_dev_t *d)
{
    i2c_delay(d);
    set_sda(d, 1);
    i2c_delay(d);
    set_scl(d, 1);
    i2c_delay(d);
    wait_scl(d);
    set_sda(d, 0);
    i2c_delay(d);
    set_scl(d, 0);
    i2c_delay(d);
}

void i2c_sendbit(lsi_dev_t *d, int bit)
{
    set_sda(d, bit);
    i2c_delay(d);
    set_scl(d, 1);
    wait_scl(d);
    i2c_delay(d);
    set_scl(d, 0);
    i2c_delay(d);
}

int i2c_getbit(lsi_dev_t *d)
{
    set_sda(d, 1);
    i2c_delay(d);
    set_scl(d, 1);
    wait_scl(d);
    i2c_delay(d);
    int val = get_sda(d);
    set_scl(d, 0);
    i2c_delay(d);
    return val;
}

void i2c_sendbyte(lsi_dev_t *d, uint8_t byte)
{
    for (int i = 0x80; i ; i >>= 1)
        i2c_sendbit(d, byte & i);
}

uint8_t i2c_getbyte(lsi_dev_t *d)
{
    uint8_t val = 0;
    for (int i = 0x80; i ; i >>= 1)
        if (i2c_getbit(d))
            val |= i;
    return val;
}

static int lsi_i2c_init(lsi_dev_t *d)
{
    uint32_t val;

    val = dcr_read32(d, DCR_SBR_CONFIG);
    if (val & 2) {
        d->sbr_addr = 0x54;
    } else {
        d->sbr_addr = 0x50;
    }
    printf("Using I2C address 0x%02x\n", d->sbr_addr);
    if (val & 8) {
        d->eep_type = EEPROM_TYPE_16BIT;
    } else {
        d->eep_type = EEPROM_TYPE_8BIT;
    }
    printf("Using EEPROM type %d\n", d->eep_type);

    chip_write32(d, CHIP_I2C_RESET, 1);
    do {
        i2c_delay(d);
    } while (chip_read32(d, CHIP_I2C_RESET) & 1);

    val = dcr_read32(d, DCR_I2C_SELECT);
    val |= 0x800000;
    dcr_write32(d, DCR_I2C_SELECT, val);

    // Make sure things are reset
    for (int i = 0; i < 9; i++)
        i2c_sendbit(d, 1);
    i2c_stop(d);
    i2c_start(d);
    i2c_stop(d);

    return 0;
}

static int lsi_i2c_read_sbr(lsi_dev_t *d, int offset, int len, uint8_t *buf)
{
    i2c_start(d);
    i2c_sendbyte(d, (d->sbr_addr << 1) | 0);
    if (i2c_getbit(d)) {
        fprintf(stderr, "SBR read failed: EEPROM did not ACK address W\n");
        return -1;
    }

    if (d->eep_type == EEPROM_TYPE_16BIT) {
        i2c_sendbyte(d, offset >> 8);
        if (i2c_getbit(d)) {
            fprintf(stderr, "SBR read failed: EEPROM did not ACK offset1\n");
            return -1;
        }
    }

    i2c_sendbyte(d, offset & 0xff);
    if (i2c_getbit(d)) {
        fprintf(stderr, "SBR read failed: EEPROM did not ACK offset0\n");
        return -1;
    }

    i2c_start(d);
    i2c_sendbyte(d, (d->sbr_addr << 1) | 1);
    if (i2c_getbit(d)) {
        fprintf(stderr, "SBR read failed: EEPROM did not ACK address R\n");
        return -1;
    }

    for (int i = 0; i < len; i++)
    {
        buf[i] = i2c_getbyte(d);
        i2c_sendbit(d, i == (len - 1));
    }

    i2c_stop(d);

    return 0;
}

static int lsi_i2c_write_sbr(lsi_dev_t *d, int offset, int len, uint8_t *buf)
{
    for (int i = offset; i < (len + offset); i++)
    {
        i2c_start(d);
        i2c_sendbyte(d, (d->sbr_addr << 1) | 0);
        if (i2c_getbit(d)) {
            fprintf(stderr, "SBR write failed: EEPROM did not ACK address W\n");
            return -1;
        }

        if (d->eep_type == EEPROM_TYPE_16BIT) {
            i2c_sendbyte(d, i >> 8);
            if (i2c_getbit(d)) {
                fprintf(stderr, "SBR write failed: EEPROM did not ACK offset1\n");
                return -1;
            }
        }

        i2c_sendbyte(d, i & 0xff);
        if (i2c_getbit(d)) {
            fprintf(stderr, "SBR write failed: EEPROM did not ACK offset0\n");
            return -1;
        }

        i2c_sendbyte(d, buf[i]);
        if (i2c_getbit(d)) {
            fprintf(stderr, "SBR write failed: EEPROM did not ACK data\n");
            return -1;
        }
        i2c_stop(d);

        usleep(5000);
    }


    return 0;
}

static int do_info(lsi_dev_t *d) {
    printf("Registers:\n");
    printf(" DOORBELL:       0x%08x\n", read32(d, MPI2_DOORBELL));
    printf(" DIAG:           0x%08x\n", read32(d, d->r_diag));
    printf(" DCR_I2C_SELECT: 0x%08x\n", dcr_read32(d, DCR_I2C_SELECT));
    printf(" DCR_SBR_SELECT: 0x%08x\n", dcr_read32(d, DCR_SBR_CONFIG));
    printf(" CHIP_I2C_PINS:  0x%08x\n", chip_read32(d, CHIP_I2C_PINS));
    return 0;
}

static int do_readsbr(lsi_dev_t *d, const char *filename) {
    uint8_t sbr[256];
    int ret;

    ret = lsi_i2c_init(d);
    if (ret < 0)
        return ret;

    printf("Reading SBR...\n");
    ret = lsi_i2c_read_sbr(d, 0, sizeof(sbr), sbr);
    if (ret < 0)
        return ret;

    int fd = open(filename, O_CREAT|O_WRONLY|O_TRUNC, 0666);
    ret = write(fd, sbr, sizeof(sbr));
    if (ret != sizeof(sbr)) {
        perror("write");
        return -1;
    }
    close(fd);
    printf("SBR saved to %s\n", filename);
    return 0;
}

static int do_writesbr(lsi_dev_t *d, const char *filename) {
    uint8_t sbr[256];
    int ret;

    ret = lsi_i2c_init(d);
    if (ret < 0)
        return ret;

    int fd = open(filename, O_RDONLY);
    ret = read(fd, sbr, sizeof(sbr));
    if (ret != sizeof(sbr)) {
        perror("read");
        return -1;
    }
    close(fd);

    printf("Writing SBR...\n");
    ret = lsi_i2c_write_sbr(d, 0, sizeof(sbr), sbr);
    if (ret < 0)
        return ret;
    printf("SBR written from %s\n", filename);

    return 0;
}

static void usage(char *argv0)
{
    fprintf(stderr, "Usage: %s <PCI ID> <operation> [args...]\n", argv0);
    fprintf(stderr, "\n");
    fprintf(stderr, "PCI ID example: 0000:01:00.0\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Supported operations:\n");
    fprintf(stderr, "  info\n");
    fprintf(stderr, "  readsbr <sbr.bin>\n");
    fprintf(stderr, "  writesbr <sbr.bin>\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Example: %s 0000:01:00.0 readsbr sbr.bin\n", argv0);
    fprintf(stderr, "\n");
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        usage(argv[0]);
        return 1;
    }

    lsi_dev_t dev;

    if (lsi_open(argv[1], &dev))
        return 1;

    if (!strcmp(argv[2], "info")) {
        return !!do_info(&dev);
    } else if (!strcmp(argv[2], "readsbr") && argc == 4) {
        return !!do_readsbr(&dev, argv[3]);
    } else if (!strcmp(argv[2], "writesbr") && argc == 4) {
        return !!do_writesbr(&dev, argv[3]);
    } else {
        usage(argv[0]);
        return 1;
    }
    return 0;
}
