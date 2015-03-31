#!/usr/bin/python
#
# Copyright 2014 by Formation Data Systems, Inc.
#

# FDS test-case pattern requirements.
import unittest

import xmlrunner
import TestCase


# Module-specific requirements
import sys
import time
import logging
import shlex
import random


def getSvcPIDforNode(svc, node, javaClass=None):
    """
    Return the process ID for the given
    service/node. Returns -1 if not found.

    Note: When searching for a java process, set svc to
    "java" and javaClass to the class name used to start the application.
    """
    log = logging.getLogger('TestFDSModMgt' + '.' + 'getSvcPIDforNode')

    cmd = "pgrep %s" % svc

    status, stdout = node.nd_agent.exec_wait(cmd, return_stdin=True)

    if len(stdout) > 0:
        # We've got a list of process IDs for the service of interest.
        # We need to make sure that among them is the one for the node of interest.
        for pid in shlex.split(stdout):
            cmd2 = "ps -www --no-headers %s" % pid
            status, stdout2 = node.nd_agent.exec_wait(cmd2, return_stdin=True)

            if len(stdout2) > 0:
                # We've got the details of the process. Let's see if it is supporting
                # the node of interest.
                #cmd3 = "grep %s" % node.nd_conf_dict['node-name']
                #status, stdout3 = node.nd_agent.exec_wait(cmd3, return_stdin=True, cmd_input=stdout2)

                if node.nd_agent.env_install:
                    # For java processes, we have also to look for the initial class of execution.
                    if (svc == "java") and (javaClass is not None):
                        if stdout2.count(javaClass) > 0:
                            return pid
                    elif (svc == "java") and (javaClass is None):
                        log.error("When searching for a java process also include the java class name.")
                        raise Exception
                    else:
                        if len(stdout.splitlines()) > 1:
                            # When the node is installed/deployed from a package,
                            # we expect only one process-set per service.
                            log.warning("More %s processes than expected: %s." % (svc, stdout))

                        return pid
                else:
                    # For java processes, we have also to look for the initial class of execution.
                    #if (len(stdout3) > 0) and (svc == "java") and (javaClass is not None):
                    if (stdout2.count(node.nd_conf_dict['node-name']) > 0) and (svc == "java") and (javaClass is not None):
                        #cmd3_1 = "grep %s" % javaClass
                        #status, stdout3 = node.nd_agent.exec_wait(cmd3_1, return_stdin=True, cmd_input=stdout3)
                        stdout2 = stdout2 if stdout2.count(javaClass) > 0 else ''
                    elif (svc == "java") and (javaClass is None):
                        log.error("When searching for a java process also include the java class name.")
                        raise Exception

                    #if len(stdout3) > 0:
                    if stdout2.count(node.nd_conf_dict['node-name']) > 0:
                        # Found it!
                        return pid

    # Didn't find it.
    return -1


def pidWaitParent(pid, child_count, node):
    """
    Wait for indicated process to present a specified number of children.
    """
    log = logging.getLogger('TestFDSModMgt' + '.' + 'pidWaitParent')

    maxcount = 20

    count = 0
    found = False
    status = 0
    cmd = "pgrep -c -P %s" % pid
    while (count < maxcount) and not found:
        time.sleep(1)

        status, stdout = node.nd_agent.exec_wait(cmd, return_stdin=True)

        if int(stdout) == child_count:
            found = True

        count += 1

    if status != 0:
        log.error("Wait for %d children of %s on %s returned status %d." %
                  (child_count, pid, node.nd_conf_dict['node-name'], status))
        return False

    if not found:
        return False

    return True

def modWait(mod, node, forShutdown = False):
    """
    Wait for the named service to become active.
    """
    log = logging.getLogger('TestFDSModMgt' + '.' + 'modWait')

    maxcount = 20

    orchMgrPID = ''
    AMAgentPID = ''
    count = 0

    # When we don't want to see the process, assume
    # that it's there, otherwise, assume that it's
    # not there.
    if forShutdown:
        found = True
    else:
        found = False

    status = 0
    while (count < maxcount) and ((forShutdown and found) or (not forShutdown and not found)):
        time.sleep(1)

        pid = getSvcPIDforNode(mod, node)
        if pid != -1:
            found = True

            if not forShutdown:
                # For process orchMgr, there should be one child process.
                if mod == "orchMgr":
                    orchMgrPID = pid

                # For process AMAgent, there should be two child processes.
                if mod == "AMAgent":
                    AMAgentPID = pid
        else:
            found = False

        count += 1

    if status != 0:
        if not forShutdown:
            log.error("Wait for %s on %s returned status %d." % (mod, node.nd_conf_dict['node-name'], status))
            return False

    if not found:
        if forShutdown:
            return True
        else:
            return False
    else:
        if forShutdown:
            return False

    if orchMgrPID != '':
        return pidWaitParent(int(orchMgrPID), 1, node)

    if AMAgentPID != '':
        return pidWaitParent(int(AMAgentPID), 2, node)

    return True


def findNodeFromInv(node_inventory, target):
    '''
    Looks for target in node_inventory and returns the target object.
    :param node_inventory: A list of nodes to search through (typically nd_confg_dict)
    :param target: Target node to find (a parameter passed into the test case)
    :return: Returns the node object identified by target
    '''

    # If we didn't get a string, just return the original object
    if not isinstance(target, str):
        return target

    # Otherwise look for a node with the target name
    for node in node_inventory:
        if node.nd_conf_dict['node-name'] == target:
            return node


def generic_kill(node, service):
    '''
    Generic method to kill a specified service running on a specified node.
    :param node: The node (object) to kill the service on
    :param service: The service to kill
    :return: An integer status code returned by the kill command
    '''

    svc_map = {
        'sm': 'StorMgr',
        'dm': 'DataMgr',
        'om': 'java', 
        'pm': 'platformd',
        'am': 'java'
    }

    java_classes = {
        'om': 'com.formationds.om.Main',
        'am': 'com.formationds.am.Main'
    }

    java_class = None
    if service == 'om' or service == 'am':
        java_class = java_classes[service]

    log = logging.getLogger('TestFDSServiceMgt' + '.' + 'generic_kill')

    if service not in svc_map.keys():
        log.error("Could not find service {} in service map. "
                  "Not sure what to kill, so aborting by returning -1!".format(service))
        return -1

    pid = getSvcPIDforNode(svc_map[service], node, java_class)

    if pid != -1:
        cmd = "kill -9 {}".format(pid)
        status = node.nd_agent.exec_wait(cmd)

    else:
        status = 0
        log.warning("{} not running on {}.".format(service, node.nd_conf_dict['node-name']))

    return status

