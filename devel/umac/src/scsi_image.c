#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "scsi_disk.h"

static int image_read(void *ctx, uint32_t lba, uint8_t sector[SCSI_SECTOR_SIZE])
{
        struct scsi_image *image = ctx;
        off_t offset = (off_t)lba * SCSI_SECTOR_SIZE;
        size_t pos = 0;
        while (pos < SCSI_SECTOR_SIZE) {
                ssize_t count = pread(image->fd, sector + pos, SCSI_SECTOR_SIZE - pos, offset + pos);
                if (count < 0 && errno == EINTR)
                        continue;
                if (count <= 0)
                        return -1;
                pos += count;
        }
        return 0;
}

static int image_write(void *ctx, uint32_t lba, const uint8_t sector[SCSI_SECTOR_SIZE])
{
        struct scsi_image *image = ctx;
        off_t offset = (off_t)lba * SCSI_SECTOR_SIZE;
        size_t pos = 0;
        while (pos < SCSI_SECTOR_SIZE) {
                ssize_t count = pwrite(image->fd, sector + pos, SCSI_SECTOR_SIZE - pos, offset + pos);
                if (count < 0 && errno == EINTR)
                        continue;
                if (count <= 0)
                        return -1;
                pos += count;
        }
        return 0;
}

static int image_flush(void *ctx)
{
        struct scsi_image *image = ctx;
        int result;
        do {
                result = fsync(image->fd);
        } while (result < 0 && errno == EINTR);
        return result;
}

int scsi_image_open(struct scsi_image *image, const char *path, bool writable)
{
        int fd = open(path, (writable ? O_RDWR : O_RDONLY) | O_CLOEXEC | O_NONBLOCK);
        if (fd < 0)
                return -1;

        struct stat st;
        int error = 0;
        if (fstat(fd, &st) < 0)
                error = errno;
        else if (!S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size % SCSI_SECTOR_SIZE)
                error = EINVAL;
        else if (st.st_size / SCSI_SECTOR_SIZE > SCSI_DISK_MAX_BLOCKS)
                error = EFBIG;
        if (error) {
                close(fd);
                errno = error;
                return -1;
        }

        *image = (struct scsi_image) {
                .fd = fd,
                .media = {
                        .ctx = image,
                        .blocks = st.st_size / SCSI_SECTOR_SIZE,
                        .read = image_read,
                        .write = writable ? image_write : NULL,
                        .flush = writable ? image_flush : NULL,
                },
        };
        return 0;
}

int scsi_image_close(struct scsi_image *image)
{
        return close(image->fd);
}
