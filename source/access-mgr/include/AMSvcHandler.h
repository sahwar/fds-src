/*
 * Copyright 2014 By Formation Data Systems, Inc.
 */
#ifndef SOURCE_ACCESS_MGR_INCLUDE_AMSVCHANDLER_H_
#define SOURCE_ACCESS_MGR_INCLUDE_AMSVCHANDLER_H_

#include <net/net-service.h>
#include <net/PlatNetSvcHandler.h>
#include <fds_typedefs.h>
#include <fdsp/AMSvc.h>
#include <AccessMgr.h>  /* For references to "am" */

/* Forward declarations */
namespace FDS_ProtocolInterface {
class AMSvcClient;
class AMSvcProcessor;
}


namespace fds {

class AMSvcHandler :  virtual public fpi::AMSvcIf, virtual public PlatNetSvcHandler
{
  public:
    AMSvcHandler();
    virtual ~AMSvcHandler();

    /**
     * Message handlers
     */
    virtual void
    notifySvcChange(boost::shared_ptr<fpi::AsyncHdr>    &hdr,
                    boost::shared_ptr<fpi::NodeSvcInfo> &msg);

    virtual void
    SetThrottleLevel(boost::shared_ptr<fpi::AsyncHdr>           &hdr,
                     boost::shared_ptr<fpi::CtrlNotifyThrottle> &msg);

    virtual void
    QoSControl(boost::shared_ptr<fpi::AsyncHdr>           &hdr,
                     boost::shared_ptr<fpi::CtrlNotifyQoSControl> &msg);

    virtual void
    NotifyModVol(boost::shared_ptr<fpi::AsyncHdr>         &hdr,
                 boost::shared_ptr<fpi::CtrlNotifyVolMod> &vol_msg);

    virtual void
    AttachVol(boost::shared_ptr<fpi::AsyncHdr>         &hdr,
              boost::shared_ptr<fpi::CtrlNotifyVolAdd> &vol_msg);

    virtual void
    DetachVol(boost::shared_ptr<fpi::AsyncHdr>            &hdr,
              boost::shared_ptr<fpi::CtrlNotifyVolRemove> &vol_msg);

    virtual void
    NotifyDMTUpdate(boost::shared_ptr<fpi::AsyncHdr>            &hdr,
                              boost::shared_ptr<fpi::CtrlNotifyDMTUpdate> &msg);

    virtual void
    NotifyDLTUpdate(boost::shared_ptr<fpi::AsyncHdr>            &hdr,
                              boost::shared_ptr<fpi::CtrlNotifyDLTUpdate> &msg);

    void
    NotifyDLTUpdateCb(boost::shared_ptr<fpi::AsyncHdr>            &hdr,
                      boost::shared_ptr<fpi::CtrlNotifyDLTUpdate> &dlt,
                      const Error                                 &err);

    virtual void
    shutdownAM(boost::shared_ptr<fpi::AsyncHdr>         &hdr,
               boost::shared_ptr<fpi::ShutdownMODMsg>   &shutdownMsg);
};

}  // namespace fds

#endif  // SOURCE_ACCESS_MGR_INCLUDE_AMSVCHANDLER_H_