# This class contains the attributes and methods to test
# randomly killing a service on a random node after a
# random amount of time
# RANDOM!
class TestRndSvcKill(TestCase.FDSTestCase):
    def __init__(self, parameters=None, nodes=None,
                 time_window=None, services=None):
        '''
        :param parameters: Gets populated with .ini config params when run by qaautotest
        :param nodes: Nodes to randomly select from, separated by white space. If None all nodes considered.
        :param time_window: Dash separated integer pair (e.g. 0-120) representing min/max in seconds to
                            randomly select from. If None time will be now.
        :param services: Services to randomly select from, separated by white space. If None all services considered.
        '''

        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_RndSvcKill,
                                             "Random service killer")

        node_inv = self.parameters['fdscfg'].rt_obj.cfg_nodes
        # Listify!
        if nodes is not None:
            nodes = nodes.split(' ')
            # Also ensure that the nodes are proper objects
            # Hooray for partial application :)
            finder = lambda x: findNodeFromInv(node_inv, x)
            # Use finder to find each of the node objects
            self.passedNodes = map(finder, nodes)
        else:
            # If we weren't passed any nodes to pick from, pick from any of them
            self.passedNodes = node_inv

        if services is not None:
            self.passedSvcs = services.split(' ')
        else:
            self.passedSvcs = ['am', 'pm', 'dm', 'dm', 'om']

        if time_window is not None:
            self.passedWindow = tuple(map(lambda x: int(x), time_window.split('-')))
        else:
            self.passedWindow = (0, 0)

    def __pick_rnds(self):
        '''
        Picks random node/service pair and returns them.
        :return: (node, service) pair
        '''

        return (random.choice(self.passedNodes),
                random.choice(self.passedSvcs))

    def test_RndSvcKill(self):
        '''
        Will randomly kill a service within the bounds of the parameters passed to the constructor.
        This test assumes that if a node is up it's services are also up. If for some reason the node
        or services are already down it will return False.
        :return: True on successful kill, False otherwise
        '''

        # Pick our random node
        selected_node = random.choice(self.passedNodes)
        self.log.info("Selected {} as random node".format(selected_node.nd_conf_dict['node-name']))

        # Now select a random service
        selected_svc = random.choice(self.passedSvcs)
        self.log.info("Selected {} as random svc to kill".format(selected_svc))

        selected_time = random.choice(range(self.passedWindow[0], self.passedWindow[1]))
        self.log.info("Selected {} as random time after which to kill {} on node {}.".format(selected_time,
                                                                                             selected_svc,
                                                                                             selected_node.nd_conf_dict['node-name']))

        # Verify the service exists for the node, if not all bets are off, pick another random node/svc
        while(selected_node.nd_services.count(selected_svc) == 0 and selected_svc != 'pm'):
            self.log.warn("Service {} not configured for node {}."
                          "Selecting new random node/service".format(selected_svc,
                                                                     selected_node.nd_conf_dict['node-name']))
            selected_node = random.choice(self.passedNodes)
            selected_svc = random.choice(self.passedSvcs)

        # Add the node, service combo to params so we can track it
        if 'svc_killed' in self.parameters:
            self.parameters['svc_killed'].append((selected_node, selected_svc))
        else:
            self.parameters['svc_killed'] = [(selected_node, selected_svc)]

        print "SELF.PARAMS.SVC_KILLED = {}".format(self.parameters.get('svc_killed', []))

        # Try the kill
        # sleep first for the random wait aspect
        time.sleep(selected_time)
        status = generic_kill(selected_node, selected_svc)

        if status != 0:
            self.log.error("{} kill on {} returned status {}.".format(selected_svc,
                                                                      selected_node.nd_conf_dict['node-name'],
                                                                      status))
            return False

        return True


# This class contains the attributes and methods to test
# bringing up a Data Manager (DM) service.
class TestDMBringUp(TestCase.FDSTestCase):
    def __init__(self, parameters=None, node=None):
        """
        When run by a qaautotest module test runner,
        "parameters" will have been populated with
        .ini configuration.
        """
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_DMBringUp,
                                             "DM service bringup")

        self.passedNode = node

    def test_DMBringUp(self):
        """
        Test Case:
        Attempt to start the DM service(s)
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]

        nodes = fdscfg.rt_obj.cfg_nodes
        for n in nodes:
            # If we were passed a node, use it and get out. Otherwise,
            # we boot all DMs we have.
            if self.passedNode is not None:
                n = findNodeFromInv(nodes, self.passedNode)

            port = n.nd_conf_dict['fds_port']
            fds_dir = n.nd_conf_dict['fds_root']
            log_dir = n.nd_agent.get_log_dir()

            # Make sure DM should be running on this node.
            if n.nd_services.count("dm") == 0:
                self.log.warning("DM not configured for node %s." % n.nd_conf_dict['node-name'])
                if self.passedNode is None:
                    break
                else:
                    continue

            self.log.info("Start DM on %s." %n.nd_conf_dict['node-name'])

            status = n.nd_agent.exec_wait('bash -c \"(nohup ./DataMgr --fds-root=%s > %s/dm.%s.out 2>&1 &) \"' %
                                          (fds_dir, log_dir, port),
                                          fds_bin=True)

            if status != 0:
                self.log.error("DM bringup on %s returned status %d." %(n.nd_conf_dict['node-name'], status))
                return False
            elif self.passedNode is not None:
                # Passed a specific node so get out.
                break

            time.sleep(2)

        return True


# This class contains the attributes and methods to test
# waiting for a Data Manager (DM) service to start.
class TestDMWait(TestCase.FDSTestCase):
    def __init__(self, parameters=None, node=None):
        """
        When run by a qaautotest module test runner,
        "parameters" will have been populated with
        .ini configuration.
        """
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_DMWait,
                                             "Wait for DM service to start")

        self.passedNode = node

    def test_DMWait(self):
        """
        Test Case:
        Wait for the DM service(s) to start
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]

        nodes = fdscfg.rt_obj.cfg_nodes
        for n in nodes:
            # If we were passed a node, check that one and exit.
            if self.passedNode is not None:
                n = findNodeFromInv(nodes, self.passedNode)

            # Make sure it wasn't the target of a random kill
            if (n, "dm") in self.parameters.get('svc_killed', []):
                self.log.warning("DM service for node {} previous"
                                 "killed by random svc kill test.".format(n.nd_conf_dict['node-name']))
                if self.passedNode is not None:
                    break
                else:
                    continue

            # Make sure this node is configured for DM.
            if "dm" not in n.nd_services:
                self.log.warning("DM service not configured for node %s." % n.nd_conf_dict['node-name'])
                if self.passedNode is not None:
                    break
                else:
                    continue

            self.log.info("Wait for DM on %s." % n.nd_conf_dict['node-name'])

            if not modWait("DataMgr", n):
                return False

            if self.passedNode is not None:
                break

        return True


