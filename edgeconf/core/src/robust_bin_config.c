#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

// Structure definition
typedef struct __attribute__((packed)) {
    char   key[8];
    char   value[32];
} my_config_t;

// The size of each record
#define RECORD_SIZE (sizeof(my_config_t))

// Configuration file path
#define CONFIG_FILE "config.bin"

// Lock mode: read/write
typedef enum {
    LOCK_READ,
    LOCK_WRITE
} lock_mode_t;

/**
 * @brief   Use fcntl to lock or unlock a file region
 * @param   fd         The open file descriptor
 * @param   mode       Lock mode (LOCK_READ / LOCK_WRITE)
 * @param   offset     Offset from the start of the file
 * @param   length     Length of the locked region
 * @param   lock_flag  1 = Lock, 0 = Unlock
 * @return  0 on success, -1 on failure (check errno for details)
 */
int lock_region(int fd, lock_mode_t mode, off_t offset, size_t length, int lock_flag)
{
    struct flock fl;
    memset(&fl, 0, sizeof(fl));

    fl.l_type = (lock_flag == 0) ? F_UNLCK : (mode == LOCK_READ) ? F_RDLCK : F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = offset;
    fl.l_len = length;

    if (fcntl(fd, F_SETLKW, &fl) == -1) {
        perror("fcntl lock/unlock failed");
        return -1;
    }

    return 0;
}

/**
 * @brief   Read a configuration record at the specified index
 * @param   index  The record index (0-based)
 * @param   out    Pointer to store the read result
 * @return  0 on success, -1 on failure
 */
int read_config_entry(int index, my_config_t *out)
{
    // Open the file (read-only)
    int fd = open(CONFIG_FILE, O_RDONLY);
    if (fd < 0) {
        if (errno == ENOENT) {
            fprintf(stderr, "File '%s' does not exist. Please write to the file first.\n", CONFIG_FILE);
        } else {
            perror("open for read");
        }
        return -1;
    }

    off_t offset = (off_t)index * RECORD_SIZE;

    if (lock_region(fd, LOCK_READ, offset, RECORD_SIZE, 1) == -1) {
        close(fd);
        return -1;
    }

    if (lseek(fd, offset, SEEK_SET) == -1) {
        perror("lseek");
        lock_region(fd, LOCK_READ, offset, RECORD_SIZE, 0);
        close(fd);
        return -1;
    }

    ssize_t rbytes = read(fd, out, RECORD_SIZE);
    if (rbytes < 0) {
        perror("read");
        lock_region(fd, LOCK_READ, offset, RECORD_SIZE, 0);
        close(fd);
        return -1;
    } else if (rbytes != RECORD_SIZE) {
        fprintf(stderr, "read size mismatch\n");
        lock_region(fd, LOCK_READ, offset, RECORD_SIZE, 0);
        close(fd);
        return -1;
    }

    if (lock_region(fd, LOCK_READ, offset, RECORD_SIZE, 0) == -1) {
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

/**
 * @brief   Write a configuration record at the specified index
 * @param   index  The record index (0-based)
 * @param   data   Pointer to the struct to be written
 * @return  0 on success, -1 on failure
 */
int write_config_entry(int index, const my_config_t *data)
{
    // Open the file in read/write mode, create it if it doesn't exist
    int fd = open(CONFIG_FILE, O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        perror("open for write");
        return -1;
    }

    off_t offset = (off_t)index * RECORD_SIZE;

    if (lock_region(fd, LOCK_WRITE, offset, RECORD_SIZE, 1) == -1) {
        close(fd);
        return -1;
    }

    if (lseek(fd, offset, SEEK_SET) == -1) {
        perror("lseek");
        lock_region(fd, LOCK_WRITE, offset, RECORD_SIZE, 0);
        close(fd);
        return -1;
    }

    ssize_t wbytes = write(fd, data, RECORD_SIZE);
    if (wbytes < 0) {
        perror("write");
        lock_region(fd, LOCK_WRITE, offset, RECORD_SIZE, 0);
        close(fd);
        return -1;
    } else if (wbytes != RECORD_SIZE) {
        fprintf(stderr, "write size mismatch\n");
        lock_region(fd, LOCK_WRITE, offset, RECORD_SIZE, 0);
        close(fd);
        return -1;
    }

    if (lock_region(fd, LOCK_WRITE, offset, RECORD_SIZE, 0) == -1) {
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

/**
 * @brief   Print the usage instructions
 * @param   prog_name  The name of the program
 */
void print_usage(const char *prog_name)
{
    fprintf(stderr, "Usage: %s <read|write> <index> [<key> <value>]\n", prog_name);
    fprintf(stderr, "  read  : Read a record at the specified index\n");
    fprintf(stderr, "  write : Write a record at the specified index with the given data\n");
    fprintf(stderr, "          Requires: <key> <value>\n");
}

/**
 * @brief   Main entry point of the program
 */
int main(int argc, char *argv[])
{
    if (argc < 3) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char *operation = argv[1];
    int index = atoi(argv[2]);

    if (strcmp(operation, "read") == 0) {
        my_config_t read_data;
        memset(&read_data, 0, sizeof(read_data));

        if (read_config_entry(index, &read_data) == 0) {
            printf("Read success! key=%s, value=%s\n", read_data.key, read_data.value);
        } else {
            fprintf(stderr, "Read failed!\n");
        }
    } else if (strcmp(operation, "write") == 0) {
        if (argc != 5) {
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }

        my_config_t write_data;
        strncpy(write_data.key, argv[3], sizeof(write_data.key) - 1);
        write_data.key[sizeof(write_data.key) - 1] = '\0';
        strncpy(write_data.value, argv[4], sizeof(write_data.value) - 1);
        write_data.value[sizeof(write_data.value) - 1] = '\0';

        if (write_config_entry(index, &write_data) == 0) {
            printf("Write success!\n");
        } else {
            fprintf(stderr, "Write failed!\n");
        }
    } else {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

