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

    std::pair<bool, bool> getMode() const;

    fds_int64_t getToken() const;
    void setToken(fds_int64_t const _token);

    /**
     * Set the current volume version, this comes from DM during a volume open
     * request.
     */
    void setVersion(fds_uint64_t const version);

    /**
     * Advance the volume version and return it
     */
    fds_uint64_t getNextVersion()
    { return vol_version.fetch_add(1, std::memory_order_relaxed) + 1; }

    /** per volume queue */
    FDS_VolumeQueue* volQueue;

    /** access token */
    boost::shared_ptr<AmVolumeAccessToken> access_token;

 private:
    std::atomic_ullong vol_version;
};

}  // namespace fds

#endif  // SOURCE_ACCESS_MGR_INCLUDE_AMVOLUME_H_
