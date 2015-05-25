/*
 * Copyright 2015 Formation Data Systems, Inc.
 */
#ifndef SOURCE_INCLUDE_NET_OMSVCPROCESS_H_
#define SOURCE_INCLUDE_NET_OMSVCPROCESS_H_

#include <boost/shared_ptr.hpp>

/* Forward declarations */
namespace FDS_ProtocolInterface {
}  // namespace FDS_ProtocolInterface
namespace fpi = FDS_ProtocolInterface;

namespace fds {

/* Forward declarations */
struct SvcProcess;

/**
* @brief OMSvcProcess
*/
struct OMSvcProcess : SvcProcess {
    OMSvcProcess(int argc, char *argv[]);
    virtual ~OMSvcProcess();

    void init(int argc, char *argv[]);

    virtual void registerSvcProcess() override;

    virtual int run() override;

    /* Messages invoked by handler */
    void registerService(boost::shared_ptr<fpi::SvcInfo>& svcInfo);
    void getSvcMap(std::vector<fpi::SvcInfo> & svcMap);
    void getDLT( ::FDS_ProtocolInterface::CtrlNotifyDLTUpdate& _return, const int64_t nullarg);
    void getDMT( ::FDS_ProtocolInterface::CtrlNotifyDMTUpdate& _return, const int64_t nullarg);
    void notifyServiceRestart(const  ::fds::apis::NotifyHealthReport& report, const int64_t nullarg);

 protected:
    /**
    * @brief Sets up configdb used for persistence
    */
    virtual void setupConfigDb_() override;

    fds_mutex svcMapLock_;
    std::unordered_map<fpi::SvcUuid, fpi::SvcInfo, SvcUuidHash> svcMap_;
};
}  // namespace fds
#endif  // SOURCE_INCLUDE_NET_OMSVCPROCESS_H_
