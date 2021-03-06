/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#include <string>
#include <set>
#include <map>
#include <utility>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <boost/crc.hpp>
#include <util/Log.h>
#include <fds_process.h>
#include <dlt.h>
#include <util/disk_utils.h>
#include <object-store/SmTokenPlacement.h>
#include <object-store/SmSuperblock.h>
extern "C" {
#include <fcntl.h>
}

namespace fds {

/************************************
 * SuperblockHeader Ifaces
 */

SmSuperblockHeader::SmSuperblockHeader()
{
    /* poison the superblock header
     */
    memset(this, SmSuperblockHdrPoison, sizeof(*this));
}

void
SmSuperblockHeader::initSuperblockHeader()
{
    /* Fill the header with static information
     */
    this->SmSbHdrMagic = SmSuperblockMagicValue;

    /* The header size incliude the padded area.
     */
    this->SmSbHdrHeaderSize = sizeof(*this);
    this->SmSbHdrMajorVer = SmSuperblockMajorVer;
    this->SmSbHdrMinorVer = SmSuperblockMinorVer;
    this->SmSbHdrDataOffset = sizeof(*this);
    this->SmSbHdrSuperblockSize = sizeof(struct SmSuperblock);
    this->SmSbHdrOffsetBeginning = offsetof(struct SmSuperblockHeader,
                                            SmSbHdrMagic);
    this->SmSbHdrOffsetEnd = offsetof(struct SmSuperblockHeader,
                                      SmSbHdrLastFieldDummy);
}


Error
SmSuperblockHeader::validateSuperblockHeader()
{
    Error err(ERR_OK);

    /* Fill the header with static information
     */
    if ((SmSbHdrMagic != SmSuperblockMagicValue) ||
        (SmSbHdrHeaderSize != sizeof(*this)) ||
        (SmSbHdrMajorVer != SmSuperblockMajorVer) ||
        (SmSbHdrMinorVer != SmSuperblockMinorVer) ||
        (SmSbHdrDataOffset != sizeof(*this)) ||
        (SmSbHdrSuperblockSize != sizeof(struct SmSuperblock)) ||
        (SmSbHdrOffsetBeginning != offsetof(struct SmSuperblockHeader,
                                            SmSbHdrMagic)) ||
        (SmSbHdrOffsetEnd != offsetof(struct SmSuperblockHeader,
                                      SmSbHdrLastFieldDummy))) {
        err = ERR_SM_SUPERBLOCK_DATA_CORRUPT;
    }

    return err;
}


/************************************
 * Superblock Ifaces
 */

SmSuperblock::SmSuperblock()
{
    memset(SmSuperblockReserved, '\0', sizeof(SmSuperblockReserved));
    memset(SmSuperblockPadding, '\0', sizeof(SmSuperblockPadding));
}

void
SmSuperblock::initSuperblock()
{
    Header.initSuperblockHeader();
    resync = true;
    DLTVersion = DLT_VER_INVALID;
}


Error
SmSuperblock::readSuperblock(const std::string& path)
{
    Error err(ERR_OK);

    /* Check if the file exists before trying to read.
     */
    std::ifstream fileStr(path.c_str());

    /* If file exist, then read the superblock.
     */
    if (fileStr.good()) {
        /* Check the file size
         */
        fileStr.seekg(0, fileStr.end);
        size_t fileStrSize = fileStr.tellg();
        fileStr.seekg(0, fileStr.beg);

        if (fileStrSize != sizeof(struct SmSuperblock)) {
            LOGERROR << "SM Superblock: file size doesn't match in-memory struct"
                     << fileStrSize
                     << "vs. "
                     << sizeof(struct SmSuperblock);
            err = ERR_SM_SUPERBLOCK_DATA_CORRUPT;

            return err;
        }

        /* Read the superblock to buffer.
         */
        fileStr.read(reinterpret_cast<char *>(this), sizeof(struct SmSuperblock));

        /* Check the bytes read from the superblock.
         */
        if (fileStr.gcount() != sizeof(struct SmSuperblock)) {
            LOGERROR << "SM superblock: read bytes ("
                     << fileStr.gcount()
                     << "), but expected ("
                     << sizeof(struct SmSuperblock)
                     << ")";
            err = ERR_SM_SUPERBLOCK_DATA_CORRUPT;

            return err;
        }

        /* Assert the magic value to see.
         */
        Header.assertMagic();
    } else if (fileStr.fail()) {
        LOGERROR << "SM Superblock: file not found on " << path;
        err = ERR_SM_SUPERBLOCK_MISSING_FILE;
    } else {
        LOGERROR << "SM Superblock: read failed on " << path;
        err = ERR_SM_SUPERBLOCK_READ_FAIL;
    }

    return err;
}

Error
SmSuperblock::writeSuperblock(const std::string& path)
{
    Error err(ERR_OK);

    /* open output stream.
     */
    std::ofstream fileStr(path.c_str());
    if (fileStr.good()) {
        /* If the output stream is good, then write the superblock to
         * disk.
         */
        Header.assertMagic();
        fileStr.write(reinterpret_cast<char *>(this), sizeof(struct SmSuperblock));

        /* There shouldn't be any truncation.  Typically no way to recover
         * in this case, since it's likely ENOSPACE.
         */
        fds_verify(fileStr.tellp() == sizeof(struct SmSuperblock));

        /* Must flush the stream immediately after writing to ensure
         * the superblock lands on the disk.
         */
        fileStr.flush();
    } else {
        LOGERROR << "Cannot open SM superblock for write on " << path;
        err = ERR_SM_SUPERBLOCK_WRITE_FAIL;
    }

    return err;
}

void
SmSuperblock::setSuperblockChecksum()
{
    fds_checksum32_t chksum = computeChecksum();

    Header.setSuperblockHeaderChecksum(chksum);
}
#

/*
 * Checksum calculation for the superblock.
 *
 */
uint32_t
SmSuperblock::computeChecksum()
{
    boost::crc_32_type checksum;

    /* Since the first 4 bytes of the superblock holds the checksum value,
     * start checksum calculation from after the first 4 bytes.  Also,
     * adjust the length.
     */
    unsigned char *bytePtr = reinterpret_cast<unsigned char*>(this);
    bytePtr += sizeof(fds_checksum32_t);
    size_t len = sizeof(*this) - sizeof(fds_checksum32_t);

    /* Quick sanity check that we are pointing to the right address.
     */
    fds_assert(bytePtr == reinterpret_cast<unsigned char*>(&Header.SmSbHdrMagic));

    /* Calculate the checksum.
     */
    checksum.process_bytes(bytePtr, len);

    return checksum.checksum();
}


Error
SmSuperblock::validateSuperblock()
{
    Error err(ERR_OK);

    fds_checksum32_t thisChecksum, computedChecksum;

    // thisChecksum = *reinterpret_cast<fds_checksum32_t *>(this);
    thisChecksum = Header.getSuperblockHeaderChecksum();

    computedChecksum = computeChecksum();

    if (thisChecksum != computedChecksum) {
        LOGERROR << "SM superblock: checksum validation failed";
        return ERR_SM_SUPERBLOCK_CHECKSUM_FAIL;
    }

    err = Header.validateSuperblockHeader();
    if (err != ERR_OK) {
        LOGERROR << "SM superblock: header validation failed";
        return ERR_SM_SUPERBLOCK_DATA_CORRUPT;
    }

    return err;
}

fds_bool_t
SmSuperblock::doResync() const {
    return resync;
}

void
SmSuperblock::setResync() {
    resync = true;
}

void
SmSuperblock::resetResync() {
    resync = false;
}

fds_bool_t
SmSuperblock::operator ==(const SmSuperblock& rhs) const {
    if (this == &rhs) {
        return true;
    }

    return (0 == memcmp(this, &rhs, sizeof(struct SmSuperblock)));
}

/************************************
 * SuperblockMgr Ifaces
 */

SmSuperblockMgr::SmSuperblockMgr(DiskChangeFnObj diskChangeFunc)
        : noDltReceived(true),
          diskChangeFn(diskChangeFunc)
{
}

SmSuperblockMgr::~SmSuperblockMgr()
{
    /*
     * TODO(sean):
     * When we have a mechanism to do a clean shutdown, do we need to
     * flush out superblock block destroying the master copy?
     * Or do we rely that whenever the superblock is modified, it will
     * be sync'ed out to disk.  TBD
     */
}

std::string
SmSuperblockMgr::getSuperblockPath(const std::string& dir_path) {
    return (dir_path + "/" + superblockName);
}

void
SmSuperblockMgr::initMaps(const DiskLocMap& latestDiskMap,
                          const DiskLocMap& latestDiskDevMap) {

    diskMap = latestDiskMap;
    diskDevMap = latestDiskDevMap;
    setDiskHealthMap();
}

Error
SmSuperblockMgr::loadSuperblock(DiskIdSet& hddIds,
                                DiskIdSet& ssdIds,
                                const DiskLocMap& latestDiskMap,
                                const DiskLocMap& latestDiskDevMap)
{
    Error err(ERR_OK);

    SCOPEDWRITE(sbLock);

    /* There should be at least one disk in the map.
     */
    fds_assert(latestDiskMap.size() > 0);

    /* Cache the diskMap to a local storage.
     */
    initMaps(latestDiskMap, latestDiskDevMap);
    LOGDEBUG << "Got disk map";

    /* Do initial state check.
     * If there is no superblock anywhere on the system, then this is the
     * first time SM started on this node.
     */
    bool pristineState = checkPristineState(hddIds, ssdIds);
    if (pristineState) {
        /* If pristine state, where there is no SM superblock on the
         * node, then we are going to generate a new set of superblock
         * on each disk on the node.
         */
        superblockMaster.initSuperblock();
        SmTokenPlacement::compute(hddIds, ssdIds, &(superblockMaster.olt));

        /* After creating a new superblock, sync to disks.
         */
        err = syncSuperblock();
        fds_assert(err == ERR_OK);
        err = ERR_SM_NOERR_PRISTINE_STATE;
    } else {
        /* Reconcile superblock().  Elect one "master" superblock, if
         * possible.  Failure to reconcile should be a *very* rare case.
         */
        err = reconcileSuperblock();
        if (err != ERR_OK) {
            LOGERROR << "Error during superblock reconciliation error code = " << err;
            checkForHandledErrors(err);
        }

        /* Since, we have a "master" superblock, check if the disk topology
         * has changed from the last persisten state.  For example:
         * 1) disk(s) were added.
         * 2) disk(s) were removed.
         * 3) 1 and 2.
         */
        checkDisksAlive(hddIds, ssdIds);
        checkDiskTopology(hddIds, ssdIds);
    }

    return err;
}

/**
 * Redistribute tokens according to the new disk map received.
 * Before recomputing token placement, make sure the disk(s)
 * in the disk map are all live and accessible.
 */
Error
SmSuperblockMgr::redistributeTokens(DiskIdSet &hddIds, DiskIdSet &ssdIds,
                                    const DiskLocMap &latestDiskMap,
                                    const DiskLocMap &latestDiskDevMap) {
    SCOPEDWRITE(sbLock);
    initMaps(latestDiskMap, latestDiskDevMap);
    checkDisksAlive(hddIds, ssdIds);
    return checkDiskTopology(hddIds, ssdIds);
}

/**
 * Redistribute tokens for the lost disk. If this method is getting
 * called that means, a disk check for this disk has already been
 * done and this disk is declared failed. No need to do disk checks
 * again, since each online disk failure will be handled separately.
 * Each online disk failure requires cleanup of existing token files
 * that were residing on the disk, before redistributing tokens to the
 * remaining disk. So it is important to handle each online disk
 * failure separately.
 */
void
SmSuperblockMgr::recomputeTokensForLostDisk(const DiskId& failedDiskId,
                                            DiskIdSet& hddIds,
                                            DiskIdSet& ssdIds) {
    SCOPEDWRITE(sbLock);
    checkDiskTopology(hddIds, ssdIds);
}

Error
SmSuperblockMgr::updateNewSmTokenOwnership(const SmTokenSet& smTokensOwned,
                                           fds_uint64_t dltVersion) {
    Error err(ERR_OK);

    SCOPEDWRITE(sbLock);
    // Restarted SM may receive a greater version from OM if SM previously failed (shutdown)
    // and OM rotated DLT columns to move this SM to a secondary position.
    // Note that in this case (or in all cases when we receive DLT after restart),
    // SM must not gain any tokens than it already knows about in the superblock.
    // Restarted SM may also receive a lower version of DLT from OM if the domain
    // shutdown in the middle of token migration, specifically between DLT commit
    // and DLT close. In that case, OM will re-start migration later, but domain
    // starts on the previous version (as if we aborted)
    if ( (dltVersion == superblockMaster.DLTVersion) ||
         ( (superblockMaster.DLTVersion != DLT_VER_INVALID) &&
           noDltReceived ) ) {
        // this is restart case, otherwise all duplicate DLTs are catched at upper layers
        fds_verify(superblockMaster.DLTVersion != DLT_VER_INVALID);
        // this is the extra check that SM is just coming up from persistent state
        // and we received the first dlt
        if (!noDltReceived) {
            LOGNORMAL << "Superblock already handled this DLT version " << dltVersion;
            return ERR_DUPLICATE;
        }

        if (dltVersion < superblockMaster.DLTVersion) {
            LOGNOTIFY << "First DLT on SM restart is lower than stored in superblock "
                      << " most likely SM went down in the middle of migration, between"
                      << " DLT commit and DLT close. Will sync based on DLT we got from OM."
                      << " Received DLT version " << dltVersion
                      << " DLT version in superblock " << superblockMaster.DLTVersion;
        } else if (dltVersion > superblockMaster.DLTVersion) {
            LOGNOTIFY << "First DLT on SM restart is higher than stored in superblock."
                      << " Most likely OM rotated DLT due to this SM failure"
                      << " Received DLT version " << dltVersion
                      << " DLT version in superblock " << superblockMaster.DLTVersion;
        }

        // Check if superblock matches with this DLT
        // We care that all tokens that owned by this SM are "valid" in superblock
        // If SM went down between DLT update and DLT close, then we may have not
        // updated SM tokens that became invalid. I think that's fine. We
        // are going to check if all smTokensOwned are marked 'valid' in superblock
        // and invalidate all other tokens (invalidation will be done by the caller)
        // Similar SM may lose DLT tokens between restarts if SM failed and OM moved
        // this SM to secondary position or completely removed it from some DLT columns
        // However, SM must bever gain DLT tokens between restarts
        err = superblockMaster.tokTbl.checkSmTokens(smTokensOwned);
        if (err == ERR_SM_SUPERBLOCK_INCONSISTENT) {
            LOGERROR << "Superblock knows about this DLT version " << dltVersion
                     << ", but at least one token that must be valid in SM "
                     << " based on DLT version is marked invalid in superblock! ";
        } else if (err == ERR_SM_NOERR_LOST_SM_TOKENS) {
            LOGNOTIFY << "Superblock knows about this DLT version " << dltVersion
                      << ", but at least one token that should be invalid in SM "
                      << " based on DLT version is marked valid in superblock. ";
        } else {
            fds_verify(err.ok());
            // if this is the first token ownership notify since the start and SM not in
            // pristine state, tell the caller
            // dltVersion must match dlt version in superblock only in that case above
            err = ERR_SM_NOERR_NEED_RESYNC;
            LOGDEBUG << "Superblock knows about this DLT version " << dltVersion;
        }
        if (!err.ok()) {
            // if error, more logging for debugging
            for (fds_token_id tokId = 0; tokId < SMTOKEN_COUNT; ++tokId){
                LOGNOTIFY << "SM token " << tokId
                          << " must be valid? " << (smTokensOwned.count(tokId) > 0)
                          << "; valid in superblock? "
                          << superblockMaster.tokTbl.isValidOnAnyTier(tokId);
            }
        }
        if ((err != ERR_SM_SUPERBLOCK_INCONSISTENT) &&
            (dltVersion != superblockMaster.DLTVersion)) {
            // make sure we save DLT version in superblock
            setDLTVersionLockHeld(dltVersion, true);
        }
    }
    noDltReceived = false;

    if (!err.ok()) {
        // we either already handled the update or error happened
        return err;
    }
    std::set<SmTokenLoc> tokenLocations;
    for (auto smToken : smTokensOwned) {
        SmTokenLoc tokenLoc;
        tokenLoc.id = smToken;
        tokenLoc.hdd = superblockMaster.olt.getDiskId(smToken, diskio::diskTier);
        tokenLoc.ssd = superblockMaster.olt.getDiskId(smToken, diskio::flashTier);
        tokenLocations.insert(tokenLoc);
    }
    fds_bool_t initAtLeastOne =
                superblockMaster.tokTbl.initializeSmTokens(tokenLocations);
    superblockMaster.DLTVersion = dltVersion;

    // sync superblock
    err = syncSuperblock();
    if (err.ok()) {
        LOGDEBUG << "SM persistent SM token ownership and DLT version="
                 << superblockMaster.DLTVersion;
        if (initAtLeastOne) {
            err = ERR_SM_NOERR_GAINED_SM_TOKENS;
        }
    } else {
        // TODO(Sean):  If the DLT version cannot be persisted, then for not, we will
        //              just ignore it.  We can retry, but the chance of failure is
        //              high at this point, since the disk is likely failed or full.
        //
        // TODO(Sean):  Make sure if the latest DLT version > persistent DLT version is ok.
        //              and change DLT and DISK mapping accordingly.
        LOGCRITICAL << "SM persistent DLT version failed to set: version "
                    << dltVersion;
    }

    return err;
}

SmTokenSet
SmSuperblockMgr::handleRemovedSmTokens(SmTokenSet& smTokensNotOwned,
                                       fds_uint64_t dltVersion) {
    SCOPEDWRITE(sbLock);

    // DLT version must be already set!
    fds_verify(dltVersion == superblockMaster.DLTVersion);

    // invalidate token state for tokens that are not owned
    SmTokenSet lostSmTokens = superblockMaster.tokTbl.invalidateSmTokens(smTokensNotOwned);

    // sync superblock
    Error err = syncSuperblock();
    if (!err.ok()) {
        // We couldn't persist tokens that are not valid anymore
        // For now ignoring it! We should be ok later recovering from this
        // inconsistency since we can always check SM ownership from DLT
        LOGCRITICAL << "Failed to persist newly invalidated SM tokens";
    }

    return lostSmTokens;
}

/**
 * This method tries to check if the disks passed
 * are accessible or not.
 * It creates a mount point and try to mount the
 * device path on it. If it fails with device not
 * found error. Then basically, device isn't there.
 * Secondly, it tries to create a file and write to
 * it and do a flush to the hardware. If the flush
 * raises io exception. Then disk is assumed to be
 * inaccessible.
 * Basically either of the test failure for a disk
 * results in marking disk as bad.
 */
void
SmSuperblockMgr::checkDisksAlive(DiskIdSet& HDDs,
                                 DiskIdSet& SSDs) {
    DiskIdSet badDisks;
    std::string tempMountDir = MODULEPROVIDER()->proc_fdsroot()->\
                               dir_fds_etc() + "testDevMount";
    FdsRootDir::fds_mkdir(tempMountDir.c_str());
    umount2(tempMountDir.c_str(), MNT_FORCE);

    LOGDEBUG << "Do mount test on disks";
    // check for unreachable HDDs first.
    for (auto& diskId : HDDs) {
        if (DiskUtils::isDiskUnreachable(diskMap[diskId],
                                         diskDevMap[diskId],
                                         tempMountDir)) {
            std::string path = diskMap[diskId] + "/.tmp";
            // check again
            if (DiskUtils::diskFileTest(path)) {
               //something's wrong with disk. Declare failed.
               badDisks.insert(diskId);
            }
        }
    }
    for (auto& badDiskId : badDisks) {
        LOGWARN << "Disk: " << badDiskId << " is unaccessible";
        markDiskBad(badDiskId);
        HDDs.erase(badDiskId);
        diskMap.erase(badDiskId);
        diskDevMap.erase(badDiskId);
    }
    badDisks.clear();

    // check for unreachable SSDs.
    for (auto& diskId : SSDs) {
        if (DiskUtils::isDiskUnreachable(diskMap[diskId],
                                         diskDevMap[diskId],
                                         tempMountDir)) {
            std::string path = diskMap[diskId] + "/.tmp";
            // check again.
            if (DiskUtils::diskFileTest(path)) {
                //something's wrong with disk. Declare failed.
                badDisks.insert(diskId);
            }
        }
    }
    for (auto& badDiskId : badDisks) {
        LOGWARN << "Disk: " << badDiskId << " is unaccessible";
        markDiskBad(badDiskId);
        SSDs.erase(badDiskId);
        diskMap.erase(badDiskId);
        diskDevMap.erase(badDiskId);
    }
    deleteMount(tempMountDir);
}

bool
SmSuperblockMgr::isDiskAlive(const DiskId& diskId) {
    std::string tempMountDir = MODULEPROVIDER()->proc_fdsroot()->\
                               dir_fds_etc() + "testDevMount";
    FdsRootDir::fds_mkdir(tempMountDir.c_str());
    umount2(tempMountDir.c_str(), MNT_FORCE);

    LOGNORMAL << "Do disk check for disk = " << diskId;
    if (DiskUtils::isDiskUnreachable(diskMap[diskId],
                                     diskDevMap[diskId],
                                     tempMountDir)) {
        LOGWARN << "Disk with diskId = " << diskId << " is unaccessible";
        markDiskBad(diskId);
        diskMap.erase(diskId);
        diskDevMap.erase(diskId);
        return false;
    }

    deleteMount(tempMountDir);
    return true;
}

/*
 * Determine if the node's disk topology has changed or not.
 *
 */
Error
SmSuperblockMgr::checkDiskTopology(DiskIdSet& newHDDs,
                                   DiskIdSet& newSSDs)
{
    Error err(ERR_OK);
    DiskIdSet persistentHDDs, persistentSSDs;
    DiskIdSet addedHDDs, removedHDDs;
    DiskIdSet addedSSDs, removedSSDs;
    bool recomputed = false;
    bool diskAdded = true;
    LOGNOTIFY << "Checking disk topology";

    /* Get the list of unique disk IDs from the OLT table.  This is
     * used to compare with the new set of HDDs and SSDs to determine
     * if any disk was added or removed for each tier.
     */
    persistentHDDs = superblockMaster.olt.getDiskSet(diskio::diskTier);
    persistentSSDs = superblockMaster.olt.getDiskSet(diskio::flashTier);

    /* Determine if any HDD was added or removed.
     */
    removedHDDs = DiskUtils::diffDiskSet(persistentHDDs, newHDDs);
    addedHDDs = DiskUtils::diffDiskSet(newHDDs, persistentHDDs);

    /* Determine if any HDD was added or removed.
     */
    removedSSDs = DiskUtils::diffDiskSet(persistentSSDs, newSSDs);
    addedSSDs = DiskUtils::diffDiskSet(newSSDs, persistentSSDs);

    typedef std::set<std::pair<fds_token_id, fds_uint16_t>> TokDiskSet;

    if ((removedHDDs.size() > 0) || (removedSSDs.size() > 0)) {
        superblockMaster.setResync();
    }
    /**
     * Check if the disk topology has changed.
     */
    if ((removedHDDs.size() > 0) ||
        (addedHDDs.size() > 0)) {
        LOGNOTIFY << "Disk Topology Changed: removed HDDs=" << removedHDDs.size()
                  << ", added HDDs=" << addedHDDs.size();

        for (auto &removedDiskId : removedHDDs) {
            LOGNOTIFY <<"Disk HDD=" << removedDiskId << " removed";
            SmTokenSet lostSmTokens = getTokensOfThisSM(removedDiskId);
            TokDiskSet smTokenDiskIdPairs;

            for (auto& lostSmToken : lostSmTokens) {
                DiskId metaDiskId = removedDiskId;
                if (g_fdsprocess->
                        get_fds_config()->
                            get<bool>("fds.sm.testing.useSsdForMeta")) {
                    metaDiskId = superblockMaster.olt.getDiskId(lostSmToken,
                                                                diskio::flashTier);
                }
                changeTokenCompactionState(removedDiskId, lostSmToken, diskio::diskTier, false, 0);
                smTokenDiskIdPairs.insert(std::make_pair(lostSmToken, metaDiskId));
            }

            if (diskChangeFn) {
                diskChangeFn(!diskAdded, removedDiskId, diskio::diskTier, smTokenDiskIdPairs);
            }
        }
        for (auto &addedHDD : addedHDDs) {
            if (diskChangeFn) {
                diskChangeFn(diskAdded, addedHDD, diskio::diskTier, TokDiskSet());
            }
        }
        recomputed |= SmTokenPlacement::recompute(persistentHDDs,
                                                  addedHDDs,
                                                  removedHDDs,
                                                  diskio::diskTier,
                                                  &(superblockMaster.olt),
                                                  diskMap,
                                                  err);
        if (!err.ok()) {
            LOGCRITICAL << "Redistribution of data failed with error " << err;
            return err;
        }
    }

    if ((removedSSDs.size() > 0) ||
        (addedSSDs.size() > 0)) {
        LOGNOTIFY << "Disk Topology Changed: removed SSDs: " << removedSSDs.size()
                  << ", added SSDs: " << addedSSDs.size();

        for (auto &removedDiskId : removedSSDs) {
            LOGNOTIFY <<"Disk SSD: " << removedDiskId << " removed";
            SmTokenSet lostSmTokens = getTokensOfThisSM(removedDiskId);
            std::set<std::pair<fds_token_id, fds_uint16_t>> smTokenDiskIdPairs;

            for (auto& lostSmToken : lostSmTokens) {
                DiskId diskId = removedDiskId;
                if (g_fdsprocess->
                        get_fds_config()->
                            get<bool>("fds.sm.testing.useSsdForMeta")) {
                    diskId = superblockMaster.olt.getDiskId(lostSmToken,
                                                            diskio::diskTier);
                }
                smTokenDiskIdPairs.insert(std::make_pair(lostSmToken, diskId));
            }

            if (diskChangeFn) {
                diskChangeFn(!diskAdded, removedDiskId, diskio::flashTier, smTokenDiskIdPairs);
            }
        }
        for (auto &addedSSD : addedSSDs) {
            if (diskChangeFn) {
                diskChangeFn(diskAdded, addedSSD, diskio::flashTier, TokDiskSet());
            }
        }
        recomputed |= SmTokenPlacement::recompute(persistentSSDs,
                                                  addedSSDs,
                                                  removedSSDs,
                                                  diskio::flashTier,
                                                  &(superblockMaster.olt),
                                                  diskMap,
                                                  err);
        if (!err.ok()) {
            LOGCRITICAL << "Redistribution of data failed with error " << err;
            return err;;
        }
    }

    /* Token mapping is recomputed.  Now sync out to disk. */
    if (recomputed) {
        removeDisksFromSuperblock(removedHDDs, removedSSDs);
        err = syncSuperblock();
        if (!err.ok()) {
            LOGERROR << "Superblock sync failed for one or more disks.";
        }
    }
    return err;
}

void
SmSuperblockMgr::removeDisksFromSuperblock(DiskIdSet &removedHDDs, DiskIdSet &removedSSDs) {

    for (auto &diskId : removedHDDs) {
        markDiskBad(diskId);
        diskMap.erase(diskId);
        diskDevMap.erase(diskId);
    }

    for (auto &diskId : removedSSDs) {
        markDiskBad(diskId);
        diskMap.erase(diskId);
        diskDevMap.erase(diskId);
    }
}

Error
SmSuperblockMgr::syncSuperblock()
{
    Error err(ERR_OK);
    std::string superblockPath;

    fds_assert(diskMap.size() > 0);

    /* At this point, in-memory superblock can be sync'ed to the disk.
     * Calculate and stuff the checksum, and write to disks.
     */
    superblockMaster.setSuperblockChecksum();

    /* Sync superblock to all devices in the disk map */
    for (auto cit = diskMap.begin(); cit != diskMap.end(); ++cit) {
        superblockPath = getSuperblockPath(cit->second.c_str());
        auto writeErr = superblockMaster.writeSuperblock(superblockPath);
        if (!writeErr.ok()) {
            LOGERROR << "Superblock sync failed for disk: " <<cit->first
                     << " with error: " << writeErr;
        }
        if (err.ok()) {
            err = writeErr;
        }
    }

    return err;
}

void
SmSuperblockMgr::setDiskHealthMap()
{ 
    for (auto cit = diskMap.begin(); cit != diskMap.end(); ++cit) {
        diskHealthMap[cit->first] = true;
    }
}

void
SmSuperblockMgr::markDiskBad(const fds_uint16_t& diskId) {
    diskHealthMap[diskId] = false;
}

bool
SmSuperblockMgr::isDiskHealthy(const fds_uint16_t& diskId) {
    return diskHealthMap[diskId];
}

Error
SmSuperblockMgr::syncSuperblock(const std::set<uint16_t>& setSuperblock)
{
    Error err(ERR_OK);
    std::string superblockPath;

    fds_assert(diskMap.size() > 0);

    for (auto cit = setSuperblock.begin(); cit != setSuperblock.end(); ++cit) {
        superblockPath = getSuperblockPath(diskMap[*cit]);
        err = superblockMaster.writeSuperblock(superblockPath);
        if (err != ERR_OK) {
            return err;
        }
    }

    return err;
}

size_t
SmSuperblockMgr::countUniqChecksum(const std::multimap<fds_checksum32_t, uint16_t>& checksumMap)
{
    size_t uniqCount = 0;

    /* TODO(sean)
     * Is there more efficient way to count unique keys in multimap?
     */
    for (auto it = checksumMap.begin(), end = checksumMap.end();
         it != end;
         it = checksumMap.upper_bound(it->first)) {
        ++uniqCount;
    }

    return uniqCount;
}

bool
SmSuperblockMgr::checkPristineState(DiskIdSet& HDDs,
                                    DiskIdSet& SSDs)
{
    uint32_t noSuperblockCnt = 0;
    fds_assert(diskMap.size() > 0);

    /**
     * Check for all HDDs and SSDs passed to SM via diskMap
     * are up and accessible. Remove the bad ones from SSD
     * and/or HDD DiskIdSet.
     * Only checking for file path could sometimes be incorrect
     * since the filepath check request could've been served from
     * filesystem cache, when the actual underlying device is
     * gone.
     */
    std::string tempMountDir = getTempMount();

    for (auto cit = diskMap.begin(); cit != diskMap.end(); ++cit) {
        std::string superblockPath = getSuperblockPath(cit->second.c_str());

        /* Check if the file exists.
         */
        int fd = open(superblockPath.c_str(), O_RDWR | O_SYNC, S_IRUSR | S_IWUSR);
        if (fd == -1 || fsync(fd) || close(fd)) {
            LOGDEBUG << "Superblock file " << superblockPath << " is not accessible";
            ++noSuperblockCnt;
        }
    }
    deleteMount(tempMountDir);
    return (noSuperblockCnt == diskMap.size());
}

std::string
SmSuperblockMgr::getTempMount() {

    std::string tempMountDir = MODULEPROVIDER()->proc_fdsroot()->\
                               dir_fds_etc() + "testMount";
    FdsRootDir::fds_mkdir(tempMountDir.c_str());
    umount2(tempMountDir.c_str(), MNT_FORCE);
    return tempMountDir;
}

void
SmSuperblockMgr::deleteMount(std::string& path) {
    umount2(path.c_str(), MNT_FORCE);
    boost::filesystem::remove_all(path.c_str());
}

/*
 * This function tries to reconcile Superblocks.
 */
Error
SmSuperblockMgr::reconcileSuperblock()
{
    Error err(ERR_OK);
    std::unordered_map<uint16_t, fds_checksum32_t> diskGoodSuperblock;
    std::multimap<fds_checksum32_t, uint16_t> checksumToGoodDiskIdMap;
    std::set<uint16_t> diskBadSuperblock;

    fds_assert(diskMap.size() > 0);

    /* TODO(sean)
     * Make this loop a function.
     */
    for (auto cit = diskMap.begin(); cit != diskMap.end(); ++cit) {
        std::string superblockPath = getSuperblockPath(cit->second);
        SmSuperblock tmpSuperblock;

        /* Check if the file can be read.
         */
        err = tmpSuperblock.readSuperblock(superblockPath);
        if (err != ERR_OK) {
            LOGWARN << "SM superblock: read disk("
                    << cit->first
                    << ") => "
                    << superblockPath
                    << " failed with error"
                    << err;
            checkForHandledErrors(err);
            auto ret = diskBadSuperblock.emplace(cit->first);
            if (!ret.second) {
                /* diskID is not unique.  Failed to add to diskBadSuperblock
                 * set, because diskID already exists.  DiskID should be unique.
                 */
                return ERR_SM_SUPERBLOCK_NO_RECONCILE;
            }
            continue;
        }

        /* Check if the SM superblock is a valid one.
         */
        err = tmpSuperblock.validateSuperblock();
        if (err != ERR_OK) {
            LOGWARN << "SM superblock: read disk("
                    << cit->first
                    << ") => "
                    << superblockPath
                    << " failed with error"
                    << err;
            checkForHandledErrors(err);
            auto ret = diskBadSuperblock.emplace(cit->first);
                /* diskID is not unique.  Failed to add to diskBadSuperblock
                 * set, because diskID already exists.  DiskID should be unique.
                 */
            if (!ret.second) {
                return ERR_SM_SUPERBLOCK_NO_RECONCILE;
            }
            continue;
        }

        /* Everything looks good, so far.
         * First, add to the checksum=>{disks} mapping.  If there are more
         * than one checksum key, then we are in trouble.  Then, we need to
         * take additional steps to reconcile.
         * If the number of of "good superblocks" and number of disks matching
         * an unique checksum, that is the simplest case.
         */
        std::pair<fds_checksum32_t, uint16_t>entry(tmpSuperblock.getSuperblockChecksum(),
                                                   cit->first);
        checksumToGoodDiskIdMap.insert(entry);

        auto ret = diskGoodSuperblock.emplace(cit->first,
                                              tmpSuperblock.getSuperblockChecksum());
        if (!ret.second) {
            /* diskID is not unique.  Failed to add to diskGoodSuperblock
             * set, because diskID already exists.  DiskID should be unique.
             */
            return ERR_SM_SUPERBLOCK_NO_RECONCILE;
        }
    }

    /* The number of good superblocks must be a simple majority (51%).  If the
     * number of bad superblocks is at least 50%, then we have two possibilities:
     * 1) panic -- too many bad superblocks.   Not sure about the state of the
     *             node.  Probably safe to panic then continue.
     * 2) re-compute and regen superblock -- throw away everything and create
     *             a new set of superblocks.
     *
     * For now, just panic.
     */
    if (diskGoodSuperblock.size() <= diskBadSuperblock.size()) {
        LOGERROR << "SM superblock: number of good superblocks: "
                 << diskGoodSuperblock.size()
                 << ", bad superblocks: "
                 << diskBadSuperblock.size();
        return ERR_SM_SUPERBLOCK_NO_RECONCILE;
    }

    /* Now we have enough "good" SM superblocks to reconcile, if possible.
     */
    size_t uniqChecksum = countUniqChecksum(checksumToGoodDiskIdMap);
    fds_verify(uniqChecksum > 0);

    /* Simple majority has quorum at this point.  Use one of the good superblock
     * as th "master."  And, if necessary, overwrite bad superblocks with
     * the "master."
     */

    // TODO(brian): Refactor this whole logic to be cleaner

    if (1 == uniqChecksum) {
        fds_verify(diskGoodSuperblock.size() > 0);
        auto goodSuperblock = diskGoodSuperblock.begin();
        std::string goodSuperblockPath = getSuperblockPath(diskMap[goodSuperblock->first]);

        /* Just another round of validation.  Being paranoid.  Nothing
         * should fail at this point.  If it fails, not sure how to recover
         * from it.  It will be a bit complicated.
         * So, for now just verify that everything has gone smoothly.
         */
        err = superblockMaster.readSuperblock(goodSuperblockPath);
        fds_verify(ERR_OK == err);
        err = superblockMaster.validateSuperblock();
        fds_verify(ERR_OK == err);
        if (diskBadSuperblock.size() > 0) {
            err = syncSuperblock(diskBadSuperblock);
            checkForHandledErrors(err);
        }
    } else {
        /* TODO(brian)
         * This case can happen if the system goes down when syncing superblocks to disks. There should NEVER be more
         * than two conflicting checksums. If there are more it's a real problem.
         *
         * With that in mind, there are a few major cases here that can occur:
         *
         * A) The superblock was updating due to a DLT update message. In this case we will likely not have all of the
         *    objects for the tokens we now own. The particular superblock we pick likely doesn't matter as we
         *    are likely already considered in error and migration has been aborted.
         *
         * B) The superblock was updating due to a DLT commit message. When the commit message arrives we have completed
         *    migration so we should select the most recent superblock as we now have all of the tokens we're
         *    responsible for.
         *
         * C) The superblock was updating due to GC running. In this case we may have a conflict regarding which is the
         *    correct token file. We should select the most recent superblock in this case, as the new token file may
         *    have been written to during compaction and may now hold objects that are not present in the old file.
         *
         * Given these three scenarios it makes the most sense to simply select the most recent superblock and run
         * with it.
         *
         */

        LOGWARN << "SM superblock detected " << uniqChecksum << " checksums, attempting to reconcile.";

        if (uniqChecksum > 2) {
            LOGERROR << "SM superblock had more than two conflicting \"good\" checksums. This is particularly bad "
                        "and should not be possible. Still attempting to repair, but this should be examined "
                        "immediately.";
        }

        fds_checksum32_t mostRecent = findMostRecentSuperblock(checksumToGoodDiskIdMap);
        auto it = checksumToGoodDiskIdMap.find(mostRecent);

        if (it == checksumToGoodDiskIdMap.end()) {
            // If we can't find the most recent entry that we *just* found then we're in an extra bad place
            // but we can try to be heroic and just pick whatever superblock is still around and hope that
            // whatever state we end up in we can be fixed with a resync.
            LOGERROR << "Tried to use checksum " << mostRecent << " but failed. Picking arbitrary superblock in"
                        "a heroic effort not to fall over. System may be unreliable.";

            it = checksumToGoodDiskIdMap.begin();
        }

        std::string goodSuperblockPath = getSuperblockPath(diskMap[it->second]);

        // Expand on our diskBadSuperblock set to include "good bad" superblocks as well
        std::set<fds_uint16_t> fixers{diskBadSuperblock};

        for (auto it = checksumToGoodDiskIdMap.begin();
            it != checksumToGoodDiskIdMap.end();
            it++) {

            if (it->first == mostRecent) {
                // These are the IDs of those with the correct superblock already
                LOGDEBUG << "Just found ourselves ! " << it->first;
                continue;
            }

            // If we got here this is a conflicting disk ID, so add it to the fixers list
            LOGDEBUG << "Adding " << it->second << " to the fixers list.";
            fixers.insert(it->second);
        }

        /* Just another round of validation.  Being paranoid.  Nothing
         * should fail at this point.  If it fails, not sure how to recover
         * from it.  It will be a bit complicated.
         * So, for now just verify that everything has gone smoothly.
         */
        err = superblockMaster.readSuperblock(goodSuperblockPath);
        if (ERR_OK != err) {
            LOGCRITICAL << "Failed to read known good superblock at " << goodSuperblockPath;
            return ERR_SM_SUPERBLOCK_NO_RECONCILE;
        }
        fds_assert(ERR_OK == err);
        err = superblockMaster.validateSuperblock();
        fds_assert(ERR_OK == err);


        // Sync the fixers
        err = syncSuperblock(fixers);
        checkForHandledErrors(err);
    }

    return err;
}

void
SmSuperblockMgr::checkForHandledErrors(Error& err) {
    switch (err.GetErrno()) {
        case ERR_SM_SUPERBLOCK_READ_FAIL:
        case ERR_SM_SUPERBLOCK_WRITE_FAIL:
        case ERR_SM_SUPERBLOCK_MISSING_FILE:
            /**
             * Disk failures are handled in the code. So
             * if a superblock file is not reachable just
             * ignore the error.
             */
            err = ERR_OK;
            break;
        default:
            break;
    }
}

Error
SmSuperblockMgr::setDLTVersionLockHeld(fds_uint64_t dltVersion, bool syncImmediately)
{
    Error err(ERR_OK);
    superblockMaster.DLTVersion = dltVersion;

    if (syncImmediately) {
        // sync superblock
        err = syncSuperblock();

        if (err.ok()) {
            LOGDEBUG << "SM persistent DLT version=" << superblockMaster.DLTVersion;
        } else {
            // TODO(Sean):  If the DLT version cannot be persisted, then for not, we will
            //              just ignore it.  We can retry, but the chance of failure is
            //              high at this point, since the disk is likely failed or full.
            //
            // TODO(Sean):  Make sure if the latest DLT version > persistent DLT version is ok.
            //              and change DLT and DISK mapping accordingly.
            LOGCRITICAL << "SM persistent DLT version failed to set.";
        }
    }

    return err;
}

Error
SmSuperblockMgr::setDLTVersion(fds_uint64_t dltVersion, bool syncImmediately) {
    SCOPEDWRITE(sbLock);
    return setDLTVersionLockHeld(dltVersion, syncImmediately);
}

fds_uint64_t
SmSuperblockMgr::getDLTVersion()
{
    SCOPEDREAD(sbLock);
    return superblockMaster.DLTVersion;
}

fds_bool_t
SmSuperblockMgr::doResync() {
    SCOPEDREAD(sbLock);
    return superblockMaster.doResync();
}

void
SmSuperblockMgr::setResync() {
    Error err(ERR_OK);
    SCOPEDWRITE(sbLock);
    superblockMaster.setResync();
    err = syncSuperblock();
    if (!err.ok()) {
        LOGCRITICAL << "SM failed to persist resync pending notice";
    }
}

void
SmSuperblockMgr::resetResync() {
    Error err(ERR_OK);
    SCOPEDWRITE(sbLock);
    superblockMaster.resetResync();
    err = syncSuperblock();
    if (!err.ok()) {
        LOGCRITICAL << "SM failed to persist no resync pending notice";
    }
}

fds_uint16_t
SmSuperblockMgr::getDiskId(fds_token_id smTokId,
                        diskio::DataTier tier) {
    SCOPEDREAD(sbLock);
    return superblockMaster.olt.getDiskId(smTokId, tier);
}

DiskIdSet
SmSuperblockMgr::getDiskIds(fds_token_id smTokId,
                            diskio::DataTier tier) {
    SCOPEDREAD(sbLock);
    return superblockMaster.olt.getDiskIds(smTokId, tier);
}

fds_uint16_t
SmSuperblockMgr::getWriteFileId(DiskId diskId,
                                fds_token_id smToken,
                                diskio::DataTier tier) {
    SCOPEDREAD(sbLock);
    return superblockMaster.tokTbl.getWriteFileId(diskId, smToken, tier);
}

fds_bool_t
SmSuperblockMgr::compactionInProgress(DiskId diskId,
                                      fds_token_id smToken,
                                      diskio::DataTier tier) {
    SCOPEDREAD(sbLock);
    return compactionInProgressNoLock(diskId, smToken, tier);
}

fds_bool_t
SmSuperblockMgr::compactionInProgressNoLock(DiskId diskId,
                                            fds_token_id smToken,
                                            diskio::DataTier tier) {
    return superblockMaster.tokTbl.isCompactionInProgress(diskId, smToken, tier);
}

Error
SmSuperblockMgr::changeCompactionState(DiskId diskId,
                                       fds_token_id smToken,
                                       diskio::DataTier tier,
                                       fds_bool_t inProg,
                                       fds_uint16_t newFileId) {
    SCOPEDWRITE(sbLock);
    return changeTokenCompactionState(diskId, smToken, tier, inProg, newFileId);
}

Error
SmSuperblockMgr::changeTokenCompactionState(DiskId diskId,
                                            fds_token_id smToken,
                                            diskio::DataTier tier,
                                            fds_bool_t inProg,
                                            fds_uint16_t newFileId) {
    Error err(ERR_OK);
    superblockMaster.tokTbl.setCompactionState(diskId, smToken, tier, inProg);
    if (inProg) {
        superblockMaster.tokTbl.setWriteFileId(diskId, smToken, tier, newFileId);
    }
    // sync superblock
    err = syncSuperblock();
    return err;
}

SmTokenSet
SmSuperblockMgr::getSmOwnedTokens() {
    SCOPEDREAD(sbLock);
    return superblockMaster.tokTbl.getSmTokens();
}

SmTokenSet
SmSuperblockMgr::getSmOwnedTokens(fds_uint16_t diskId) {
    SCOPEDREAD(sbLock);
    return getTokensOfThisSM(diskId);
}

SmTokenSet
SmSuperblockMgr::getTokensOfThisSM(fds_uint16_t diskId) {
    // get all tokens that can reside on disk diskId
    SmTokenSet diskToks = superblockMaster.olt.getSmTokens(diskId);
    // filter tokens that this SM owns
    SmTokenSet ownedTokens = superblockMaster.tokTbl.getSmTokens();
    SmTokenSet retToks;
    for (SmTokenSet::const_iterator cit = diskToks.cbegin();
         cit != diskToks.cend();
         ++cit) {
        if (ownedTokens.count(*cit) > 0) {
            retToks.insert(*cit);
        }
    }
    return retToks;
}

// So we can print class members for logging
std::ostream& operator<< (std::ostream &out,
                          const SmSuperblockMgr& sbMgr) {
    out << "Current DLT Version=" << sbMgr.superblockMaster.DLTVersion << "\n";
    out << "Current disk map:\n" << sbMgr.diskMap;
    return out;
}

std::ostream& operator<< (std::ostream &out,
                          const DiskLocMap& diskMap) {
    for (auto cit = diskMap.begin(); cit != diskMap.end(); ++cit) {
        out << cit->first << " : " << cit->second << "\n";
    }
    return out;
}

// since DiskIdSet is typedef of std::set, need to specifically overlaod boost ostream
std::ostream& operator<< (std::ostream& out,
                          const DiskIdSet& diskIds) {
    for (auto cit = diskIds.begin(); cit != diskIds.end(); ++cit) {
        out << "[" << *cit << "] ";
    }
    out << "\n";
    return out;
}

// since DiskIdSet is typedef of std::set, need to specifically overlaod boost ostream
boost::log::formatting_ostream& operator<< (boost::log::formatting_ostream& out,
                                            const DiskIdSet& diskIds) {
    for (auto cit = diskIds.begin(); cit != diskIds.end(); ++cit) {
        out << "[" << *cit << "] ";
    }
    out << "\n";
    return out;
}

/* This iface is only used for the SmSuperblock unit test.  Should not
 * be called by anyone else.
 */
std::string
SmSuperblockMgr::SmSuperblockMgrTestGetFileName()
{
    return superblockName;
}

/**
 * Method determines which good superblock was most recently written. This is useful for complex reconciliation where
 * we have multiple "good" disk maps.
 *
 * @param checkMap A multimap of <fds_checksum32_t, fds_int16_t> where keys are checksums, and values are disk IDs
 *                 containing a superblock that checksums to it's key.
 * @returns A key corresponding to the most recent superblock
 */
fds_checksum32_t
SmSuperblockMgr::findMostRecentSuperblock(std::multimap<fds_checksum32_t, fds_uint16_t> checkMap) {
    LOGNORMAL << "Trying to find most recent superblock!";
    struct stat statbuf;
    memset(&statbuf, 0, sizeof(statbuf));

    // Track the path/time of the superblock that has been most recently updated
    fds_checksum32_t latest_superblock {checkMap.begin()->first};
    std::chrono::system_clock::time_point time_latest {};

    // Stat each of the superblock files to determine which is the most recently written
    for (auto it = checkMap.begin(); it != checkMap.end(); it++) {
        std::string superblockPath = getSuperblockPath(diskMap[it->second]);

        LOGDEBUG << "Trying to stat " << superblockPath << "...";

         if (stat(superblockPath.c_str(), &statbuf) == -1) {
            LOGERROR << "Tried to stat superblock @ " << superblockPath << " but the stat failed. "
                        "Continuing anyway...";
            continue;
        }

        std::chrono::system_clock::time_point file_time =
            std::chrono::system_clock::from_time_t(statbuf.st_mtim.tv_sec);

        if (file_time > time_latest) {
            time_latest = file_time;
            latest_superblock = it->first;
        }

    }
    return latest_superblock;
}
}  // namespace fds
