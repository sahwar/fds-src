/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

#include <string>
#include <PerfTrace.h>
#include <fds_process.h>
#include <object-store/ObjectPersistData.h>

namespace fds {

ObjectPersistData::ObjectPersistData(const std::string &modName,
                                     SmIoReqHandler *data_store)
        : Module(modName.c_str()),
          shuttingDown(false),
          scavenger(new ScavControl("SM Disk Scavenger", data_store, this)) {
}

ObjectPersistData::~ObjectPersistData() {
    // First destruct scavenger
    scavenger.reset();
    // Now close levelDBs
    diskio::FilePersisDataIO* delFdesc = NULL;
    for (TokFileMap::iterator it = tokFileTbl.begin();
         it != tokFileTbl.end();
         ++it) {
        delFdesc = it->second;
        if (delFdesc) {
            // file is closed in FilePersisDataIO destructor
            delete delFdesc;
            it->second = NULL;
        }
    }
}

Error
ObjectPersistData::openPersistDataStore(const SmDiskMap::const_ptr& diskMap,
                                        fds_bool_t pristineState) {
    Error err(ERR_OK);
    smDiskMap = diskMap;
    DiskIdSet ssdIds = diskMap->getDiskIds(diskio::flashTier);
    LOGDEBUG << "Will open token data files";

    SmTokenSet smToks = diskMap->getSmTokens();
    diskio::DataTier tier = diskio::diskTier;
    for (fds_uint32_t tierNum = 0; tierNum < 2; ++tierNum) {
        for (SmTokenSet::const_iterator cit = smToks.cbegin();
             cit != smToks.cend();
             ++cit) {
            // get write file IDs from SM superblock and open corresponding token files
            fds_uint64_t wkey = getWriteFileIdKey(tier, *cit);
            fds_uint16_t fileId = diskMap->superblock->getWriteFileId(*cit, tier);
            fds_verify(fileId != SM_INVALID_FILE_ID);
            write_synchronized(mapLock) {
                fds_verify(writeFileIdMap.count(wkey) == 0);
                writeFileIdMap[wkey] = fileId;
            }

            // actually open SM token file
            err = openTokenFile(tier, *cit, fileId);
            fds_verify(err != ERR_DUPLICATE);  // file should not be already open
            if (!err.ok()) {
                LOGERROR << "Failed to open File for SM token " << *cit
                         << " tier " << tier << " " << err;
                break;
            }

            // also open old file if compaction is in progress
            if (diskMap->superblock->compactionInProgress(*cit, tier)) {
                fds_uint16_t oldFileId = getShadowFileId(fileId);
                err = openTokenFile(diskio::diskTier, *cit, oldFileId);
                fds_verify(err.ok());
            }
        }
        if ((tierNum == 0) && (ssdIds.size() > 0)) {
            // also open token files on SSDs
            fds_verify(tier != diskio::flashTier);
            LOGDEBUG << "Will open token files on SSDs";
            tier = diskio::flashTier;
        } else {
            // no SSDs, finished opening token files
            break;
        }
    }

    // at this moment scavenger should be disabled
    if (!pristineState) {
        // for now this just tells scavenger that some
        // stats may not be available (like delete bytes,
        // reclaimable bytes) because we don't persist them (yet)
        scavenger->setPersistState();
    }

    // we enable scavenger by default
    err = scavenger->enableScavenger(diskMap, SM_CMD_INITIATOR_USER);
    if (!err.ok()) {
        if (err == ERR_SM_GC_TEMP_DISABLED) {
            LOGWARN << "Failed to start scavenger because a background "
                    << "process requires scavenger not run; Scavenger will "
                    << "enabled when the background process finishes";
            err = ERR_OK;   // ok, scavenger will be enabled later
        } else {
            LOGERROR << "Failed to start Scavenger " << err;
        }
    }
    return err;
}

Error
ObjectPersistData::writeObjectData(const ObjectID& objId,
                                   diskio::DiskRequest* req) {
    fds_token_id smTokId = smDiskMap->smTokenId(objId);
    fds_uint16_t fileId = getWriteFileId(req->getTier(), smTokId);
    diskio::PersisDataIO *iop = getTokenFile(req->getTier(),
                                             smTokId,
                                             fileId, false);
    if (shuttingDown) {
        return ERR_NOT_READY;
    }

    fds_verify(iop);
    return iop->disk_write(req);
}

Error
ObjectPersistData::readObjectData(const ObjectID& objId,
                                  diskio::DiskRequest* req) {
    obj_phy_loc_t *loc = req->req_get_phy_loc();
    fds_token_id smTokId = smDiskMap->smTokenId(objId);
    fds_uint16_t fileId = loc->obj_file_id;
    diskio::PersisDataIO *iop = getTokenFile(req->getTier(),
                                             smTokId,
                                             fileId, true);
    if (shuttingDown) {
        return ERR_NOT_READY;
    }

    fds_verify(iop);
    return iop->disk_read(req);
}

void
ObjectPersistData::notifyDataDeleted(const ObjectID& objId,
                                     fds_uint32_t objSize,
                                     const obj_phy_loc_t* loc) {
    fds_token_id smTokId = smDiskMap->smTokenId(objId);
    fds_uint16_t fileId = loc->obj_file_id;
    diskio::PersisDataIO *iop = getTokenFile((diskio::DataTier)loc->obj_tier,
                                             smTokId,
                                             fileId, true);

    if (shuttingDown) {
      return;  // ignore for now, anyway delete stats are not persisted
    }
    iop->disk_delete(objSize);
}

void
ObjectPersistData::notifyStartGc(fds_token_id smTokId,
                                 diskio::DataTier tier) {
    Error err(ERR_OK);
    fds_uint16_t curFileId = SM_INVALID_FILE_ID;
    fds_uint16_t newFileId = SM_INVALID_FILE_ID;
    fds_uint64_t wkey = getWriteFileIdKey(tier, smTokId);

    // if compaction already in progress, shadow file does not change
    if (smDiskMap->superblock->compactionInProgress(smTokId, tier)) {
        LOGNOTIFY << "Compaction already in progress for token " << smTokId
                  << " tier " << tier << " -- will continue";
        return;
    }

    read_synchronized(mapLock) {
        fds_verify(writeFileIdMap.count(wkey) > 0);
        curFileId = writeFileIdMap[wkey];
    }

    // calculate new file id (file we will start writing
    // and to which non-garbage data will be copied)
    // but do not set until we open token file
    newFileId = getShadowFileId(curFileId);

    // first persist new file ID and set compaction state
    err = smDiskMap->superblock->changeCompactionState(smTokId, tier,
                                                       true, newFileId);
    fds_verify(err.ok());

    // we shouldn't have a file with newFileId open; if it is open,
    // most likely we did not finish last GC properly
    // openTokenFile() method will verify this
    // open file we are going to write to
    err = openTokenFile(tier, smTokId, newFileId);
    fds_verify(err.ok());

    // set new file id
    write_synchronized(mapLock) {
        fds_verify(writeFileIdMap.count(wkey) > 0);
        writeFileIdMap[wkey] = newFileId;
    }
}

Error
ObjectPersistData::notifyEndGc(fds_token_id smTokId,
                               diskio::DataTier tier) {
    Error err(ERR_OK);
    fds_uint64_t wkey = getWriteFileIdKey(tier, smTokId);
    fds_uint16_t shadowFileId = SM_INVALID_FILE_ID;
    fds_uint16_t oldFileId = SM_INVALID_FILE_ID;
    diskio::FilePersisDataIO *delFdesc = NULL;

    write_synchronized(mapLock) {
        fds_verify(writeFileIdMap.count(wkey) > 0);
        shadowFileId = writeFileIdMap[wkey];

        // old file id is the same as next shadow file id
        oldFileId = getShadowFileId(shadowFileId);

        // remove the token file with old file id (garbage collect)
        // it is up to the TokenCompactor to check that it copied all
        // non-garbage data to the shadow file
        // TODO(Anna) update below code when supporting multiple files
        fds_uint64_t fkey = getFileKey(tier, smTokId, oldFileId);
        fds_verify(tokFileTbl.count(fkey) > 0);
        delFdesc = tokFileTbl[fkey];
        if (delFdesc) {
            err = delFdesc->delete_file();
            delete delFdesc;
            delFdesc = NULL;
        }
        tokFileTbl.erase(fkey);
    }

    if (err.ok()) {
        // compaction is done and we successfully deleted the old file
        err = smDiskMap->superblock->changeCompactionState(smTokId, tier, false, 0);
        fds_verify(err.ok());
    }
    return err;
}

fds_bool_t
ObjectPersistData::isShadowLocation(obj_phy_loc_t* loc,
                                    fds_token_id smTokId) {
    fds_uint16_t curFileId = getWriteFileId(static_cast<diskio::DataTier>(loc->obj_tier),
                                            smTokId);
    return (curFileId == loc->obj_file_id);
}

Error
ObjectPersistData::openTokenFile(diskio::DataTier tier,
                                 fds_token_id smTokId,
                                 fds_uint16_t fileId) {
    Error err(ERR_OK);
    diskio::FilePersisDataIO *fdesc = NULL;
    fds_uint16_t diskId = smDiskMap->getDiskId(smTokId, tier);

    // this method expects explicit file ids
    fds_verify(fileId != SM_WRITE_FILE_ID);
    fds_verify(fileId != SM_INVALID_FILE_ID);

    // check if we got a valid disk ID
    if (!ObjectLocationTable::isDiskIdValid(diskId)) {
        // means there are no disks on this tier
        LOGWARN << "Couldn't open SM token file for SM token "
                << smTokId << " tier " << tier << ", most likely no disks on requested tier";
        return ERR_NOT_FOUND;  // TODO(Anna) do we want separate error?
    }

    // if we are here, params are valid
    fds_uint64_t fkey = getFileKey(tier, smTokId, fileId);  // key to actual persist io datastruct
    std::string filename = smDiskMap->getDiskPath(smTokId, tier) + "/tokenFile_"
            + std::to_string(smTokId) + "_" + std::to_string(fileId);

    SCOPEDWRITE(mapLock);
    if (tokFileTbl.count(fkey) > 0) {
        return ERR_DUPLICATE;
    }
    fdesc = new(std::nothrow) diskio::FilePersisDataIO(filename.c_str(), fileId, diskId);
    if (!fdesc) {
        LOGERROR << "Failed to create FilePersisDataIO for " << filename;
        return ERR_OUT_OF_MEMORY;
    }
    tokFileTbl[fkey] = fdesc;
    return err;
}

void
ObjectPersistData::closeTokenFile(diskio::DataTier tier,
                                  fds_token_id smTokId,
                                  fds_uint16_t fileId) {
    diskio::FilePersisDataIO *delFdesc = NULL;
    fds_uint64_t fkey = getFileKey(tier, smTokId, fileId);

    // if need to close current file ID, must specify file id
    // explicitly, instead of using SM_WRITE_FILE_ID
    fds_verify(fileId != SM_WRITE_FILE_ID);
    fds_verify(fileId != SM_INVALID_FILE_ID);

    SCOPEDWRITE(mapLock);
    if (tokFileTbl.count(fkey) > 0) {
        delFdesc = tokFileTbl[fkey];
        if (delFdesc) {
            delete delFdesc;
            delFdesc = NULL;
        }
        tokFileTbl.erase(fkey);
    }
}

diskio::FilePersisDataIO*
ObjectPersistData::getTokenFile(diskio::DataTier tier,
                                fds_token_id smTokId,
                                fds_uint16_t fileId,
                                fds_bool_t openIfNotExist) {
    diskio::FilePersisDataIO *fdesc = NULL;
    fds_uint64_t fkey = getFileKey(tier, smTokId, fileId);

    read_synchronized(mapLock) {
        if (tokFileTbl.count(fkey) > 0) {
            return tokFileTbl[fkey];
        } else {
            if (shuttingDown) {
                return NULL;
            }
            fds_verify(openIfNotExist == true);
        }
    }

    // if we are here, file not open and we were asked to open it
    Error err = openTokenFile(tier, smTokId, fileId);
    fds_verify(err.ok() || (err == ERR_DUPLICATE));
    SCOPEDREAD(mapLock);
    fds_verify(tokFileTbl.count(fkey) > 0);
    return tokFileTbl[fkey];
}

fds_uint16_t
ObjectPersistData::getWriteFileId(diskio::DataTier tier,
                                  fds_token_id smTokId) {
    fds_uint64_t key = getWriteFileIdKey(tier, smTokId);
    SCOPEDREAD(mapLock);
    fds_verify(writeFileIdMap.count(key) > 0);
    return writeFileIdMap[key];
}

void
ObjectPersistData::getSmTokenStats(fds_token_id smTokId,
                                   diskio::DataTier tier,
                                   diskio::TokenStat* retStat) {
    fds_uint16_t fileId = getWriteFileId(tier, smTokId);
    diskio::FilePersisDataIO *fdesc = getTokenFile(tier, smTokId, fileId, false);
    if (shuttingDown) {
        return;
    }

    fds_verify(fdesc);

    // TODO(Anna) if we do multiple files per SM token, this method should
    // change; current assumption there is only one file per SM token
    // (or two we are garbage collecting)

    // fill in token stat that we return
    (*retStat).tkn_id = smTokId;
    (*retStat).tkn_tot_size = fdesc->get_total_bytes();
    (*retStat).tkn_reclaim_size = fdesc->get_deleted_bytes();
}

Error
ObjectPersistData::scavengerControlCmd(SmScavengerCmd* scavCmd) {
    Error err(ERR_OK);
    LOGDEBUG << "Executing scavenger command " << scavCmd->command;
    switch (scavCmd->command) {
        case SmScavengerCmd::SCAV_ENABLE:
            err = scavenger->enableScavenger(smDiskMap, scavCmd->initiator);
            break;
        case SmScavengerCmd::SCAV_DISABLE:
            scavenger->disableScavenger(scavCmd->initiator);
            break;
        case SmScavengerCmd::SCAV_START:
            scavenger->startScavengeProcess();
            break;
        case SmScavengerCmd::SCAV_STOP:
            scavenger->stopScavengeProcess();
            break;
        case SmScavengerCmd::SCAV_SET_POLICY:
            {
                SmScavengerSetPolicyCmd *policyCmd =
                        static_cast<SmScavengerSetPolicyCmd*>(scavCmd);
                err = scavenger->setScavengerPolicy(policyCmd->policy->dsk_threshold1,
                                                    policyCmd->policy->dsk_threshold2,
                                                    policyCmd->policy->token_reclaim_threshold,
                                                    policyCmd->policy->tokens_per_dsk);
            }
            break;
        case SmScavengerCmd::SCAV_GET_POLICY:
            {
                SmScavengerGetPolicyCmd *policyCmd =
                        static_cast<SmScavengerGetPolicyCmd*>(scavCmd);
                err = scavenger->getScavengerPolicy(policyCmd->retPolicy);
            }
            break;
        case SmScavengerCmd::SCAV_GET_PROGRESS:
            {
                SmScavengerGetProgressCmd *progCmd =
                        static_cast<SmScavengerGetProgressCmd*>(scavCmd);
                progCmd->retProgress = scavenger->getProgress();
            }
            break;
        case SmScavengerCmd::SCAV_GET_STATUS:
            {
                SmScavengerGetStatusCmd *statCmd =
                        static_cast<SmScavengerGetStatusCmd*>(scavCmd);
                scavenger->getScavengerStatus(statCmd->retStatus);
            }
            break;
        case SmScavengerCmd::SCRUB_ENABLE:
            {
                scavenger->setDataVerify(true);
            }
            break;
        case SmScavengerCmd::SCRUB_DISABLE:
            {
                scavenger->setDataVerify(false);
            }
            break;
        case SmScavengerCmd::SCRUB_GET_STATUS:
            {
                SmScrubberGetStatusCmd *scrubCmd =
                        static_cast<SmScrubberGetStatusCmd*>(scavCmd);
                scavenger->getDataVerify(scrubCmd->scrubStat);
            }
            break;
        default:
            fds_panic("Unknown scavenger command");
    };

    return err;
}

/**
 * Module initialization
 */
int
ObjectPersistData::mod_init(SysParams const *const p) {
    shuttingDown = false;
    static Module *objPersistDepMods[] = {
        scavenger.get(),
        NULL
    };
    mod_intern = objPersistDepMods;

    fds_bool_t verify =
            g_fdsprocess->get_fds_config()->get<bool>("fds.sm.data_verify_background");
    scavenger->setDataVerify(verify);

    Module::mod_init(p);
    return 0;
}

/**
 * Module startup
 */
void
ObjectPersistData::mod_startup() {
    Module::mod_startup();
}

/**
 * Module shutdown
 */
void
ObjectPersistData::mod_shutdown() {
    LOGNOTIFY;
    {
        SCOPEDWRITE(mapLock);
        shuttingDown = true;
        // Now close levelDBs
        diskio::FilePersisDataIO* delFdesc = NULL;
        for (TokFileMap::iterator it = tokFileTbl.begin();
             it != tokFileTbl.end();
             ++it) {
            delFdesc = it->second;
            if (delFdesc) {
                // file is closed in FilePersisDataIO destructor
                delete delFdesc;
                it->second = NULL;
            }
        }
        tokFileTbl.clear();
    }
    Module::mod_shutdown();
    LOGDEBUG << "Done.";
}

}  // namespace fds
