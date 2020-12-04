/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "TrustyAppLoader"

#include <android-base/logging.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <trusty/tipc.h>
#include <unistd.h>
#include <algorithm>
#include <string>

#include "apploader_ipc.h"

using std::string;

/*
 * According to man sendfile, the function always transfers at most 0x7fff000
 * bytes per call.
 */
#define SENDFILE_MAX 0x7ffff000L

constexpr const char kTrustyDeviceName[] = "/dev/trusty-ipc-dev0";

static const char* _sopts = "h";
static const struct option _lopts[] = {
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0},
};

static const char* usage =
        "Usage: %s [options] package-file\n"
        "\n"
        "options:\n"
        "  -h, --help            prints this message and exit\n"
        "\n";

static void print_usage_and_exit(const char* prog, int code) {
    fprintf(stderr, usage, prog);
    exit(code);
}

static void parse_options(int argc, char** argv) {
    int c;
    int oidx = 0;

    while (1) {
        c = getopt_long(argc, argv, _sopts, _lopts, &oidx);
        if (c == -1) {
            break; /* done */
        }

        switch (c) {
            case 'h':
                print_usage_and_exit(argv[0], EXIT_SUCCESS);
                break;

            default:
                print_usage_and_exit(argv[0], EXIT_FAILURE);
        }
    }
}

static int read_file(const char* file_name, off64_t* out_file_size) {
    int ret;
    int fd = -1;
    int memfd = -1;
    long page_size = sysconf(_SC_PAGESIZE);
    off64_t file_size, file_page_offset, file_page_size;
    struct stat64 st;

    fd = open(file_name, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "Error opening file '%s': %s\n", file_name, strerror(errno));

        ret = fd;
        goto err_open;
    }

    ret = fstat64(fd, &st);
    if (ret < 0) {
        fprintf(stderr, "Error calling stat on file '%s': %s\n", file_name, strerror(errno));
        goto err_fstat;
    }

    file_size = st.st_size;
    if (out_file_size) *out_file_size = file_size;

    memfd = memfd_create("trusty-app", 0);
    if (memfd < 0) {
        fprintf(stderr, "Error creating memfd: %s\n", strerror(errno));

        ret = memfd;
        goto err_memfd_create;
    }

    // The memfd size need to be a multiple of the page size
    file_page_offset = file_size & (page_size - 1);
    if (file_page_offset) file_page_offset = page_size - file_page_offset;
    if (__builtin_add_overflow(file_size, file_page_offset, &file_page_size)) {
        fprintf(stderr, "Failed to page-align file size\n");
        ret = -1;
        goto err_page_align;
    }

    ret = ftruncate64(memfd, file_page_size);
    if (ret < 0) {
        fprintf(stderr, "Error truncating memfd: %s\n", strerror(errno));
        goto err_ftruncate;
    }

    while (file_size > 0) {
        size_t len = std::min(file_size, SENDFILE_MAX);
        ssize_t num_sent = sendfile(memfd, fd, NULL, len);
        if (num_sent < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }

            fprintf(stderr, "Error copying package file '%s': %s\n", file_name, strerror(errno));

            ret = num_sent;
            goto err_sendfile;
        }

        assert(num_sent <= file_size);
        file_size -= num_sent;
    }

    close(fd);
    return memfd;

err_sendfile:
err_ftruncate:
err_page_align:
    close(memfd);
err_memfd_create:
err_fstat:
    close(fd);
err_open:
    return ret;
}

static ssize_t send_load_message(int tipc_fd, int package_fd, off64_t package_size) {
    struct apploader_message msg = {
            .cmd = APPLOADER_LOAD_APPLICATION,
    };
    struct iovec tx[2] = {{&msg, sizeof(msg)}, {&package_size, sizeof(package_size)}};
    struct trusty_shm shm = {
            .fd = package_fd,
            .transfer = TRUSTY_SHARE,
    };
    return tipc_send(tipc_fd, tx, 2, &shm, 1);
}

static ssize_t send_app_package(const char* package_file_name) {
    ssize_t ret = 0;
    int tipc_fd = -1;
    int package_fd = -1;
    off64_t package_size;

    package_fd = read_file(package_file_name, &package_size);
    if (package_fd < 0) {
        ret = package_fd;
        goto err_read_file;
    }

    tipc_fd = tipc_connect(kTrustyDeviceName, APPLOADER_PORT);
    if (tipc_fd < 0) {
        fprintf(stderr, "Failed to connect to Trusty app loader: %s\n", strerror(-tipc_fd));
        ret = tipc_fd;
        goto err_tipc_connect;
    }

    ret = send_load_message(tipc_fd, package_fd, package_size);
    if (ret < 0) {
        fprintf(stderr, "Failed to send package\n");
        goto err_send;
    }

    // TODO: wait for response???

    tipc_close(tipc_fd);
    close(package_fd);
    return 0;

err_send:
    tipc_close(tipc_fd);
err_tipc_connect:
    close(package_fd);
err_read_file:
    return ret;
}

int main(int argc, char** argv) {
    parse_options(argc, argv);
    if (optind + 1 != argc) {
        print_usage_and_exit(argv[0], EXIT_FAILURE);
    }

    int ret = send_app_package(argv[optind]);
    return ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
