/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include <infiltratr/filesystem_module_v2.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    int fd;
    uint32_t sector;
    int fail;
} Image;

static int read_at(void *context, uint64_t offset, void *out, uint32_t length)
{
    Image *image = context;
    if (image->fail || length != image->sector || offset % image->sector)
        return -1;
    return pread(image->fd, out, length, (off_t)offset) == (ssize_t)length ? 0 : -1;
}
static void *allocate(void *context, size_t size) { (void)context; return malloc(size); }
static void release(void *context, void *ptr) { (void)context; free(ptr); }

#define REQUIRE(cond) do { if (!(cond)) { fprintf(stderr, "%d: %s\n", __LINE__, #cond); return 1; } } while (0)

int main(int argc, char **argv)
{
    REQUIRE(argc == 5 || argc == 6);
    void *module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    REQUIRE(module);
    void *symbol = dlsym(module, IFS_FS_COMMON_EXPORT_V2);
    IfsFsModuleApiEntryV2 entry;
    REQUIRE(symbol && sizeof(entry) == sizeof(symbol));
    memcpy(&entry, &symbol, sizeof(entry));
    const IfsFsModuleApiV2 *api = entry();
    REQUIRE(ifs_fs_api_v2_valid(api, "xfs"));
    const IfsFsCoreOpsV2 *core = (const void *)ifs_fs_group_v2(api, IFS_FS_GROUP_CORE);
    REQUIRE(core && core->probe && core->mount && core->unmount);
    Image image = { .fd = open(argv[2], O_RDONLY), .sector = (uint32_t)strtoul(argv[3], NULL, 10) };
    struct stat status;
    REQUIRE(image.fd >= 0 && !fstat(image.fd, &status));
    if (!strcmp(argv[4], "geometry")) {
        REQUIRE(argc == 6);
        uint32_t bytes = (uint32_t)strtoul(argv[5], NULL, 10);
        unsigned char sb[4096];
        REQUIRE((bytes == 512 || bytes == sizeof(sb)) && pread(image.fd, sb, bytes, 0) == bytes);
        uint32_t crc = UINT32_MAX;
        for (uint32_t i = 0; i < bytes; ++i) {
            crc ^= (i >= 224 && i < 228) ? 0u : sb[i];
            for (unsigned j = 0; j < 8; ++j)
                crc = (crc >> 1) ^ ((0u - (crc & 1u)) & UINT32_C(0x82f63b78));
        }
        crc = ~crc;
        for (unsigned i = 0; i < 4; ++i)
            REQUIRE(sb[224+i] == (unsigned char)(crc >> (i*8)));
    }
    IfsFsHostRuntimeV2 host = {
        .abi_version = IFS_FS_HOST_ABI_V2, .structure_bytes = IFS_FS_HOST_V2_BASE_BYTES,
        .capabilities = IFS_FS_HOST_READ_ONLY, .volume_bytes = (uint64_t)status.st_size,
        .logical_sector_bytes = image.sector, .physical_sector_bytes = image.sector,
        .maximum_transfer_bytes = image.sector, .context = &image,
        .read_at = read_at, .allocate = allocate, .release = release
    };
    uint32_t version = 0;
    IfsFsVolumeInfoV2 info = { .structure_bytes = sizeof(info) };
    void *mount = NULL;
    int expected = !strcmp(argv[4], "clean") ? IFS_FS_OK : IFS_FS_CORRUPT;
    REQUIRE(core->probe(&host, &version) == expected);
    REQUIRE(core->mount(&host, &mount, &info) == expected);
    if (expected == IFS_FS_OK) {
        REQUIRE(version == 5 && mount && info.block_bytes == 4096 &&
                info.total_bytes == host.volume_bytes && info.free_bytes < info.total_bytes);
        REQUIRE(core->unmount(mount) == IFS_FS_OK);
    } else {
        REQUIRE(!version && !mount);
    }
    image.fail = 1;
    REQUIRE(core->probe(&host, &version) == IFS_FS_IO_ERROR && !version);
    REQUIRE(!close(image.fd) && !dlclose(module));
    return 0;
}
