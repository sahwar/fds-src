/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#include <unordered_map>
#include <map>
#include <utility>
#include <vector>
#include <string>

#include <orch-mgr/om-service.h>
#include <fds_process.h>
#include <OmDataPlacement.h>

namespace fds {

/**********
 * Functions definitions for data
 * placement
 **********/
DataPlacement::DataPlacement()
        : Module("Data Placement Engine"),
          placeAlgo(NULL),
          commitedDlt(NULL),
          newDlt(NULL) {
    placementMutex = new fds_mutex("data placement mutex");
    curClusterMap = &gl_OMClusMapMod;
}

DataPlacement::~DataPlacement() {
    delete placementMutex;
    // delete curClusterMap;
    if (commitedDlt != NULL) {
        delete commitedDlt;
    }
    if (newDlt != NULL) {
        delete newDlt;
    }
}

void
DataPlacement::setAlgorithm(PlacementAlgorithm::AlgorithmTypes type) {
    placementMutex->lock();
    delete placeAlgo;
    switch (type) {
        case PlacementAlgorithm::AlgorithmTypes::RoundRobin:
            placeAlgo = new RoundRobinAlgorithm();
            break;
        case PlacementAlgorithm::AlgorithmTypes::ConsistHash:
            placeAlgo = new ConsistHashAlgorithm();
            break;

        default:
            fds_panic("Received unknown data placement algorithm type %u",
                      type);
    }
    algoType = type;
    placementMutex->unlock();
}

Error
DataPlacement::updateMembers(const NodeList &addNodes,
                             const NodeList &rmNodes) {
    placementMutex->lock();
    Error err = curClusterMap->updateMap(addNodes, rmNodes);
    // TODO(Andrew): We should be recomputing the DLT here.
    placementMutex->unlock();

    return err;
}

void
DataPlacement::computeDlt() {
    // Currently always create a new empty DLT.
    // Will change to be relative to the current.
    fds_uint64_t version;
    fds_verify(newDlt == NULL);

    if (commitedDlt == NULL) {
        version = DLT_VER_INVALID + 1;
    } else {
        version = commitedDlt->getVersion() + 1;
    }
    // If we have fewer members than total replicas
    // use the number of members as the replica count
    fds_uint32_t depth = curDltDepth;
    if (curClusterMap->getNumMembers() < curDltDepth) {
        depth = curClusterMap->getNumMembers();
    }

    // Allocate and compute new DLT
    newDlt = new DLT(curDltWidth,
                     depth,
                     version,
                     true);
    placementMutex->lock();
    placeAlgo->computeNewDlt(curClusterMap,
                             commitedDlt,
                             newDlt);

    // Compute DLT's reverse node to token map
    newDlt->generateNodeTokenMap();

    // store the dlt to config db
    fds_verify(configDB != NULL);
    if (!configDB->storeDlt(*newDlt, "next")) {
        GLOGWARN << "unable to store dlt to config db "
                << "[" << newDlt->getVersion() << "]";
    }

    // TODO(Andrew): We should version the (now) old DLT
    // before we delete it and replace it with the
    // new DLT. We should also update the DLT's
    // internal version.
    placementMutex->unlock();
}

/**
 * Begins a rebalance command between the nodes affect
 * in the current DLT change.
 */
Error
DataPlacement::beginRebalance() {
    Error err(ERR_OK);

    FDS_ProtocolInterface::FDSP_MsgHdrTypePtr msgHdr(
            new FDS_ProtocolInterface::FDSP_MsgHdrType());
    FDS_ProtocolInterface::FDSP_DLT_Data_TypePtr dltMsg(
            new FDS_ProtocolInterface::FDSP_DLT_Data_Type());

    placementMutex->lock();
    // find all nodes which need to do migration (=have new tokens)
    rebalanceNodes.clear();
    for (ClusterMap::const_iterator cit = curClusterMap->cbegin();
         cit != curClusterMap->cend();
         ++cit) {
        TokenList new_toks, old_toks;
        newDlt->getTokens(&new_toks, cit->first, 0);
        if (commitedDlt) {
            commitedDlt->getTokens(&old_toks, cit->first, 0);
        }
        // TODO(anna) TokenList should be a set so we can easily
        // search rather than vector -- anyway we shouldn't have duplicate
        // tokens in the TokenList
        for (fds_uint32_t i = 0; i < new_toks.size(); ++i) {
            fds_bool_t found = false;
            for (fds_uint32_t j = 0; j < old_toks.size(); ++j) {
                if (new_toks[i] == old_toks[j]) {
                    found = true;
                    break;
                }
            }
            // at least one new token for this node
            if (!found) {
                rebalanceNodes.insert(cit->first);
                break;
            }
        }
    }

    // pupulate dlt message -- same for all the nodes that need to migrate toks
    dltMsg->dlt_type= true;
    err = newDlt->getSerialized(dltMsg->dlt_data);
    if (!err.ok()) {
        LOGERROR << "Failed to fill in dlt_data, not sending migration msgs";
        placementMutex->unlock();
        return err;
    }

    // send start migration message to all nodes that have new tokens
    for (NodeUuidSet::const_iterator nit = rebalanceNodes.cbegin();
         nit != rebalanceNodes.cend();
         ++nit) {
        NodeUuid  uuid = *nit;
        OM_SmAgent::pointer na = OM_NodeDomainMod::om_local_domain()->om_sm_agent(uuid);
        NodeAgentCpReqClientPtr naClient = na->getCpClient();
        na->set_node_state(FDS_ProtocolInterface::FDS_Start_Migration);
        na->init_msg_hdr(msgHdr);
        msgHdr->msg_code = FDS_ProtocolInterface::FDSP_MSG_NOTIFY_MIGRATION;

        FDS_PLOG_SEV(g_fdslog, fds_log::notification)
                << "Sending the DLT migration request to node 0x"
                << std::hex << uuid.uuid_get_val() << std::dec;

        // invoke the RPC
        naClient->NotifyStartMigration(msgHdr, dltMsg);
    }
    placementMutex->unlock();

    FDS_PLOG_SEV(g_fdslog, fds_log::notification)
            << "Sent DLT migration event to " << rebalanceNodes.size() << " nodes";
    newDlt->dump();
    return err;
}

/**
 * Commits the current DLT as an 'official'
 * copy. The commit stores the DLT to the
 * permanent DLT history.
 */
void
DataPlacement::commitDlt() {
    fds_verify(newDlt != NULL);
    placementMutex->lock();
    fds_uint64_t oldVersion = -1;
    if (commitedDlt) {
        oldVersion = commitedDlt->getVersion();
        delete commitedDlt;
        commitedDlt = NULL;
    }

    // both the new & the old dlts should already be in the config db
    // just set their types

    if (!configDB->setDltType(newDlt->getVersion(), "current")) {
        LOGWARN << "unable to store dlt type to config db "
                << "[" << newDlt->getVersion() << "]";
    }

    if (!configDB->setDltType(0, "next")) {
        LOGWARN << "unable to store dlt type to config db "
                << "[" << newDlt->getVersion() << "]";
    }

    // TODO(prem) do we need to remove the old dlt from the config db ??
    // oldVersion ?

    commitedDlt = newDlt;
    newDlt = NULL;
    placementMutex->unlock();
}

const DLT*
DataPlacement::getCommitedDlt() const {
    return commitedDlt;
}

fds_uint64_t
DataPlacement::getLatestDltVersion() const {
    if (newDlt) {
        return newDlt->getVersion();
    } else if (commitedDlt) {
        return commitedDlt->getVersion();
    }
    return DLT_VER_INVALID;
}

const ClusterMap*
DataPlacement::getCurClustMap() const {
    return curClusterMap;
}

NodeUuidSet
DataPlacement::getRebalanceNodes() const {
    return rebalanceNodes;
}

int
DataPlacement::mod_init(SysParams const *const param) {
    Module::mod_init(param);

    FdsConfigAccessor conf_helper(g_fdsprocess->get_conf_helper());
    std::string algo_type_str = conf_helper.get<std::string>("placement_algo");
    curDltWidth = conf_helper.get<int>("token_factor");
    curDltDepth = conf_helper.get<int>("replica_factor");

    PlacementAlgorithm::AlgorithmTypes type =
            PlacementAlgorithm::AlgorithmTypes::ConsistHash;
    if (algo_type_str.compare("ConsistHash") == 0) {
        type = PlacementAlgorithm::AlgorithmTypes::ConsistHash;
    } else if (algo_type_str.compare("RoundRobin") == 0) {
        type = PlacementAlgorithm::AlgorithmTypes::RoundRobin;
    } else {
        FDS_PLOG_SEV(g_fdslog, fds_log::warning)
                <<"DataPlacement: unknown placement algorithm type in "
                << "config file, will use Consistent Hashing algorith";
    }

    FDS_PLOG_SEV(g_fdslog, fds_log::notification)
            << "DataPlacement: DLT width " << curDltWidth
            << ", dlt depth " << curDltDepth
            << ", algorithm " << algo_type_str;

    setAlgorithm(type);

    curClusterMap = OM_Module::om_singleton()->om_clusmap_mod();

    return 0;
}

void
DataPlacement::mod_startup() {
}

void
DataPlacement::mod_shutdown() {
}

Error DataPlacement::loadDltsFromConfigDB(const NodeUuidSet& sm_services) {
    Error err(ERR_OK);

    // this function should be called only during init..
    fds_verify(commitedDlt == NULL);
    fds_verify(newDlt == NULL);

    // if there are no nodes, we are not going to load any dlts
    if (sm_services.size() == 0) {
        LOGNOTIFY << "Not loading DLTs from configDB since there are "
                  << "no persisted SMs";
        return err;
    }

    fds_uint64_t currentVersion = configDB->getDltVersionForType("current");
    if (currentVersion > 0) {
        DLT* dlt = new DLT(0, 0, 0, false);
        if (!configDB->getDlt(*dlt, currentVersion)) {
            LOGCRITICAL << "unable to load (current) dlt version "
                        <<"["<< currentVersion << "] from configDB";
            err = Error(ERR_PERSIST_STATE_MISMATCH);
        } else {
            // check if DLT is valid with respect to nodes
            // i.e. only contains node uuis that are in nodes' set
            // does not contain any zeroes, etc.
            err = checkDltValid(dlt, sm_services);
            if (err.ok()) {
                LOGNOTIFY << "Current DLT in config DB is valid!";
                // we will set newDLT because we don't know yet if
                // the nodes in DLT will actually come up...
                newDlt = dlt;
            }
        }

        if (!err.ok()) {
            delete dlt;
            return err;
        }
    } else {
        // we got > 0 nodes from configDB but there is no DLT
        // going to throw away persistent state, because there is a mismatch
        LOGWARN << "No current DLT even though we persisted "
                << sm_services.size() << " nodes";
        return Error(ERR_PERSIST_STATE_MISMATCH);
    }

    // At this point we are not supporting the case when we went down
    // during migration -- if we see target DLT in configDB, we will just
    // throw away persistent state
    // TODO(anna) implement support for recovering from 'in the middle of
    // migration' state.
    fds_uint64_t nextVersion = configDB->getDltVersionForType("next");
    if (nextVersion > 0 && nextVersion != currentVersion) {
        // at this moment not handling this case, so return error for now
        LOGWARN << "We are not yet supporting recovering persistent state "
                << "when we were in the middle of migration, so will for now "
                << " ignore persisted DLT and nodes";
        delete newDlt;
        newDlt = NULL;
        return Error(ERR_NOT_IMPLEMENTED);
        /*
        DLT* dlt = new DLT(0, 0, 0, false);
        if (!configDB->getDlt(*dlt, nextVersion)) {
            LOGCRITICAL << "unable to get (next) dlt version [" << currentVersion << "]";
            delete dlt;
        } else {
            // set the next dlt
            newDlt = dlt;
        }
        */
    } else {
        if (0 == nextVersion) {
            LOGDEBUG << "There is only commited DLT in configDB (OK)";
        } else {
            LOGDEBUG << "Both current and target DLT are of same version (OK): "
                     << nextVersion;
        }
    }

    return err;
}

void DataPlacement::setConfigDB(kvstore::ConfigDB* configDB) {
    this->configDB = configDB;
}

//
// Checks if DLT matches the set of given nodes:
// -- DLT must not contain any uuids that are not in nodes set
// -- DLT should not have any cells with 0
// -- nodes in each DLT column must be unique
//
Error DataPlacement::checkDltValid(const DLT* dlt,
                                   const NodeUuidSet& sm_services) {
    Error err(ERR_OK);
    fds_uint32_t col_depth = dlt->getDepth();
    fds_uint32_t num_tokens = dlt->getNumTokens();

    // we should not have more rows than nodes
    if (col_depth > sm_services.size()) {
        LOGWARN << "DLT has more rows (" << col_depth
                << ") than nodes (" << sm_services.size() << ")";
        return Error(ERR_INVALID_DLT);
    }

    // check each column in DLT
    NodeUuidSet col_set;
    for (fds_token_id i = 0; i < num_tokens; ++i) {
        col_set.clear();
        DltTokenGroupPtr column = dlt->getNodes(i);
        for (fds_uint32_t j = 0; j < col_depth; ++j) {
            NodeUuid cur_uuid = column->get(j);
            if ((cur_uuid.uuid_get_val() == 0) ||
                (sm_services.count(cur_uuid) == 0)) {
                // unexpected uuid in this DLT cell
                LOGWARN << "DLT contains unexpected uuid " << std::hex
                        << cur_uuid.uuid_get_val() << std::dec;
                return Error(ERR_INVALID_DLT);
            }
            col_set.insert(cur_uuid);
        }

        // make sure that column contains all unique uuids
        if (col_set.size() < col_depth) {
            LOGWARN << "Found non-unique uuids in DLT column " << i;
            return Error(ERR_INVALID_DLT);
        }
    }

    return err;
}

}  // namespace fds
