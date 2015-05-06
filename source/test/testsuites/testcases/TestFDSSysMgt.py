#!/usr/bin/python
#
# Copyright 2014 by Formation Data Systems, Inc.
#

# FDS test-case pattern requirements.
import unittest
import traceback
import TestCase

# Module-specific requirements
import sys
import os
from TestFDSServiceMgt import TestAMKill, TestSMKill, TestDMKill, TestOMKill, TestPMKill
from TestFDSServiceMgt import TestAMBringUp


# This class contains the attributes and methods to test
# activation of an FDS domain starting the same, specified
# services on each node.
class TestDomainActivateServices(TestCase.FDSTestCase):
    def __init__(self, parameters=None, services="dm,sm,am"):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_DomainActivateServices,
                                             "Domain activation")

        self.passedServices = services

    def test_DomainActivateServices(self):
        """
        Test Case:
        Attempt to activate services in a domain.
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]

        om_node = fdscfg.rt_om_node
        fds_dir = om_node.nd_conf_dict['fds_root']
        log_dir = om_node.nd_agent.get_log_dir()

        self.log.info("Activate domain starting %s services on each node." % self.passedServices)

        status = om_node.nd_agent.exec_wait('bash -c \"(./fdsconsole.py domain activateServices local {} > '
                                            '{}/fdsconsole.out 2>&1) \"'.format(self.passedServices, log_dir),
                                            fds_tools=True)

        if status != 0:
            self.log.error("Domain activation on %s returned status %d." % (om_node.nd_conf_dict['node-name'], status))
            return False

        # After activation we should be able to spin through our nodes to obtain
        # some useful information about them.
        for n in fdscfg.rt_obj.cfg_nodes:
            status = n.nd_populate_metadata(om_node=om_node)
            if status != 0:
                break

        if status != 0:
            self.log.error("Node meta-data lookup for %s returned status %d." % (n.nd_conf_dict['node-name'], status))
            return False

        return True


# This class contains the attributes and methods to test
# the kill the services of an FDS node.
class TestNodeKill(TestCase.FDSTestCase):
    def __init__(self, parameters=None, node=None):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_NodeKill,
                                             "Node kill",
                                             True)  # Always run.

        self.passedNode = node

    def test_NodeKill(self, ansibleBoot=False):
        """
        Test Case:
        Attempt to kill all services on a node.

        We try to kill just the services defined for a node unless we're killing
        an Ansible boot in which case we go after all services on all nodes.
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]

        nodes = fdscfg.rt_obj.cfg_nodes
        for n in nodes:
            # If we were passed a node, use it and get out. Otherwise,
            # we spin through the defined nodes killing them.
            if self.passedNode is not None:
                n = self.passedNode

            self.log.info("Kill node %s." % n.nd_conf_dict['node-name'])

            # First kill AM if on this node.
            if (n.nd_services.count("am") > 0) or ansibleBoot:
                killAM = TestAMKill(node=n)
                killSuccess = killAM.test_AMKill()

                if not killSuccess and not ansibleBoot:
                    self.log.error("Node kill on %s failed." % (n.nd_conf_dict['node-name']))
                    return False

            # SM and DM next.
            if (n.nd_services.count("sm") > 0) or ansibleBoot:
                killSM = TestSMKill(node=n)
                killSuccess = killSM.test_SMKill()

                if not killSuccess and not ansibleBoot:
                    self.log.error("Node kill on %s failed." % (n.nd_conf_dict['node-name']))
                    return False

            if (n.nd_services.count("dm") > 0) or ansibleBoot:
                killDM = TestDMKill(node=n)
                killSuccess = killDM.test_DMKill()

                if not killSuccess and not ansibleBoot:
                    self.log.error("Node kill on %s failed." % (n.nd_conf_dict['node-name']))
                    return False

            # Next, kill OM if on this node.
            if (fdscfg.rt_om_node.nd_conf_dict['node-name'] == n.nd_conf_dict['node-name']) or ansibleBoot:
                killOM = TestOMKill(node=n)
                killSuccess = killOM.test_OMKill()

                if not killSuccess and not ansibleBoot:
                    self.log.error("Node kill on %s failed." % (n.nd_conf_dict['node-name']))
                    return False

            # Finally PM.
            killPM = TestPMKill(node=n)
            killSuccess = killPM.test_PMKill()

            if not killSuccess:
                self.log.error("Node kill on %s failed." % (n.nd_conf_dict['node-name']))
                return False

            if self.passedNode is not None:
                # We took care of the specified node. Now get out.
                break

        return True


