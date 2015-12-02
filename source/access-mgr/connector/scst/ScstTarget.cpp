/*
 * ScstTarget.cpp
 *
 * Copyright (c) 2015, Brian Szmyd <szmyd@formationds.com>
 * Copyright (c) 2015, Formation Data Systems
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include "connector/scst/ScstTarget.h"

#include <algorithm>
#include <iterator>
#include <fstream>
#include <thread>

#include <ev++.h>

#include "connector/scst/ScstDevice.h"
#include "connector/scst/ScstCommon.h"
#include "util/Log.h"

namespace fds {

static std::string scst_iscsi_target_path   { "/sys/kernel/scst_tgt/targets/iscsi/" };
static std::string scst_iscsi_target_enable { "/enabled" };
static std::string scst_iscsi_target_mgmt   { "mgmt" };

static std::string scst_iscsi_cmd_add       { "add_target" };
static std::string scst_iscsi_cmd_remove    { "del_target" };

static std::string scst_iscsi_ini_path      { "/ini_groups/" };
static std::string scst_iscsi_ini_mgmt      { "/ini_groups/mgmt" };
static std::string scst_iscsi_lun_mgmt      { "/luns/mgmt" };

static std::string scst_iscsi_host_mgmt      { "/initiators/mgmt" };

ScstTarget::ScstTarget(std::string const& name, 
                       size_t const followers,
                       std::weak_ptr<AmProcessor> processor) :
    LeaderFollower(followers, false), 
    amProcessor(processor),
    target_name(name)
{
    // TODO(bszmyd): Sat 12 Sep 2015 12:14:19 PM MDT
    // We can support other target-drivers than iSCSI...TBD
    // Create an iSCSI target in the SCST mid-ware for our handler
    GLOGDEBUG << "Creating iSCSI target for connector: [" << target_name << "]";
    std::ofstream tgt_dev(scst_iscsi_target_path + scst_iscsi_target_mgmt, std::ios::out);

    if (!tgt_dev.is_open()) {
        GLOGERROR << "Could not create target, no iSCSI devices will be presented!";
        throw ScstError::scst_error;
    }

    // Add a new target for ourselves
    tgt_dev << scst_iscsi_cmd_add << " " << target_name << std::endl;
    tgt_dev.close();

    // Clear any mappings we might have left from before
    clearMasking();

    // Start watching the loop
    evLoop = std::make_shared<ev::dynamic_loop>(ev::NOENV | ev::POLL);
    if (!evLoop) {
        LOGERROR << "Failed to initialize lib_ev...SCST is not serving devices";
        throw ScstError::scst_error;
    }

    asyncWatcher = std::unique_ptr<ev::async>(new ev::async());
    asyncWatcher->set(*evLoop);
    asyncWatcher->set<ScstTarget, &ScstTarget::wakeupCb>(this);
    asyncWatcher->start();

    // Spawn a leader thread to start this target handling requests
    auto t = std::thread(&ScstTarget::follow, this);
    t.detach();
}

ScstTarget::~ScstTarget() = default;

void
ScstTarget::addDevice(std::string const& volume_name) {
    std::unique_lock<std::mutex> l(deviceLock);

    // Check if we have a device with this name already
    if (device_map.end() != device_map.find(volume_name)) {
        GLOGDEBUG << "Already have a device for volume: [" << volume_name << "]";
        return;
    }

    auto processor = amProcessor.lock();
    if (!processor) {
        GLOGERROR << "No processing layer, shutdown.";
        return;
    }

    // Get the next available LUN number
    auto lun_it = std::find_if_not(lun_table.begin(),
                                   lun_table.end(),
                                   [] (device_ptr& p) -> bool { return (!!p); });
    if (lun_table.end() == lun_it) {
        GLOGNOTIFY << "Target [" << target_name << "] has exhausted all LUNs.";
        return;
    }
    int32_t lun_number = std::distance(lun_table.begin(), lun_it);
    GLOGDEBUG << "Mapping [" << volume_name
              << "] to LUN [" << lun_number << "]";

    ScstDevice* device = nullptr;
    try {
    device = new ScstDevice(volume_name, this, processor);
    } catch (ScstError& e) {
        return;
    }

    lun_it->reset(device);
    device_map[volume_name] = lun_it;

    devicesToStart.push_back(lun_number);
    asyncWatcher->send();
    // Wait for devices to start before continuing
    deviceStartCv.wait(l, [this] () -> bool { return devicesToStart.empty(); });
}

void ScstTarget::mapDevices() {
    // Map the luns to either the security group or default group depending on
    // whether we have any assigned initiators.
    std::lock_guard<std::mutex> g(deviceLock);
    
    // add to default group
    auto lun_mgmt_path = (ini_members.empty() ?
                              (scst_iscsi_target_path + target_name + scst_iscsi_lun_mgmt) :
                              (scst_iscsi_target_path + target_name + scst_iscsi_ini_path
                                    + "allowed" + scst_iscsi_lun_mgmt));

    std::ofstream lun_mgmt(lun_mgmt_path, std::ios::out);
    if (!lun_mgmt.is_open()) {
        GLOGERROR << "Could not map luns for [" << target_name << "]";
        return;
    }

    for (auto const& device : device_map) {
        lun_mgmt << "add " << device.first
                 << " " << std::distance(lun_table.begin(), device.second)
                 << std::endl;
    }
}

void
ScstTarget::clearMasking() {
    GLOGNORMAL << "Clearing initiator mask for: " << target_name;
    // Remove the security group
    {
        std::ofstream ini_mgmt(scst_iscsi_target_path + target_name + scst_iscsi_ini_mgmt,
                              std::ios::out);
        if (ini_mgmt.is_open()) {
            ini_mgmt << "del allowed" << std::endl;
        }
    }
    ini_members.clear();
}

void
ScstTarget::setInitiatorMasking(std::vector<std::string> const& new_members) {
    std::unique_lock<std::mutex> l(deviceLock);

    if (new_members.empty()) {
        return clearMasking();
    }

    // Ensure ini group exists
    {
        std::ofstream ini_mgmt(scst_iscsi_target_path + target_name + scst_iscsi_ini_mgmt,
                              std::ios::out);
        if (!ini_mgmt.is_open()) {
            throw ScstError::scst_error;
        }
        ini_mgmt << "create allowed" << std::endl;
    }

    {
        std::ofstream host_mgmt(scst_iscsi_target_path
                                   + target_name
                                   + scst_iscsi_ini_path
                                   + "allowed"
                                   + scst_iscsi_host_mgmt,
                               std::ios::out);
        if (! host_mgmt.is_open()) {
            throw ScstError::scst_error;
        }
        // For each ini that is no longer in the list, remove from group
        std::vector<std::string> manip_list;
        std::set_difference(ini_members.begin(), ini_members.end(),
                            new_members.begin(), new_members.end(),
                            std::inserter(manip_list, manip_list.begin()));

        for (auto const& host : manip_list) {
            host_mgmt << "del " << host << std::endl;
        }
        manip_list.clear();

        // For each ini that is new to the list, add to the group
        std::set_difference(new_members.begin(), new_members.end(),
                            ini_members.begin(), ini_members.end(),
                            std::inserter(manip_list, manip_list.begin()));
        for (auto const& host : manip_list) {
            host_mgmt << "add " << host << std::endl;
        }

        ini_members = new_members;
    }
}

// Starting the devices has to happen inside the ev-loop
void
ScstTarget::startNewDevices() {
    {
        std::lock_guard<std::mutex> g(deviceLock);
        for (auto& lun: devicesToStart) {
            auto& device = lun_table[lun];

            // Persist changes in the LUN table and device map
            device->start(evLoop);
        }
        devicesToStart.clear();
    }
    deviceStartCv.notify_all();
}

void
ScstTarget::toggle_state(bool const enable) {
    GLOGDEBUG << "Toggling iSCSI target.";
    std::ofstream dev(scst_iscsi_target_path + target_name + scst_iscsi_target_enable,
                      std::ios::out);
    if (!dev.is_open()) {
        GLOGERROR << "Could not toggle target, no iSCSI devices will be presented!";
        throw ScstError::scst_error;
    }

    // Enable target
    if (enable) {
        mapDevices();
        dev << "1" << std::endl;
    } else { 
        dev << "0" << std::endl;
    }
    dev.close();
    GLOGNORMAL << "iSCSI target [" << target_name
               << "] has been " << (enable ? "enabled" : "disabled");
}

void
ScstTarget::wakeupCb(ev::async &watcher, int revents) {
    startNewDevices();
}

void
ScstTarget::lead() {
    evLoop->run(0);
}

}  // namespace fds
