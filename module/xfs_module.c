/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ifs_xfs_module_v1.h"
int ifs_xfs_worker_main(int,char **);
int ifs_bind_services(const IfsDefragServicesV1 *);
void ifs_unbind_services(void);
static int run(int argc,char **argv,const IfsDefragServicesV1 *services) {
 if(ifs_bind_services(services)!=0)return 1;
 int result=ifs_xfs_worker_main(argc,argv);
 ifs_unbind_services();
 return result;
}
int ifs_xfs_fixture_run(const void *,const char *,int,const void *);
static int fixture(const void *spec,const char *path,int create,
 const IfsDefragServicesV1 *services,const IfsXfsFixtureHostV1 *host) {
 if(ifs_bind_services(services)!=0)return -1;
 int result=ifs_xfs_fixture_run(spec,path,create,host);
 ifs_unbind_services();
 return result;
}
static const IfsXfsModuleV1 api={IFS_XFS_MODULE_V1,sizeof(IfsXfsModuleV1),"xfs",IFS_XFS_CAP_RUN | IFS_XFS_CAP_FIXTURE,run,fixture};
const IfsXfsModuleV1 *infiltrator_filesystem_xfs_v1(void){return &api;}
