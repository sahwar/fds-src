/*
 * Copyright 2013 Formation Data Systems, Inc.
 */
#define GTEST_USE_OWN_TR1_TUPLE 0

#include <string>
#include <vector>
#include <map>
#include <thread>
#include <fdsn-server.h>
#include <util/fds_stat.h>
#include <native_api.h>
#include <am-platform.h>
#include <net/net-service.h>
#include <AccessMgr.h>

#include <gtest/gtest.h>
#include <testlib/DataGen.hpp>

namespace fds {

AccessMgr::unique_ptr am;

class AmProcessWrapper : public FdsProcess {
  public:
    AmProcessWrapper(int argc, char * argv[], const std::string & config,
                     const std::string & basePath, Module * vec[])
            : FdsProcess(argc, argv, config, basePath, vec) {
        am = AccessMgr::unique_ptr(new AccessMgr("AM Functional Test Module",
                                                 this));
    }
    virtual ~AmProcessWrapper() {
    }

    virtual int run() override {
        return 0;
    }
};

class AmLoadProc {
  public:
    AmLoadProc()
            : domainName(new std::string("Test Domain")),
              volumeName(new std::string("Test Volume")) {
        // register and populate volumes
        VolumeDesc volDesc(*volumeName, 5);
        volDesc.iops_min = 0;
        volDesc.iops_max = 0;
        volDesc.relativePrio = 1;
        fds_verify(am->registerVolume(volDesc) == ERR_OK);

        // hardcoding test config
        concurrency = 8;
        totalOps = 1000000;
        blobSize = 4096;
        opCount = ATOMIC_VAR_INIT(0);

        threads_.resize(concurrency);
    }
    virtual ~AmLoadProc() {
    }
    typedef std::unique_ptr<AmLoadProc> unique_ptr;

    enum TaskOps {
        PUT,
        GET,
        STAT
    };

    void task(int id, TaskOps opType) {
        fds_uint32_t ops = atomic_fetch_add(&opCount, (fds_uint32_t)1);
        GLOGDEBUG << "Starting thread " << id;
        // constants we are going to pass to api
        boost::shared_ptr<fds_int32_t> blobMode(new fds_int32_t);
        *blobMode = 1;
        boost::shared_ptr<int32_t> blobLength = boost::make_shared<int32_t>(blobSize);
        boost::shared_ptr<apis::ObjectOffset> off(new apis::ObjectOffset());
        std::map<std::string, std::string> metaData;
        boost::shared_ptr< std::map<std::string, std::string> > meta =
                boost::make_shared<std::map<std::string, std::string>>(metaData);

        SequentialBlobDataGen blobGen(blobSize, id);
        while ((ops + 1) <= totalOps) {
            if (opType == PUT) {
                try {
                    am->dataApi->updateBlobOnce(domainName, volumeName,
                                                blobGen.blobName, blobMode,
                                                blobGen.blobData, blobLength, off, meta);
                } catch(apis::ApiException fdsE) {
                    fds_panic("updateBlob failed");
                }
            } else if (opType == GET) {
                // do GET
            } else if (opType == STAT) {
                try {
                    apis::BlobDescriptor blobDesc;
                    am->dataApi->statBlob(blobDesc, domainName, volumeName, blobGen.blobName);
                } catch(apis::ApiException fdsE) {
                    fds_panic("statBlob failed");
                }
            } else {
                fds_panic("Unknown op type");
            }
            ops = atomic_fetch_add(&opCount, (fds_uint32_t)1);
            blobGen.generateNext();
        }
    }

    virtual int runTask(TaskOps opType) {
        opCount = 0;
        fds_uint64_t start_nano = util::getTimeStampNanos();
        for (unsigned i = 0; i < concurrency; ++i) {
            std::thread* new_thread = new std::thread(&AmLoadProc::task, this, i, opType);
            threads_[i] = new_thread;
        }

        // Wait for all threads
        for (unsigned x = 0; x < concurrency; ++x) {
            threads_[x]->join();
        }

        fds_uint64_t duration_nano = util::getTimeStampNanos() - start_nano;
        double time_sec = duration_nano / 1000000000.0;
        if (time_sec < 10) {
            std::cout << "Experiment ran for too short time to calc IOPS" << std::endl;
            GLOGNOTIFY << "Experiment ran for too short time to calc IOPS";
        } else {
            std::cout << "Average IOPS = " << totalOps / time_sec << std::endl;
            GLOGNOTIFY << "Average IOPS = " << totalOps / time_sec;
            std::cout << "Average latency = " << duration_nano / totalOps << std::endl;
            GLOGNOTIFY << "Averate latency = " << duration_nano / totalOps;
        }

        for (unsigned x = 0; x < concurrency; ++x) {
            std::thread* th = threads_[x];
            delete th;
            threads_[x] = NULL;
        }

        return 0;
    }

  private:
    /// performance test setup
    fds_uint32_t concurrency;
    /// total number of ops to execute
    fds_uint32_t totalOps;
    /// blob size in bytes
    fds_uint32_t blobSize;

    /// threads for blocking AM
    std::vector<std::thread*> threads_;

    /// to count number of ops completed
    std::atomic<fds_uint32_t> opCount;

    /// domain name
    boost::shared_ptr<std::string> domainName;
    boost::shared_ptr<std::string> volumeName;
};

AmLoadProc::unique_ptr amLoad;

TEST(AccessMgr, updateBlobOnce) {
    GLOGDEBUG << "Testing updateBlobOnce";
    amLoad->runTask(AmLoadProc::PUT);
}

TEST(AccessMgr, statBlob) {
    GLOGDEBUG << "Testing statBlob";
    amLoad->runTask(AmLoadProc::STAT);
}

}  // namespace fds

int
main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    fds::Module *amVec[] = {
        nullptr
    };
    fds::AmProcessWrapper apw(argc, argv, "platform.conf", "fds.am.", amVec);
    apw.main();

    amLoad = AmLoadProc::unique_ptr(new AmLoadProc());

    return RUN_ALL_TESTS();
}
