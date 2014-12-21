/*
 * Copyright 2013 Formation Data Systems, Inc.
 */

#include <string>
#include <fstream>

#include <lib/Catalog.h>
#include <leveldb/filter_policy.h>
#include <leveldb/cache.h>
#include <leveldb/copy_env.h>

namespace {

int doCopyFile(void * arg, const char* fname, fds_uint64_t length) {
    fds_assert(fname && *fname != 0);

    fds::CopyDetails * details = reinterpret_cast<fds::CopyDetails *>(arg);
    GLOGNORMAL << "Copying file '" << fname << "' to directory '" << details->destPath
            << "' from '" << details->srcPath;

    std::string srcFile = details->srcPath + "/" + fname;
    std::string destFile = details->destPath + "/" + fname;

    std::ifstream infile(srcFile.c_str(), std::fstream::binary);
    std::ofstream outfile(destFile.c_str(), std::fstream::binary);
    if (static_cast<fds_uint64_t>(-1) == length) {
        outfile << infile.rdbuf();
    } else if (length) {
        char * buffer = new char[length];
        infile.read(buffer, length);
        outfile.write(buffer, length);
        delete[] buffer;
    }
    outfile.close();
    infile.close();

    return 0;
}

}  // namespace

namespace fds {

const fds_uint32_t Catalog::WRITE_BUFFER_SIZE = 4 * 1024 * 1024;
const fds_uint32_t Catalog::CACHE_SIZE = 8 * 1024 * 1024;

#define FILTER_BITS_PER_KEY 12

const std::string Catalog::empty;

/** Catalog constructor
 */
Catalog::Catalog(const std::string& _file,
                 fds_uint32_t writeBufferSize /* = WRITE_BUFFER_SIZE */,
                 fds_uint32_t cacheSize /* = CACHE_SIZE */,
                 const std::string& logDirName /* = empty */,
                 const std::string& logFilePrefix /* = empty */,
                 fds_uint32_t maxLogFiles /* = 0 */,
                 leveldb::Comparator * cmp /* = 0 */) : backing_file(_file) {
    /*
     * Setup DB options
     */
    options.create_if_missing = 1;
    options.filter_policy     =
            leveldb::NewBloomFilterPolicy(FILTER_BITS_PER_KEY);
    options.write_buffer_size = writeBufferSize;
    options.block_cache = leveldb::NewLRUCache(cacheSize);
    if (cmp) {
        options.comparator = cmp;
    }
    env = new leveldb::CopyEnv(leveldb::Env::Default());
    if (!logDirName.empty() && !logFilePrefix.empty()) {
        leveldb::CopyEnv * cenv = static_cast<leveldb::CopyEnv*>(env);
        cenv->logDirName() = logDirName;
        cenv->logFilePrefix() = logFilePrefix;
        cenv->maxLogFiles() = maxLogFiles;

        cenv->logRotate() = !logFilePrefix.empty();
    }

    options.env = env;

    write_options.sync = true;

    leveldb::Status status = leveldb::DB::Open(options, backing_file, &db);

    /* Open has to succeed */
    if (!status.ok())
    {
        throw CatalogException(std::string(__FILE__) + ":" + std::to_string(__LINE__) +
                               " :leveldb::DB::Open(): " + status.ToString());
    }
}

/** The default destructor
 */
Catalog::~Catalog() {
    delete options.filter_policy;
    delete db;
}

/** Updates the catalog
 * @param[in] key the key to write to
 * @param[in] val the data to write
 * @return The result of the update
 */
Error
Catalog::Update(const Record& key, const Record& val) {
    Error err(ERR_OK);

    std::string test = val.ToString();

    leveldb::Status status = db->Put(write_options, key, val);
    if (!status.ok()) {
        err = Error(ERR_DISK_WRITE_FAILED);
    }

    return err;
}

Error
Catalog::Update(CatWriteBatch* batch) {
    Error err(ERR_OK);

    leveldb::Status status = db->Write(write_options, batch);
    if (!status.ok()) {
        err = Error(ERR_DISK_WRITE_FAILED);
    }

    return err;
}

/** Queries the catalog
 * @param[in]  key   the key to write to
 * @param[out] value the data found
 * @return The result of the query
 */
Error
Catalog::Query(const Record& key, std::string* value) {
    Error err(ERR_OK);

    leveldb::Status status = db->Get(read_options, key, value);
    if (status.IsNotFound()) {
        err = fds::Error(fds::ERR_CAT_ENTRY_NOT_FOUND);
        return err;
    } else if (!status.ok()) {
        err = fds::Error(fds::ERR_DISK_READ_FAILED);
        return err;
    }

    return err;
}

/** Delete a key/value pair
 * @param[in] key the key to delete
 * @return The result of the delete
 */
Error
Catalog::Delete(const Record& key) {
    Error err(ERR_OK);

    leveldb::Status status = db->Delete(write_options, key);
    if (!status.ok()) {
        err = Error(ERR_DISK_WRITE_FAILED);
    }

    return err;
}

/*
 * browse through the  DB and  if the db is valid  data return 
 * False( non Empty) else return true ( empty)
 */

bool
Catalog::DbEmpty() {
Catalog::catalog_iterator_t *db_it = NewIterator();
    for (db_it->SeekToFirst(); db_it->Valid(); db_it->Next()) {
       return true;
    }
    return false;
}

bool
Catalog::DbDelete() {
    delete options.filter_policy;
    delete db;
    return true;
}

Error
Catalog::DbSnap(const std::string& fileName) {
    fds_assert(!fileName.empty());
    Error err(ERR_OK);
    leveldb::CopyEnv * env = static_cast<leveldb::CopyEnv*>(options.env);
    fds_assert(env);

    leveldb::Status status = env->CreateDir(fileName);
    if (!status.ok()) {
        err = Error(ERR_DISK_WRITE_FAILED);
    }

    CopyDetails * details = new CopyDetails(backing_file, fileName);
    status = env->Copy(backing_file, &doCopyFile, reinterpret_cast<void *>(details));
    if (!status.ok()) {
        err = ERR_DISK_WRITE_FAILED;
    }

    return err;
}

Error
Catalog::QueryNew(const std::string& _file, const Record& key, std::string* value) {
    Error err(ERR_OK);
    leveldb::Status status;
    options.create_if_missing = 1;
    options.filter_policy     =
            leveldb::NewBloomFilterPolicy(FILTER_BITS_PER_KEY);
    options.write_buffer_size = WRITE_BUFFER_SIZE;

    write_options.sync = true;

    delete db;
    status = leveldb::DB::Open(options, _file, &db);
    assert(status.ok());

    status = db->Get(read_options, key, value);
    if (status.IsNotFound()) {
        err = fds::Error(fds::ERR_CAT_ENTRY_NOT_FOUND);
        return err;
    } else if (!status.ok()) {
        err = fds::Error(fds::ERR_DISK_READ_FAILED);
        return err;
    }

    return err;
}

Error
Catalog::QuerySnap(const std::string& _file, const Record& key, std::string* value) {
    Error err(ERR_OK);

    leveldb::DB* dbSnap;
    leveldb::Status status;
    options.create_if_missing = 1;
    options.filter_policy     =
            leveldb::NewBloomFilterPolicy(FILTER_BITS_PER_KEY);
    options.write_buffer_size = WRITE_BUFFER_SIZE;

    write_options.sync = true;

    status = leveldb::DB::Open(options, _file, &dbSnap);
    assert(status.ok());

    status = dbSnap->Get(read_options, key, value);
    if (status.IsNotFound()) {
        err = fds::Error(fds::ERR_CAT_ENTRY_NOT_FOUND);
        return err;
    } else if (!status.ok()) {
        err = fds::Error(fds::ERR_DISK_READ_FAILED);
        return err;
    }

    return err;
}

/** Gets backing file name
 * @return Copy of backing file name
 */
std::string
Catalog::GetFile() const {
    return backing_file;
}
}  // namespace fds
