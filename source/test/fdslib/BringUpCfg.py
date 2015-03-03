#!/usr/bin/env python

# Copyright 2014 by Formations Data Systems, Inc.
#
import ConfigParser
import re
import sys
import subprocess
import time
import logging
import os
import os.path
import glob
import shutil
import FdsSetup as inst

###
# Base config class, which is key/value dictionary.
#
class FdsConfig(object):
    def __init__(self, items, verbose):
        self.nd_verbose   = verbose
        self.nd_conf_dict = {}
        if items is not None:
            for it in items:
                self.nd_conf_dict[it[0]] = it[1]

    def get_config_val(self, key):
        return self.nd_conf_dict[key]

    def debug_print(self, mesg):
        if self.nd_verbose['verbose'] or self.nd_verbose['dryrun']:
            print mesg
        return self.nd_verbose['dryrun']

###
# Handle node config section
#
class FdsNodeConfig(FdsConfig):
    def __init__(self, name, items, verbose):
        log = logging.getLogger(self.__class__.__name__ + '.' + '__init__')

        super(FdsNodeConfig, self).__init__(items, verbose)
        self.nd_conf_dict['node-name'] = name
        self.nd_host  = None
        self.nd_agent = None
        self.nd_package   = None
        self.nd_local_env = None
        self.nd_assigned_name = None
        self.nd_uuid = None
        self.nd_services = None

        # Is the node local or remote?
        if 'ip' not in self.nd_conf_dict:
            log.error("Missing ip keyword in the node section %s." % self.nd_conf_dict['node-name'])
            sys.exit(1)
        else:
            if (self.nd_conf_dict['ip'] == 'localhost') or (self.nd_conf_dict['ip'] == '127.0.0.1'):
                self.nd_local = True
            else:
                self.nd_local = False

    ###
    # Establish ssh connection with the remote node.  After this call, the obj
    # can use nd_agent to send ssh commands to the remote node.
    #
    # If the IP indicates the node is localhost and we're running from the
    # Test Harness, no SSH connection is made.
    #
    def nd_connect_agent(self, env, quiet=False):
        log = logging.getLogger(self.__class__.__name__ + '.' + 'nd_connect_agent')

        if 'fds_root' not in self.nd_conf_dict:
            if env.env_test_harness:
                log.error("Missing fds-root keyword in the node section %s." % self.nd_conf_dict['node-name'])
            else:
                print("Missing fds-root keyword in the node section %s." % self.nd_conf_dict['node-name'])
            sys.exit(1)

        if 'ip' not in self.nd_conf_dict:
            if env.env_test_harness:
                log.error("Missing ip keyword in the node section %s." % self.nd_conf_dict['node-name'])
            else:
                print("Missing ip keyword in the node section %s." % self.nd_conf_dict['node-name'])
            sys.exit(1)

        self.nd_local_env = env
        self.nd_host  = self.nd_conf_dict['ip']

        root = self.nd_conf_dict['fds_root']

        # At this time, only Test Harness usage will treat localhost
        # connectivity locally. This is to satisfy Jenkins/Docker
        # usages. Otherwise, all hosts, localhost or otherwise, are
        # treated remotely with SSH connectivity.
        if self.nd_local and env.env_test_harness:
            self.nd_agent = inst.FdsLocalEnv(root, verbose=self.nd_verbose, install=env.env_install,
                                             test_harness=env.env_test_harness)
            self.nd_agent.env_user     = env.env_user
            self.nd_agent.env_password = env.env_password
            self.nd_agent.env_sudo_password = env.env_sudo_password
            if not quiet:
                log.info("Making local connection to %s as node %s." % (self.nd_host, self.nd_conf_dict['node-name']))
            self.nd_agent.local_connect()
        else:
            self.nd_agent = inst.FdsRmtEnv(root, verbose=self.nd_verbose, test_harness=env.env_test_harness)
            self.nd_agent.env_user     = env.env_user
            self.nd_agent.env_password = env.env_password
            if not quiet:
                if env.env_test_harness:
                    log.info("Making ssh connection to %s as %s." % (self.nd_host, self.nd_conf_dict['node-name']))
                else:
                    print("Making ssh connection to %s as %s." % (self.nd_host, self.nd_conf_dict['node-name']))

            self.nd_agent.ssh_connect(self.nd_host)

    ###
    # Install the tar ball package at local location to the remote node.
    #
    def nd_install_rmt_pkg(self):
        if self.nd_agent is None:
            print "You need to call nd_connect_agent() first"
            sys.exit(0)

        print "Installing FDS package to:", self.nd_host_name()
        pkg = inst.FdsPackage(self.nd_agent)
        tar = self.nd_local_env.get_pkg_tar()
        status = pkg.package_install(self.nd_agent, tar)

        return status

    def nd_host_name(self):
        return '[' + self.nd_host + '] ' + self.nd_conf_dict['node-name']

    ###
    # Return True if this node is configured to run OM.
    #
    def nd_run_om(self):
        if 'om' in self.nd_conf_dict:
            return True if self.nd_conf_dict['om'] == "true" else False
        return False

    ###
    # We only start OM if the node is configured to run OM.
    #
    def nd_start_om(self, test_harness=False, _bin_dir=None, _log_dir=None):
        log = logging.getLogger(self.__class__.__name__ + '.' + __name__)

        if self.nd_run_om():
            if self.nd_agent is None:
                print "You need to call nd_connect_agent() first"
                sys.exit(0)

            fds_dir = self.nd_conf_dict['fds_root']

            if _bin_dir is None:
                bin_dir = fds_dir + '/bin'
            else:
                bin_dir = _bin_dir

            if _log_dir is None:
                log_dir = bin_dir
            else:
                log_dir = _log_dir

            print "\nStart OM in", self.nd_host_name()

            if test_harness:
                cur_dir = os.getcwd()
                os.chdir(bin_dir)
                status = self.nd_agent.exec_wait('bash -c \"(nohup ./orchMgr --fds-root=%s > %s/om.out 2>&1 &) \"' %
                                                 (fds_dir, log_dir if self.nd_agent.env_install else "."))
                os.chdir(cur_dir)
            else:
                status = self.nd_agent.ssh_exec_fds(
                    "orchMgr --fds-root=%s > %s/om.out" % (fds_dir, log_dir))

            time.sleep(2)
        else:
            log.warn("Attempting to start OM on node %s which is not configured to host OM." % self.nd_host_name())
            status = -1

        return status

    ###
    # Start platform services in all nodes.
    #
    def nd_start_platform(self, om_ip = None, test_harness=False, _bin_dir=None, _log_dir=None):
        port_arg = '--fds-root=%s --fds.pm.id=%s' % \
                   (self.nd_conf_dict['fds_root'], self.nd_conf_dict['node-name'])
        if 'fds_port' in self.nd_conf_dict:
            port = self.nd_conf_dict['fds_port']
            port_arg = port_arg + (' --fds.pm.platform_port=%s' % port)
        else:
            port = 7000  # PM default.

        if om_ip is not None:
            port_arg = port_arg + (' --fds.pm.om_ip=%s' % om_ip)

        fds_dir = self.nd_conf_dict['fds_root']

        if _bin_dir is None:
            bin_dir = fds_dir + '/bin'
        else:
            bin_dir = _bin_dir

        if _log_dir is None:
            log_dir = bin_dir
        else:
            log_dir = _log_dir

        print "\nStart platform daemon in", self.nd_host_name()

        # When running from the test harness, we want to wait for results
        # but not assume we are running from an FDS package install.
        if test_harness:
            cur_dir = os.getcwd()
            os.chdir(bin_dir)
            status = self.nd_agent.exec_wait('bash -c \"(nohup ./platformd --fds-root=%s > %s/pm.%s.out 2>&1 &) \"' %
                                            (fds_dir, log_dir if self.nd_agent.env_install else ".",
                                             port))
            os.chdir(cur_dir)
        else:
            self.nd_agent.exec_wait('bash -c \"(rm -rf /dev/shm/0x*)\"')
            status = self.nd_agent.ssh_exec_fds('platformd ' + port_arg +
                                            ' > %s/pm.out' % log_dir)

        if status == 0:
            time.sleep(4)

        return status

    ###
    # Capture the node's assigned name and UUID.
    #
    def nd_populate_metadata(self, _bin_dir=None):
        log = logging.getLogger(self.__class__.__name__ + '.' + 'nd_populate_metadata')

        if 'fds_port' in self.nd_conf_dict:
            port = self.nd_conf_dict['fds_port']
        else:
            port = 7000  # PM default.

        fds_dir = self.nd_conf_dict['fds_root']

        if _bin_dir is None:
            bin_dir = fds_dir + '/bin'
        else:
            bin_dir = _bin_dir

        # From the --list-services output we can determine node name
        # and node UUID.
        cur_dir = os.getcwd()
        os.chdir(bin_dir)
        status, stdout = self.nd_agent.exec_wait('bash -c \"(./fdscli --fds-root=%s --list-services) \"' % (fds_dir),
                                                 return_stdin=True)
        os.chdir(cur_dir)

        if status == 0:
            for line in stdout.split('\n'):
                if line.count("Node UUID") > 0:
                    uuid = line.split()[2]
                if line.count("Name") > 0:
                    assigned_name = line.split()[1]
                if line.count("Control") > 0:
                    if int(line.split()[2]) - 1 == int(port):
                        self.nd_assigned_name = assigned_name
                        self.nd_uuid = uuid
                        break

            log.debug("Node %s has assigned name %s and UUID 0x%s." %
                      (self.nd_conf_dict["node-name"], self.nd_assigned_name, self.nd_uuid))
        else:
            log.error("status = %s" % status)
            log.error(stdout)

        return status

    ###
    # Kill all fds daemons
    #
    def nd_cleanup_daemons(self):
        fds_dir = self.nd_conf_dict['fds_root']
        bin_dir = fds_dir + '/bin'
        sbin_dir = fds_dir + '/sbin'
        tools_dir = sbin_dir + '/tools'
        var_dir = fds_dir + '/var'
        print("\nCleanup running processes on: %s, %s" % (self.nd_host_name(), bin_dir))
        # TODO (Bao): order to kill: AM, SM/DM, OM
        # TODO WIN-936 signal handler improvements - should use a different signal
        # and give the processes a chance to shutdown cleanly
        self.nd_agent.exec_wait('pkill -9 -f com.formationds.am.Main')
        self.nd_agent.exec_wait('pkill -9 bare_am')
        self.nd_agent.exec_wait('pkill -9 AMAgent')
        self.nd_agent.exec_wait('pkill -9 StorMgr')
        self.nd_agent.exec_wait('pkill -9 DataMgr')
        self.nd_agent.exec_wait('pkill -9 orchMgr')
        self.nd_agent.exec_wait('pkill -9 platformd')
        self.nd_agent.exec_wait('pkill -9 -f com.formationds.om.Main')
        time.sleep(2)

    def nd_cleanup_daemons_with_fdsroot(self, fds_root):
        bin_dir = fds_root + '/bin'
        sbin_dir = fds_root + '/sbin'
        tools_dir = sbin_dir + '/tools'
        var_dir = fds_root + '/var'
        print("\nCleanup running processes on: %s, %s" % (self.nd_host_name(), bin_dir))
        # TODO (Bao): order to kill: AM, SM/DM, OM
        self.nd_agent.ssh_exec('pkill -9 -f \'\-\-fds\-root={}\''.format(fds_root), wait_compl=True)

    ###
    # cleanup any cores, redis, and logs.
    #
    def nd_cleanup_node(self, test_harness=False, _bin_dir=None):
        log = logging.getLogger(self.__class__.__name__ + '.' + 'nd_cleanup_node')

        fds_dir = self.nd_conf_dict['fds_root']
        bin_dir = fds_dir + '/bin'
        sbin_dir = fds_dir + '/sbin'
        tools_dir = sbin_dir + '/tools'
        dev_dir = fds_dir + '/dev'
        var_dir = fds_dir + '/var'

        if test_harness:
            log.info("Cleanup cores/logs/redis in: %s, %s" % (self.nd_host_name(), bin_dir))

            fds_dir = self.nd_conf_dict['fds_root']
            bin_dir = _bin_dir
            sbin_dir = _bin_dir + '../sbin'
            tools_dir = sbin_dir + '/tools'
            dev_dir = fds_dir + '/dev'
            var_dir = fds_dir + '/var'

            cur_dir = os.getcwd()

            if os.path.exists(bin_dir):
                log.info("Cleanup cores in: %s" % bin_dir)
                os.chdir(bin_dir)
                if os.path.exists("core"):
                    os.remove("core")

                files = glob.glob("*.core")
                for filename in files:
                    os.remove(filename)

            if os.path.exists(var_dir):
                log.info("Cleanup logs and stats in: %s" % var_dir)
                os.chdir(var_dir)
                if os.path.exists("logs"):
                    shutil.rmtree("logs")
                if os.path.exists("stats"):
                    shutil.rmtree("stats")

            if os.path.exists('/corefiles'):
                log.info("Cleanup cores in: %s" % '/corefiles')
                os.chdir('/corefiles')
                files = glob.glob("*.core")
                for filename in files:
                    os.remove(filename)

            if os.path.exists(var_dir + '/core'):
                log.info("Cleanup cores in: %s" % var_dir + '/core')
                os.chdir(var_dir + '/core')
                files = glob.glob("*.core")
                for filename in files:
                    os.remove(filename)

            if os.path.exists(tools_dir):
                log.info("Running ./fds clean -i in %s" % tools_dir)
                os.chdir(tools_dir)
                self.nd_agent.exec_wait('./fds clean -i')

            if os.path.exists(dev_dir):
                log.info("Cleanup hdd-* and sdd-* in: %s" % dev_dir)
                os.chdir(dev_dir)
                shutil.rmtree("hdd-*")
                shutil.rmtree("ssd-*")

            if os.path.exists(fds_dir):
                log.info("Cleanup sys-repo and user-repo in: %s" % fds_dir)
                os.chdir(fds_dir)
                shutil.rmtree("sys-repo")
                shutil.rmtree("user-repo")

            if os.path.exists('/dev/shm'):
                log.info("Cleanup 0x* in: %s" % '/dev/shm')
                os.chdir('/dev/shm')
                files = glob.glob("0x*")
                for filename in files:
                    os.remove(filename)

            os.chdir(cur_dir)
            status = 0
        else:
            print("Cleanup cores/logs/redis in: %s, %s" % (self.nd_host_name(), bin_dir))
            status = self.nd_agent.exec_wait('(cd %s && rm core *.core); ' % bin_dir +
                '(cd %s && rm -r logs stats); ' % var_dir +
                '(cd /corefiles && rm *.core); '  +
                '(cd %s/core && rm *.core); ' % var_dir +
                '(cd %s && ./fds clean -i); ' % tools_dir +
                '(cd %s && rm -rf hdd-*/* && rm -f ssd-*/*); ' % dev_dir +
                '(cd %s && rm -r sys-repo/ && rm -r user-repo/); ' % fds_dir +
                '(cd /dev/shm && rm -f 0x*)')

        if status == -1:
            # ssh_exec() returns -1 when there is output to syserr and
            # status from the command execution was 0. In this case, we'll
            # assume that the failure was due to some of these componets
            # missing. But since we wanted to delete them anyway, we'll
            # call it good.
            status = 0

        return status

