#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/errno.h>
#include <sys/user.h>
#include <fcntl.h>
#include <unistd.h>

#define MPI2_DOORBELL               0x00

#define MPI2_DOORBELL_STATE_MASK    0xF0000000
#define MPI2_DOORBELL_READY         0x10000000
#define MPI2_DOORBELL_OPERATIONAL   0x20000000
#define MPI2_DOORBELL_FAULT         0x40000000

#define MPI2_WRSEQ                  0x04

#define MPI2_DIAG                   0x08
#define MPI2_DIAG_SBR_RELOAD        0x2000
#define MPI2_DIAG_BOOTDEVICE_MASK   0x1800
#define MPI2_DIAG_BOOTDEVICE_DEF    0x0000
#define MPI2_DIAG_BOOTDEVICE_HCDW   0x0800
#define MPI2_DIAG_CLR_FLASH_BAD_SIG 0x0400
#define MPI2_DIAG_FORCE_HCB         0x0200
#define MPI2_DIAG_HCB_MODE          0x0100
#define MPI2_DIAG_WRITE_ENABLE      0x0080
#define MPI2_DIAG_FLASH_BAD_SIG     0x0040
#define MPI2_DIAG_RESET_HISTORY     0x0020
#define MPI2_DIAG_RW_ENABLE         0x0010
#define MPI2_DIAG_RESET_ADAPTER     0x0004
#define MPI2_DIAG_HOLD_IOC_RESET    0x0002

#define MPI2_DIAG_RW_DATA           0x10
#define MPI2_DIAG_RW_ADDRESS_LOW    0x14
#define MPI2_DIAG_RW_ADDRESS_HIGH   0x18

#define MPI2_DCR_DATA               0x38
#define MPI2_DCR_ADDRESS            0x3c

#define MPI2_HCDW_SIZE              0x74
#define MPI2_HCDW_SIZE_SIZE_MASK    0xFFFFF000
#define MPI2_HCDW_SIZE_HCB_ENABLE   0x00000001

#define MPI2_HCDW_ADDR_LOW          0x78
#define MPI2_HCDW_ADDR_HIGH         0x7C

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

#define HCDW_SIZE 0x200000

