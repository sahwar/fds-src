#!/usr/bin/python
import os, sys, re
from exp_framework import *

def create_tests():
    template = {
        "test_type" : "tgen",
        "nvols" : 4,
        "threads" : 1,
        "nreqs" : 10000,
        "type" : "PUT",
        "fsize" : 4096,
        "nfiles" : 1000
        }

    ###########################

    tests = []
    size = 4096 * 1024   # 4096
    test = dict(template)
    test["type"] = "PUT"
    test["nreqs"] = 100  # 100000
    test["nfiles"] = 100 # 10000
    test["nvols"] = 1 # 4
    test["threads"] = 1
    test["fsize"] = size
    tests.append(test)
    # for nvols in [1, 2, 3, 4]:
    #     for th in [1, 2, 5, 10, 15]:
    #         test = dict(template)
    #         test["type"] = "PUT"
    #         test["nreqs"] = 10000
    #         test["nfiles"] = 10000
    #         test["nvols"] = nvols
    #         test["threads"] = th
    #         tests.append(test)
    for nvols in [1]: # [1, 2, 3, 4]:
        for th in [1, 5, 10]: #[1, 2, 5, 10, 15]:
            test = dict(template)
            test["type"] = "GET"
            test["nreqs"] = 1000 # 100000
            test["nfiles"] = 100 # 10000
            test["nvols"] = nvols
            test["threads"] = th
            test["fsize"] = size
            tests.append(test)
    size = 4096 * 1024
    test = dict(template)
    test["type"] = "PUT"
    test["nreqs"] = 1000000
    test["nfiles"] = 100
    test["nvols"] = 4
    test["threads"] = 1
    test["fsize"] = size
    #tests.append(test)
    for nvols in [1, 2, 3, 4]:
        for th in [1, 2, 5, 10, 15]:
            test = dict(template)
            test["type"] = "GET"
            test["nreqs"] = 100000
            test["nfiles"] = 100
            test["nvols"] = nvols
            test["threads"] = th
            test["fsize"] = size
            #tests.append(test)

    return tests

def main():
    parser = OptionParser()
    #parser.add_option("-d", "--directory", dest = "directory", help = "Directory")
    #parser.add_option("-d", "--node-name", dest = "node_name", default = "node", help = "Node name")
    parser.add_option("-n", "--name-server-ip", dest = "ns_ip", default = "10.1.10.102", help = "IP of the name server")
    parser.add_option("-p", "--name-server-pprt", dest = "ns_port", type = "int", default = 47672, help = "Port of the name server")
    parser.add_option("-l", "--local", dest = "local", action = "store_true", default = True, help = "Run locally")
    #parser.add_option("-s", "--server-ip", dest = "s_ip", default = "10.1.10.17", help = "IP of the server")
    #parser.add_option("-c", "--column", dest = "column", help = "Column")
    #parser.add_option("-p", "--plot-enable", dest = "plot_enable", action = "store_true", default = False, help = "Plot graphs")
    #parser.add_option("-A", "--ab-enable", dest = "ab_enable", action = "store_true", default = False, help = "AB mode")
    (options, args) = parser.parse_args()

    # start FDS

    test_put = {
        "test_type" : "tgen",
        "nvols" : 4,
        "threads" : 1,
        "nreqs" : 100000,
        "type" : "PUT",
        "fsize" : 4096,
        "nfiles" : 1000
        }
    test_get = {
        "test_type" : "tgen",
        "nvols" : 4,
        "threads" : 15,
        "nreqs" : 100000,
        "type" : "GET",
        "fsize" : 4096,
        "nfiles" : 1000
        }
    #tests = [test_put, test_get]
    tests = create_tests()
    fds = FdsCluster("tiefighter", options)
    fds.start()

    print "tests -->", tests

    for t in tests:
        print t
        results_dir = get_results_dir("results", t)
        fds.init_test(t["test_type"], results_dir, t)
        # start stats collection
        counter_server = CounterServer(results_dir)
        #cmds = ["iostat -d 1", "collectl"]
        monitors = Monitors(None, results_dir, nodes, options)
        monitors.compute_monitors_cmds(fds.get_pidmap())
        monitors.run()
    
        # execute test
        # time.sleep(5)
        fds.run_test(t["test_type"], t)
    
        # terminate stats collection
        monitors.terminate()
        counter_server.terminate()

        time.sleep(5)
    # terminate FDS
    # fds.terminate()

if __name__ == "__main__":
    main()