###
# Handle AM config section
#
class FdsAMConfig(FdsConfig):
    def __init__(self, name, items, verbose):
        super(FdsAMConfig, self).__init__(items, verbose)
        self.nd_am_node = None
        self.nd_conf_dict['am-name'] = name

    ###
    # Connect the association between AM section to the matching node that would run
    # AM there.  From the node, we can send ssh commands to start the AM.
    #
    def am_connect_node(self, nodes):
        if 'fds_node' not in self.nd_conf_dict:
            print('sh/am section must have "fds_node" keyword')
            sys.exit(1)

        name = self.nd_conf_dict['fds_node']
        for n in nodes:
            if n.nd_conf_dict['node-name'] == name:
                self.nd_am_node = n
                return

        print('Can not find matching fds_node in %s sections' % name)
        sys.exit(1)

    ###
    # Required am_connect_node()
    #
    def am_start_service(self):
        cmd = ' --fds-root=%s' % self.nd_am_node.nd_conf_dict['fds_root']

        self.nd_am_node.nd_agent.ssh_exec_fds('AMAgent' + cmd)

###
# Handle Volume config section
#
class FdsVolConfig(FdsConfig):
    def __init__(self, name, items, verbose):
        super(FdsVolConfig, self).__init__(items, verbose)
        self.nd_am_conf = None
        self.nd_am_node = None
        self.nd_conf_dict['vol-name'] = name

    def vol_connect_to_am(self, am_nodes, all_nodes):
        if 'client' not in self.nd_conf_dict:
            print('volume section must have "client" keyword')
            sys.exit(1)

        client = self.nd_conf_dict['client']
        for am in am_nodes:
            if am.nd_conf_dict['am-name'] == client:
                self.nd_am_conf = am
                self.nd_am_node = am.nd_am_node
                return

        # The system test framework does not use {sh|am] sections.
        # So look at all nodes to bind the volume to an AM.
        for n in all_nodes:
            if n.nd_conf_dict['node-name'] == client:
                self.nd_am_node = n
                return

        print('Can not find matching AM node in %s' % self.nd_conf_dict['vol-name'])
        sys.exit(1)

    def vol_create(self, cli):
        cmd = (' --volume-create %s' % self.nd_conf_dict['vol-name'])
        if 'id' not in self.nd_conf_dict:
            print('Volume section must have "id" keyword')
            sys.exit(1)

        cmd = cmd + (' -i %s' % self.nd_conf_dict['id'])
        if 'size' not in self.nd_conf_dict:
            print('Volume section must have "size" keyword')
            sys.exit(1)

        cmd = cmd + (' -s %s' % self.nd_conf_dict['size'])
        if 'policy' not in self.nd_conf_dict:
            print('Volume section must have "policy" keyword')
            sys.exit(1)

        cmd = cmd + (' -p %s' % self.nd_conf_dict['policy'])
        if 'access' not in self.nd_conf_dict:
            access = 's3'
        else:
            access = self.nd_conf_dict['access']

        cmd = cmd + (' -y %s' % access)
        cli.run_cli(cmd)

    def vol_attach(self, cli):
        cmd = (' --volume-attach %s -i %s -n %s' %
               (self.nd_conf_dict['vol-name'],
                self.nd_conf_dict['id'],
                self.nd_am_conf.nd_conf_dict['am-name']))
        cli.run_cli(cmd)

    def vol_apply_cfg(self, cli):
        self.vol_create(cli)
        time.sleep(1)
        self.vol_attach(cli)

