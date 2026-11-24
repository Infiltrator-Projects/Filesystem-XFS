/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _POSIX_C_SOURCE 200809L
#include <dlfcn.h>
#include <fcntl.h>
#include <infiltratr/filesystem_module_v2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                        \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)
typedef struct {
    int fd;
    uint32_t sector;
    int fail;
} Device;
static const IfsFsCoreOpsV2 *unmount_during_read;
static void *active_volume;
static int observed_unmount;
static int read_at(void *ctx, uint64_t at, void *out, uint32_t n)
{
    if (unmount_during_read) {
        const IfsFsCoreOpsV2 *ops=unmount_during_read;
        unmount_during_read=NULL;
        observed_unmount=ops->unmount(active_volume);
        if (observed_unmount!=IFS_FS_BUSY) return -1;
    }
    Device *d = ctx;
    if (d->fail || at % d->sector || !n || n % d->sector || n > 16384)
        return -1;
    return pread(d->fd, out, n, (off_t)at) == (ssize_t)n ? 0 : -1;
}
static void *alloc(void *ctx, size_t n)
{
    (void)ctx;
    return malloc(n);
}
static void drop(void *ctx, void *p)
{
    (void)ctx;
    free(p);
}
static int check_file(const IfsFsCoreOpsV2 *core, void *mount, const char *source, const char *name)
{
    char local[1024], path[300], result[260];
    CHECK(snprintf(local, sizeof(local), "%s/boot/%s", source, name) < (int)sizeof(local));
    CHECK(snprintf(path, sizeof(path), "/boot/%s", name) < (int)sizeof(path));
    int fd = open(local, O_RDONLY);
    struct stat st;
    CHECK(fd >= 0 && !fstat(fd, &st));
    IfsFsEntryV2 entry = {
        .structure_bytes = sizeof(entry), .name = result, .name_capacity = sizeof(result)};
    CHECK(core->stat(mount, path, &entry) == IFS_FS_OK && entry.type == IFS_FS_ENTRY_FILE);
    CHECK(entry.size_bytes == (uint64_t)st.st_size && !strcmp(result, name) &&
          entry.name_bytes == strlen(name));
    uint8_t got[65533], expected[sizeof(got)];
    uint64_t offset = 0;
    while (offset < entry.size_bytes) {
        uint32_t n = entry.size_bytes - offset > sizeof(got)
                         ? sizeof(got)
                         : (uint32_t)(entry.size_bytes - offset);
        CHECK(pread(fd, expected, n, (off_t)offset) == n);
        int rc = core->read(mount, path, offset, got, n);
        if (rc != IFS_FS_OK)
            fprintf(stderr, "%s offset %llu read %d\n", path, (unsigned long long)offset, rc);
        CHECK(rc == IFS_FS_OK && !memcmp(got, expected, n));
        offset += n;
    }
    CHECK(core->read(mount, path, offset, NULL, 0) == IFS_FS_OK);
    CHECK(core->read(mount, path, offset, got, 1) == IFS_FS_INVALID);
    CHECK(!close(fd));
    return 0;
}
int main(int argc, char **argv)
{
    CHECK(argc == 6);
    void *so = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!so)
        fprintf(stderr, "%s\n", dlerror());
    CHECK(so);
    IfsFsModuleApiEntryV2 fn = NULL;
    void *symbol = dlsym(so, IFS_FS_COMMON_EXPORT_V2);
    CHECK(symbol && sizeof(fn) == sizeof(symbol));
    memcpy(&fn, &symbol, sizeof(fn));
    const IfsFsModuleApiV2 *api = fn();
    CHECK(ifs_fs_api_v2_valid(api, "xfs"));
    const IfsFsCoreOpsV2 *core = (const void *)ifs_fs_group_v2(api, IFS_FS_GROUP_CORE);
    const IfsFsVolumeOpsV2 *vol = (const void *)ifs_fs_group_v2(api, IFS_FS_GROUP_VOLUME);
    CHECK(core && core->stat && core->directory_entry && core->read && !core->write && vol &&
          vol->query);
    Device device = {open(argv[2], O_RDONLY), (uint32_t)strtoul(argv[4], NULL, 10), 0};
    struct stat st;
    CHECK(device.fd >= 0 && !fstat(device.fd, &st));
    IfsFsHostRuntimeV2 host = {.abi_version = 2,
                               .structure_bytes = IFS_FS_HOST_V2_BASE_BYTES,
                               .volume_bytes = (uint64_t)st.st_size,
                               .capabilities = IFS_FS_HOST_READ_ONLY,
                               .logical_sector_bytes = device.sector,
                               .physical_sector_bytes = device.sector,
                               .maximum_transfer_bytes = 16384,
                               .context = &device,
                               .read_at = read_at,
                               .allocate = alloc,
                               .release = drop};
    void *mount = NULL, *second = NULL;
    uint32_t variant = 0;
    IfsFsVolumeInfoV2 info = {.structure_bytes = sizeof(info)};
    CHECK(core->probe(&host, &variant) == IFS_FS_OK && (variant == 4 || variant == 5));
    int rc = core->mount(&host, &mount, &info);
    if (!strcmp(argv[5], "dirty-log")) {
        CHECK(rc == IFS_FS_UNSUPPORTED && !mount);
        goto done;
    }
    if (!strcmp(argv[5], "bad-tree")) {
        CHECK(rc == IFS_FS_CORRUPT && !mount);
        goto done;
    }
    if (rc != IFS_FS_OK)
        fprintf(stderr, "mount returned %d, image %s\n", rc, argv[2]);
    CHECK(rc == IFS_FS_OK && mount && info.total_bytes <= host.volume_bytes);
    if (!strcmp(argv[5], "bad-bmap")) {
        char byte;
        CHECK(core->read(mount, "/fragmented/extent", 0, &byte, 1) == IFS_FS_CORRUPT);
    } else if (!strcmp(argv[5], "fragmented")) {
        char name[256];
        IfsFsEntryV2 e = {
            .structure_bytes = sizeof(e), .name = name, .name_capacity = sizeof(name)};
        CHECK(core->stat(mount, "/fragmented/extent", &e) == IFS_FS_OK &&
              e.size_bytes > 63u * 8192u);
        uint8_t wanted[8192], actual[8192];
        for (unsigned i = 0; i < 64; ++i) {
            memset(wanted, 0, sizeof(wanted));
            int len = snprintf((char *)wanted, sizeof(wanted), "written segment %u", i);
            CHECK(len > 0);
            uint32_t bytes = i == 63 ? (uint32_t)len : (uint32_t)sizeof(actual);
            CHECK(core->read(mount, "/fragmented/extent", (uint64_t)i * 8192u, actual, bytes) ==
                  IFS_FS_OK);
            CHECK(!memcmp(actual, wanted, bytes));
        }
    } else if (!strcmp(argv[5], "bad-inode")) {
        uint8_t byte;
        CHECK(core->read(mount, "/boot/vmlinuz", 0, &byte, 1) == IFS_FS_CORRUPT);
    } else {
        CHECK(!strcmp(argv[5], "clean"));
        CHECK(core->mount(&host, &second, &info) == IFS_FS_OK && second && second != mount);
        CHECK(core->unmount(second) == IFS_FS_OK && vol->query(second, &info) == IFS_FS_INVALID);
        char name[256];
        IfsFsEntryV2 e = {
            .structure_bytes = sizeof(e), .name = name, .name_capacity = sizeof(name)};
        CHECK(core->stat(mount, "/", &e) == IFS_FS_OK && e.type == IFS_FS_ENTRY_DIRECTORY);
        CHECK(core->stat(mount, "/boot/missing", &e) == IFS_FS_NOT_FOUND);
        const IfsFsMetadataOpsV2 *meta=(const void *)ifs_fs_group_v2(api,IFS_FS_GROUP_METADATA);
        IfsFsMetadataV2 metadata={.structure_bytes=sizeof(metadata)};
        CHECK(meta && meta->get && !meta->set &&
              meta->header.capabilities==IFS_FS_META_GET_MODE);
        CHECK(meta->get(mount,"/boot/inline",&metadata)==IFS_FS_OK &&
              metadata.mask==IFS_FS_META_GET_MODE &&
              (metadata.mode&0170000u)==0100000u);
        CHECK(metadata.mode==0100644u && metadata.owner==0u && metadata.group==0u);
        CHECK(meta->get(mount,"/boot/missing",&metadata)==IFS_FS_NOT_FOUND);
        CHECK(meta->get(second,"/",&metadata)==IFS_FS_INVALID);
        if (access(argv[3], R_OK) == 0) {
            char check_path[1024];
            CHECK(snprintf(check_path, sizeof(check_path), "%s/many", argv[3]) <
                  (int)sizeof(check_path));
            if (access(check_path, R_OK) == 0) {
                CHECK(core->stat(mount, "/many/file-511", &e) == IFS_FS_OK && e.size_bytes == 4);
                char content[4];
                CHECK(core->read(mount, "/many/file-511", 0, content, 4) == IFS_FS_OK &&
                      !memcmp(content, "511\n", 4));
            }
        }
        unsigned seen = 0;
        for (uint32_t i = 0;; ++i) {
            rc = core->directory_entry(mount, "/boot", i, &e);
            if (rc == IFS_FS_END)
                break;
            CHECK(rc == IFS_FS_OK && i < 4);
            if (!strcmp(name, "vmlinuz"))
                seen |= 1;
            else if (!strcmp(name, "initramfs"))
                seen |= 2;
            else if (!strcmp(name, "inline"))
                seen |= 4;
            else if (!strcmp(name, "sparse"))
                seen |= 8;
            else
                CHECK(0);
        }
        CHECK((seen & 7) == 7);
        /* The existing reader pin must survive a reentrant unmount from
         * actual host I/O. A cache hit need not invoke the callback. */
        active_volume=mount;
        unmount_during_read=core;
        observed_unmount=IFS_FS_INVALID;
        CHECK(!check_file(core, mount, argv[3], "vmlinuz") &&
              !check_file(core, mount, argv[3], "initramfs") &&
              !check_file(core, mount, argv[3], "inline"));
        CHECK(!unmount_during_read && observed_unmount==IFS_FS_BUSY);
        if (seen & 8)
            CHECK(!check_file(core, mount, argv[3], "sparse"));
        strcpy(name, "sentinel");
        e.name_capacity = 2;
        CHECK(core->stat(mount, "/boot/inline", &e) == IFS_FS_NO_SPACE &&
              !strcmp(name, "sentinel"));
        device.fail = 1;
        uint8_t byte;
        CHECK(core->read(mount, "/boot/vmlinuz", 0, &byte, 1) == IFS_FS_IO_ERROR);
        device.fail = 0;
    }
    CHECK(core->unmount(mount) == IFS_FS_OK && core->unmount(mount) == IFS_FS_INVALID);
done:
    CHECK(!close(device.fd) && !dlclose(so));
    return 0;
}