# This class contains the attributes and methods to test
# activating Data Manager (DM) services on a domain.
class TestDMActivate(TestCase.FDSTestCase):
    def __init__(self, parameters=None, node=None):
        """
        When run by a qaautotest module test runner,
        "parameters" will have been populated with
        .ini configuration.
        """
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_DMActivate,
                                             "DM service activation")

        self.passedNode = node

    def test_DMActivate(self):
        """
        Test Case:
        Attempt to activate the DM service(s)
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]

        om_node = fdscfg.rt_om_node
        fds_dir = om_node.nd_conf_dict['fds_root']
        log_dir = om_node.nd_agent.get_log_dir()

        # Do all DMs in the domain or just the DM for the passed node?
        if self.passedNode is None:
            self.log.info("Activate domain DMs from OM node %s." % om_node.nd_conf_dict['node-name'])

            status = om_node.nd_agent.exec_wait('bash -c \"(./fdsconsole.py domain activateServices local dm > '
                                                '{}/fdsconsole.out 2>&1) \"'.format(log_dir),
                                                fds_tools=True)
        else:
            node = self.passedNode

            # Make sure this node is configured for an DM service.
            if node.nd_services.count("dm") == 0:
                self.log.error("Node %s is not configured to host a DM service." % node.nd_conf_dict['node-name'])
                return False

            # Make sure we have the node's meta-data.
            status = node.nd_populate_metadata(om_node=om_node)
            if status != 0:
                self.log.error("Getting meta-data for node %s returned status %d." %
                               (node.nd_conf_dict['node-name'], status))
                return False

            self.log.info("Activate DM for node %s from OM node %s." % (node.nd_conf_dict['node-name'],
                                                                        om_node.nd_conf_dict['node-name']))

            status = om_node.nd_agent.exec_wait('bash -c \"(./fdsconsole.py service '
                                                'addService {} dm > {}/cli.out 2>&1) \"'.format(
                                                int(node.nd_uuid, 16), log_dir,), fds_tools=True)

        if status != 0:
            self.log.error("DM activation on %s returned status %d." % (self.passedNode.nd_conf_dict['node-name'], status))
            return False

        return True


# This class contains the attributes and methods to test
# removing Data Manager (DM) services on a domain.
class TestDMRemove(TestCase.FDSTestCase):
    def __init__(self, parameters=None, node=None):
        """
        When run by a qaautotest module test runner,
        "parameters" will have been populated with
        .ini configuration.
        """
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_DMRemove,
                                             "DM service removal")

        self.passedNode = node

    def test_DMRemove(self):
        """
        Test Case:
        Attempt to remove the DM service(s)
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]

        om_node = fdscfg.rt_om_node
        fds_dir = om_node.nd_conf_dict['fds_root']
        log_dir = om_node.nd_agent.get_log_dir()

        nodes = fdscfg.rt_obj.cfg_nodes
        for node in nodes:
            # If we were passed a node, check that one and exit.
            if self.passedNode is not None:
                node = self.passedNode

            # Make sure this node is configured for an DM service.
            if (node.nd_services.count("dm") == 0) and self.passedNode is not None:
                self.log.error("Node %s is not configured to host a DM service." % node.nd_conf_dict['node-name'])
                return False

            # Make sure we have the node's meta-data.
            status = node.nd_populate_metadata(om_node=om_node)
            if status != 0:
                self.log.error("Getting meta-data for node %s returned status %d." %
                               (node.nd_conf_dict['node-name'], status))
                return False

            self.log.info("Remove DM for node %s using OM node %s." % (node.nd_conf_dict['node-name'],
                                                                        om_node.nd_conf_dict['node-name']))

            self.log.warn("Remove services call currently not implemented. THIS CALL WILL FAIL!")
            status = om_node.nd_agent.exec_wait('bash -c \"(./fdsconsole.py service removeService {} dm > '
                                                '{}/cli.out 2>&1) \"'.format(node.nd_uuid, log_dir),
                                                fds_tools=True)

            if status != 0:
                self.log.error("DM removal from %s returned status %d." % (node.nd_conf_dict['node-name'], status))
                return False

            if self.passedNode is not None:
                break

        return True


# This class contains the attributes and methods to test
# killing a Data Manager (DM) service.
class TestDMKill(TestCase.FDSTestCase):
    def __init__(self, parameters=None, node=None):
        """
        When run by a qaautotest module test runner,
        "parameters" will have been populated with
        .ini configuration.
        """
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_DMKill,
                                             "DM service kill")

        self.passedNode = node

    def test_DMKill(self):
        """
        Test Case:
        Attempt to shutdown the DM service(s)
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]

        nodes = fdscfg.rt_obj.cfg_nodes
        for n in nodes:
            # If we were passed a specific node, shutdown that one and get out.
            if self.passedNode is not None:
                n = findNodeFromInv(nodes, self.passedNode)

            # Make sure DM should be running on this node.
            if n.nd_services.count("dm") == 0:
                self.log.warning("DM not configured to run on node %s." % n.nd_conf_dict["node-name"])
                if self.passedNode is None:
                    continue
                else:
                    break

            self.log.info("Kill DM on %s." % n.nd_conf_dict['node-name'])
            # Get the PID of the processes in question and ... kill them!
            pid = getSvcPIDforNode('DataMgr', n)
            if pid != -1:
                cmd = "kill -9 %s" % pid
                status = n.nd_agent.exec_wait(cmd)

                if status != 0:
                    self.log.error("DM kill on %s returned status %d." %
                                   (n.nd_conf_dict['node-name'], status))
                    return False
            else:
                status = 0
                self.log.warning("DM not running on %s." % (n.nd_conf_dict['node-name']))

            if status != 0:
                self.log.error("DM kill on %s returned status %d." % (n.nd_conf_dict['node-name'], status))
                return False
            elif self.passedNode is not None:
                # Took care of the node passed in so get out.
                break

        return True


# This class contains the attributes and methods to test
# whether a Data Manager (DM) service is down.
class TestDMVerifyDown(TestCase.FDSTestCase):
    def __init__(self, parameters=None, node=None):
        """
        When run by a qaautotest module test runner,
        "parameters" will have been populated with
        .ini configuration.
        """
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_DMVerifyDown,
                                             "Verify a DM service is down")

        self.passedNode = node

    def test_DMVerifyDown(self):
        """
        Test Case:
        Wait for the DM service(s) to start
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]

        nodes = fdscfg.rt_obj.cfg_nodes
        for n in nodes:
            # If we were passed a specific node, shutdown that one and get out.
            if self.passedNode is not None:
                n = findNodeFromInv(nodes, self.passedNode)

            self.log.info("Verify the DM on %s is down." %n.nd_conf_dict['node-name'])

            if not modWait("DataMgr", n, forShutdown=True):
                return False
            elif self.passedNode is not None:
                # Took care of the node passed in so get out.
                break

        return True


# This class contains the attributes and methods to test
# bringing up a Storage Manager (SM) service.
class TestSMBringUp(TestCase.FDSTestCase):
    def __init__(self, parameters=None, node=None):
        """
        When run by a qaautotest module test runner,
        "parameters" will have been populated with
        .ini configuration.
        """
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_SMBringUp,
                                             "SM service bringup")

        self.passedNode = node

    def test_SMBringUp(self):
        """
        Test Case:
        Attempt to start the SM service(s)
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]

        nodes = fdscfg.rt_obj.cfg_nodes
        for n in nodes:
            # If we were passed a node, use it and get out. Otherwise,
            # we boot all SMs we have.
            if self.passedNode is not None:
                n = findNodeFromInv(nodes, self.passedNode)

            port = n.nd_conf_dict['fds_port']
            fds_dir = n.nd_conf_dict['fds_root']
            bin_dir = n.nd_agent.get_bin_dir(debug=False)
            log_dir = n.nd_agent.get_log_dir()

            # Make sure SM should be running on this node.
            if n.nd_services.count("sm") == 0:
                self.log.warning("SM not configured for node %s." % n.nd_conf_dict['node-name'])
                if self.passedNode is None:
                    break
                else:
                    continue

            self.log.info("Start SM on %s." %n.nd_conf_dict['node-name'])

            status = n.nd_agent.exec_wait('bash -c \"(nohup ./StorMgr --fds-root=%s > %s/sm.%s.out 2>&1 &) \"' %
                                          (fds_dir, log_dir, port),
                                          fds_bin=True)

            if status != 0:
                self.log.error("SM on %s returned status %d." %(n.nd_conf_dict['node-name'], status))
                return False
            elif self.passedNode is not None:
                # Passed a specific node so get out.
                break

        return True


# This class contains the attributes and methods to test
# waiting for a Storage Manager (SM) service to start.
class TestSMWait(TestCase.FDSTestCase):
    def __init__(self, parameters=None, node=None):
        """
        When run by a qaautotest module test runner,
        "parameters" will have been populated with
        .ini configuration.
        """
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_SMWait,
                                             "Wait for SM service to start")

        self.passedNode = node

    def test_SMWait(self):
        """
        Test Case:
        Wait for the SM service(s) to start
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]

        nodes = fdscfg.rt_obj.cfg_nodes
        for n in nodes:
            # If we were passed a node, check that one and exit.
            if self.passedNode is not None:
                n = findNodeFromInv(nodes, self.passedNode)

            # Make sure it wasn't the target of a random kill
            if (n, "sm") in self.parameters.get('svc_killed', []):
                self.log.warning("SM service for node {} previous"
                                 "killed by random svc kill test.".format(n.nd_conf_dict['node-name']))
                if self.passedNode is not None:
                    break
                else:
                    continue


            # Make sure this node is configured for SM.
            if "sm" not in n.nd_services:
                self.log.warning("SM service not configured for node %s." % n.nd_conf_dict['node-name'])
                if self.passedNode is not None:
                    break
                else:
                    continue


            self.log.info("Wait for SM on %s." %n.nd_conf_dict['node-name'])

            if not modWait("StorMgr", n):
                return False

            if self.passedNode is not None:
                break

        return True