###
# Handle Volume Policy Config
#
class FdsVolPolicyConfig(FdsConfig):
    def __init__(self, name, items, verbose):
        super(FdsVolPolicyConfig, self).__init__(items, verbose)
        self.nd_conf_dict['pol-name'] = name

    def policy_apply_cfg(self, cli):
        if 'id' not in self.nd_conf_dict:
            print('Policy section must have an id')
            sys.exit(1)

        pol = self.nd_conf_dict['id']
        cmd = (' --policy-create policy_%s -p %s' % (pol, pol))

        if 'iops_min' in self.nd_conf_dict:
            cmd = cmd + (' -g %s' % self.nd_conf_dict['iops_min'])

        if 'iops_max' in self.nd_conf_dict:
            cmd = cmd + (' -m %s' % self.nd_conf_dict['iops_max'])

        if 'priority' in self.nd_conf_dict:
            cmd = cmd + (' -r %s' % self.nd_conf_dict['priority'])

        cli.run_cli(cmd)

###
# Handler user setup
#
class FdsUserConfig(FdsConfig):
    def __init__(self, name, items, verbose):
        super(FdsUserConfig, self).__init__(items, verbose)

###
# Handle cli related
#
class FdsCliConfig(FdsConfig):
    def __init__(self, name, items, verbose):
        super(FdsCliConfig, self).__init__(items, verbose)
        self.nd_om_node = None
        self.nd_conf_dict['cli-name'] = name

    ###
    # Find the OM node that we'll run this CLI from that node.
    #
    def bind_to_om(self, nodes):
        for n in nodes:
            if n.nd_run_om() == True:
                self.nd_om_node = n
                return n
        self.nd_om_node = nodes[0]
        return nodes[0]

    ###
    # Pass arguments to fdscli running on OM node.
    #
    def run_cli(self, command):
        if self.nd_om_node is None:
            print "You must bind to an OM node first"
            sys.exit(1)

        om = self.nd_om_node
        self.debug_print("Run %s on OM %s" % (command, om.nd_host_name()))

        om.nd_agent.ssh_exec_fds(
            ('fdscli --fds-root %s ') % om.nd_agent.get_fds_root() + command,
            wait_compl=True)

