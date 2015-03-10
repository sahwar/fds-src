/*
 * Copyright 2013-2015 by Formation Data Systems, Inc.
 */
#ifndef SOURCE_ORCH_MGR_INCLUDE_OM_SVC_HANDLER_H_
#define SOURCE_ORCH_MGR_INCLUDE_OM_SVC_HANDLER_H_

#include <OmResources.h>
#include <fdsp/om_api_types.h>
#include <net/PlatNetSvcHandler.h>
#include <OmEventTracker.h>

namespace fds {

class OmSvcHandler : virtual public PlatNetSvcHandler
{
  public:
    OmSvcHandler();
    virtual ~OmSvcHandler();

    void om_node_svc_info(boost::shared_ptr<fpi::AsyncHdr>    &hdr,
                          boost::shared_ptr<fpi::NodeSvcInfo> &svc);

    void om_node_info(boost::shared_ptr<fpi::AsyncHdr>    &hdr,
                      boost::shared_ptr<fpi::NodeInfoMsg> &node);

    virtual void
    TestBucket(boost::shared_ptr<fpi::AsyncHdr>         &hdr,
                 boost::shared_ptr<fpi::CtrlTestBucket> &msg);

    virtual void
    CreateBucket(boost::shared_ptr<fpi::AsyncHdr>         &hdr,
                 boost::shared_ptr<fpi::CtrlCreateBucket> &msg);

    virtual void
    DeleteBucket(boost::shared_ptr<fpi::AsyncHdr>         &hdr,
                 boost::shared_ptr<fpi::CtrlDeleteBucket> &msg);

    virtual void
    ModifyBucket(boost::shared_ptr<fpi::AsyncHdr>         &hdr,
                 boost::shared_ptr<fpi::CtrlModifyBucket> &msg);

    virtual void
    SvcEvent(boost::shared_ptr<fpi::AsyncHdr>         &hdr,
                 boost::shared_ptr<fpi::CtrlSvcEvent> &msg);

    virtual void
    AbortTokenMigration(boost::shared_ptr<fpi::AsyncHdr> &hdr,
                        boost::shared_ptr<fpi::CtrlTokenMigrationAbort> &msg);

  protected:
    OM_NodeDomainMod         *om_mod;
    EventTracker<NodeUuid, Error, UuidHash, ErrorHash> event_tracker;

  private:
    void init_svc_event_handlers();
};

}  // namespace fds
#endif  // SOURCE_ORCH_MGR_INCLUDE_OM_SVC_HANDLER_H_
