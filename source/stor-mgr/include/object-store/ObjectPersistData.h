/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#ifndef SOURCE_STOR_MGR_INCLUDE_OBJECT_STORE_OBJECTPERSISTDATA_H_
#define SOURCE_STOR_MGR_INCLUDE_OBJECT_STORE_OBJECTPERSISTDATA_H_

#include <string>
#include <map>
#include <fds_module.h>
#include <fds_types.h>
#include <concurrency/RwLock.h>
#include <SmCtrl.h>
#include <persistent-layer/dm_io.h>
#include <persistent-layer/persistentdata.h>
#include <object-store/Scavenger.h>
#include <object-store/SmDiskMap.h>

namespace fds {


/**
 * TODO(Anna) We keep up to SM_MAX_TOKEN_FILES number of files per SM token.
 * We start writing to the first file, and once we exceed the max size
 * we start writing to a new file, and so on. Initially, the first file
 * id is set to SM_INIT_FILE_ID.
 * Currently we write to one file with ID, which on initial SM startup
 * is SM_INIT_FILE_ID.
 * When garbage collection starts, it creates a new file (we call shadow file)
 * and all writes go to this file. The id of shadow file is calculated by
 * flipping the bit SM_START_TOKFILE_ID_MASK of the current initial file id.
 * When garbage collection finishes moving all valid data from old files to new
 * file(s), the old files are removed and shadow files remain.
 */
#define SM_INVALID_FILE_ID         0
/**
 * Not a real file id. Use SM_WRITE_FILE_ID when need to access a file
 * we are currently appending
 */
#define SM_WRITE_FILE_ID           0x0001
#define SM_INIT_FILE_ID            0x0002
#define SM_START_TOKFILE_ID_MASK   0x4000
/**
 * Maximum number of files per SM token.
 * Ok to change this define to a higher value (up to 0x2000), increasing this
 * number will only increase the runtime of notifyEndGC() function, but not much
 */
#define SM_MAX_TOKEN_FILES        64

/**
 * Abstract class for handling scavenger (garbage collection) tasks on
 * persistent store
 */
class SmPersistStoreHandler {
  public:
    /**
     * @param[out] retStat stats of active file (which we are writing,
     * not gc) for a given SM token and tier
     */
    virtual void getSmTokenStats(fds_token_id smTokId,
                                 diskio::DataTier tier,
                                 diskio::TokenStat* retStat) = 0;

    /**
     * Notify about start garbage collection for given token id 'tok_id'
     * and tier. If many disks contain this token, then token file on each
     * disk will be compacted. All new writes will be re-routed to the
     * appropriate (new) token file.
     */
    virtual void notifyStartGc(fds_token_id smTokId,
                               diskio::DataTier tier) = 0;

    /**
     * Notify about end of garbage collection for a given token id
     * 'tok_id' and tier.
     */
    virtual Error notifyEndGc(fds_token_id smTokId,
                              diskio::DataTier tier) = 0;