###
# Run a specific test step
#
class FdsScenarioConfig(FdsConfig):
    def __init__(self, name, items, verbose):
        super(FdsScenarioConfig, self).__init__(items, verbose)
        self.nd_conf_dict['scenario-name'] = name
        self.cfg_sect_user    = []
        self.cfg_sect_nodes   = []
        self.cfg_sect_ams     = []
        self.cfg_sect_vol_pol = []
        self.cfg_sect_volumes = []
        self.cfg_sect_cli     = []

    def start_scenario(self):
        assert(len(self.cfg_sect_cli) == 1)
        cli    = self.cfg_sect_cli[0]
        script = self.nd_conf_dict['script']
        if 'delay_wait' in self.nd_conf_dict:
            delay = int(self.nd_conf_dict['delay_wait'])
        else:
            delay = 1
        if 'wait_completion' in self.nd_conf_dict:
            wait  = self.nd_conf_dict['wait_completion']
        else:
            wait  = 'false' 
        if re.match('\[node.+\]', script) != None:
            for s in self.cfg_sect_nodes:
                if '[' + s.nd_conf_dict['node-name'] + ']' == script:
                    s.nd_start_om()
                    s.nd_start_platform(self.cfg_om)
                    break
        elif re.match('\[sh.+\]', script) != None:
            for s in self.cfg_sect_ams:
                if '[' + s.nd_conf_dict['am-name'] + ']' == script:
                    s.am_start_service()
                    break
        elif re.match('\[policy.+\]', script) != None:
            for s in self.cfg_sect_vol_pol:
                if '[' + s.nd_conf_dict['pol-name'] + ']' == script:
                    s.policy_apply_cfg(cli)
                    break
        elif re.match('\[volume.+\]', script) != None:
            for s in self.cfg_sect_volumes:
                if '[' + s.nd_conf_dict['vol-name'] + ']' == script:
                    s.vol_apply_cfg(cli)
                    break
        elif re.match('\[fdscli.*\]', script) != None:
            for s in self.cfg_sect_cli:
                if '[' + s.nd_conf_dict['cli-name'] + ']' == script:
                    #s.run_cli('--activate-nodes abc -k 1 -e sm,dm')
                    s.run_cli(self.nd_conf_dict['script_args'])
                    break
        else:
            if 'script_args' in self.nd_conf_dict:
                script_args = self.nd_conf_dict['script_args']
            else:
                script_args = ''
            print "Running external script: ", script, script_args 
            args = script_args.split()
            p1 = subprocess.Popen([script] + args)
            if wait == 'true':
                p1.wait()

        if delay > 1:
            print 'Delaying for %d seconds' % delay
        time.sleep(delay)

    def sce_bind_sections(self, user, nodes, am, vol_pol, volumes, cli, om):
        self.cfg_sect_user    = user
        self.cfg_sect_nodes   = nodes
        self.cfg_sect_ams     = am
        self.cfg_sect_vol_pol = vol_pol
        self.cfg_sect_volumes = volumes
        self.cfg_sect_cli     = cli
        self.cfg_om           = om

