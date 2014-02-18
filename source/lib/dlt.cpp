/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
#include <cmath>
#include <vector>
#include <map>
#include <dlt.h>
#include <iostream>
#include <string>
#include <util/Log.h>
#include <sstream>
#include <iomanip>
#include <ostream>
/**
 *  Implementationg for DLT ...
 */
namespace fds {

std::ostream& operator<< (std::ostream &out, const DltTokenGroup& tokenGroup) {
    out << "TokenGroup : ";
    for (uint i = 0; i < tokenGroup.length; i++) {
        out << "[" << i << ":" << tokenGroup.get(i).uuid_get_val() <<"] ";
    }
    return out;
}

DLT::DLT(fds_uint32_t numBitsForToken,
         fds_uint32_t depth,
         fds_uint64_t version,
         bool fInit)
        : Module("data placement table"),
          version(version) ,
          numBitsForToken(numBitsForToken),
          numTokens(pow(2, numBitsForToken)),
          depth(depth) {
    distList = boost::shared_ptr<std::vector<DltTokenGroupPtr> >
            (new std::vector<DltTokenGroupPtr>());
    mapNodeTokens = boost::shared_ptr<NodeTokenMap>(
        new NodeTokenMap());

    LOGDEBUG << "dlt.init : " << "numbits:" << numBitsForToken
             << " depth:" << depth << " version:" << version;
    // Pre-allocate token groups for each token
    if (fInit) {
        distList->reserve(numTokens);
        for (uint i = 0; i < numTokens; i++) {
            distList->push_back(boost::shared_ptr<DltTokenGroup>(
                new DltTokenGroup(depth)));
        }
    }
}

DLT::DLT(const DLT& dlt)
        : Module("data placement table"),
          numBitsForToken(dlt.numBitsForToken),
          depth(dlt.depth),
          numTokens(dlt.numTokens),
          version(dlt.version),
          distList(dlt.distList),
          mapNodeTokens(dlt.mapNodeTokens) {
}

DLT::~DLT() {
}


DLT* DLT::clone() const {
    LOGTRACE << "cloning";
    DLT *pDlt = new DLT(numBitsForToken, depth, version, true);

    pDlt->version = version;
    pDlt->timestamp = timestamp;

    for (uint i = 0; i < numTokens; i++) {
        for (uint j = 0 ; j < depth ; j++) {
            pDlt->distList->at(i)->set(j, distList->at(i)->get(j));
        }
    }
    return pDlt;
}

int DLT::mod_init(SysParams const *const param) {
    Module::mod_init(param);
    return 0;
}

void DLT::mod_startup() {
}

void DLT::mod_shutdown() {
}

fds_uint64_t
DLT::getVersion() const {
    return version;
}

fds_uint32_t
DLT::getDepth() const {
    return depth;
}

fds_uint32_t DLT::getWidth() const {
    return getNumBitsForToken();
}

fds_uint32_t DLT::getNumBitsForToken() const {
    return numBitsForToken;
}

fds_uint32_t DLT::getNumTokens() const {
    return numTokens;
}

fds_token_id DLT::getToken(const ObjectID& objId) const {
    fds_uint64_t token_bitmask = ((1 << numBitsForToken) - 1);
    fds_uint64_t bit_offset = (sizeof(objId.GetHigh())*8 - numBitsForToken);
    return (fds_token_id)(token_bitmask & (objId.GetHigh() >> bit_offset));
}

// get all the Nodes for a token/objid
DltTokenGroupPtr DLT::getNodes(fds_token_id token) const {
    fds_verify(token < numTokens);
    return distList->at(token);
}

DltTokenGroupPtr DLT::getNodes(const ObjectID& objId) const {
    fds_token_id token = getToken(objId);
    fds_verify(token < numTokens);
    return distList->at(token);
}

// get the primary node for a token/objid
NodeUuid DLT::getPrimary(fds_token_id token) const {
    fds_verify(token < numTokens);
    return getNodes(token)->get(0);
}

NodeUuid DLT::getPrimary(const ObjectID& objId) const {
    return getNodes(objId)->get(0);
}

NodeUuid DLT::getNode(fds_token_id token, uint index) const {
    fds_verify(token < numTokens);
    return getNodes(token)->get(index);
}

NodeUuid DLT::getNode(const ObjectID& objId, uint index) const {
    return getNodes(objId)->get(index);
}

void DLT::setNode(fds_token_id token, uint index, NodeUuid nodeuuid) {
    fds_verify(index < depth);
    fds_verify(token < numTokens);
    distList->at(token)->set(index, nodeuuid);
}

void DLT::setNodes(fds_token_id token, const DltTokenGroup& nodes) {
    fds_verify(nodes.getLength() == depth);
    for (uint i = 0; i < depth ; i++) {
        distList->at(token)->set(i, nodes.get(i));
    }
}

void DLT::generateNodeTokenMap() {
    LOGNORMAL << "generating node-token map";
    std::vector<DltTokenGroupPtr>::const_iterator iter;
    mapNodeTokens->clear();
    uint i;
    fds_token_id token = 0;
    for (iter = distList->begin(); iter != distList->end(); iter++) {
        const DltTokenGroupPtr& nodeList= *iter;
        for (i = 0; i < depth; i++) {
            (*mapNodeTokens)[nodeList->get(i)].push_back(token);
        }
        token++;
    }
}

// get the Tokens for a given Node
const TokenList& DLT::getTokens(const NodeUuid &uid) const{
    static TokenList emptyTokenList;
    NodeTokenMap::const_iterator iter = mapNodeTokens->find(uid);
    if (iter != mapNodeTokens->end()) {
        return iter->second;
    } else {
        // TODO(prem) : need to revisit this
        return emptyTokenList;
    }
}

// get the Tokens for a given Node at a specific index
void DLT::getTokens(TokenList* tokenList, const NodeUuid &uid, uint index) const {
    NodeTokenMap::const_iterator iter = mapNodeTokens->find(uid);
    if (iter != mapNodeTokens->end()) {
        TokenList::const_iterator tokenIter;
        NodeUuid curNode;
        const TokenList& tlist = iter->second;
        for (tokenIter = tlist.begin(); tokenIter != tlist.end(); ++tokenIter) {
            curNode = distList->at(*tokenIter)->get(index);
            if (curNode == uid) {
                tokenList->push_back(*tokenIter);
            }
        }
    }
}

void DLT::dump() const {
    // go thru with the fn iff loglevel is debug or lesser.

    LOGNORMAL<< "Dlt :"
             << "[version: " << version  << "] "
             << "[timestamp: " << timestamp  << "] "
             << "[depth: " << depth  << "] "
             << "[num.Tokens: " << numTokens  << "] "
             << "[num.Nodes: " << mapNodeTokens->size() << "]";

    LEVELCHECK(debug){} else return; // NOLINT

    LOGDEBUG << "token groups : --- ";
    // print the whole dlt
    std::vector<DltTokenGroupPtr>::const_iterator iter;
    std::ostringstream oss;
    uint count = 0;
    for (iter = distList->begin(); iter != distList->end(); iter++) {
        const DltTokenGroupPtr& nodeList= *iter;
        oss.str("");
        oss<< std::setw(6) << count++ <<":";
        for (uint i = 0; i < depth; i++) {
            oss<< std::setw(8) << nodeList->get(i).uuid_get_val() << ",";
        }
        LOGDEBUG << oss.str();
    }

    LOGDEBUG  << "node token map : --- ";
    NodeTokenMap::const_iterator tokenMapiter;

    // build the unique uuid list
    fds_uint64_t token = 0, prevToken = 0 , firstToken = 0;

    for (tokenMapiter = mapNodeTokens->begin(); tokenMapiter != mapNodeTokens->end(); ++tokenMapiter) { // NOLINT
        TokenList::const_iterator tokenIter;
        const TokenList& tlist = tokenMapiter->second;
        oss.str("");
        oss << std::setw(8) << tokenMapiter->first.uuid_get_val() << ":";
        prevToken = 0;
        firstToken = 0;
        for (tokenIter = tlist.begin(); tokenIter != tlist.end(); ++tokenIter) {
            token = *tokenIter;
            if (firstToken == 0) {
                oss << token;
                firstToken = token;
            } else if (token == (prevToken + 1)) {
                // do nothing .. this is a range
            } else {
                if (prevToken != firstToken) {
                    // this is a range
                    oss << " - " << prevToken;
                }
                oss << "," << token;
                firstToken = token;
            }
            prevToken = token;
        }
        LOGDEBUG << oss.str();
    }
}

uint32_t DLT::write(serialize::Serializer*  s   ) {
    LOGNORMAL << "serializing dlt";
    uint32_t b = 0;

    b += s->writeI64(version);
    // b += s->writeI64(timestamp);
    b += s->writeI32(numBitsForToken);
    b += s->writeI32(depth);
    b += s->writeI32(numTokens);

    if (mapNodeTokens->empty()) generateNodeTokenMap();

    typedef std::map<fds_uint64_t, uint32_t> UniqueUUIDMap;
    std::vector<fds_uint64_t> uuidList;
    UniqueUUIDMap uuidmap;

    NodeTokenMap::const_iterator tokenMapiter;

    // build the unique uuid list
    uint32_t count = 0;
    fds_uint64_t uuid;
    for (tokenMapiter = mapNodeTokens->begin(); tokenMapiter != mapNodeTokens->end(); ++tokenMapiter, ++count) { //NOLINT
        uuid = tokenMapiter->first.uuid_get_val();
        uuidmap[uuid] = count;
        uuidList.push_back(uuid);
    }
    //std::cout<<"count:"<<count<<": listsize:" <<uuidList.size()<<std::endl; // NOLINT
    // write the unique Node list
    // size of the node list
    b += s->writeI32(count);
    for (auto uuid : uuidList) {
        b += s->writeI64(uuid);
    }

    std::vector<DltTokenGroupPtr>::const_iterator iter;
    uint32_t lookupid;
    // with a byte we can support 256  unique Nodes
    // with 16-bit we can support 65536 unique Nodes
    bool fByte = (count <= 256);

    for (iter = distList->begin(); iter != distList->end(); iter++) {
        const DltTokenGroupPtr& nodeList= *iter;
        for (uint i = 0; i < depth; i++) {
            uuid = nodeList->get(i).uuid_get_val();
            lookupid = uuidmap[uuid];
            if (fByte) {
                b += s->writeByte(lookupid);
            } else {
                b += s->writeI16(lookupid);
            }
        }
    }
    return b;
}

uint32_t DLT::read(serialize::Deserializer* d) {
    LOGNORMAL << "de serializing dlt";
    uint32_t b = 0;

    b += d->readI64(version);
    // b += s->readI64(timestamp);
    b += d->readI32(numBitsForToken);
    b += d->readI32(depth);
    b += d->readI32(numTokens);

    fds_uint64_t uuid;
    distList->clear();
    distList->reserve(numTokens);

    uint32_t count = 0;
    std::vector<fds_uint64_t> uuidList;

    // read the Unique Node List
    b += d->readI32(count);
    uuidList.reserve(count);
    for (uint i = 0; i < count ; i++) {
        b += d->readI64(uuid);
        uuidList.push_back(uuid);
    }

    bool fByte = (count <= 256);
    fds_uint8_t i8;
    fds_uint16_t i16;

    std::vector<DltTokenGroupPtr>::const_iterator iter;
    for (uint i = 0; i < numTokens ; i++) {
        DltTokenGroupPtr ptr = boost::shared_ptr<DltTokenGroup>(new DltTokenGroup(depth));
        for (uint j = 0; j < depth; j++) {
            if (fByte) {
                b += d->readByte(i8);
                ptr->set(j, uuidList.at(i8));
            } else {
                b += d->readI16(i16);
                ptr->set(j, uuidList.at(i16));
            }
        }
        distList->push_back(ptr);
    }

    return b;
}

void DLT::getSerialized(std::string& serializedData) { // NOLINT
    serialize::Serializer *s = serialize::getMemSerializer(512*KB);
    uint32_t bytesWritten = this->write(s);
    LOGDEBUG << "dlt.getSerialized : byteswritten : " << bytesWritten;
    serializedData.append(s->getBufferAsString());
    delete s;
}

bool DLT::loadSerialized(std::string& serializedData) { // NOLINT
    serialize::Deserializer *d = serialize::getMemDeserializer(serializedData);
    uint32_t bytesRead =this->read(d);
    LOGNORMAL << "dlt.loadSerialized : byteswritten : " <<bytesRead;
    delete d;
    return true;
}

//================================================================================

/**
 *  Implementation for DLT Diff
 */

DLTDiff::DLTDiff(DLT* baseDlt, fds_uint64_t version) {
    fds_verify(NULL != baseDlt);
    this->baseDlt = baseDlt;
    baseVersion = baseDlt->version;
    this->version = (version > 0)?version:baseVersion+1;
}

// get all the Nodes for a token/objid
DltTokenGroupPtr DLTDiff::getNodes(fds_token_id token) const {
    std::map<fds_token_id, DltTokenGroupPtr>::const_iterator iter;
    iter = mapTokenNodes.find(token);
    if (iter != mapTokenNodes.end()) return iter->second;

    return baseDlt->getNodes(token);
}

DltTokenGroupPtr DLTDiff::getNodes(const ObjectID& objId) const {
    return getNodes(baseDlt->getToken(objId));
}

// get the primary node for a token/objid
NodeUuid DLTDiff::getPrimary(fds_token_id token) const {
    return getNodes(token)->get(0);
}

NodeUuid DLTDiff::getPrimary(const ObjectID& objId) const {
    return getNodes(objId)->get(0);
}

void DLTDiff::setNode(fds_token_id token, uint index, NodeUuid nodeuuid) {
    std::map<fds_token_id, DltTokenGroupPtr>::const_iterator iter;
    iter = mapTokenNodes.find(token);
    if (iter != mapTokenNodes.end()) {
        iter->second->set(index, nodeuuid);
        return;
    }

    mapTokenNodes[token] = boost::shared_ptr<DltTokenGroup>
            (new DltTokenGroup(baseDlt->depth));
    mapTokenNodes[token]->set(index, nodeuuid);
}

//================================================================================
/**
 *  Implementation for DLT Manager
 */
DLTManager::DLTManager(fds_uint8_t maxDlts) : maxDlts(maxDlts) {
}

void DLTManager::checkSize() {
    // check if the no.of dlts stored are within limits
    if (dltList.size() > maxDlts) {
        DLT* pDlt = dltList.front();
        // make sure the top one is not the current One
        if (pDlt != curPtr) {
            // OK go ahead remove & delete
            dltList.erase(dltList.begin());
            delete pDlt;
        }
    }
}

bool DLTManager::add(const DLT& _newDlt) {
    DLT* pNewDlt = new DLT(_newDlt);
    DLT& newDlt = *pNewDlt;

    if (dltList.empty()) {
        dltList.push_back(pNewDlt);
        curPtr = pNewDlt;
        // TODO(prem): checkSize();
        return true;
    }

    const DLT& current = *curPtr;

    for (uint i = 0; i < newDlt.numTokens; i++) {
        if (current.distList->at(i) != newDlt.distList->at(i)) {
            // There is the diff in pointer data
            // so check if there is a diff in actual data
            bool fSame = true;
            for (uint j = 0 ; j < newDlt.depth ; j++) {
                if (current.distList->at(i)->get(j) != newDlt.distList->at(i)->get(j)) {
                    fSame = false;
                    break;
                }
            }
            if (fSame) {
                // the contents are same
                newDlt.distList->at(i) = current.distList->at(i);
            } else {
                // Nothing to do for now.. We will leave it as is
            }
        }
    }
    dltList.push_back(&newDlt);
    // TODO(prem): checkSize();

    // switch this to current ???
    curPtr = &newDlt;

    return true;
}

bool DLTManager::addSerializedDLT(std::string& serializedData, bool fFull) { //NOLINT
    DLT dlt(0, 0, 0, false);
    // Deserialize the DLT
    dlt.loadSerialized(serializedData);
    // Recompute the node token map cache
    dlt.generateNodeTokenMap();
    return add(dlt);
}

bool DLTManager::add(const DLTDiff& dltDiff) {
    const DLT *baseDlt = dltDiff.baseDlt;
    if (!baseDlt) baseDlt=getDLT(dltDiff.baseVersion);
    LOGNOTIFY << "adding a diff - base:" << dltDiff.baseVersion
              << " version:" << dltDiff.version;

    DLT *dlt = new DLT(baseDlt->numBitsForToken, baseDlt->depth, dltDiff.version, false);
    dlt->distList->reserve(dlt->numTokens);

    DltTokenGroupPtr ptr;
    std::map<fds_token_id, DltTokenGroupPtr>::const_iterator iter;

    for (uint i = 0; i < dlt->numTokens; i++) {
        ptr = baseDlt->distList->at(i);

        iter = dltDiff.mapTokenNodes.find(i);
        if (iter != dltDiff.mapTokenNodes.end()) ptr = iter->second;

        if (curPtr->distList->at(i) != ptr) {
            bool fSame = true;
            for (uint j = 0 ; j < dlt->depth ; j++) {
                if (curPtr->distList->at(i)->get(j) != ptr->get(j)) {
                    fSame = false;
                    break;
                }
            }
            if (fSame) {
                ptr = curPtr->distList->at(i);
            }
        }
        dlt->distList->push_back(ptr);
    }

    dltList.push_back(dlt);

    return true;
}


const DLT* DLTManager::getDLT(const fds_uint64_t version) const {
    if (0 == version) {
        return curPtr;
    }
    std::vector<DLT*>::const_iterator iter;
    for (iter = dltList.begin(); iter != dltList.end(); iter++) {
        if (version == (*iter)->version) {
            return *iter;
        }
    }

    return NULL;
}

void DLTManager::setCurrent(fds_uint64_t version) {
    LOGNOTIFY <<" setting the current dlt to version: " <<version;
    const DLT* pdlt = getDLT(version);
    fds_verify(pdlt != NULL);
    curPtr = pdlt;
}

DltTokenGroupPtr DLTManager::getNodes(fds_token_id token) const {
    return curPtr->getNodes(token);
}

DltTokenGroupPtr DLTManager::getNodes(const ObjectID& objId) const {
    return curPtr->getNodes(objId);
}

// get the primary node for a token/objid
// NOTE:: from the current dlt!!!
NodeUuid DLTManager::getPrimary(fds_token_id token) const {
    return curPtr->getPrimary(token);
}

NodeUuid DLTManager::getPrimary(const ObjectID& objId) const {
    return getPrimary(objId);
}

uint32_t DLTManager::write(serialize::Serializer*  s) {
    LOGTRACE << " serializing dltmgr  ";
    uint32_t bytes = 0;
    // current version number
    bytes += s->writeI64(curPtr->version);

    // max no.of dlts
    bytes += s->writeByte(maxDlts);

    // number of dlts
    bytes += s->writeByte(dltList.size());

    // write the individual dlts
    std::vector<DLT*>::const_iterator iter;
    for (iter = dltList.begin(); iter != dltList.end(); iter++) {
        bytes += (*iter)->write(s);
    }

    return bytes;
}

uint32_t DLTManager::read(serialize::Deserializer* d) {
    LOGTRACE << " deserializing dltmgr  ";
    uint32_t bytes = 0;
    fds_uint64_t version;
    // current version number
    bytes += d->readI64(version);

    // max no.of dlts
    bytes += d->readByte(maxDlts);

    fds_uint8_t numDlts = 0;
    // number of dlts
    bytes += d->readByte(numDlts);

    // first clear the current dlts
    dltList.clear();

    // the individual dlts
    for (uint i = 0; i < numDlts ; i++) {
        DLT dlt(0, 0, 0, false);
        bytes += dlt.read(d);
        this->add(dlt);
    }

    // now set the current version
    setCurrent(version);

    return bytes;
}

bool DLTManager::loadFromFile(std::string filename) {
    LOGNOTIFY << "loading dltmgr from file : " << filename;
    serialize::Deserializer *d= serialize::getFileDeserializer(filename);
    read(d);
    delete d;
    return true;
}

bool DLTManager::storeToFile(std::string filename) {
    LOGNOTIFY << "storing dltmgr to file : " << filename;
    serialize::Serializer *s= serialize::getFileSerializer(filename);
    write(s);
    delete s;
    return true;
}

void DLTManager::dump() const {
    LOGNORMAL << "Dlt Manager : "
              << "[num.dlts : " << dltList.size() << "] "
              << "[maxDlts  : " << maxDlts << "]";

    std::vector<DLT*>::const_iterator iter;

    for (iter = dltList.begin() ; iter != dltList.end() ; iter++) {
        (*iter)->dump();
    }
}

}  // namespace fds
