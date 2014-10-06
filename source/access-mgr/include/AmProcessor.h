/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#ifndef SOURCE_ACCESS_MGR_INCLUDE_AMPROCESSOR_H_
#define SOURCE_ACCESS_MGR_INCLUDE_AMPROCESSOR_H_

#include <string>
#include <fds_module.h>
#include <FdsRandom.h>
#include <StorHvVolumes.h>
#include <StorHvQosCtrl.h>
#include <am-tx-mgr.h>
#include <AmDispatcher.h>

namespace fds {

/**
 * AM request processing layer. The processor handles state and
 * execution for AM requests.
 */
class AmProcessor : public Module, public boost::noncopyable {
  public:
    /**
     * The processor takes shared ptrs to a cache and tx manager.
     * TODO(Andrew): Remove the cache and tx from constructor
     * and make them owned by the processor. It's only this way
     * until we clean up the legacy path.
     * TODO(Andrew): Use a different structure than SHVolTable.
     */
    AmProcessor(const std::string &modName,
                AmDispatcher::shared_ptr _amDispatcher,
                StorHvQosCtrl     *_qosCtrl,
                StorHvVolumeTable *_volTable,
                AmTxManager::shared_ptr _amTxMgr);
    ~AmProcessor();
    typedef std::unique_ptr<AmProcessor> unique_ptr;

    /**
     * Module methods
     */
    int mod_init(SysParams const *const param);
    void mod_startup();
    void mod_shutdown();

    /**
     * Processes a start blob transaction
     */
    void startBlobTx(AmQosReq *qosReq);

    /**
     * Callback for start blob transaction
     */
    void startBlobTxCb(AmQosReq* qosReq,
                       const Error& error);

  private:
    /// Raw pointer to QoS controller
    // TODO(Andrew): Move this to unique once it's owned here.
    StorHvQosCtrl *qosCtrl;

    /// Raw pointer to table of attached volumes
    // TODO(Andrew): Move this unique once it's owned here.
    // Also, probably want a simpler class structure
    StorHvVolumeTable *volTable;

    /// Shared ptr to the dispatcher layer
    // TODO(Andrew): Decide if AM or Process owns this and make unique.
    // I'm leaning towards this layer owning it.
    AmDispatcher::shared_ptr amDispatcher;

    /// Shared ptr to the transaction manager
    // TODO(Andrew): Move to unique once owned here.
    AmTxManager::shared_ptr txMgr;

    /// Unique ptr to a random num generator for tx IDs
    RandNumGenerator::unique_ptr randNumGen;
};

}  // namespace fds

#endif  // SOURCE_ACCESS_MGR_INCLUDE_AMPROCESSOR_H_