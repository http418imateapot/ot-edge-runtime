/**
 * @file   cli.c
 * @brief  Command-line interface for the robust-binary-config library.
 *
 * Usage:
 *   robust_cfg_tool <file> <command> [args...]
 *
 * Commands:
 *   read    <key>           Read value for the given key
 *   write   <key> <value>   Write (or update) a key-value pair
 *   delete  <key>           Delete a key
 *   repair                  Repair corrupt slots (reset to EMPTY)
 *   compact                 Compact file (remove DELETED/CORRUPT slots)
 */

#define _POSIX_C_SOURCE 200809L

#include "robust_cfg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s <file> <command> [args...]\n"
        "\n"
        "Commands:\n"
        "  read   <key>           Read value for the given key\n"
        "  write  <key> <value>   Write (or update) a key-value pair\n"
        "  delete <key>           Delete a key\n"
        "  repair                 Reset CORRUPT slots to EMPTY\n"
        "  compact                Remove DELETED/CORRUPT slots\n"
        "\n"
        "Examples:\n"
        "  %s config.bin write server_url https://example.com\n"
        "  %s config.bin read  server_url\n"
        "  %s config.bin delete server_url\n"
        "  %s config.bin repair\n"
        "  %s config.bin compact\n",
        prog, prog, prog, prog, prog, prog);
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    const char *file_path = argv[1];
    const char *command   = argv[2];

    robust_cfg_handle_t *h = robust_cfg_open(file_path, 0);
    if (!h) {
        fprintf(stderr, "Error: failed to open config file '%s'\n", file_path);
        return EXIT_FAILURE;
    }

    int exit_code = EXIT_SUCCESS;

    if (strcmp(command, "read") == 0) {
        if (argc != 4) {
            fprintf(stderr, "Error: 'read' requires exactly one argument: <key>\n");
            exit_code = EXIT_FAILURE;
            goto cleanup;
        }

        const char *key = argv[3];
        char value[ROBUST_CFG_VALUE_MAX];

        int rc = robust_cfg_read(h, key, value, sizeof(value));
        switch (rc) {
        case ROBUST_CFG_OK:
            printf("key=%s value=%s\n", key, value);
            break;
        case ROBUST_CFG_NOT_FOUND:
            fprintf(stderr, "Error: key '%s' not found\n", key);
            exit_code = EXIT_FAILURE;
            break;
        case ROBUST_CFG_CORRUPT:
            fprintf(stderr, "Error: CRC mismatch for key '%s' — run repair\n", key);
            exit_code = EXIT_FAILURE;
            break;
        default:
            fprintf(stderr, "Error: read failed (code %d)\n", rc);
            exit_code = EXIT_FAILURE;
            break;
        }

    } else if (strcmp(command, "write") == 0) {
        if (argc != 5) {
            fprintf(stderr,
                    "Error: 'write' requires two arguments: <key> <value>\n");
            exit_code = EXIT_FAILURE;
            goto cleanup;
        }

        const char *key   = argv[3];
        const char *value = argv[4];

        int rc = robust_cfg_write(h, key, value);
        switch (rc) {
        case ROBUST_CFG_OK:
            printf("Write success: key=%s\n", key);
            break;
        case ROBUST_CFG_FULL:
            fprintf(stderr,
                    "Error: config file is full — run compact to reclaim space\n");
            exit_code = EXIT_FAILURE;
            break;
        case ROBUST_CFG_BAD_ARG:
            fprintf(stderr,
                    "Error: key or value exceeds maximum length "
                    "(key<%d, value<%d)\n",
                    ROBUST_CFG_KEY_MAX, ROBUST_CFG_VALUE_MAX);
            exit_code = EXIT_FAILURE;
            break;
        default:
            fprintf(stderr, "Error: write failed (code %d)\n", rc);
            exit_code = EXIT_FAILURE;
            break;
        }

    } else if (strcmp(command, "delete") == 0) {
        if (argc != 4) {
            fprintf(stderr,
                    "Error: 'delete' requires exactly one argument: <key>\n");
            exit_code = EXIT_FAILURE;
            goto cleanup;
        }

        const char *key = argv[3];
        int rc = robust_cfg_delete(h, key);
        switch (rc) {
        case ROBUST_CFG_OK:
            printf("Delete success: key=%s\n", key);
            break;
        case ROBUST_CFG_NOT_FOUND:
            fprintf(stderr, "Error: key '%s' not found\n", key);
            exit_code = EXIT_FAILURE;
            break;
        default:
            fprintf(stderr, "Error: delete failed (code %d)\n", rc);
            exit_code = EXIT_FAILURE;
            break;
        }

    } else if (strcmp(command, "repair") == 0) {
        int rc = robust_cfg_repair(h);
        if (rc == ROBUST_CFG_OK)
            printf("Repair complete\n");
        else {
            fprintf(stderr, "Error: repair failed (code %d)\n", rc);
            exit_code = EXIT_FAILURE;
        }

    } else if (strcmp(command, "compact") == 0) {
        int rc = robust_cfg_compact(h);
        if (rc == ROBUST_CFG_OK)
            printf("Compact complete\n");
        else {
            fprintf(stderr, "Error: compact failed (code %d)\n", rc);
            exit_code = EXIT_FAILURE;
        }

    } else {
        fprintf(stderr, "Error: unknown command '%s'\n", command);
        print_usage(argv[0]);
        exit_code = EXIT_FAILURE;
    }

cleanup:
    robust_cfg_close(h);
    return exit_code;
}
