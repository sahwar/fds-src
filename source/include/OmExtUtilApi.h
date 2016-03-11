/*
 * Copyright 2015 Formation Data Systems, Inc.
 */

#ifndef SOURCE_INCLUDE_OMEXTUTILAPI_H_
#define SOURCE_INCLUDE_OMEXTUTILAPI_H_

#include <vector>
#include <utility>

#include <fds_typedefs.h>

#include <util/Log.h>
#include <kvstore/configdb.h>

namespace fds
{
    class OmExtUtilApi
    {
    public:

        /* Making this class a singleton for ease of access */
        static OmExtUtilApi* getInstance();

        ~OmExtUtilApi() { };

        /* Functions to keep track of removed nodes */
        void         addToRemoveList(int64_t uuid, fpi::FDSP_MgrIdType svc_type);
        bool         isMarkedForRemoval(int64_t uuid);
        void         clearFromRemoveList(int64_t uuid);
        int          getPendingNodeRemoves(fpi::FDSP_MgrIdType svc_type);

        /* Functions to hold info related to migration aborts on OM restart
         * for both DM and SM */

        // SM related
        void         setSMAbortParams(bool abort, fds_uint64_t version);
        bool         isSMAbortAfterRestartTrue();
        void         clearSMAbortParams();
        fds_uint64_t getSMTargetVersionForAbort();

        // DM related
        void         setDMAbortParams(bool abort, fds_uint64_t version);
        bool         isDMAbortAfterRestartTrue();
        void         clearDMAbortParams();
        fds_uint64_t getDMTargetVersionForAbort();

        /*
         * Other service related utility functions that will be used by both
         * the OM as well as other components such as svcMgr
         */
        bool         isIncomingUpdateValid(fpi::SvcInfo& incomingSvcInfo, fpi::SvcInfo currentInfo);
        bool         isTransitionAllowed(fpi::ServiceStatus incoming, fpi::ServiceStatus current);

        static std::string  printSvcStatus(fpi::ServiceStatus svcStatus);
    private:

        OmExtUtilApi();
        // Stop compiler generating methods to copy the object
        OmExtUtilApi(OmExtUtilApi const& copy);
        OmExtUtilApi& operator=(OmExtUtilApi const& copy);

        static OmExtUtilApi*   m_instance;

        /* Member to track removedNodes */
        std::vector<std::pair<int64_t, fpi::FDSP_MgrIdType>> removedNodes;

        /* Keep track of migration interruptions */
        bool                sendSMMigAbortAfterRestart;
        bool                sendDMMigAbortAfterRestart;
        fds_uint64_t        dltTargetVersionForAbort;
        fds_uint64_t        dmtTargetVersionForAbort;


        //+--------------------------+---------------------------------------+
        //|     Incoming State       |    Transition allowed on States       |
        //+--------------------------+---------------------------------------+
        //    (0)  INVALID           |  Any
        //    (1)  ACTIVE            |  STARTED, DISCOVERED, INACTIVE_FAILED,
        //                           |  INACTIVE_STOPPED
        //    (2)  INACTIVE_STOPPED  |  STOPPED
        //    (3)  DISCOVERED        |  REMOVED, (svc not present before)
        //    (4)  STANDBY           |  ACTIVE (applicable only for PM)
        //    (5)  ADDED             |  svc not present before
        //    (6)  STARTED           |  ADDED, STOPPED, INACTIVE_STOPPED
        //    (7)  STOPPED           |  ACTIVE, STARTED, INACTIVE_FAILED
        //    (8)  REMOVED           |  STANDBY( applicable only for PM),
        //                           |  INACTIVE_STOPPED, STOPPED
        //    (9)  INACTIVE_FAILED   |  ACTIVE, STARTED
        //+--------------------------+---------------------------------------+


        std::vector<std::vector<fpi::ServiceStatus>> allowedStateTransitions =
        {
                // valid current states for incoming: INVALID(0)
                { fpi::SVC_STATUS_ACTIVE, fpi::SVC_STATUS_INACTIVE_STOPPED, fpi::SVC_STATUS_DISCOVERED,
                  fpi::SVC_STATUS_STANDBY, fpi::SVC_STATUS_ADDED, fpi::SVC_STATUS_STARTED, fpi::SVC_STATUS_STOPPED,
                  fpi::SVC_STATUS_REMOVED, fpi::SVC_STATUS_INACTIVE_FAILED },
                // valid current states for incoming: ACTIVE(1)
                { fpi::SVC_STATUS_STARTED, fpi::SVC_STATUS_DISCOVERED, fpi::SVC_STATUS_INACTIVE_FAILED,
                  fpi::SVC_STATUS_INACTIVE_STOPPED },
                // valid current states for incoming: INACTIVE_STOPPED(2)
                { fpi::SVC_STATUS_STOPPED },
                // valid current states for incoming: DISCOVERED(3)
                { fpi::SVC_STATUS_REMOVED },
                // valid current states for incoming: STANDBY(4)
                { fpi::SVC_STATUS_ACTIVE },
                // valid current states for incoming: ADDED(5) , empty since svc is not going to be present
                {},
                // valid current states for incoming: STARTED(6)
                { fpi::SVC_STATUS_ADDED, fpi::SVC_STATUS_STOPPED, fpi::SVC_STATUS_INACTIVE_STOPPED },
                // valid current states for incoming: STOPPED(7)
                { fpi::SVC_STATUS_ACTIVE, fpi::SVC_STATUS_STARTED, fpi::SVC_STATUS_INACTIVE_FAILED },
                // valid current states for incoming: REMOVED(8)
                { fpi::SVC_STATUS_STANDBY, fpi::SVC_STATUS_INACTIVE_STOPPED, fpi::SVC_STATUS_STOPPED },
                // valid current states for incoming: INACTIVE_FAILED(9)
                { fpi::SVC_STATUS_ACTIVE, fpi::SVC_STATUS_STARTED }
        };
    };

} // namespace fds

#endif  // SOURCE_INCLUDE_OMEXTUTILAPI_H_
