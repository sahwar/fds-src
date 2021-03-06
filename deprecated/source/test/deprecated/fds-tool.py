#!/usr/bin/env python

# Copyright 2014 by Formation Data Systems, Inc.
#
import fdslib.FdsSetup as inst
import fdslib.BringUpCfg as fdscfg
import optparse, sys, time
import pdb

if __name__ == '__main__':
    parser = optparse.OptionParser("usage: %prog [options]")
    parser.add_option('-f', '--file', dest = 'config_file',
                      help = 'configuration file (e.g. sample.cfg)', metavar = 'FILE')
    parser.add_option('-v', '--verbose', action = 'store_true', dest = 'verbose',
                      help = 'enable verbosity')
    parser.add_option('-u', '--up', action = 'store_true', dest = 'clus_up',
                      help = 'bring up cluster')
    parser.add_option('--reboot', action = 'store_true', dest = 'reboot',
                      help = 'reboot cluster')
    parser.add_option('-d', '--down', action = 'store_true', dest = 'clus_down',
                      help = 'bring down cluster')
    parser.add_option('-s', '--status', action = 'store_true', dest = 'clus_status',
                      help = 'get cluster status')
    parser.add_option('-c', '--clean', action = 'store_true', dest = 'clus_clean',
                      help = 'cleanup cluster')
    parser.add_option('-r', '--dryrun', action = 'store_true', dest = 'dryrun',
                      help = 'dry run, print commands only')
    parser.add_option('-R', '--fds-root', dest = 'fds_root', default = '/fds',
                      help = 'specify fds-root directory')
    parser.add_option('-t', '--target', dest = 'target', default = None,
                      help = 'target node to apply -u, -d, -c, and -s against')

    (options, args) = parser.parse_args()

    if options.config_file is None:
        print "You need to pass config file"
        sys.exit(1)

    cfg = fdscfg.FdsConfigRun(None, options)

    # Get all the configuration
    nodes = cfg.rt_get_obj('cfg_nodes')
    ams   = cfg.rt_get_obj('cfg_am')
    cli = cfg.rt_get_obj('cfg_cli')
    start_om = True

    # filter nodes and ams based on the target
    if options.target:
        print "Target specified.  Applying commands against target: {}".format(options.target)
        # filter nodes
        nodes = [n for n in nodes if n.nd_conf_dict['node-name'] == options.target]
        if len(nodes) is 0:
            print "No matching nodes for the target"
            sys.exit(1)
        # filter ams
        ams = [a for a in ams if a.nd_conf_dict['fds_node'] == options.target]
        # Check if OM needs to be started
        if cfg.rt_om_node.nd_conf_dict['node-name'] != options.target:
            start_om = False


    # Shutdown
    if options.clus_down:
        for n in nodes:
            n.nd_cleanup_daemons()
            # todo(Rao): Unregister the services here.  Once we implement name based
            # services this should be easy
            print("NOTE: Run fdscli --remove-services to remove services")

    # Cleanup
    if options.clus_clean:
        for n in nodes:
            n.nd_cleanup_node()
            n.nd_rmt_agent.ssh_exec('/fds/sbin/redis.sh clean')
            n.nd_rmt_agent.ssh_exec('rm /fds/uuid_port')

    # Status
    if options.clus_status:
        for n in nodes:
            n.nd_rmt_agent.ssh_exec('ps -ef | grep -v grep | grep -v bash | grep com.formationds.om.Main', output = True)
            n.nd_rmt_agent.ssh_exec('ps -ef | grep -v grep | grep -v bash | grep com.formationds.am.Main', output = True)
            n.nd_rmt_agent.ssh_exec('ps -ef | grep -v grep | grep -v bash | grep plat', output = True)
            n.nd_rmt_agent.ssh_exec('ps -ef | grep -v grep | grep -v bash | grep Mgr', output = True)
            print '\n'
        sys.exit(0)

    if options.reboot:
        om = cfg.rt_om_node
        om_ip = om.nd_conf_dict['ip']

        for n in nodes:
            n.nd_start_platform(om_ip)

        if start_om:
            print "Start OM on IP", om_ip
            om.nd_start_om()
        time.sleep(30) # Do not remove this sleep
        sys.exit(0)

    if options.clus_up is None:
        sys.exit(0)

    for n in nodes:
        n.nd_rmt_agent.ssh_exec('python -m disk_type -m', wait_compl=True)
        n.nd_rmt_agent.ssh_exec('/fds/sbin/redis.sh start', wait_compl=True, output=True)

    om = cfg.rt_om_node
    om_ip = om.nd_conf_dict['ip']

    for n in nodes:
        n.nd_start_platform(om_ip)

    if start_om:
        print "Start OM on IP", om_ip
        om.nd_start_om()
	time.sleep(8)

    # activa nodes activates all services in the domain. Only
    # need to call it once
    """
    for n in nodes:
        if n.nd_conf_dict['node-name'] == 'node1':
            cli.run_cli('--activate-nodes abc -k 1 -e sm,dm')
        else:
            cli.run_cli('--activate-nodes abc -k 1 -e sm,dm')
        print "Waiting for node %s to come up" % n.nd_rmt_host
    	time.sleep(3)
    """

    cli.run_cli('--activate-nodes abc -k 1 -e sm,dm')
    print "Waiting for services to come up"
    time.sleep(8)

    for am in ams:
        am.am_start_service()
    print "Waiting for all commands to complete before existing"
    time.sleep(30) # Do not remove this sleep