    /**
     * Returns true if a given location is a shadow file
     */
    virtual fds_bool_t isShadowLocation(obj_phy_loc_t* loc,
                                        fds_token_id smTokId) = 0;
};

/**
 * Class that manages persistent storage of object data.
 */
class ObjectPersistData : public Module,
        public boost::noncopyable,
        public SmPersistStoreHandler {
  private:
    SmDiskMap::const_ptr smDiskMap;

    /**
     * Map of <tier, SM token id, file id> to FilePersistDataIO struct
     * <tier, token, file id> is encoded into fds_uint64_t
     * see getFileKey() method.
     */
    typedef std::map<fds_uint64_t, diskio::FilePersisDataIO *> TokFileMap;
    TokFileMap tokFileTbl;

    /**
     * Map from <tier, SM token id> to a current file id that we are writing
     * to. Note that we can infer the old file id from new file id by flipping
     * the "start fileid" bit
     */
    std::map<fds_uint64_t, fds_uint16_t> writeFileIdMap;
    fds_rwlock mapLock;  // lock for both tokFileTbl and writeFileIdMap

    // Scavenget (garbage collector)
    ScavControl::unique_ptr scavenger;

  public:
    explicit ObjectPersistData(const std::string &modName,
                               SmIoReqHandler *data_store);
    ~ObjectPersistData();

    typedef std::unique_ptr<ObjectPersistData> unique_ptr;

    /**
     * Opens object data files
     * @param[in] diskMap map of SM tokens to disks
     */
    Error openPersistDataStore(const SmDiskMap::const_ptr& diskMap);

    /**
     * Peristently stores object data.
     */
    Error writeObjectData(const ObjectID& objId,
                          diskio::DiskRequest* req);

    /**
     * Reads object data from persistent layer
     */
    Error readObjectData(const ObjectID& objId,
                         diskio::DiskRequest* req);

    /**
     * Notify that object was deleted to update stats for garbage collection
     * TODO(Anna) review this when revisiting GC
     */
    void notifyDataDeleted(const ObjectID& objId,
                           fds_uint32_t objSize,
                           const obj_phy_loc_t* loc);

     // Implementation of SmScavengerHandler
    void notifyStartGc(fds_token_id smTokId,
                       diskio::DataTier tier);
    Error notifyEndGc(fds_token_id smTokId,
                      diskio::DataTier tier);
    fds_bool_t isShadowLocation(obj_phy_loc_t* loc,
                                fds_token_id smTokId);
    void getSmTokenStats(fds_token_id smTokId,
                         diskio::DataTier tier,
                         diskio::TokenStat* retStat);

    // control commands
    Error scavengerControlCmd(SmScavengerCmd* scavCmd);

    // FDS module control functions
    int  mod_init(SysParams const *const param);
    void mod_startup();
    void mod_shutdown();

  private:  // methods
    /**
     * Opens SM token file on a given tier and given file id
     */
    Error openTokenFile(diskio::DataTier tier,
                        fds_token_id smTokId,
                        fds_uint16_t fileId);

    /**
     * Closes SM token file on a given tier and givel file id
     * and removes associated entry from tokFileTbl
     */
    void closeTokenFile(diskio::DataTier tier,
                        fds_token_id smTokId,
                        fds_uint16_t fileId);
    /**
     * Translates <tier, SM token id, file id> into uint64 key
     * into tokFileTbl
     * 0...31 bits: SM token id
     * 32..47 bits: file id
     * 48..63 bits: tier
     */
    inline fds_uint64_t getFileKey(diskio::DataTier tier,
                                   fds_token_id smTokId,
                                   fds_uint16_t fileId) const {
        fds_uint64_t key = tier;
        return (((key << 16) | fileId) << 16) | smTokId;
    }

    /**
     * Translates <tier, token id> into uint64 key into writeFileIdMap
     * 0...31 bits: token id
     * 32..47 bits: 0
     * 48..63 bits: tier
     */
    inline fds_uint64_t getWriteFileIdKey(diskio::DataTier tier,
                                          fds_token_id smTokId) const {
        return getFileKey(tier, smTokId, 0);
    }

    /**
     * Given file id, returns file id of the shadow file (shadow
     * file may actually not exist, so check that separately).
     * Only valid for file id of the first file in sequence.
     */
    inline fds_uint16_t getShadowFileId(fds_uint16_t fileId) const {
        if (fileId & SM_START_TOKFILE_ID_MASK)
            return fileId & (~SM_START_TOKFILE_ID_MASK);
        return fileId | SM_START_TOKFILE_ID_MASK;
    }

    /**
     * Returns file id of file we are writing to for this <tokId, tier>
     * tuple. If garbage collection is in progress, this will be the index of
     * of a shadow file, otherwise it will be the index of an active file
     */
    fds_uint16_t getWriteFileId(diskio::DataTier tier,
                                fds_token_id smTokId);

    diskio::FilePersisDataIO* getTokenFile(diskio::DataTier tier,
                                           fds_token_id smTokId,
                                           fds_uint16_t fileId);
};

}  // namespace fds

#endif  // SOURCE_STOR_MGR_INCLUDE_OBJECT_STORE_OBJECTPERSISTDATA_H_
