#ifndef TESTAM_HPP_
#define TESTAM_HPP_

/*
 * Copyright 2016 Formation Data Systems, Inc.
 */
#include <fds_process.h>
#include <fdsp/PlatNetSvc.h>
#include <net/SvcMgr.h>
#include <net/SvcProcess.h>
#include <net/PlatNetSvcHandler.h>
#include <testlib/ProcessHandle.hpp>
#include <testlib/SvcMsgFactory.h>
#include <testlib/TestUtils.h>
#include <net/VolumeGroupHandle.h>
#include <boost/filesystem.hpp>
#include "fdsp/common_constants.h"


namespace fds {

struct TestAm : SvcProcess {
    TestAm(int argc, char *argv[], bool initAsModule)
    {
        handler_ = MAKE_SHARED<PlatNetSvcHandler>(this);
        auto processor = boost::make_shared<fpi::PlatNetSvcProcessor>(handler_);

        /**
         * Note on Thrift service compatibility:
         * Because asynchronous service requests are routed manually, any new
         * PlatNetSvc version MUST extend a previous PlatNetSvc version.
         * Only ONE version of PlatNetSvc API can be included in the list of
         * multiplexed services.
         *
         * For other new major service API versions (not PlatNetSvc), pass
         * additional pairs of processor and Thrift service name.
         */
        TProcessorMap processors;
        processors.insert(std::make_pair<std::string,
            boost::shared_ptr<apache::thrift::TProcessor>>(
                fpi::commonConstants().PLATNET_SERVICE_NAME, processor));

        init(argc, argv, initAsModule, "platform.conf",
             "fds.am.", "am.log", nullptr, handler_, processors);
        REGISTER_FDSP_MSG_HANDLER_GENERIC(handler_, \
                                          fpi::AddToVolumeGroupCtrlMsg, addToVolumeGroup);
    }
    virtual int run() override
    {
        readyWaiter.done();
        shutdownGate_.waitUntilOpened();
        return 0;
    }
    void setVolumeHandle(VolumeGroupHandle *h) {
        vc_ = h;
    }
    void addToVolumeGroup(fpi::AsyncHdrPtr& asyncHdr,
                          fpi::AddToVolumeGroupCtrlMsgPtr& addMsg)
    {
        fds_volid_t volId(addMsg->groupId);
        vc_->handleAddToVolumeGroupMsg(
            addMsg,
            [this, asyncHdr](const Error& e,
                             const fpi::AddToVolumeGroupRespCtrlMsgPtr &payload) {
            asyncHdr->msg_code = e.GetErrno();
            handler_->sendAsyncResp(*asyncHdr,
                                    FDSP_MSG_TYPEID(fpi::AddToVolumeGroupRespCtrlMsg),
                                    *payload);
            });
    }
    PlatNetSvcHandlerPtr handler_;
    VolumeGroupHandle *vc_{nullptr};
};


} // namespace fds

#endif