# This class contains the attributes and methods to test
# activating Storage Manager (SM) services on a domain.
class TestSMActivate(TestCase.FDSTestCase):
    def __init__(self, parameters=None, node=None):
        """
        When run by a qaautotest module test runner,
        "parameters" will have been populated with
        .ini configuration.
        """
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_SMActivate,
                                             "SM service activation")

        self.passedNode = node

    def test_SMActivate(self):
        """
        Test Case:
        Attempt to activate the SM service(s)
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]

        om_node = fdscfg.rt_om_node
        fds_dir = om_node.nd_conf_dict['fds_root']
        log_dir = om_node.nd_agent.get_log_dir()

        # Do all SMs in the domain or just the SM for the passed node?
        if self.passedNode is None:
            self.log.info("Activate domain SMs from OM node %s." % om_node.nd_conf_dict['node-name'])

            status = om_node.nd_agent.exec_wait('bash -c \"(./fdsconsole.py domain activateServices local sm > '
                                                '{}/fdsconsole.out 2>&1) \"'.format(log_dir),
                                                fds_tools=True)
        else:
            node = self.passedNode

            # Make sure this node is configured for an SM service.
            if node.nd_services.count("sm") == 0:
                self.log.error("Node %s is not configured to host an SM service." % node.nd_conf_dict['node-name'])
                return False

            # Make sure we have the node's meta-data.
            status = node.nd_populate_metadata(om_node=om_node)
            if status != 0:
                self.log.error("Getting meta-data for node %s returned status %d." %
                               (node.nd_conf_dict['node-name'], status))
                return False

            self.log.info("Activate SM for node %s from OM node %s." % (node.nd_conf_dict['node-name'],
                                                                        om_node.nd_conf_dict['node-name']))

            status = om_node.nd_agent.exec_wait('bash -c \"(./fdsconsole.py service '
                                                'addService {} sm > {}/cli.out 2>&1) \"'.format(
                                                int(node.nd_uuid, 16), log_dir,), fds_tools=True)

        if status != 0:
            self.log.error("SM activation on %s returned status %d." % (self.passedNode.nd_conf_dict['node-name'], status))
            return False

        return True


# This class contains the attributes and methods to test
# removing Storage Manager (SM) services on a domain.
class TestSMRemove(TestCase.FDSTestCase):
    def __init__(self, parameters=None, node=None):
        """
        When run by a qaautotest module test runner,
        "parameters" will have been populated with
        .ini configuration.
        """
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_SMRemove,
                                             "SM service removal")

        self.passedNode = node

    def test_SMRemove(self):
        """
        Test Case:
        Attempt to remove the SM service(s)
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]

        om_node = fdscfg.rt_om_node
        fds_dir = om_node.nd_conf_dict['fds_root']
        log_dir = om_node.nd_agent.get_log_dir()

        nodes = fdscfg.rt_obj.cfg_nodes
        for node in nodes:
            # If we were passed a node, check that one and exit.
            if self.passedNode is not None:
                node = self.passedNode

            # Make sure this node is configured for an SM service.
            if (node.nd_services.count("sm") == 0) and self.passedNode is not None:
                self.log.error("Node %s is not configured to host a SM service." % node.nd_conf_dict['node-name'])
                return False

            # Make sure we have the node's meta-data.
            status = node.nd_populate_metadata(om_node=om_node)
            if status != 0:
                self.log.error("Getting meta-data for node %s returned status %d." %
                               (node.nd_conf_dict['node-name'], status))
                return False

            self.log.info("Remove SM for node %s using OM node %s." % (node.nd_conf_dict['node-name'],
                                                                        om_node.nd_conf_dict['node-name']))

            status = om_node.nd_agent.exec_wait('bash -c \"(./fdsconsole.py service removeService {} sm > '
                                                '{}/cli.out 2>&1) \"'.format(node.nd_uuid, log_dir),
                                                fds_tools=True)

            if status != 0:
                self.log.error("SM removal from %s returned status %d." % (self.passedNode.nd_conf_dict['node-name'], status))
                return False

            if self.passedNode is not None:
                break

        return True


# This class contains the attributes and methods to test
# killing a Storage Manager (SM) service.
class TestSMKill(TestCase.FDSTestCase):
    def __init__(self, parameters=None, node=None):
        """
        When run by a qaautotest module test runner,
        "parameters" will have been populated with
        .ini configuration.
        """
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_SMKill,
                                             "SM service kill")

        self.passedNode = node

    def test_SMKill(self):
        """
        Test Case:
        Attempt to shutdown the SM service(s)
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]

        nodes = fdscfg.rt_obj.cfg_nodes
        for n in nodes:
            # If we were passed a specific node, shutdown that one and get out.
            if self.passedNode is not None:
                n = findNodeFromInv(nodes, self.passedNode)

            # Make sure SM should be running on this node.
            if n.nd_services.count("sm") == 0:
                self.log.warning("SM not configured for node %s." % n.nd_conf_dict['node-name'])
                if self.passedNode is None:
                    break
                else:
                    continue

            self.log.info("Kill SM on %s." %n.nd_conf_dict['node-name'])
            # Get the PID of the process in question and ... kill it!
            pid = getSvcPIDforNode('StorMgr', n)
            if pid != -1:
                cmd = "kill -9 %s" % pid
                status = n.nd_agent.exec_wait(cmd)

                if status != 0:
                    self.log.error("SM kill on %s returned status %d." %
                                   (n.nd_conf_dict['node-name'], status))
                    return False
            else:
                status = 0
                self.log.warning("SM not running on %s." % (n.nd_conf_dict['node-name']))

            if status != 0:
                self.log.error("SM kill on %s returned status %d." % (n.nd_conf_dict['node-name'], status))
                return False

            if self.passedNode is not None:
                # Took care of the one node passed so get out.
                break

        return True


# This class contains the attributes and methods to test
# whether a Storage Manager (SM) service is down.
class TestSMVerifyDown(TestCase.FDSTestCase):
    def __init__(self, parameters=None, node=None):
        """
        When run by a qaautotest module test runner,
        "parameters" will have been populated with
        .ini configuration.
        """
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_SMVerifyDown,
                                             "Verify the SM service is down")

        self.passedNode = node

    def test_SMVerifyDown(self):
        """
        Test Case:
        Wait for the SM service(s) to start
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]

        nodes = fdscfg.rt_obj.cfg_nodes
        for n in nodes:
            # If we were passed a specific node, shutdown that one and get out.
            if self.passedNode is not None:
                n = findNodeFromInv(nodes, self.passedNode)

            self.log.info("Verify the SM on %s is down." %n.nd_conf_dict['node-name'])

            if not modWait("StorMgr", n, forShutdown=True):
                return False
            elif self.passedNode is not None:
                # Took care of the node passed in so get out.
                break

        return True


