/*
 * Copyright 2013-2015 Formation Data Systems, Inc.
 */

#ifndef SOURCE_ACCESS_MGR_INCLUDE_AMPROCESSOR_H_
#define SOURCE_ACCESS_MGR_INCLUDE_AMPROCESSOR_H_

#include <memory>
#include <string>

#include "fds_volume.h"

namespace FDS_ProtocolInterface {
class AddToVolumeGroupRespCtrlMsg;
}

namespace fds {

/**
 * Forward declarations
 */
struct AmProcessor_impl;
struct AmRequest;
struct AccessMgr;
struct CommonModuleProviderIf;
struct AmVolume;

using AddToVolumeGroupCb =
    std::function<void(const Error&,
                       const boost::shared_ptr<FDS_ProtocolInterface::AddToVolumeGroupRespCtrlMsg>&)>;

class AmProcessor : public std::enable_shared_from_this<AmProcessor>
{
    using shutdown_cb_type = std::function<void(void)>;
  public:
    AmProcessor(AccessMgr* parent, CommonModuleProviderIf *modProvider);
    AmProcessor(AmProcessor const&) = delete;
    AmProcessor& operator=(AmProcessor const&) = delete;
    ~AmProcessor();

    /**
     * Start AmProcessor and register a callback that will
     * indicate a shutdown sequence and all I/O has drained
     */
    void start();

    /**
     * Synchronous shutdown initiation.
     */
    void stop();

    void prepareForShutdownMsgRespBindCb(shutdown_cb_type&& cb);

    /**
     * Enqueue a connector request
     */
    void enqueueRequest(AmRequest* amReq);

    void getVolumes(std::vector<VolumeDesc>& volumes);

    bool isShuttingDown() const;

    /**
     * Update volume description
     */
    Error modifyVolumePolicy(const VolumeDesc& vdesc);

    /* Volumegroup control message from volume replica */
    void addToVolumeGroup(const FDS_ProtocolInterface::AddToVolumeGroupCtrlMsgPtr &addMsg,
                          const AddToVolumeGroupCb &cb);


    /**
     * Create object/metadata/offset caches for the given volume
     */
    void registerVolume(const VolumeDesc& volDesc);

    /**
     * Remove object/metadata/offset caches for the given volume
     */
    void removeVolume(const VolumeDesc& volDesc);

    /**
     * DMT/DLT table updates
     */
    Error updateDlt(bool dlt_type, std::string& dlt_data, std::function<void (const Error&)> const& cb);
    Error updateDmt(bool dmt_type, std::string& dmt_data, std::function<void (const Error&)> const& cb);

    /**
     * Update QoS' rate and throttle
     */
    Error updateQoS(long int const* rate, float const* throttle);

    void flushVolume(AmRequest* req, std::string const& vol);
    VolumeDesc* getVolume(fds_volid_t const vol_uuid) const;

  private:
    std::unique_ptr<AmProcessor_impl> _impl;
};


}  // namespace fds

#endif  // SOURCE_ACCESS_MGR_INCLUDE_AMPROCESSOR_H_
