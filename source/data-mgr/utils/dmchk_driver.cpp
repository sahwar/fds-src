/*
 * Copyright 2015 by Formation Data Systems, Inc.
 */

#include <algorithm>
#include <boost/program_options.hpp>
#include <dmchk.h>
#include <vector>
namespace po = boost::program_options;

int main(int argc, char* argv[]) {
    fds_volid_t volumeUuid = invalid_vol_id;
    std::string blobName,root;
    // process command line options
    po::options_description desc("\nDM checker command line options");
    po::positional_options_description m_positional;
    desc.add_options()
            ("help,h", "help/ usage message")
            ("list,l", "list all volumes")
            ("blobs,b", "list blob contents for given volume")
            ("stats,s", "show stats")
            ("fds-root,f", po::value<std::string>(&root), "root dir")
            ("objects,o", po::value<std::string>(&blobName), "show objects for blob")
            ("volume,v", po::value<std::vector<uint64_t> >()->multitoken());

    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv)
              .options(desc)
              .allow_unregistered()
              .positional(m_positional)
              .run(), vm);
    po::notify(vm);
    bool fShowVolumeList =   vm.count("list") > 0;
    bool fShowStats =   vm.count("stats") > 0;
    if (vm.count("help")) {
        std::cout << desc << std::endl;
        return 0;
    }

    std::vector<fds_volid_t> volumes, allVolumes;
    if (vm.count("volume")) {
        auto parsedIds = vm["volume"].as<std::vector<uint64_t> >();
        if (!parsedIds.empty()) {
            std::transform(parsedIds.begin(), parsedIds.end(), std::back_inserter(volumes),
                           [](uint64_t const& val) -> fds_volid_t { return fds_volid_t(val); });
        }
    }

    if (!(fShowVolumeList || vm.count("blobs") || fShowStats || vm.count("objects"))) {
        fShowStats = true;
    }

    Module *dmchkVec[] = {
        nullptr
    };
    
    DmChecker dmchk(argc, argv, "platform.conf", "fds.dm.", dmchkVec, "DM checker driver");

    if (volumes.empty() || fShowVolumeList) {
        dmchk.getVolumeIds(allVolumes);
    }

    if (volumes.empty()) {
        volumes = allVolumes;
    }

    if (fShowVolumeList) {
        int count = 0;
        std::cout << "volumes in the system >> " << std::endl;
        std::cout << "no  :  volid " << std::endl;
        for (const auto v : allVolumes) {
            std::cout << "[" << ++count << "] : volid:" << v << std::endl;
        }
        std::cout << std::endl;
    }
    
    if (vm.count("blobs") || fShowStats) {
        bool fStatOnly = !vm.count("blobs");
        for (const auto v : volumes) {
            if (dmchk.loadVolume(v).ok()) {
                dmchk.listBlobs(fStatOnly);
            } else {
                std::cout << "unable to load volume:" << v << std::endl;
            }
        }
    }

    if (vm.count("objects")) {
        for (const auto v : volumes) {
            if (dmchk.loadVolume(v).ok()) {
                dmchk.blobInfo(blobName);
            } else {
                std::cout << "unable to load volume:" << v << std::endl;
            }
        }
    }
    return 0;
}