# This class contains the attributes and methods to test
# bringing up a Platform Manager (PM) component.
class TestPMBringUp(TestCase.FDSTestCase):
    def __init__(self, parameters=None, node=None):
        """
        When run by a qaautotest module test runner,
        "parameters" will have been populated with
        .ini configuration.
        """
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_PMBringUp,
                                             "PM service bringup")

        self.passedNode = node

    def test_PMBringUp(self):
        """
        Test Case:
        Attempt to start the PM service(s)
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]

        om_node = fdscfg.rt_om_node
        om_ip = om_node.nd_conf_dict['ip']

        nodes = fdscfg.rt_obj.cfg_nodes
        for n in nodes:
            # If we were passed a node, use it and get out. Otherwise,
            # we spin through in defined node setting it up.
            if self.passedNode is not None:
                n = findNodeFromInv(nodes, self.passedNode)
            else:
                # Skip the PM for the OM's node. That one is handled by TestPMForOMBringUp()
                if n.nd_conf_dict['node-name'] == om_node.nd_conf_dict['node-name']:
                    self.log.info("Skipping OM's PM on %s." % n.nd_conf_dict['node-name'])
                    continue

            bin_dir = n.nd_agent.get_bin_dir(debug=False)
            log_dir = n.nd_agent.get_log_dir()

            self.log.info("Start PM on %s." % n.nd_conf_dict['node-name'])

            status = n.nd_start_platform(om_ip, test_harness=True, _bin_dir=bin_dir, _log_dir=log_dir)

            if status != 0:
                self.log.error("PM on %s returned status %d." % (n.nd_conf_dict['node-name'], status))
                return False
            elif self.passedNode is not None:
                # If we were passed a specific node, just install
                # that one and exit.
                return True

        return True


# This class contains the attributes and methods to test
# waiting for a Platform Manager (PM) service to start.
class TestPMWait(TestCase.FDSTestCase):
    def __init__(self, parameters=None, node=None):
        """
        When run by a qaautotest module test runner,
        "parameters" will have been populated with
        .ini configuration.
        """
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_PMWait,
                                             "Wait for PM service to start")

        self.passedNode = node

    def test_PMWait(self):
        """
        Test Case:
        Wait for the PM service(s) to start
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]
        om_node = fdscfg.rt_om_node

        nodes = fdscfg.rt_obj.cfg_nodes
        for n in nodes:
            # If we were passed a node, use it and get out. Otherwise,
            # we spin through all defined nodes setting them up.
            if self.passedNode is not None:
                n = findNodeFromInv(nodes, self.passedNode)

            # Make sure it wasn't the target of a random kill
            if (n, "pm") in self.parameters.get('svc_killed', []):
                self.log.warning("PM service for node {} previously"
                                 "killed by random svc kill test.".format(n.nd_conf_dict['node-name']))
                if self.passedNode is not None:
                    break
                else:
                    continue

                # Skip the PM for the OM's node. That one is handled by TestPMForOMWait()
                if n.nd_conf_dict['node-name'] == om_node.nd_conf_dict['node-name']:
                    self.log.info("Skipping OM's PM on %s." %n.nd_conf_dict['node-name'])
                    continue

            self.log.info("Wait for PM on %s." %n.nd_conf_dict['node-name'])

            if not modWait("platformd", n):
                return False
            elif self.passedNode is not None:
                # If we were passed a specific node, just take care
                # of that one and exit.
                return True

        return True


# This class contains the attributes and methods to test
# killing a Platform Manager (PM) service.
class TestPMKill(TestCase.FDSTestCase):
    def __init__(self, parameters=None, node=None):
        """
        When run by a qaautotest module test runner,
        "parameters" will have been populated with
        .ini configuration.
        """
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_PMKill,
                                             "PM service kill")

        self.passedNode = node

    def test_PMKill(self):
        """
        Test Case:
        Attempt to shutdown the PM service(s)
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]
        om_node = fdscfg.rt_om_node

        nodes = fdscfg.rt_obj.cfg_nodes
        for n in nodes:
            # If we were passed a specific node, shutdown that one and get out.
            if self.passedNode is not None:
                n = findNodeFromInv(nodes, self.passedNode)
            else:
                # Skip the PM for the OM's node. That one is handled by TestPMForOMKill()
                if n.nd_conf_dict['node-name'] == om_node.nd_conf_dict['node-name']:
                    self.log.info("Skipping OM's PM on %s." %n.nd_conf_dict['node-name'])
                    continue

            self.log.info("Kill PM on %s." %n.nd_conf_dict['node-name'])
            # Get the PID of the process in question and ... kill it!
            pid = getSvcPIDforNode('platformd', n)
            if pid != -1:
                cmd = "kill -9 %s" % pid
                status = n.nd_agent.exec_wait(cmd)

                if status != 0:
                    self.log.error("PM kill on %s returned status %d." %
                                   (n.nd_conf_dict['node-name'], status))
                    return False
            else:
                status = 0
                self.log.warning("PM is not running on %s." % (n.nd_conf_dict['node-name']))

            if status != 0:
                self.log.error("PM kill on %s returned status %d." % (n.nd_conf_dict['node-name'], status))
                return False
            elif self.passedNode is not None:
                # Took care of the node passed in. Get out.
                break

        return True


# This class contains the attributes and methods to test
# whether a Platform Manager (PM) service is down .
class TestPMVerifyDown(TestCase.FDSTestCase):
    def __init__(self, parameters=None, node=None):
        """
        When run by a qaautotest module test runner,
        "parameters" will have been populated with
        .ini configuration.
        """
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_PMVerifyDown,
                                             "Verify a PM service is down")

        self.passedNode = node

    def test_PMVerifyDown(self):
        """
        Test Case:
        Verify the PM service(s) are down
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]
        om_node = fdscfg.rt_om_node

        nodes = fdscfg.rt_obj.cfg_nodes
        for n in nodes:
            # If we were passed a specific node, shutdown that one and get out.
            if self.passedNode is not None:
                n = findNodeFromInv(nodes, self.passedNode)
            else:
                # Skip the PM for the OM's node. That one is handled by TestPMForOMKill()
                if n.nd_conf_dict['node-name'] == om_node.nd_conf_dict['node-name']:
                    self.log.info("Skipping OM's PM on %s." % n.nd_conf_dict['node-name'])
                    continue

            self.log.info("Verify the PM on %s is down." % n.nd_conf_dict['node-name'])

            if not modWait("platformd", n, forShutdown=True):
                return False
            elif self.passedNode is not None:
                # Took care of the node passed in. Get out.
                break

        return True


