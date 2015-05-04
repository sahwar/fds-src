/*
 * Copyright 2013-2015 Formation Data Systems, Inc.
 */

#ifndef SOURCE_ACCESS_MGR_INCLUDE_AMVOLUME_H_
#define SOURCE_ACCESS_MGR_INCLUDE_AMVOLUME_H_

#include <memory>
#include "fds_volume.h"

namespace fds
{

struct AmVolumeAccessToken;
struct FDS_VolumeQueue;

struct AmVolume : public FDS_Volume {
    AmVolume(VolumeDesc const& vol_desc, FDS_VolumeQueue* queue, boost::shared_ptr<AmVolumeAccessToken> _access_token);

    AmVolume(AmVolume const& rhs) = delete;
    AmVolume& operator=(AmVolume const& rhs) = delete;

    ~AmVolume();

    fds_int64_t getToken() const;
    void setToken(fds_int64_t const _token);

    /** per volume queue */
    FDS_VolumeQueue* volQueue;

    /** access token */
    boost::shared_ptr<AmVolumeAccessToken> access_token;
};

}  // namespace fds

#endif  // SOURCE_ACCESS_MGR_INCLUDE_AMVOLUME_H_
