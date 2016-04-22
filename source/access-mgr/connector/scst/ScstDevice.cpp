/*
 * ScstDevice.cpp
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

#include "connector/scst/ScstDevice.h"

#include <cerrno>
#include <string>
#include <type_traits>

extern "C" {
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
}

#include <ev++.h>
#include <boost/make_shared.hpp>

// TODO(bszmyd): Sun 18 Oct 2015 04:44:55 PM MDT
// REMOVE THIS!!!!
// =================
#include "fds_process.h"
#include "fds_config.hpp"
#include "util/Log.h"
// =================

#include "connector/scst/ScstTarget.h"
#include "fdsp/config_types_types.h"
extern "C" {
#include "connector/scst/scst_user.h"
}

#include "connector/scst/ScstTask.h"
#include "connector/scst/ScstMode.h"
#include "connector/scst/ScstInquiry.h"

static uint32_t const ieee_oui = 0x88A084;
static uint8_t const vendor_name[] = { 'F', 'D', 'S', ' ', ' ', ' ', ' ', ' ' };

static constexpr bool ensure(bool b)
{ return (!b ? throw fds::ScstError::scst_error : true); }

namespace fds
{

ScstDevice::ScstDevice(VolumeDesc const& vol_desc,
                       ScstTarget* target,
                       std::shared_ptr<AmProcessor> processor)
        : amProcessor(processor),
          scst_target(target),
          volumeName(vol_desc.name),
          readyResponses(4000),
          inquiry_handler(new InquiryHandler()),
          mode_handler(new ModeHandler())
{
    {
        FdsConfigAccessor config(g_fdsprocess->get_conf_helper());
        standalone_mode = config.get_abs<bool>("fds.am.testing.standalone", false);
    }

    setupModePages();
    setupInquiryPages(vol_desc.volUUID.get());
}

void ScstDevice::registerDevice(uint8_t const device_type, uint32_t const logical_block_size) {
    // Open the SCST user device
    if (0 > (scstDev = openScst())) {
        throw ScstError::scst_not_found;
    }

    // REGISTER a device
    scst_user_dev_desc scst_descriptor {
        (unsigned long)DEV_USER_VERSION, // Constant
        (unsigned long)"GPL",       // License string
        device_type,                  // Device type
        1, 0, 0, 0,                 // SGV enabled
        {                           // SCST options
                SCST_USER_PARSE_STANDARD,                   // parse type
                SCST_USER_ON_FREE_CMD_CALL,                 // command on-free type
                SCST_USER_MEM_REUSE_ALL,                    // buffer reuse type
                SCST_USER_PARTIAL_TRANSFERS_NOT_SUPPORTED,  // partial transfer type
                0,                                          // partial transfer length
                SCST_TST_0_SINGLE_TASK_SET,                 // task set sharing
                0,                                          // task mgmt only (on fault)
                SCST_QUEUE_ALG_1_UNRESTRICTED_REORDER,      // maintain consistency in reordering
                SCST_QERR_0_ALL_RESUME,                     // fault does not abort all cmds
                1, 0, 0, 0                                  // TAS/SWP/DSENSE/ORDER MGMT
        },
        logical_block_size,         // Block size
        0,                          // PR cmd Notifications
        {},
        "bare_am",
    };
    snprintf(scst_descriptor.name, SCST_MAX_NAME, "%s", volumeName.c_str());
    auto res = ioctl(scstDev, SCST_USER_REGISTER_DEVICE, &scst_descriptor);
    if (0 > res) {
        LOGERROR << "failed to register device:" << strerror(errno);
        throw ScstError::scst_error;
    }

    LOGNORMAL << "vol:" << volumeName
              << " blocksize:" << logical_block_size
              << " new device";
}

void ScstDevice::setupModePages()
{
    ControlModePage control_page;
    control_page &= ControlModePage::DefaultProtectionDisabled;
    control_page &= ControlModePage::FixedFormatSense;
    control_page &= ControlModePage::UnrestrictedCommandOrdering;
    control_page &= ControlModePage::NoUAOnRelease;
    control_page &= ControlModePage::AbortStatusOnTMF;
    control_page &= ControlModePage::SingleTaskSet;
    mode_handler->addModePage(control_page);
}

void ScstDevice::setupInquiryPages(uint64_t const volume_id)
{
    // Setup the standard inquiry page
    auto inquiry = inquiry_handler->getStandardInquiry();
    inquiry.setVendorId("FDS");
    inquiry.setProductId("FormationOne");
    inquiry.setRevision("BETA");
    inquiry_handler->setStandardInquiry(inquiry);

    // Write the Serial Number (0x80) Page
    char serial_number[17];
    snprintf(serial_number, sizeof(serial_number), "%.16lX", volume_id);
    VPDPage serial_page;
    serial_page.writePage(0x80, serial_number, 16);
    inquiry_handler->addVPDPage(serial_page);

    // Build our device identification descriptors
    DescriptorBuilder builder;
    builder &= VendorSpecificIdentifier(volumeName);
    builder &= T10Designator("FDS");
    builder &= NAADesignator(ieee_oui, volume_id);

    // Write the Device Identification (0x83) Page
    VPDPage device_id_page;
    device_id_page.writePage(0x83, builder.data(), builder.length());
    inquiry_handler->addVPDPage(device_id_page);

    // Write the Extended Inquiry (0x86) Page
    ExtVPDParameters evpd_parameters;
    evpd_parameters &= ExtVPDParameters::HeadAttrSupport;
    evpd_parameters &= ExtVPDParameters::SimpleAttrSupport;
    evpd_parameters &= ExtVPDParameters::OrderedAttrSupport;
    VPDPage evpd_page;
    evpd_page.writePage(0x86, &evpd_parameters, sizeof(ExtVPDParameters));
    inquiry_handler->addVPDPage(evpd_page);
}

void
ScstDevice::devicePoke(ConnectionState const state) {
    if (ConnectionState::NOCHANGE != state) {
        state_ = state;
    }
    asyncWatcher->send();
}

void
ScstDevice::start(std::shared_ptr<ev::dynamic_loop> loop) {
    ioWatcher = std::unique_ptr<ev::io>(new ev::io());
    ioWatcher->set(*loop);
    ioWatcher->set<ScstDevice, &ScstDevice::ioEvent>(this);
    ioWatcher->start(scstDev, ev::READ);

    asyncWatcher = std::unique_ptr<ev::async>(new ev::async());
    asyncWatcher->set(*loop);
    asyncWatcher->set<ScstDevice, &ScstDevice::wakeupCb>(this);
    asyncWatcher->start();
}

ScstDevice::~ScstDevice() {
    if (0 <= scstDev) {
        close(scstDev);
        scstDev = -1;
    }
    LOGNORMAL << "vol:" << volumeName << " stopped";
}

void
ScstDevice::shutdown() {
    state_ = ConnectionState::DRAINING;
    asyncWatcher->send();
}

int
ScstDevice::openScst() {
    int dev = open(DEV_USER_PATH DEV_USER_NAME, O_RDWR | O_NONBLOCK);
    if (0 > dev) {
        LOGERROR << "opening SCST device failed:" << strerror(errno);
    }
    return dev;
}

void
ScstDevice::wakeupCb(ev::async &watcher, int revents) {
    if (ConnectionState::RUNNING != state_) {
        startShutdown();
        if (ConnectionState::STOPPED == state_ ||
            ConnectionState::DRAINED == state_) {
            asyncWatcher->stop();                   // We are not responding
            ioWatcher->stop();
            if (ConnectionState::STOPPED == state_) {
                stopped();
                scst_target->deviceDone(volumeName);    // We are FIN!
                return;
            }
        }
    }

    // It's ok to keep writing responses if we've been shutdown
    if (!readyResponses.empty()) {
        ioEvent(*ioWatcher, ev::WRITE);
    }
}

void ScstDevice::execAllocCmd() {
    auto& length = cmd.alloc_cmd.alloc_len;
    LOGTRACE << "length:" << length << " allocation requested";

    // Allocate a page aligned memory buffer for Scst usage
    ensure(0 == posix_memalign((void**)&fast_reply.alloc_reply.pbuf, sysconf(_SC_PAGESIZE), length));
    // This is mostly to shutup valgrind
    memset((void*) fast_reply.alloc_reply.pbuf, 0x00, length);
    fastReply();
}

void ScstDevice::execMemFree() {
    LOGTRACE << "deallocation requested";
    free((void*)cmd.on_cached_mem_free.pbuf);
    fast_reply.result = 0;
    fastReply(); // Setup the reply for the next ioctl
}

void ScstDevice::execCompleteCmd() {
    LOGTRACE << "cmd:" << cmd.cmd_h << " complete";

    auto it = repliedResponses.find(cmd.cmd_h);
    if (repliedResponses.end() != it) {
        repliedResponses.erase(it);
    }
    if (!cmd.on_free_cmd.buffer_cached && 0 < cmd.on_free_cmd.pbuf) {
        free((void*)cmd.on_free_cmd.pbuf);
    }
    fast_reply.result = 0;
    fastReply(); // Setup the reply for the next ioctl
}

void ScstDevice::execParseCmd() {
    LOGWARN << "need parsing help";
    fds_panic("Should not be here!");
    fastReply(); // Setup the reply for the next ioctl
}

void ScstDevice::execTaskMgmtCmd() {
    auto& tmf_cmd = cmd.tm_cmd;
    auto done = (SCST_USER_TASK_MGMT_DONE == cmd.subcode) ? true : false;
    if (SCST_TARGET_RESET == tmf_cmd.fn) {
        LOGNOTIFY << "fn:" << tmf_cmd.fn
                  << "vol:" << volumeName
                  << " target reset " << (done ? "done" : "received");
    } else {
        LOGIO << "fn:" << tmf_cmd.fn
              << "vol:" << volumeName
              << " task management request " << (done ? "done" : "received");
    }

    if (done) {
        // Reset the reservation if we get a target reset
        switch (tmf_cmd.fn) {
        case SCST_TARGET_RESET:
        case SCST_LUN_RESET:
        case SCST_PR_ABORT_ALL:
            {
                reservation_session_id = invalid_session_id;
            }
        default:
            break;;
        }
    }

    fast_reply.result = 0;
    fastReply(); // Setup the reply for the next ioctl
}

void ScstDevice::execUserCmd() {
    auto& scsi_cmd = cmd.exec_cmd;
    auto& op_code = scsi_cmd.cdb[0];
    auto task = new ScstTask(cmd.cmd_h, SCST_USER_EXEC);

    // We may need to allocate a buffer for SCST, if we do and fail we'll need
    // to clean it up rather than expect a release command for it
    auto buffer = (uint8_t*)scsi_cmd.pbuf;
    size_t buflen = scsi_cmd.bufflen;
    if (!buffer && 0 < scsi_cmd.alloc_len) {
        ensure(0 == posix_memalign((void**)&buffer, sysconf(_SC_PAGESIZE), scsi_cmd.alloc_len));
        memset((void*) buffer, 0x00, scsi_cmd.alloc_len);
        task->setResponseBuffer(buffer, buflen, false);
    } else {
        task->setResponseBuffer(buffer, buflen, true);
    }

    // Poor man's goto
    do {
    // These commands do not require a reservation if one exists
    bool ignore_res = false;
    switch (op_code) {
    case INQUIRY:
    case LOG_SENSE:
    case RELEASE:
    case TEST_UNIT_READY:
      ignore_res = true;
    default:
      break;
    }

    // Check current reservation
    if (!ignore_res &&
        invalid_session_id != reservation_session_id &&
        reservation_session_id != scsi_cmd.sess_h) {
        task->reservationConflict();
        continue;
    }

    switch (op_code) {
    case TEST_UNIT_READY:
        LOGTRACE << "test unit ready received";
        break;
    case INQUIRY:
        {
            memset(buffer, '\0', buflen);

            // Check EVPD bit
            if (scsi_cmd.cdb[1] & 0x01) {
                auto& page = scsi_cmd.cdb[2];
                LOGTRACE << "page:" << (uint32_t)(page) << " requested";
                inquiry_handler->writeVPDPage(task, page);
            } else {
                LOGTRACE << "standard inquiry requested";
                inquiry_handler->writeStandardInquiry(task);
            }
        }
        break;
    case MODE_SENSE:
    case MODE_SENSE_10:
        {
            bool dbd = (0x00 != (scsi_cmd.cdb[1] & 0x08));
            uint8_t pc = scsi_cmd.cdb[2] / 0x40;
            uint8_t page_code = scsi_cmd.cdb[2] % 0x40;
            uint8_t& subpage = scsi_cmd.cdb[3];
            LOGTRACE << "dbd:" << dbd
                     << " pc:" << (uint32_t)pc
                     << " pagecode:" << (uint32_t)page_code
                     << " subpage:" << (uint32_t)subpage
                     << " mode sense";

            // We do not support any persistent pages, subpages
            if (0x01 & pc || 0x00 != subpage) {
                task->checkCondition(SCST_LOAD_SENSE(scst_sense_invalid_field_in_cdb));
                continue;
            }

            memset(buffer, '\0', buflen);

            if (MODE_SENSE == op_code) {
                mode_handler->writeModeParameters6(task, !dbd, page_code);
            } else {
                mode_handler->writeModeParameters10(task, !dbd, page_code);
            }
        }
        break;
    case RESERVE:
        LOGIO << "vol:" << volumeName << " reserving device";
        reservation_session_id = scsi_cmd.sess_h;
        break;
    case RELEASE:
        if (reservation_session_id == scsi_cmd.sess_h) {
            LOGIO << "vol:" << " releasing device";
            reservation_session_id = invalid_session_id;
        } else {
            LOGTRACE << "vol:" << volumeName << " release without reservation";
        }
        break;
    default:
        // See if this is a device specific command
        execDeviceCmd(task);
        return;
    }
    } while (false);
    readyResponses.push(task);
    deferredReply();
}

void
ScstDevice::getAndRespond() {
    cmd.preply = 0ull;
    do {
        // If we do not have reply read a response into it
        if (0ull == cmd.preply && !readyResponses.empty()) {
            ScstTask* resp {nullptr};
            ensure(readyResponses.pop(resp));
            cmd.preply = resp->getReply();
            repliedResponses[resp->getHandle()].reset(resp);
        }

        int res = 0;
        cmd.cmd_h = 0x00;
        cmd.subcode = 0x00;
        if (0 != cmd.preply) {
            auto const& reply = *reinterpret_cast<scst_user_reply_cmd*>(cmd.preply);
            LOGTRACE << "cmd:"<< reply.cmd_h
                     << " subcode:" << reply.subcode
                     << " result:" << reply.result
                     << " responding";
        }
        do { // Make sure and finish the ioctl
        res = ioctl(scstDev, SCST_USER_REPLY_AND_GET_CMD, &cmd);
        } while ((0 > res) && (EINTR == errno));

        if (0 != res) {
            switch (errno) {
            case ENOTTY:
            case EBADF:
                LOGNOTIFY << "Scst device no longer has a valid handle to the kernel, terminating";
                throw ScstError::scst_error;
            case EFAULT:
            case EINVAL:
                LOGNOTIFY << "Scst device sent invalid argument up the stack, terminating.";
                throw ScstError::scst_error;
            case EAGAIN:
                // If we still have responses, keep replying
                if (!readyResponses.empty()) {
                    deferredReply();
                    continue;
                }
            default:
                return;
            }
        }

        LOGTRACE << "cmd:" << cmd.cmd_h
                 << " subcode:" << cmd.subcode
                 << " received SCST command";
        switch (cmd.subcode) {
        case SCST_USER_ATTACH_SESS:
        case SCST_USER_DETACH_SESS:
            execSessionCmd();
            break;
        case SCST_USER_ON_FREE_CMD:
            execCompleteCmd();
            break;
        case SCST_USER_TASK_MGMT_RECEIVED:
        case SCST_USER_TASK_MGMT_DONE:
            execTaskMgmtCmd();
            break;
        case SCST_USER_ON_CACHED_MEM_FREE:
            execMemFree();
            break;
        case SCST_USER_ALLOC_MEM:
            execAllocCmd();
            break;
        case SCST_USER_PARSE:
            execParseCmd();
            break;
        case SCST_USER_EXEC:
            execUserCmd();
            break;
        default:
            LOGNOTIFY << "cmd:" << cmd.cmd_h
                      << " subcode:" << cmd.subcode
                      << " received unknown subcommand";
            break;
        }
    } while (true); // Keep replying while we have responses or requests
}

void
ScstDevice::ioEvent(ev::io &watcher, int revents) {
    if (EV_ERROR & revents) {
        LOGERROR << "invalid libev event";
        return;
    }

    // We are guaranteed to be the only thread acting on this file descriptor
    try {
        // Get the next command, and/or reply to any existing finished commands
        getAndRespond();
    } catch(ScstError const& e) {
        state_ = ConnectionState::DRAINING;
        if (e == ScstError::scst_error) {
            // If we had an error, stop the event loop too
            state_ = ConnectionState::DRAINED;
        }
    }
}

}  // namespace fds