# This class contains the attributes and methods to test
# bringing up a Platform Manager (PM) for the Orchestration Manager (OM) node.
class TestPMForOMBringUp(TestCase.FDSTestCase):
    def __init__(self, parameters=None):
        """
        When run by a qaautotest module test runner,
        "parameters" will have been populated with
        .ini configuration.
        """
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_PMForOMBringUp,
                                             "PM for OM service bringup")

    def test_PMForOMBringUp(self):
        """
        Test Case:
        Attempt to start the PM component for an OM node
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]

        om_node = fdscfg.rt_om_node
        bin_dir = om_node.nd_agent.get_bin_dir(debug=False)
        log_dir = om_node.nd_agent.get_log_dir()

        # Start the PM for this OM.
        om_ip = om_node.nd_conf_dict['ip']

        self.log.info("Start PM for OM on node %s." % om_node.nd_conf_dict['node-name'])

        status = om_node.nd_start_platform(om_ip, test_harness=True, _bin_dir=bin_dir, _log_dir=log_dir)

        if status != 0:
            self.log.error("PM for OM on node %s returned status %d." % (om_node.nd_conf_dict['node-name'], status))
            return False

        return True


# This class contains the attributes and methods to test
# waiting for the Platform Manager (PM) service for the OM node to start.
class TestPMForOMWait(TestCase.FDSTestCase):
    def __init__(self, parameters=None, node=None):
        """
        When run by a qaautotest module test runner,
        "parameters" will have been populated with
        .ini configuration.
        """
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_PMForOMWait,
                                             "Wait for PM service on OM node to start")

        self.passedNode = node

    def test_PMForOMWait(self):
        """
        Test Case:
        Wait for the PM service on the OM node to start
        """
        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]

        # If a list of nodes were passed, make sure it includes
        # the OM's node. If not, do not check this PM.
        if self.passedNode is not None:
            om_node = None
            if self.passedNode.nd_conf_dict['node-name'] == fdscfg.rt_om_node.nd_conf_dict['node-name']:
                om_node = fdscfg.rt_om_node
            else:
                self.log.warn("Node %s not configured for an OM service." % self.passedNode.nd_conf_dict['node-name'])
        else:
            om_node = fdscfg.rt_om_node

        if om_node is not None:

            # Make sure it wasn't the target of a random kill
            if (om_node, "pm") in self.parameters.get('svc_killed', []):
                self.log.warning("PM service for node {} previous"
                                 "killed by random svc kill test. PASSING test.".format(om_node.nd_conf_dict['node-name']))
                return True

            self.log.info("Wait for PM on OM's node, %s." % om_node.nd_conf_dict['node-name'])

            if not modWait("platformd", om_node):
                return False
        else:
            self.log.warn("PM on non-OM node, %s, not to be checked." % self.passedNode.nd_conf_dict['node-name'])

        return True


# This class contains the attributes and methods to test
# killing the Platform Manager (PM) service on the OM node.
class TestPMForOMKill(TestCase.FDSTestCase):
    def __init__(self, parameters=None):
        """
        When run by a qaautotest module test runner,
        "parameters" will have been populated with
        .ini configuration.
        """
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_PMForOMKill,
                                             "PM service for OM node kill")

    def test_PMForOMKill(self):
        """
        Test Case:
        Attempt to shutdown the PM service for the OM node
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]
        om_node = fdscfg.rt_om_node

        self.log.info("Kill PM on OM's node, %s." % om_node.nd_conf_dict['node-name'])

        status = om_node.nd_agent.exec_wait("pkill -9 platformd")

        if status != 0:
            self.log.error("PM kill on OM's node, %s, returned status %d." %
                           (om_node.nd_conf_dict['node-name'], status))
            return False

        return True


# This class contains the attributes and methods to test
# whether the Platform Manager (PM) service for the OM's node is down.
class TestPMForOMVerifyDown(TestCase.FDSTestCase):
    def __init__(self, parameters=None, node=None):
        """
        When run by a qaautotest module test runner,
        "parameters" will have been populated with
        .ini configuration.
        """
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_PMForOMVerifyDown,
                                             "Verifying the PM service for the OM's node is down")

        self.passedNode = node

    def test_PMForOMVerifyDown(self):
        """
        Test Case:
        Verify the PM service for the OM's node is down
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]

        # If we were passed a node and it is not the OM node
        # captured during config parsing, just exit.
        if self.passedNode is not None:
            om_node = None
            if self.passedNode.nd_conf_dict['node-name'] == fdscfg.rt_om_node.nd_conf_dict['node-name']:
                om_node = self.passedNode
            else:
                self.log.warn("Node %s not configured for an OM service." % self.passedNode.nd_conf_dict['node-name'])
        else:
            om_node = fdscfg.rt_om_node

        if om_node is not None:
            self.log.info("Verify the PM on OM's node, %s, is down." % om_node.nd_conf_dict['node-name'])

            if not modWait("platformd", om_node, forShutdown=True):
                return False

        return True


# This class contains the attributes and methods to test
# bringing up an Orchestration Manager (OM) service.
class TestOMBringUp(TestCase.FDSTestCase):
    def __init__(self, parameters=None, node=None):
        """
        When run by a qaautotest module test runner,
        "parameters" will have been populated with
        .ini configuration.
        """
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_OMBringUp,
                                             "OM service bringup")

        self.passedNode = node

    def test_OMBringUp(self):
        """
        Test Case:
        Attempt to start the OM service(s)
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]

        # If we were passed a node and it is not the OM node
        # captured during config parsing, just exit.
        if self.passedNode is not None:
            self.passedNode = findNodeFromInv(fdscfg.rt_obj.cfg_nodes, self.passedNode)
            om_node = None
            if self.passedNode.nd_conf_dict['node-name'] == fdscfg.rt_om_node.nd_conf_dict['node-name']:
                om_node = self.passedNode
            else:
                self.log.warn("Node %s not configured for an OM service." % self.passedNode.nd_conf_dict['node-name'])
        else:
            om_node = fdscfg.rt_om_node

        if om_node is not None:
            self.log.info("Start OM on %s." % om_node.nd_conf_dict['node-name'])
            bin_dir = om_node.nd_agent.get_bin_dir(debug=False)
            log_dir = om_node.nd_agent.get_log_dir()

            status = om_node.nd_start_om(test_harness=True, _bin_dir=bin_dir, _log_dir=log_dir)

            if status != 0:
                self.log.error("OM on %s returned status %d." % (om_node.nd_conf_dict['node-name'], status))
                return False

        return True


