/*
 * Copyright 2013 Formation Data Systems, Inc.
 */
#ifndef SOURCE_LIB_CATALOG_H_
#define SOURCE_LIB_CATALOG_H_

#include <memory>
#include <string>
#include <stdint.h>
#include <stdio.h>

#include <fds_types.h>
#include <fds_error.h>
#include <leveldb/db.h>
#include <leveldb/env.h>
#include <leveldb/filter_policy.h>
#include "leveldb/write_batch.h"
#include <leveldb/copy_env.h>

namespace fds {

struct CatalogException : std::runtime_error {
	   explicit CatalogException (const std::string& what_arg) : std::runtime_error(what_arg) {}
};

struct CopyDetails {
    CopyDetails(const std::string & src, const std::string & dest)
            : srcPath(src), destPath(dest) {}
    const std::string srcPath;
    const std::string destPath;
};

/**
 * Just use leveldb's slice. We should consider our
 * own class in the future.
 */
typedef leveldb::Slice Record;
typedef leveldb::WriteBatch CatWriteBatch;

/**
 * Generic catalog super class.
 * Provides basic backing storage, update, and query.
 */
class Catalog {
  private:
    /**
     * Backing file name
     */
    std::string backing_file;

    /** The backing db.
     * Currently implements an LSM-tree.
     * We should make this more generic
     * and provide a generic interface for
     * when we change the backing DB.
     */
    std::unique_ptr<leveldb::DB> db;

    /*
     * leveldb file system interface 
     */
    std::unique_ptr<leveldb::CopyEnv> env;

    /*
     * Database options. These are not expected
     * to change.
     */
    leveldb::Options      options;       /**< LevelDB options */
    leveldb::WriteOptions write_options; /**< LevelDB write options */
    leveldb::ReadOptions  read_options;  /**< LevelDB read options */

    std::unique_ptr<leveldb::FilterPolicy const> filter_policy;

    static const std::string empty;

  public:
    static const fds_uint32_t WRITE_BUFFER_SIZE;
    static const fds_uint32_t CACHE_SIZE;

    /** Constructor */
    Catalog(const std::string& _file, fds_uint32_t writeBufferSize = WRITE_BUFFER_SIZE,
            fds_uint32_t cacheSize = CACHE_SIZE, const std::string& logDirName = empty,
            const std::string& logFilePrefix = empty, fds_uint32_t maxLogFiles = 0,
            leveldb::Comparator * cmp = 0);

    ~Catalog();

    /** Uses the underlying leveldb iterator */
    typedef leveldb::Iterator catalog_iterator_t;
    /** Gets catalog iterator
     * @return Pointer to catalog iterator
     */
    std::unique_ptr<catalog_iterator_t> NewIterator() {
        return std::unique_ptr<catalog_iterator_t>(db->NewIterator(read_options));
    }

    typedef leveldb::ReadOptions catalog_roptions_t;

    inline const leveldb::Options & GetOptions() const {
        return options;
    }

    inline leveldb::WriteOptions & GetWriteOptions() {
        return write_options;
    }

    fds::Error Update(const Record& key, const Record& val);
    fds::Error Update(CatWriteBatch* batch);
    fds::Error Query(const Record& key, std::string* val);
    fds::Error Delete(const Record& key);

    /**
     * Wrapper for leveldb::GetSnapshot(), ReleaseSnapshot()
     */
    void GetSnapshot(catalog_roptions_t& opts) {
        opts.snapshot = db->GetSnapshot();
    }
    void ReleaseSnapshot(catalog_roptions_t opts) {
        db->ReleaseSnapshot(opts.snapshot);
    }
    std::unique_ptr<catalog_iterator_t> NewIterator(catalog_roptions_t opts) {
        return std::unique_ptr<catalog_iterator_t>(db->NewIterator(opts));
    }

    bool DbEmpty();
    bool DbDelete();
    fds::Error DbSnap(const std::string& fileName);
    fds::Error QuerySnap(const std::string& _file, const Record& key, std::string* value);
    fds::Error QueryNew(const std::string& _file, const Record& key, std::string* value);

    inline void clearLogRotate() {
        fds_assert(env);
        env->logRotate() = false;
    }

    std::string GetFile() const;
  };

}  // namespace fds

#endif  // SOURCE_LIB_CATALOG_H_