typedef struct {
    char pci_id[16];

    void *bar1;

    void *hcdw;

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

static int lsi_reopen(lsi_dev_t *d)
{
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

    if (read32(d, MR_DIAG) & MPI2_DIAG_WRITE_ENABLE)
        goto megaraid;

    printf("Trying unlock in MPT mode...\n");
    lsi_unlock(d);

    val = read32(d, d->r_diag);
    if (val & MPI2_DIAG_WRITE_ENABLE) {
        write32(d, d->r_diag, val | MPI2_DIAG_RW_ENABLE);
        printf("Device in MPT mode\n");
        return 0;
    }

megaraid:
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

    return -1;
}

static int lsi_open(const char *pci_id, lsi_dev_t *d)
{
    char path[128];

    memset(d, 0, sizeof(*d));

    if (strlen(pci_id) > 15)
        return -1;

    strcpy(d->pci_id, pci_id);

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

    return lsi_reopen(d);
}

static int lsi_setup_hcdw(lsi_dev_t *d)
{
    char path[128];
    int fd;

    printf("Setting up HCB...\n");

    // Enable bus mastering
    sprintf(path, "/sys/bus/pci/devices/%s/config", d->pci_id);

    fd = open(path, O_RDWR);
    if (fd < 0) {
        perror("open config");
        return fd;
    }
    if (lseek(fd, 4, SEEK_SET) < 0) {
        perror("lseek cmd");
        return -1;
    }
    uint16_t cmd;
    if (read(fd, &cmd, 2) != 2) {
        perror("read cmd");
        return -1;
    }
    cmd |= 0x4; // bus master
    if (lseek(fd, 4, SEEK_SET) < 0) {
        perror("lseek cmd");
        return -1;
    }
    if (write(fd, &cmd, 2) != 2) {
        perror("write cmd");
        return -1;
    }
    close(fd);

    d->hcdw = mmap(NULL, HCDW_SIZE, PROT_READ|PROT_WRITE,
                   MAP_PRIVATE|MAP_ANONYMOUS|MAP_HUGETLB|MAP_LOCKED, 0, 0);

    if (d->hcdw == MAP_FAILED) {
        perror("mmap hcdw");
        fprintf(stderr, "Do you have hugepages enabled?\n");
        fprintf(stderr, "Try: echo 16 > /proc/sys/vm/nr_hugepages\n");
        return -1;
    }

    printf("HCDW virtual: %p\n", d->hcdw);

    fd = open("/proc/self/pagemap", O_RDONLY);
    if (fd < 0) {
        perror("open /proc/self/pagemap");
        return fd;
    }
    if (lseek(fd, ((uintptr_t)d->hcdw) >> (PAGE_SHIFT - 3), SEEK_SET) < 0) {
        perror("lseek /proc/self/pagemap");
        return -1;
    }
    uint64_t phys;
    if (read(fd, &phys, 8) != 8) {
        perror("read /proc/self/pagemap");
        return -1;
    }
    close(fd);

    phys = (phys & ((1ULL<<55) - 1)) << PAGE_SHIFT;

    printf("HCDW physical: 0x%lx\n", phys);

    write32(d, MPI2_HCDW_ADDR_LOW, phys & 0xffffffff);
    write32(d, MPI2_HCDW_ADDR_HIGH, phys >> 32);
    write32(d, MPI2_HCDW_SIZE, (0xfffff000 & ~(HCDW_SIZE-1)) | 1);

    return 0;
}

static int lsi_disable_hcdw(lsi_dev_t *d)
{
    write32(d, MPI2_HCDW_SIZE, 0);
    write32(d, MPI2_HCDW_ADDR_LOW, 0);
    write32(d, MPI2_HCDW_ADDR_HIGH, 0);

    int ret = munmap(d->hcdw, HCDW_SIZE);
    if (ret < 0) {
        perror("munmap hcdw");
        return ret;
    }

    return 0;
}

static int lsi_unbind_driver(lsi_dev_t *d)
{
    char path[128];
    int ret;

    sprintf(path, "/sys/bus/pci/devices/%s/driver/unbind", d->pci_id);

    int fd = open(path, O_WRONLY|O_TRUNC);
    if (fd < 0) {
        if (errno == ENOENT)
            return 0;
        perror("open unbind");
        return fd;
    }

    ret = write(fd, d->pci_id, strlen(d->pci_id));
    if (ret != strlen(d->pci_id)) {
        perror("write unbind");
        close(fd);
        return ret;
    }

    printf("Kernel driver unbound from device\n");

    close(fd);
    return 1;
}

static int lsi_rescan(lsi_dev_t *d)
{
    char path[128];
    int ret;

    printf("Removing PCI device...\n");

    sprintf(path, "/sys/bus/pci/devices/%s/remove", d->pci_id);

    int fd = open(path, O_WRONLY|O_TRUNC);
    if (fd < 0) {
        if (errno == ENOENT)
            return 0;
        perror("open remove");
        return fd;
    }

    ret = write(fd, "1", 1);
    if (ret != 1) {
        perror("write remove");
        close(fd);
        return ret;
    }

    close(fd);

    printf("Rescanning PCI bus...\n");

    fd = open("/sys/bus/pci/rescan", O_WRONLY|O_TRUNC);
    if (fd < 0) {
        if (errno == ENOENT)
            return 0;
        perror("open rescan");
        return fd;
    }

    ret = write(fd, "1", 1);
    if (ret != 1) {
        perror("write rescan");
        close(fd);
        return ret;
    }

    close(fd);

    printf("PCI bus rescan complete.\n");

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

static void i2c_stop(lsi_dev_t *d)
{
    i2c_delay(d);
    set_sda(d, 0);
    i2c_delay(d);
    set_scl(d, 1);
    i2c_delay(d);
    set_sda(d, 1);
    i2c_delay(d);
}

static void i2c_start(lsi_dev_t *d)
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

static void i2c_sendbit(lsi_dev_t *d, int bit)
{
    set_sda(d, bit);
    i2c_delay(d);
    set_scl(d, 1);
    wait_scl(d);
    i2c_delay(d);
    set_scl(d, 0);
    i2c_delay(d);
}

static int i2c_getbit(lsi_dev_t *d)
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

static void i2c_sendbyte(lsi_dev_t *d, uint8_t byte)
{
    for (int i = 0x80; i ; i >>= 1)
        i2c_sendbit(d, byte & i);
}

static uint8_t i2c_getbyte(lsi_dev_t *d)
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

static int lsi_i2c_close(lsi_dev_t *d)
{
    uint32_t val;

    chip_write32(d, CHIP_I2C_RESET, 1);
    do {
        i2c_delay(d);
    } while (chip_read32(d, CHIP_I2C_RESET) & 1);

    val = dcr_read32(d, DCR_I2C_SELECT);
    val &= ~0x800000;
    dcr_write32(d, DCR_I2C_SELECT, val);

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

static void print_ioc_state(lsi_dev_t *d)
{
    uint32_t doorbell = read32(d, MPI2_DOORBELL);

    printf("IOC is ");
    if (!(doorbell & MPI2_DOORBELL_STATE_MASK)) {
        printf("RESET ");
    }
    if (doorbell & MPI2_DOORBELL_READY) {
        printf("READY ");
    }
    if (doorbell & MPI2_DOORBELL_OPERATIONAL) {
        printf("OPERATIONAL ");
    }
    if (doorbell & MPI2_DOORBELL_FAULT) {
        printf("FAULT ");
    }
    printf("\n");
}

static int do_info(lsi_dev_t *d)
{
    printf("Registers:\n");
    printf(" DOORBELL:       0x%08x\n", read32(d, MPI2_DOORBELL));
    printf(" DIAG:           0x%08x\n", read32(d, d->r_diag));
    printf(" DCR_I2C_SELECT: 0x%08x\n", dcr_read32(d, DCR_I2C_SELECT));
    printf(" DCR_SBR_SELECT: 0x%08x\n", dcr_read32(d, DCR_SBR_CONFIG));
    printf(" CHIP_I2C_PINS:  0x%08x\n", chip_read32(d, CHIP_I2C_PINS));

    print_ioc_state(d);

    return 0;
}

static int do_readsbr(lsi_dev_t *d, const char *filename)
{
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

    lsi_i2c_close(d);
    return 0;
}

static int do_writesbr(lsi_dev_t *d, const char *filename)
{
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

    lsi_i2c_close(d);
    return 0;
}

static int do_reset(lsi_dev_t *d)
{
    int ret;
    uint32_t val;

    ret = lsi_unbind_driver(d);
    if (ret < 0)
        return ret;

    printf("Resetting adapter...\n");
    val = read32(d, d->r_diag);
    val &= ~MPI2_DIAG_BOOTDEVICE_MASK;
    val &= ~MPI2_DIAG_FORCE_HCB;
    val &= ~MPI2_DIAG_HCB_MODE;
    write32(d, d->r_diag, val);

    usleep(100000);

    val = read32(d, d->r_diag);
    val |= MPI2_DIAG_RESET_ADAPTER;
    write32(d, d->r_diag, val);

    usleep(100000);
    print_ioc_state(d);

    for (int i = 0; i < 200; i++) {
        if (read32(d, MPI2_DOORBELL) & MPI2_DOORBELL_READY)
            break;
        usleep(10000);
    }

    print_ioc_state(d);

    if (!(read32(d, MPI2_DOORBELL) & MPI2_DOORBELL_READY)) {
        printf("IOC failed to become ready\n");
        return -1;
    }

    return 0;
}

static int do_halt(lsi_dev_t *d)
{
    int ret;
    uint32_t val;

    ret = lsi_unbind_driver(d);
    if (ret < 0)
        return ret;

    printf("Resetting adapter in HCB mode...\n");
    val = read32(d, d->r_diag);
    val |= MPI2_DIAG_FORCE_HCB;
    write32(d, d->r_diag, val);
    val |= MPI2_DIAG_RESET_ADAPTER;
    write32(d, d->r_diag, val);

    usleep(1000000);

    lsi_reopen(d);

    val = read32(d, d->r_diag);
    val &= ~MPI2_DIAG_FORCE_HCB;
    write32(d, d->r_diag, val);

    print_ioc_state(d);

    if (read32(d, MPI2_DOORBELL) & MPI2_DOORBELL_STATE_MASK) {
        printf("IOC failed to stay in reset\n");
        return -1;
    }

    return 0;
}

static int do_hostboot(lsi_dev_t *d, const char *filename)
{
    int ret;
    uint32_t val;

    ret = do_halt(d);
    if (ret < 0)
        return ret;

    val = read32(d, d->r_diag);
    val |= MPI2_DIAG_CLR_FLASH_BAD_SIG;
    val &= ~MPI2_DIAG_BOOTDEVICE_MASK;
    val &= ~MPI2_DIAG_FORCE_HCB;
    val &= ~MPI2_DIAG_RESET_HISTORY;
    write32(d, d->r_diag, val);

    ret = lsi_setup_hcdw(d);
    if (ret < 0) {
        return ret;
    }

    printf("Loading firmware...\n");
    memset(d->hcdw, 0x42, HCDW_SIZE);
    int fd = open(filename, O_RDONLY);
    if (fd < 0) {
        perror("open firmware");
        return fd;
    }
    ssize_t length = read(fd, d->hcdw, HCDW_SIZE);
    if (length < 0) {
        perror("read firmware");
        return length;
    }
    memmove(d->hcdw + HCDW_SIZE - length, d->hcdw, length);
    printf("Loaded %ld bytes\n", length);

    val = read32(d, d->r_diag);
    val |= MPI2_DIAG_BOOTDEVICE_HCDW;
    write32(d, d->r_diag, val);

    printf("Booting IOC...\n");

    val &= ~MPI2_DIAG_HOLD_IOC_RESET;
    val &= ~MPI2_DIAG_FORCE_HCB;
    write32(d, d->r_diag, val);

    for (int i = 0; i < 200; i++) {
        if (read32(d, MPI2_DOORBELL) & MPI2_DOORBELL_READY)
            break;
        usleep(10000);
    }

    print_ioc_state(d);

    if (!(read32(d, MPI2_DOORBELL) & MPI2_DOORBELL_READY)) {
        printf("IOC failed to become ready\n");
        return -1;
    }

    usleep(500000);

    printf("IOC Host Boot successful.\n");

    lsi_disable_hcdw(d);

    return 0;
}

static int do_unbind(lsi_dev_t *d)
{
    return lsi_unbind_driver(d);
}

static int do_rescan(lsi_dev_t *d)
{
    int ret;

    ret = lsi_unbind_driver(d);
    if (ret < 0)
        return ret;

    return lsi_rescan(d);
}

static void usage(char *argv0)
{
    fprintf(stderr, "Usage: %s <PCI ID> <operation> [args...]\n", argv0);
    fprintf(stderr, "\n");
    fprintf(stderr, "PCI ID example: 0000:01:00.0\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Supported operations:\n");
    fprintf(stderr, "  info\n");
    fprintf(stderr, "    Print the device state and registers\n");
    fprintf(stderr, "  readsbr <sbr.bin>\n");
    fprintf(stderr, "    Read the SBR.\n");
    fprintf(stderr, "  writesbr <sbr.bin>\n");
    fprintf(stderr, "    Write the SBR.\n");
    fprintf(stderr, " *reset\n");
    fprintf(stderr, "    Perform a normal adapter reset. This also reloads\n");
    fprintf(stderr, "    the SBR.\n");
    fprintf(stderr, " *halt\n");
    fprintf(stderr, "    Perform an HCB reset, not allowing the IOC to boot.\n");
    fprintf(stderr, " *hostboot <firmware.bin>\n");
    fprintf(stderr, "    Reset the adapter and boot the specified firmware\n");
    fprintf(stderr, "    directly from host memory. Note: requires HugeTLB\n");
    fprintf(stderr, "    and is not compatible with IOMMUs.\n");
    fprintf(stderr, " *unbind\n");
    fprintf(stderr, "    Unbind the kernel driver from the PCI device.\n");
    fprintf(stderr, " *rescan\n");
    fprintf(stderr, "    Tell the kernel to remove and rescan the PCI device.\n");
    fprintf(stderr, "    This automatically picks up VID/PID changes and\n");
    fprintf(stderr, "    rebinds the driver.\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "* Operation forcefully unbinds the kernel driver. Make\n");
    fprintf(stderr, "  sure your disks are not in use!\n");
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
    } else if (!strcmp(argv[2], "reset")) {
        return !!do_reset(&dev);
    } else if (!strcmp(argv[2], "halt")) {
        return !!do_halt(&dev);
    } else if (!strcmp(argv[2], "hostboot") && argc == 4) {
        return !!do_hostboot(&dev, argv[3]);
    } else if (!strcmp(argv[2], "unbind")) {
        return !!do_unbind(&dev);
    } else if (!strcmp(argv[2], "rescan")) {
        return !!do_rescan(&dev);
    } else {
        usage(argv[0]);
        return 1;
    }
    return 0;
}
