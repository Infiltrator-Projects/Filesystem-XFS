/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <infiltratr/filesystem_module_v2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern const IfsFsModuleApiV2 *infiltrator_filesystem_api_v2(void);
/* Canonical CRC implementation in the source-owned XFS engine. */
extern void xfs_write_crc_le(uint8_t *data,size_t length,size_t field);
static uint8_t disk[1048576];
static int reader(void *ctx,uint64_t at,void *out,uint32_t n){
    (void)ctx;
    if(n!=512u || at>sizeof(disk) || n>sizeof(disk)-at) return -1;
    memcpy(out,disk+(size_t)at,n);return 0;
}
static void *alloc(void *c,size_t n){(void)c;return malloc(n);}
static void release(void *c,void *p){(void)c;free(p);}
static void be16(uint8_t *p,uint16_t n){p[0]=(uint8_t)(n>>8);p[1]=(uint8_t)n;}
static void be32(uint8_t *p,uint32_t n){for(int i=0;i<4;++i)p[i]=(uint8_t)(n>>(24-8*i));}
static void be64(uint8_t *p,uint64_t n){for(int i=0;i<8;++i)p[i]=(uint8_t)(n>>(56-8*i));}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"XFS V2: %s\n",#x);return 1;}}while(0)
int main(void){
    const IfsFsModuleApiV2 *api=infiltrator_filesystem_api_v2();
    CHECK(ifs_fs_api_v2_valid(api,"xfs"));
    const IfsFsCoreOpsV2 *core=(const void *)ifs_fs_group_v2(api,IFS_FS_GROUP_CORE);
    const IfsFsVolumeOpsV2 *vol=(const void *)
        ifs_fs_group_v2(api,IFS_FS_GROUP_VOLUME);
    CHECK(core && core->probe && core->mount && core->unmount &&
          vol && vol->query && !core->write);
    CHECK(core->header.capabilities==(IFS_FS_CORE_PROBE|IFS_FS_CORE_MOUNT));
    CHECK(vol->header.capabilities==IFS_FS_VOLUME_QUERY);
    IfsFsHostRuntimeV2 h={0};
    h.abi_version=IFS_FS_HOST_ABI_V2;h.structure_bytes=sizeof(h);
    h.capabilities=IFS_FS_HOST_READ_ONLY;h.volume_bytes=sizeof(disk);
    h.logical_sector_bytes=512;h.physical_sector_bytes=512;
    h.maximum_transfer_bytes=512;h.context=disk;
    h.read_at=reader;h.allocate=alloc;h.release=release;
    uint32_t variant=0;
    CHECK(core->probe(&h,&variant)==IFS_FS_NOT_FOUND);
    memcpy(disk,"XFSB",4);
    be32(disk+4,4096);be64(disk+8,256);
    be64(disk+56,128);be64(disk+144,100);be32(disk+84,128);be32(disk+88,2);
    be16(disk+100,4);be16(disk+102,512);be16(disk+104,256);
    be16(disk+106,16);disk[120]=12;disk[121]=9;
    disk[122]=8;disk[123]=4;disk[124]=7;
    CHECK(core->probe(&h,&variant)==IFS_FS_OK && variant==4);
    IfsFsVolumeInfoV2 info={.structure_bytes=sizeof(info)};
    void *mounted=NULL,*old=NULL;
    CHECK(core->mount(&h,&mounted,&info)==IFS_FS_OK && mounted);
    CHECK(info.variant==4u && info.block_bytes==4096u &&
          info.total_bytes==sizeof(disk) && info.free_bytes==409600u);
    CHECK(core->mount(&h,&old,&info)==IFS_FS_BUSY && !old);
    info.structure_bytes=sizeof(info);
    CHECK(vol->query(mounted,&info)==IFS_FS_OK);
    old=mounted;
    CHECK(core->unmount(mounted)==IFS_FS_OK);
    CHECK(core->unmount(old)==IFS_FS_INVALID);
    CHECK(vol->query(old,&info)==IFS_FS_INVALID);
    be64(disk+144,257u);
    info.structure_bytes=sizeof(info);
    CHECK(core->mount(&h,&mounted,&info)==IFS_FS_CORRUPT && !mounted);
    be64(disk+144,100u);
    /* The canonical native CRC writer is the oracle for a 4-KiB
     * version-5 superblock, not a duplicate test-only checksum parser. */
    be16(disk+100,5);be16(disk+102,4096);
    xfs_write_crc_le(disk,4096u,224u);
    CHECK(core->probe(&h,&variant)==IFS_FS_OK && variant==5);
    disk[1024]^=1u;variant=0;
    CHECK(core->probe(&h,&variant)==IFS_FS_CORRUPT && variant==0);
    disk[1024]^=1u;
    /* Malformed non-power-of-two disk sectors must be rejected before the
     * CRC reader allocates a too-small buffer for an aligned host read. */
    const uint16_t invalid_sectors[]={513u,1023u,4095u};
    for (size_t i=0;i<sizeof(invalid_sectors)/sizeof(invalid_sectors[0]);++i) {
        be16(disk+102,invalid_sectors[i]);variant=0;
        CHECK(core->probe(&h,&variant)==IFS_FS_CORRUPT && variant==0);
    }
    be16(disk+100,4);be16(disk+102,512);
    be64(disk+8,UINT64_MAX);variant=0;
    CHECK(core->probe(&h,&variant)==IFS_FS_CORRUPT && variant==0);
    be64(disk+8,256);disk[0]=0;
    CHECK(core->probe(&h,&variant)==IFS_FS_NOT_FOUND);
    disk[0]='X';h.maximum_transfer_bytes=0;
    CHECK(core->probe(&h,&variant)==IFS_FS_INVALID);
    return 0;
}