# This class contains the attributes and methods to test
# node activation. (I.e. activate the node's specified
# services.)
class TestNodeActivate(TestCase.FDSTestCase):
    def __init__(self, parameters=None, node=None):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_NodeActivateService,
                                             "Node services activation")

        self.passedNode = node

    def test_NodeActivateService(self):
        """
        Test Case:
        Attempt to activate the services of the specified node(s).
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]
        om_node = fdscfg.rt_om_node
        fds_dir = om_node.nd_conf_dict['fds_root']
        log_dir = om_node.nd_agent.get_log_dir()
        nodes = fdscfg.rt_obj.cfg_nodes

        for n in nodes:
            # If we were provided a node, activate that one and exit.
            if self.passedNode is not None:
                n = self.passedNode

            # If we're asked to activate nodes, we should be able to spin through our nodes to obtain
            # some useful information about them.
            status = n.nd_populate_metadata(om_node=om_node)
            if status != 0:
                self.log.error("Getting meta-data for node %s returned status %d." %
                               (n.nd_conf_dict['node-name'], status))
                return False

            services = n.nd_services

            if services != '':
                self.log.info("Activate services %s for node %s." % (services, n.nd_conf_dict['node-name']))

                status = om_node.nd_agent.exec_wait('bash -c \"(./fdsconsole.py service '
                                                    'addService {} {} > {}/cli.out 2>&1) \"'.format(
                    int(n.nd_uuid, 16), services, log_dir,), fds_tools=True)

            if status != 0:
                self.log.error("Service activation of node %s returned status %d." %
                               (n.nd_conf_dict['node-name'], status))
                return False

            elif self.passedNode is not None:
                # If we were passed a specific node, exit now.
                return True

        return True


# This class contains the attributes and methods to test
# services removal of nodes. (I.e. remove the node from
# the domain.)
class TestNodeRemoveServices(TestCase.FDSTestCase):
    def __init__(self, parameters=None, node=None):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_NodeRemoveService,
                                             "Node services removal")

        self.passedNode = node

    def test_NodeRemoveService(self):
        """
        Test Case:
        Attempt to remove the services of nodes from a domain.
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]
        om_node = fdscfg.rt_om_node
        fds_dir = om_node.nd_conf_dict['fds_root']
        log_dir = om_node.nd_agent.get_log_dir()

        nodes = fdscfg.rt_obj.cfg_nodes
        for n in nodes:
            # If we were provided a node, remove that one and exit.
            if self.passedNode is not None:
                n = self.passedNode
#            else: Should we skip the OM's node?
#                if n.nd_conf_dict['node-name'] == om_node.nd_conf_dict['node-name']:
#                    self.log.info("Skipping OM's node on %s." % n.nd_conf_dict['node-name'])
#                    continue

            self.log.info("Remove services for node %s." % n.nd_conf_dict['node-name'])

            self.log.warn("Remove services call currently unimplemented. THIS CALL WILL FAIL!")
            status = om_node.nd_agent.exec_wait('bash -c \"(./fdsconsole.py service removeService {} {} > '
                                                '{}/cli.out 2>&1) \"'.format(n.nd_uuid, n.nd_services, log_dir),
                                                fds_tools=True)

            if status != 0:
                self.log.error("Service removal of node %s returned status %d." %
                               (n.nd_conf_dict['node-name'], status))
                return False
            elif self.passedNode is not None:
                # If we were passed a specific node, exit now.
                return True

        return True


# This class contains the attributes and methods to test
# domain shutdown
class TestDomainShutdown(TestCase.FDSTestCase):
    def __init__(self, parameters=None):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_DomainShutdown,
                                             "Domain shutdown")

    def test_DomainShutdown(self):
        """
        Test Case:
        Attempt to execute domain-shutdown.
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]
        om_node = fdscfg.rt_om_node

        self.log.info("Shutdown domain.")

        status = om_node.nd_agent.exec_wait('bash -c \"(./fdsconsole.py domain shutdown local) \"',
                                            fds_tools=True)

        if status != 0:
            self.log.error("Domain shutdown returned status %d." % (status))
            return False
        else:
            return True

# This class contains the attributes and methods to test
# domain restart
class TestDomainStartup(TestCase.FDSTestCase):
    def __init__(self, parameters=None):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_DomainStartup,
                                             "Domain startup")

    def test_DomainStartup(self):
        """
        Test Case:
        Attempt to execute domain-startup
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]
        om_node = fdscfg.rt_om_node

        self.log.info("Startup domain after shutdown.")

        status = om_node.nd_agent.exec_wait('bash -c \"(./fdsconsole.py domain startup local) \"',
                                            fds_tools=True)

        if status != 0:
            self.log.error("Domain startup returned status %d." % (status))
            return False
        else:
            return True


# This class contains the attributes and methods to test
# domain create
class TestDomainCreate(TestCase.FDSTestCase):
    def __init__(self, parameters=None):
        super(self.__class__, self).__init__(parameters,
                                             self.__class__.__name__,
                                             self.test_DomainCreate,
                                             "Domain create")

    def test_DomainCreate(self):
        """
        Test Case:
        Attempt to execute domain-create.
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]
        om_node = fdscfg.rt_om_node

        self.log.info("Create domain.")

        status = om_node.nd_agent.exec_wait('bash -c \"(./fdsconsole.py domain create my_domain Woodville,MS) \"',
                                            fds_tools=True)

        if status != 0:
            self.log.error("Domain create returned status %d." % (status))
            return False
        else:
            return True

if __name__ == '__main__':
    TestCase.FDSTestCase.fdsGetCmdLineConfigs(sys.argv)
    unittest.main()
