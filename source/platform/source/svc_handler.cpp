/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

#include <platform/svc_handler.h>
#include <net/SvcMgr.h>
#include <net/SvcRequestPool.h>
#include <platform/platform_manager.h>

namespace fds
{
    namespace pm
    {
        SvcHandler::SvcHandler (CommonModuleProviderIf *provider, PlatformManager *platform) : PlatNetSvcHandler (provider)
        {
            this->platform = platform;

/* to be deprecated START */
            REGISTER_FDSP_MSG_HANDLER (fpi::ActivateServicesMsg, activateServices);
            REGISTER_FDSP_MSG_HANDLER (fpi::DeactivateServicesMsg, deactivateServices);
/* to be deprecated End */

            REGISTER_FDSP_MSG_HANDLER (fpi::NotifyAddServiceMsg, addService);
            REGISTER_FDSP_MSG_HANDLER (fpi::NotifyRemoveServiceMsg, removeService);
            REGISTER_FDSP_MSG_HANDLER (fpi::NotifyStartServiceMsg, startService);
            REGISTER_FDSP_MSG_HANDLER (fpi::NotifyStopServiceMsg, stopService);
        }

/* to be deprecated START */
        void SvcHandler::activateServices (boost::shared_ptr <fpi::AsyncHdr> &hdr, boost::shared_ptr <fpi::ActivateServicesMsg> &activateMsg)
        {
            platform->activateServices (activateMsg);
            sendAsyncResp (*hdr, FDSP_MSG_TYPEID (fpi::EmptyMsg), fpi::EmptyMsg());
        }

        void SvcHandler::deactivateServices (boost::shared_ptr <fpi::AsyncHdr> &hdr, boost::shared_ptr <fpi::DeactivateServicesMsg> &deactivateMsg)
        {
            platform->deactivateServices (deactivateMsg);
            sendAsyncResp (*hdr, FDSP_MSG_TYPEID (fpi::EmptyMsg), fpi::EmptyMsg());
        }
/* to be deprecated End */

        void SvcHandler::addService (boost::shared_ptr <fpi::AsyncHdr> &hdr, boost::shared_ptr <fpi::NotifyAddServiceMsg> &addServiceMessage)
        {
            sendAsyncResp (*hdr, FDSP_MSG_TYPEID (fpi::EmptyMsg), fpi::EmptyMsg());
        }

        void SvcHandler::removeService (boost::shared_ptr <fpi::AsyncHdr> &hdr, boost::shared_ptr <fpi::NotifyRemoveServiceMsg> &removeServiceMessage)
        {
            sendAsyncResp (*hdr, FDSP_MSG_TYPEID (fpi::EmptyMsg), fpi::EmptyMsg());
        }

        void SvcHandler::startService (boost::shared_ptr <fpi::AsyncHdr> &hdr, boost::shared_ptr <fpi::NotifyStartServiceMsg> &startServiceMessage)
        {
            sendAsyncResp (*hdr, FDSP_MSG_TYPEID (fpi::EmptyMsg), fpi::EmptyMsg());
        }

        void SvcHandler::stopService (boost::shared_ptr <fpi::AsyncHdr> &hdr, boost::shared_ptr <fpi::NotifyStopServiceMsg> &stopServiceMessage)
        {
            sendAsyncResp (*hdr, FDSP_MSG_TYPEID (fpi::EmptyMsg), fpi::EmptyMsg());
        }

    }  // namespace pm
}  // namespace fds
