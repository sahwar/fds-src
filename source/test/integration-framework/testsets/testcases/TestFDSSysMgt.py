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
import time
import os


# This class contains the attributes and methods to test
# activation of an FDS node where PM and OM have been started.
class TestNodeActivate(TestCase.FDSTestCase):
    def __init__(self, parameters=None):
        super(TestNodeActivate, self).__init__(parameters)


    def runTest(self):
        test_passed = True

        if TestCase.pyUnitTCFailure:
            self.log.warning("Skipping Case %s. stop-on-fail/failfast set and a previous test case has failed." %
                             self.__class__.__name__)
            return unittest.skip("stop-on-fail/failfast set and a previous test case has failed.")
        else:
            self.log.info("Running Case %s." % self.__class__.__name__)

        try:
            if not self.test_NodeActivate():
                test_passed = False
        except Exception as inst:
            self.log.error("Node activation caused exception:")
            self.log.error(traceback.format_exc())
            test_passed = False

        super(self.__class__, self).reportTestCaseResult(test_passed)

        # If there is any test fixture teardown to be done, do it here.

        if self.parameters["pyUnit"]:
            self.assertTrue(test_passed)
        else:
            return test_passed


    def test_NodeActivate(self):
        """
        Test Case:
        Attempt to activate a node where OM and PM have been started.
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]

        nodes = fdscfg.rt_obj.cfg_nodes
        for n in nodes:
            fds_dir = n.nd_conf_dict['fds_root']
            bin_dir = fdscfg.rt_env.get_bin_dir(debug=False)
            log_dir = fdscfg.rt_env.get_log_dir()

            self.log.info("Activate node %s." % n.nd_conf_dict['node-name'])

            # We need a better way to ensure that the PM has registered with OM before doing this ...
            # a new test case that talks to OM or PM perhaps.
            time.sleep(5)

            cur_dir = os.getcwd()
            os.chdir(bin_dir)
            status = n.nd_agent.exec_wait('bash -c \"(nohup ./fdscli --fds-root %s --activate-nodes abc -k 1 -e am,dm,sm > '
                                          '%s/cli.out 2>&1 &) \"' %
                                          (fds_dir, log_dir if n.nd_agent.env_install else "."))
            os.chdir(cur_dir)

            if status != 0:
                self.log.error("Node activation on %s returned status %d." %(n.nd_conf_dict['node-name'], status))
                return False

            time.sleep(2)

        return True


# This class contains the attributes and methods to test
# the shutdown of an FDS node.
class TestNodeShutdown(TestCase.FDSTestCase):
    def __init__(self, parameters=None):
        super(TestNodeShutdown, self).__init__(parameters)


    def runTest(self):
        test_passed = True

        # We'll continue to shutdown the nodes even in the event of failure upstream.
        if TestCase.pyUnitTCFailure and not True:
            self.log.warning("Skipping Case %s. stop-on-fail/failfast set and a previous test case has failed." %
                             self.__class__.__name__)
            return unittest.skip("stop-on-fail/failfast set and a previous test case has failed.")
        else:
            self.log.info("Running Case %s." % self.__class__.__name__)

        try:
            if not self.test_NodeShutdown():
                test_passed = False
        except Exception as inst:
            self.log.error("Node shutdown caused exception:")
            self.log.error(traceback.format_exc())
            test_passed = False

        super(self.__class__, self).reportTestCaseResult(test_passed)

        # If there is any test fixture teardown to be done, do it here.

        if self.parameters["pyUnit"]:
            self.assertTrue(test_passed)
        else:
            return test_passed


    def test_NodeShutdown(self):
        """
        Test Case:
        Attempt to shutdown all FDS daemons on a node.
        """

        # Get the FdsConfigRun object for this test.
        fdscfg = self.parameters["fdscfg"]

        nodes = fdscfg.rt_obj.cfg_nodes
        for n in nodes:
            fds_dir = n.nd_conf_dict['fds_root']
            bin_dir = fdscfg.rt_env.get_bin_dir(debug=False)
            log_dir = fdscfg.rt_env.get_log_dir()

            self.log.info("Shutdown node %s." % n.nd_conf_dict['node-name'])

            # TODO(GREG): Currently, no return from this method.
            n.nd_cleanup_daemons()

            #if status != 0:
            #    self.log.error("Node shutdown on %s returned status %d." % (n.nd_conf_dict['node-name'], status))
            #    return False

            time.sleep(2)

        return True

if __name__ == '__main__':
    TestCase.FDSTestCase.fdsGetCmdLineConfigs(sys.argv)
    unittest.main()