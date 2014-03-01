/*
 * Copyright 2014 by Formation Data Systems, Inc.
 */
#include <fds_assert.h>
#include <om-discovery.h>
#include <kvstore/configdb.h>

namespace fds {

int
OmDiscoveryMod::vol_persist(VolumeInfo::pointer vol)
{
    om_cfg_mgr->addVolume(*vol->vol_get_properties());
    return 0;
}

void
OmDiscoveryMod::vol_restore()
{
    VolumeDesc *desc;

    fds_verify(om_vol_mgr != NULL);
    fds_verify(om_cfg_mgr != NULL);
}

}  // namespace fds