# This class contains the attributes and methods to wait
# for the Orchestration Manager (OM) service to become active.
class TestOMWait(TestCase.FDSTestCase):
    def __init__(self, parameters=None, node=None):
        """
        When run by a qaautotest module test runner,
        "parameters" will have been populated with
        .ini configuration.
        """
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_OMWait,
                                             "OM service wait")

        self.passedNode = node

    def test_OMWait(self):
        """
        Test Case:
        Wait for the named service to become active.
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]

        # If we were passed a node and it is not the OM node
        # captured during config parsing, just exit.
        if self.passedNode is not None:
            self.passedNode = findNodeFromInv(fdscfg.rt_obj.cfg_nodes, self.passedNode)
            om_node = None
            if self.passedNode.nd_conf_dict['node-name'] == fdscfg.rt_om_node.nd_conf_dict['node-name']:
                om_node = self.passedNode
            else:
                self.log.warn("Node %s not configured for an OM service." % self.passedNode.nd_conf_dict['node-name'])
        else:
            om_node = fdscfg.rt_om_node

        if om_node is not None:

            # Make sure it wasn't the target of a random kill
            if (om_node, "om") in self.parameters.get('svc_killed', []):
                self.log.warning("OM service for node {} previous"
                                 "killed by random svc kill test. PASSING test!".format(om_node.nd_conf_dict['node-name']))
                return True

            self.log.info("Wait for OM on %s." % om_node.nd_conf_dict['node-name'])

            return modWait("orchMgr", om_node)
        else:
            return True


# This class contains the attributes and methods to test
# killing an Orchestration Manager (OM) service.
class TestOMKill(TestCase.FDSTestCase):
    def __init__(self, parameters=None, node=None):
        """
        When run by a qaautotest module test runner,
        "parameters" will have been populated with
        .ini configuration.
        """
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_OMKill,
                                             "OM service kill")

        self.passedNode = node

    def test_OMKill(self):
        """
        Test Case:
        Attempt to shutdown the OM service(s)
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]


        # If we were passed a node and it is not the OM node
        # captured during config parsing, just exit.
        if self.passedNode is not None:
            self.passedNode = findNodeFromInv(fdscfg.rt_obj.cfg_nodes, self.passedNode)
            om_node = None
            if self.passedNode.nd_conf_dict['node-name'] == fdscfg.rt_om_node.nd_conf_dict['node-name']:
                om_node = self.passedNode
            else:
                self.log.warn("Node %s not configured for an OM service." % self.passedNode.nd_conf_dict['node-name'])
        else:
            om_node = fdscfg.rt_om_node

        if om_node is not None:
            self.log.info("Kill OM on %s." % om_node.nd_conf_dict['node-name'])

            status = om_node.nd_agent.exec_wait('pkill -9 -f com.formationds.om.Main')

            # Probably we get the -9 return because pkill for a java process will
            # not find it based on the class name alone. See getSvcPIDforNode().
            # For a remote install, a failed kill (because its already dead) returns 1.
            if (status != -9) and (status != 0) and (om_node.nd_agent.env_install and status != 1):
                self.log.error("OM (com.formationds.om.Main) kill on %s returned status %d." %
                               (om_node.nd_conf_dict['node-name'], status))
                return False

            status = om_node.nd_agent.exec_wait("pkill -9 orchMgr")

            if (status != 1) and (status != 0):
                self.log.error("OM (orchMgr) kill on %s returned status %d." %
                               (om_node.nd_conf_dict['node-name'], status))
                return False

        return True


# This class contains the attributes and methods to
# verify that the the Orchestration Manager (OM) service is down.
class TestOMVerifyDown(TestCase.FDSTestCase):
    def __init__(self, parameters=None, node=None):
        """
        When run by a qaautotest module test runner,
        "parameters" will have been populated with
        .ini configuration.
        """
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_OMVerifyDown,
                                             "OM service verify down")

        self.passedNode = node

    def test_OMVerifyDown(self):
        """
        Test Case:
        Verify the OM is down.
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]

        # If we were passed a node and it is not the OM node
        # captured during config parsing, just exit.
        if self.passedNode is not None:
            om_node = None
            if self.passedNode.nd_conf_dict['node-name'] == fdscfg.rt_om_node.nd_conf_dict['node-name']:
                om_node = self.passedNode
            else:
                self.log.warn("Node %s not configured for an OM service." % self.passedNode.nd_conf_dict['node-name'])
        else:
            om_node = fdscfg.rt_om_node

        if om_node is not None:
            self.log.info("Verify the OM on %s is down." % fdscfg.rt_om_node.nd_conf_dict['node-name'])

            return modWait("orchMgr", fdscfg.rt_om_node, forShutdown=True)

        return True


# This class contains the attributes and methods to test
# bringing up an Access Manager (AM) service.
class TestAMBringUp(TestCase.FDSTestCase):
    def __init__(self, parameters=None, node=None):
        """
        When run by a qaautotest module test runner,
        "parameters" will have been populated with
        .ini configuration.
        """
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_AMBringUp,
                                             "AM service bringup")

        self.passedNode = node

    def test_AMBringUp(self):
        """
        Test Case:
        Attempt to start the AM service(s)
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]
        nodes = fdscfg.rt_obj.cfg_nodes

        for n in nodes:
            # If we were passed a node, use it and get out. Otherwise,
            # we boot all AMs we have.
            if self.passedNode is not None:
                n = findNodeFromInv(nodes, self.passedNode)

            # Make sure AM should be running on this node.
            if n.nd_services.count("am") == 0:
                self.log.warning("AM not configured for node %s." % n.nd_conf_dict['node-name'])
                if self.passedNode is None:
                    break
                else:
                    continue

            self.log.info("Start AM on %s." % n.nd_conf_dict['node-name'])

            port = n.nd_conf_dict['fds_port']
            fds_dir = n.nd_conf_dict['fds_root']
            bin_dir = n.nd_agent.get_bin_dir(debug=False)
            log_dir = n.nd_agent.get_log_dir()

            # The AMAgent script expected to be invoked from the bin directory in which it resides.
            status = n.nd_agent.exec_wait('bash -c \"(nohup ./AMAgent --fds-root=%s 0<&- &> %s/am.%s.out &) \"' %
                                          (fds_dir, log_dir, port),
                                          fds_bin=True)

            if status != 0:
                self.log.error("AM on %s returned status %d." % (n.nd_conf_dict['node-name'], status))
                return False
            elif self.passedNode is not None:
                # We were passed a specific AM so get out now.
                return True

        return True


# This class contains the attributes and methods to test
# activating Access Manager (AM) services on a domain.
class TestAMActivate(TestCase.FDSTestCase):
    def __init__(self, parameters=None, node=None):
        """
        When run by a qaautotest module test runner,
        "parameters" will have been populated with
        .ini configuration.
        """
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_AMActivate,
                                             "AM service activation")

        self.passedNode = node

    def test_AMActivate(self):
        """
        Test Case:
        Attempt to activate the AM service(s)
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]

        om_node = fdscfg.rt_om_node
        fds_dir = om_node.nd_conf_dict['fds_root']
        log_dir = om_node.nd_agent.get_log_dir()

        # Do all AMs in the domain or just the AM for the passed node?
        if self.passedNode is None:
            self.log.info("Activate domain AMs from OM node %s." % om_node.nd_conf_dict['node-name'])

            status = om_node.nd_agent.exec_wait('bash -c \"(./fdsconsole.py domain activateServices local am > '
                                                '{}/cli.out 2>&1) \"'.format(log_dir),
                                                fds_tools=True)
        else:
            node = self.passedNode

            # Make sure this node is configured for an AM service.
            if node.nd_services.count("am") == 0:
                self.log.error("Node %s is not configured to host an AM service." % node.nd_conf_dict['node-name'])
                return False

            # Make sure we have the node's meta-data.
            status = node.nd_populate_metadata(om_node=om_node)
            if status != 0:
                self.log.error("Getting meta-data for node %s returned status %d." %
                               (node.nd_conf_dict['node-name'], status))
                return False

            self.log.info("Activate AM for node %s from OM node %s." % (node.nd_conf_dict['node-name'],
                                                                        om_node.nd_conf_dict['node-name']))

            status = om_node.nd_agent.exec_wait('bash -c \"(./fdsconsole.py service '
                                                'addService {} am > {}/cli.out 2>&1) \"'.format(
                                                int(node.nd_uuid, 16), log_dir,), fds_tools=True)

        if status != 0:
            self.log.error("AM activation on %s returned status %d." % (self.passedNode.nd_conf_dict['node-name'], status))
            return False

        return True


# This class contains the attributes and methods to test
# removing Access Manager (AM) services on a domain.
class TestAMRemove(TestCase.FDSTestCase):
    def __init__(self, parameters=None, node=None):
        """
        When run by a qaautotest module test runner,
        "parameters" will have been populated with
        .ini configuration.
        """
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_AMRemove,
                                             "AM service removal")

        self.passedNode = node

    def test_AMRemove(self):
        """
        Test Case:
        Attempt to remove the AM service(s)
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]

        om_node = fdscfg.rt_om_node
        fds_dir = om_node.nd_conf_dict['fds_root']
        log_dir = om_node.nd_agent.get_log_dir()

        nodes = fdscfg.rt_obj.cfg_nodes
        for node in nodes:
            # If we were passed a node, check that one and exit.
            if self.passedNode is not None:
                node = self.passedNode

            # Make sure this node is configured for an AM service.
            if (node.nd_services.count("am") == 0) and self.passedNode is not None:
                self.log.error("Node %s is not configured to host a AM service." % node.nd_conf_dict['node-name'])
                return False

            # Make sure we have the node's meta-data.
            status = node.nd_populate_metadata(om_node=om_node)
            if status != 0:
                self.log.error("Getting meta-data for node %s returned status %d." %
                               (node.nd_conf_dict['node-name'], status))
                return False

            self.log.info("Remove AM for node %s using OM node %s." % (node.nd_conf_dict['node-name'],
                                                                        om_node.nd_conf_dict['node-name']))

            status = om_node.nd_agent.exec_wait('bash -c \"(./fdsconsole.py service removeService {} am > '
                                                '{}/cli.out 2>&1) \"'.format(node.nd_uuid, log_dir),
                                                fds_tools=True)

            if status != 0:
                self.log.error("AM removal from %s returned status %d." % (self.passedNode.nd_conf_dict['node-name'], status))
                return False

            if self.passedNode is not None:
                break

        return True


