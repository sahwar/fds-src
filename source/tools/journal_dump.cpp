// Copyright (c) 2012 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <stdio.h>
#include <iostream>
#include <vector>
#include <string>
#include "port/port.h"
#include "db/dbformat.h"
#include "db/filename.h"
#include "db/log_reader.h"
#include "db/version_edit.h"
#include "db/write_batch_internal.h"
#include "leveldb/env.h"
#include "leveldb/iterator.h"
#include "leveldb/options.h"
#include "leveldb/status.h"
#include "leveldb/table.h"
#include "leveldb/write_batch.h"
#include "util/logging.h"

#include <boost/scoped_ptr.hpp>
#include <DmBlobTypes.h>
#include <dm-vol-cat/DmPersistVolCat.h>

#define USE_NEW_LDB_STRUCTURES
#include <leveldb/cat_journal.h>

namespace leveldb {

namespace {

// Print contents of a log file. (*func)() is called on every record.
bool PrintLogContents(Env* env, const std::string& fname,
                      void (*func)(Slice)) {
    SequentialFile* file;
    Status s = env->NewSequentialFile(fname, &file);
    if (!s.ok()) {
        fprintf(stderr, "%s\n", s.ToString().c_str());
        return false;
    }
    CorruptionReporter reporter;
    log::Reader reader(file, &reporter, true, 0);
    Slice record;
    std::string scratch;
    while (reader.ReadRecord(&record, &scratch)) {
        printf("--- offset %llu; ",
               static_cast<unsigned long long>(reader.LastRecordOffset())); //NOLINT
        (*func)(record);
    }
    delete file;
    return true;
}

// Called on every item found in a WriteBatch.
class WriteBatchItemPrinter : public WriteBatch::Handler {
  public:
    uint64_t offset_;
    uint64_t sequence_;

    virtual void Put(const Slice& key, const Slice& value) {
#ifdef USE_NEW_LDB_STRUCTURES
        const fds::BlobObjKey * objKey = reinterpret_cast<const fds::BlobObjKey *>(key.data());
        if (0 == objKey->blobId && 0 == objKey->objIndex) {
            std::cout << "= >Timestamp: '" <<
                    *reinterpret_cast<const fds_uint64_t *>(value.data()) << "'\n";
        } else if (objKey->objIndex != fds::BLOB_META_INDEX) {
            std::cout << "= >put Blob: '" << objKey->blobId << "' [Index: " << objKey->objIndex
                      << " -> "
                      << fds::ObjectID(reinterpret_cast<const uint8_t *>(value.data()), value.size()) //NOLINT
                      << "]\n";
        } else {
            std::string dataStr(value.data(), value.size());
            fds::BlobMetaDesc blobMeta;
            blobMeta.loadSerialized(dataStr);

            std::cout << "= >put Blob: '" << objKey->blobId
                      << "' Name: '" << blobMeta.desc.blob_name
                      << "' Size: '" << blobMeta.desc.blob_size
                      << "' Version: '" << blobMeta.desc.version
                      << "'\n";
            std::cout << "  [ ";
            for (const auto & it : blobMeta.meta_list) {
                std::cout << it.first << ":" << it.second << " ";
            }
            std::cout << "]\n";
        }
#else
        std::string keyStr(key.data(), key.size());
        std::string dataStr(value.data(), value.size());

        fds::ExtentKey extKey;
        extKey.loadSerialized(keyStr);

        boost::scoped_ptr<fds::BlobExtent> extent;
        if (extKey.extent_id) {
            extent.reset(new fds::BlobExtent(extKey.extent_id, 2048, 0, 2048));
        } else {
            extent.reset(new fds::BlobExtent0(extKey.blob_name, 2048, 0, 2048));
        }

        extent->loadSerialized(dataStr);

        std::cout << "\n= >put 'Blob:" << extKey.blob_name << ", Extent:" << extKey.extent_id
                  << "'" << std::endl;
        std::vector<fds::ObjectID> oids;
        extent->getAllObjects(oids);
        for (unsigned i = 0; i < oids.size(); ++i) {
            std::cout << "[Index: " << i << " -> " << oids[i] << "]\n";
        }
#endif
    }

    virtual void Delete(const Slice& key) {
#ifdef USE_NEW_LDB_STRUCTURES
        const fds::BlobObjKey * objKey = reinterpret_cast<const fds::BlobObjKey *>(key.data());
        std::cout << "= >del Blob: '" << objKey->blobId
                  << "' Index: '" << objKey->objIndex << "'\n";
#else
        std::string keyStr(key.data(), key.size());
        fds::ExtentKey extKey;
        extKey.loadSerialized(keyStr);
        std::cout << "  del 'Blob:" << extKey.blob_name << ", Extent:" << extKey.extent_id
                  << "'" << std::endl;
#endif
    }
};


// Called on every log record (each one of which is a WriteBatch)
// found in a kLogFile.
static void WriteBatchPrinter(Slice record) {
    if (record.size() < 12) {
        printf("log record length %d is too small\n",
               static_cast<int>(record.size()));
        return;
    }
    WriteBatch batch;
    WriteBatchInternal::SetContents(&batch, record);
    printf("sequence %llu\n",
           static_cast<unsigned long long>(WriteBatchInternal::Sequence(&batch))); //NOLINT
    WriteBatchItemPrinter batch_item_printer;
    Status s = batch.Iterate(&batch_item_printer);
    std::cout << std::endl;
    if (!s.ok()) {
        printf("  error: %s\n", s.ToString().c_str());
    }
}

bool DumpLog(Env* env, const std::string& fname) {
    return PrintLogContents(env, fname, WriteBatchPrinter);
}

bool HandleDumpCommand(Env* env, char** files, int num) {
    bool ok = true;
    for (int i = 0; i < num; i++) {
        ok &= DumpLog(env, files[i]);
    }
    return ok;
}

} //NOLINT
}  // namespace leveldb

static void Usage() {
    std::cerr << "Usage: journal-dump files...\n"
              << "   files...         -- dump contents of specified journal files\n";
}

int main(int argc, char** argv) {
    leveldb::Env* env = leveldb::Env::Default();
    bool ok = true;
    if (argc < 2) {
        Usage();
        ok = false;
    } else {
        std::string command("dump");
        if (command == "dump") {
            // ok = leveldb::HandleDumpCommand(env, argv+1, argc-1);
            leveldb::CatJournalIterator iter(argv[1]);
            leveldb::WriteBatchItemPrinter batch_item_printer;
            for (; iter.isValid(); iter.Next()) {
                const leveldb::WriteBatch &wb = iter.GetBatch();
                leveldb::Status s = wb.Iterate(&batch_item_printer);
                std::cout << std::endl;
                if (!s.ok()) {
                    printf("  error: %s\n", s.ToString().c_str());
                }
            }
        } else {
            Usage();
            ok = false;
        }
    }
    return (ok ? 0 : 1);
}
