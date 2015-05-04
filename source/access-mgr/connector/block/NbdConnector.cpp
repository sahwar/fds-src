/*
 * Copyright 2014 by Formation Data Systems, Inc.
 */
#include <set>
#include <string>
#include <thread>

extern "C" {
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/uio.h>
}

#include <ev++.h>
#include <boost/shared_ptr.hpp>

#include "connector/block/NbdConnector.h"
#include "connector/block/NbdConnection.h"
#include "fds_process.h"

namespace fds {

NbdConnector::NbdConnector(std::weak_ptr<AmProcessor> processor)
        : nbdPort(10809),
          amProcessor(processor) {
    initialize();
}

void NbdConnector::initialize() {
    FdsConfigAccessor conf(g_fdsprocess->get_fds_config(), "fds.am.connector.nbd.");
    int pmPort = g_fdsprocess->get_fds_config()->get<uint32_t>("fds.pm.platform_port", 7000);
    nbdPort = pmPort + conf.get<uint32_t>("server_port_offset", 3809);

    cfg_no_delay = conf.get<bool>("options.no_delay", cfg_no_delay);
    cfg_keep_alive = conf.get<uint32_t>("options.keep_alive", cfg_keep_alive);

    // Shutdown the socket if we are reinitializing
    if (0 <= nbdSocket)
        { stop(); }

    // Bind to NBD listen port
    nbdSocket = createNbdSocket();
    if (nbdSocket < 0) {
        LOGERROR << "Could not bind to NBD port. No Nbd attachments can be made.";
        return;
    }

    // Setup event loop
    if (!evIoWatcher) {
        evIoWatcher = std::unique_ptr<ev::io>(new ev::io());
        evIoWatcher->set<NbdConnector, &NbdConnector::nbdAcceptCb>(this);
        evIoWatcher->start(nbdSocket, ev::READ);
        // Run event loop in thread
        runThread.reset(new std::thread(&NbdConnector::runNbdLoop, this));
        runThread->detach();
    } else {
        evIoWatcher->set(nbdSocket, ev::READ);
    }
}

void NbdConnector::stop() {
    if (0 <= nbdSocket) {
        shutdown(nbdSocket, SHUT_RDWR);
        close(nbdSocket);
        nbdSocket = -1;
    }
}

void NbdConnector::configureSocket(int fd) const {
    // Enable Non-Blocking mode
    if (0 > fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK)) {
        LOGWARN << "Failed to set socket NON-BLOCKING on NBD connection";
    }

    // Disable Nagle's algorithm, we do our own Corking
    if (cfg_no_delay) {
        LOGDEBUG << "Disabling Nagle's algorithm.";
        int opt_val = 1;
        if (0 > setsockopt(fd, SOL_TCP, TCP_NODELAY, &opt_val, sizeof(opt_val))) {
            LOGWARN << "Failed to set socket NON-BLOCKING on NBD connection";
        }
    }

    // Keep-alive
    // Discover dead peers and prevent network disconnect due to inactivity
    if (0 < cfg_keep_alive) {
        // The number of retry attempts
        static int const ka_probes = 9;
        // The time between retries
        int ka_intvl = (cfg_keep_alive / ka_probes) + 1;

        // Configure timeout
        if (setsockopt(fd, SOL_TCP, TCP_KEEPIDLE, &cfg_keep_alive, sizeof(cfg_keep_alive)) < 0) {
            LOGWARN << "Failed to set KEEPALIVE_IDLE on NBD connection";
        }
        if (setsockopt(fd, SOL_TCP, TCP_KEEPINTVL, &ka_intvl, sizeof(ka_intvl)) < 0) {
            LOGWARN << "Failed to set KEEPALIVE_INTVL on NBD connection";
        }
        if (setsockopt(fd, SOL_TCP, TCP_KEEPCNT, &ka_probes, sizeof(ka_probes)) < 0) {
            LOGWARN << "Failed to set KEEPALIVE_CNT on NBD connection";
        }

        // Enable KEEPALIVE on socket
        int optval = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval)) < 0) {
            LOGWARN << "Failed to set KEEPALIVE on NBD connection";
        }
    }
}

NbdConnector::~NbdConnector() {
    stop();
}

void
NbdConnector::nbdAcceptCb(ev::io &watcher, int revents) {
    if (EV_ERROR & revents) {
        LOGERROR << "Got invalid libev event";
        return;
    }

    /** First see if we even have a processing layer */
    auto processor = amProcessor.lock();
    if (!processor) {
        LOGNORMAL << "No processing layer, shutdown.";
        stop();
        return;
    }

    int clientsd = 0;
    while (0 <= clientsd) {
        socklen_t client_len = sizeof(sockaddr_in);
        sockaddr_in client_addr;

        // Accept a new NBD client connection
        do {
            clientsd = accept(watcher.fd,
                              (sockaddr *)&client_addr,
                              &client_len);
        } while ((0 > clientsd) && (EINTR == errno));

        if (0 <= clientsd) {
            // Setup some TCP options on the socket
            configureSocket(clientsd);

            // Create a handler for this NBD connection
            // Will delete itself when connection dies
            NbdConnection *client = new NbdConnection(clientsd, processor);
            LOGNORMAL << "Created client connection...";
        } else {
            switch (errno) {
            case ENOTSOCK:
            case EOPNOTSUPP:
            case EINVAL:
            case EBADF:
                // Reinitialize server
                LOGWARN << "Accept error: " << strerror(errno)
                        << " shutting down server.";
                evIoWatcher->stop();
                break;
            default:
                break; // Nothing special, no more clients
            }
        }
    };
}

int
NbdConnector::createNbdSocket() {
    sockaddr_in serv_addr;
    memset(&serv_addr, '0', sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(nbdPort);

    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        LOGERROR << "Failed to create NBD socket";
        return listenfd;
    }

    // If we crash this allows us to reuse the socket before it's fully closed
    int optval = 1;
    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        LOGWARN << "Failed to set REUSEADDR on NBD socket";
    }

    if (bind(listenfd,
             (sockaddr*)&serv_addr,
             sizeof(serv_addr)) == 0) {
        fcntl(listenfd, F_SETFL, fcntl(listenfd, F_GETFL, 0) | O_NONBLOCK);
        listen(listenfd, 10);
    } else {
        LOGERROR << "Bind to listening socket failed: " << strerror(errno);
        listenfd = -1;
    }

    return listenfd;
}

void
NbdConnector::runNbdLoop() {
    LOGNORMAL << "Accepting NBD connections on port " << nbdPort;
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGPIPE);
    if (0 != pthread_sigmask(SIG_BLOCK, &set, nullptr)) {
        LOGWARN << "Failed to enable SIGPIPE mask on NBD server.";
    }

    ev::default_loop loop;
    loop.run(0);
    LOGNORMAL << "Stopping NBD loop...";
}

}  // namespace fds