# This class contains the attributes and methods to test
# waiting for an Access Manager (AM) service to start.
class TestAMWait(TestCase.FDSTestCase):
    def __init__(self, parameters=None, node=None):
        """
        When run by a qaautotest module test runner,
        "parameters" will have been populated with
        .ini configuration.
        """
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_AMWait,
                                             "Wait for AM service to start")

        self.passedNode = node

    def test_AMWait(self):
        """
        Test Case:
        Wait for the AM service(s) to start
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]

        nodes = fdscfg.rt_obj.cfg_nodes
        for n in nodes:
            # If we were passed a node, check that one and exit.
            if self.passedNode is not None:
                self.passedNode = findNodeFromInv(fdscfg.rt_obj.cfg_nodes, self.passedNode)
                # But only check it if it should have an AM.
                if self.passedNode.nd_conf_dict['node-name'] != n.nd_conf_dict['node-name']:
                    continue

            # Make sure it wasn't the target of a random kill
            if (n, "am") in self.parameters.get('svc_killed', []):
                self.log.warning("AM service for node {} previously"
                                 "killed by random svc kill test.".format(n.nd_conf_dict['node-name']))
                if self.passedNode is not None:
                    break
                else:
                    continue

            # Make sure this node is configured for AM.
            if n.nd_services.count('am') == 0:
                self.log.warning("AM service not configured for node %s." % n.nd_conf_dict['node-name'])
                if self.passedNode is not None:
                    break
                else:
                    continue

            self.log.info("Wait for AM on %s." % n.nd_conf_dict['node-name'])

            if not modWait("AMAgent", n):
                return False

            if self.passedNode is not None:
                break

        return True


# This class contains the attributes and methods to test
# killing an Access Manager (AM) service.
class TestAMKill(TestCase.FDSTestCase):
    def __init__(self, parameters=None, node=None):
        """
        When run by a qaautotest module test runner,
        "parameters" will have been populated with
        .ini configuration.
        """
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_AMKill,
                                             "AM service kill")

        self.passedNode = node

    def test_AMKill(self):
        """
        Test Case:
        Attempt to shutdown the AM service(s)
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]

        nodes = fdscfg.rt_obj.cfg_nodes
        for n in nodes:
            # If a specific node was passed in, use that one and get out.
            if self.passedNode is not None:
                n = findNodeFromInv(nodes, self.passedNode)

            # Make sure there is supposed to be an AM service on this node.
            if n.nd_services.count("am") == 0:
                self.log.warning("AM service not configured for node %s." % n.nd_conf_dict["node-name"])
                if self.passedNode is None:
                    continue
                else:
                    break

            self.log.info("Shutdown AM on %s." % n.nd_conf_dict['node-name'])

            # Get the PID of the processes in question and ... kill them!
            pid = getSvcPIDforNode('java', n, javaClass='com.formationds.am.Main')
            if pid != -1:
                cmd = "kill -9 %s" % pid
                status = n.nd_agent.exec_wait(cmd)

                if status != 0:
                    self.log.error("AM (com.formationds.am.Main) shutdown on %s returned status %d." %
                                   (n.nd_conf_dict['node-name'], status))
                    return False
            else:
                status = 0
                self.log.warning("AM (com.formationds.am.Main) already shutdown on %s." % (n.nd_conf_dict['node-name']))

            pid = getSvcPIDforNode('bare_am', n)
            if pid != -1:
                cmd = "kill -9 %s" % pid
                status = n.nd_agent.exec_wait(cmd)

                if status != 0:
                    self.log.error("AM (bare_am) shutdown on %s returned status %d." %
                                   (n.nd_conf_dict['node-name'], status))
                    return False
            else:
                status = 0
                self.log.warning("AM (bare_am) already shutdown on %s." % (n.nd_conf_dict['node-name']))

            pid = getSvcPIDforNode('AMAgent', n)
            if pid != -1:
                cmd = "kill -9 %s" % pid
                status = n.nd_agent.exec_wait(cmd)

                if (status != 1) and (status != 0):
                    self.log.error("AM (AMAgent) shutdown on %s returned status %d." %
                                   (n.nd_conf_dict['node-name'], status))
                    return False
            else:
                status = 0
                self.log.warning("AM (AMAgent) already shutdown on %s." % (n.nd_conf_dict['node-name']))

            if (status != 1) and (status != 0):
                self.log.error("AM shutdown on %s returned status %d." % (n.nd_conf_dict['node-name'], status))
                return False
            elif self.passedNode is not None:
                # We took care of the one node. Get out.
                break

        return True


# This class contains the attributes and methods to test
# whether an Access Manager (AM) service is down.
class TestAMVerifyDown(TestCase.FDSTestCase):
    def __init__(self, parameters=None, node=None):
        """
        When run by a qaautotest module test runner,
        "parameters" will have been populated with
        .ini configuration.
        """
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_AMVerifyDown,
                                             "Verify the AM service is down")

        self.passedNode = node

    def test_AMVerifyDown(self):
        """
        Test Case:
        Verify the AM service(s) are down
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]

        nodes = fdscfg.rt_obj.cfg_nodes
        for n in nodes:
            # If we were passed a specific node, check that one and get out.
            if self.passedNode is not None:
                n = findNodeFromInv(nodes, self.passedNode)

            self.log.info("Verify AM on %s is down" % n.nd_conf_dict['node-name'])

            if not modWait("AMAgent", n, forShutdown=True):
                return False
            elif self.passedNode is not None:
                # We took care of the one node. Get out.
                break

        return True


if __name__ == '__main__':
    TestCase.FDSTestCase.fdsGetCmdLineConfigs(sys.argv)
    unittest.main(testRunner=xmlrunner.XMLTestRunner(output='test-reports'))