class FdsIOBlockConfig(FdsConfig):
    def __init__(self, name, items, verbose):
        super(FdsIOBlockConfig, self).__init__(items, verbose)
        self.nd_conf_dict['io-block-name'] = name

class FdsDatagenConfig(FdsConfig):
    def __init__(self, name, items, verbose):
        super(FdsDatagenConfig, self).__init__(items, verbose)
        self.nd_conf_dict['datagen-name'] = name

    def dg_parse_blocks(self):
        blocks = self.nd_conf_dict['rand_blocks']
        self.nd_conf_dict['rand_blocks'] = re.split(',', blocks)

        blocks = self.nd_conf_dict['dup_blocks']
        self.nd_conf_dict['dup_blocks'] = re.split(',', blocks)

###
# Handle fds bring up config parsing
#
class FdsConfigFile(object):
    def __init__(self, cfg_file, verbose = False, dryrun = False):
        self.cfg_file      = cfg_file
        self.cfg_verbose   = verbose
        self.cfg_dryrun    = dryrun
        self.cfg_am        = []
        self.cfg_user      = []
        self.cfg_nodes     = []
        self.cfg_volumes   = []
        self.cfg_vol_pol   = []
        self.cfg_scenarios = []
        self.cfg_io_blocks = []
        self.cfg_datagen   = []
        self.cfg_cli       = None
        self.cfg_om        = None
        self.cfg_parser    = ConfigParser.ConfigParser()
        self.cfg_localHost = None

    def config_parse(self):
        verbose = {
            'verbose': self.cfg_verbose,
            'dryrun' : self.cfg_dryrun
        }
        self.cfg_parser.read(self.cfg_file)
        if len (self.cfg_parser.sections()) < 1:
            print 'ERROR:  Unable to open or parse the config file "%s".' % (self.cfg_file)
            sys.exit (1)

        nodeID = 0
        for section in self.cfg_parser.sections():
            items = self.cfg_parser.items(section)
            if re.match('user', section) != None:
                self.cfg_user.append(FdsUserConfig('user', items, verbose))

            elif re.match('node', section) != None:
                n = None
                items_d = dict(items)
                if 'enable' in items_d:
                    if items_d['enable'] == 'true':
                        n = FdsNodeConfig(section, items, verbose)
                else:
                    # Store OM ip address to pass to PMs during scenarios
                    if 'om' in items_d:
                        if items_d['om'] == 'true':
                            self.cfg_om = items_d['ip']

                    n = FdsNodeConfig(section, items, verbose)

                if n is not None:
                    n.nd_nodeID = nodeID

                    if "services" in n.nd_conf_dict:
                        n.nd_services = n.nd_conf_dict["services"]
                    else:
                        n.nd_services = "dm,sm,am"

                    self.cfg_nodes.append(n)
                    nodeID = nodeID + 1

            elif (re.match('sh', section) != None) or (re.match('am', section) != None):
                self.cfg_am.append(FdsAMConfig(section, items, verbose))

            elif re.match('policy', section) != None:
                self.cfg_vol_pol.append(FdsVolPolicyConfig(section, items, verbose))

            elif re.match('volume', section) != None:
                self.cfg_volumes.append(FdsVolConfig(section, items, verbose))
            elif re.match('scenario', section)!= None:
                self.cfg_scenarios.append(FdsScenarioConfig(section, items, verbose))
            elif re.match('io_block', section) != None:
                self.cfg_io_blocks.append(FdsIOBlockConfig(section, items, verbose))
            elif re.match('datagen', section) != None:
                self.cfg_datagen.append(FdsDatagenConfig(section, items, verbose))
            else:
                print "Unknown section", section

        if self.cfg_cli is None:
            self.cfg_cli = FdsCliConfig("fdscli", None, verbose)
            self.cfg_cli.bind_to_om(self.cfg_nodes)

        for am in self.cfg_am:
            am.am_connect_node(self.cfg_nodes)

        for vol in self.cfg_volumes:
            vol.vol_connect_to_am(self.cfg_am, self.cfg_nodes)

        for sce in self.cfg_scenarios:
            sce.sce_bind_sections(self.cfg_user, self.cfg_nodes,
                                  self.cfg_am, self.cfg_vol_pol,
                                  self.cfg_volumes, [self.cfg_cli],
                                  self.cfg_om)

        for dg in self.cfg_datagen:
            dg.dg_parse_blocks()

        # Generate a localhost node instance so that we have a
        # node agent to execute commands locally when necessary.
        items = [('enable', True), ('ip','localhost'), ('fds_root', '/fds')]
        self.cfg_localhost = FdsNodeConfig("localhost", items, verbose)

    def config_parse_scenario(self):
        """
        Parse a potentially new FDS Scenario config file looking
        just for scenario sections. This one supports running
        different scenarios against the same resources and topology
        of a given FDS Scenario config but in a forked process.
        """
        verbose = {
            'verbose': self.cfg_verbose,
            'dryrun' : self.cfg_dryrun
        }

        self.cfg_parser.read(self.cfg_file)
        if len (self.cfg_parser.sections()) < 1:
            print 'ERROR:  Unable to open or parse the config file "%s".' % (self.cfg_file)
            sys.exit (1)

        for section in self.cfg_parser.sections():
            items = self.cfg_parser.items(section)
            if re.match('scenario', section)!= None:
                self.cfg_scenarios.append(FdsScenarioConfig(section, items, verbose))

        for sce in self.cfg_scenarios:
            sce.sce_bind_sections(self.cfg_user, self.cfg_nodes,
                                  self.cfg_am, self.cfg_vol_pol,
                                  self.cfg_volumes, [self.cfg_cli],
                                  self.cfg_om)


