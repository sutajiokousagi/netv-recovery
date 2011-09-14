#if defined(linux) && defined(DANGEROUS)
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <strings.h>
#include <unistd.h>

#if !defined(BLKSSZGET)
# define BLKSSZGET _IO(0x12, 104)
#endif
#if !defined(BLKGETSIZE64)
# define BLKGETSIZE64 _IOR(0x12,114,size_t)
#endif

#define CONFIG_SIZE 16384
#define RFS_SIZE 500000

typedef uint32_t sector_t;
typedef unsigned long long uoff_t;


struct part_entry {
    uint8_t status;
    uint8_t chs1;
    uint8_t chs2;
    uint8_t chs3;
    uint8_t type;
    uint8_t chs4;
    uint8_t chs5;
    uint8_t chs6;
    uint32_t lba_address;
    uint32_t lba_size;
} __attribute__((__packed__));

struct mbr {
    uint8_t code[440];                  /* 0   - 440 */
    uint8_t disk_signature[4];          /* 440 - 444 */
    uint8_t reserved[2];                /* 444 - 446 */
    struct part_entry partitions[4];    /* 446 - 510 */
    uint8_t signature[2];               /* 510 - 512 */
} __attribute__((__packed__));

static sector_t bb_BLKGETSIZE_sectors(int fd)
{
    uint64_t v64;
    unsigned long longsectors;

    if (ioctl(fd, BLKGETSIZE64, &v64) == 0) {
        /* Got bytes, convert to 512 byte sectors */
        v64 >>= 9;
        if (v64 != (sector_t)v64) {
ret_trunc:
            /* Not only DOS, but all other partition tables
             * we support can't record more than 32 bit
             * sector counts or offsets
             */
            fprintf(stderr, "device has more than 2^32 sectors, can't use all of them");
            v64 = (uint32_t)-1L;
        }
        return v64;
    }
    /* Needs temp of type long */
    if (ioctl(fd, BLKGETSIZE, &longsectors)) {
        /* Perhaps this is a disk image */
        off_t sz = lseek(fd, 0, SEEK_END);
        longsectors = 0;
        if (sz > 0)
            longsectors = (uoff_t)sz / 512;
        lseek(fd, 0, SEEK_SET);
    }
    if (sizeof(long) > sizeof(sector_t)
     && longsectors != (sector_t)longsectors
    ) {
        goto ret_trunc;
    }
    return longsectors;
}


int
prepare_partitions(void)
{
    struct mbr mbr;
    int fd;
    sector_t last_sector;
    
    fd = open("/dev/mmcblk0", O_RDWR);
    if (fd == -1) {
        perror("Unable to open MMC card");
        return -1;
    }
    bzero(&mbr, sizeof(mbr));

    last_sector = bb_BLKGETSIZE_sectors(fd);
    if (!last_sector) {
        close(fd);
        return -2;
    }

    mbr.signature[0] = 0x55;
    mbr.signature[1] = 0xAA;

    mbr.partitions[0].status = 0x80;
    mbr.partitions[0].lba_address = 4;
    mbr.partitions[0].lba_size = CONFIG_SIZE*2;
    mbr.partitions[0].type = 0x53;

    mbr.partitions[1].status = 0x00;
    mbr.partitions[1].lba_address = mbr.partitions[0].lba_address + mbr.partitions[0].lba_size;
    mbr.partitions[1].lba_size = RFS_SIZE*2;
    mbr.partitions[1].type = 0x83;

/*
    mbr.partitions[2].status = 0x00;
    mbr.partitions[2].lba_address = mbr.partitions[1].lba_address + mbr.partitions[1].lba_size;
    mbr.partitions[2].lba_size = last_sector - mbr.partitions[2].lba_address - 100;
    mbr.partitions[2].type = 0x83;
*/

    if (-1 == lseek(fd, 0, SEEK_SET)) {
        perror("Unable to seek");
        close(fd);
        return -3;
    }
    if (sizeof(mbr) != write(fd, &mbr, sizeof(mbr))) {
        perror("Unable to write MBR");
        close(fd);
        return -4;
    }


    sync();
    if (-1 == ioctl(fd, BLKRRPART, NULL)) {
        perror("Unable to refresh partition table");
        close(fd);
        return -5;
    }

    close(fd);
    return 0;
}

#else

int
prepare_partitions(void)
{
    return -6;
}
#endif /* linux && DANGEROUS */