###
# Parse the config file and setup runtime env for it.
#
class FdsConfigRun(object):
    def __init__(self, env, opt, test_harness=False):
        self.rt_om_node = None
        self.rt_my_node = None
        if opt.config_file is None:
            print "You need to pass config file [-f config]"
            sys.exit(1)

        self.rt_env = env
        if self.rt_env is None:
            self.rt_env = inst.FdsEnv(opt.fds_root, _install=opt.install, _fds_source_dir=opt.fds_source_dir,
                                      _verbose=opt.verbose, _test_harness=test_harness)

        self.rt_obj = FdsConfigFile(opt.config_file, opt.verbose, opt.dryrun)
        self.rt_obj.config_parse()

        # Fixup user/passwd in runtime env from config file.
        users = self.rt_obj.cfg_user
        if users is not None:
            usr = users[0]
            self.rt_env.env_user     = usr.get_config_val('user_name')
            self.rt_env.env_password = usr.get_config_val('password')

            if self.rt_env.env_test_harness:
                # Get sudo password from the commandline if there. If not,
                # check the config file.
                if opt.sudo_password is not None:
                    self.rt_env.env_sudo_password = opt.sudo_password
                elif 'sudo_password' in usr.nd_conf_dict:
                    self.rt_env.env_sudo_password = usr.get_config_val('sudo_password')

        # Setup ssh agent to connect all nodes and find the OM node.
        
        if hasattr (opt, 'target') and opt.target:
            print "Target specified.  Applying commands against target: {}".format (opt.target)
            self.rt_obj.cfg_nodes  = [node for node in self.rt_obj.cfg_nodes 
                if node.nd_conf_dict['node-name'] == opt.target]
            if len (self.rt_obj.cfg_nodes) is 0:
                print "No matching nodes for the target '%s'" %(opt.target)
                sys.exit(1)

        quiet_ssh = False

        if hasattr (opt, 'ssh_quiet') and opt.ssh_quiet:
            quiet_ssh = True

        nodes = self.rt_obj.cfg_nodes

        for n in nodes:
            n.nd_connect_agent(self.rt_env, quiet_ssh)
            n.nd_agent.setup_env('')

            if n.nd_run_om() == True:
                self.rt_om_node = n

        # Set up our localhost "node" as well.
        self.rt_obj.cfg_localhost.nd_connect_agent(self.rt_env, quiet_ssh)
        self.rt_obj.cfg_localhost.nd_agent.setup_env('')

    def rt_get_obj(self, obj_name):
        return getattr(self.rt_obj, obj_name)

    def rt_fds_bootstrap(self, run_om = True):
        if self.rt_om_node == None:
            print "You must have OM node in your config"
            sys.exit(1)

        om_node = self.rt_om_node
        if 'ip' not in om_node.nd_conf_dict:
            print "You need to have IP in the OM node of your config"
            sys.exit(1)

        om_ip = om_node.nd_conf_dict['ip']
        for n in self.rt_obj.cfg_nodes:
            n.nd_start_platform(om_ip)

        if run_om == True:
            om_node.nd_start_om()
