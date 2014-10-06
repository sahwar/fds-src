#!/usr/bin/python
#
# Copyright 2014 by Formation Data Systems, Inc.
#

import sys
import unittest
import xmlrunner
import testcases.TestCase
import testcases.TestFDSEnvMgt
import testcases.TestFDSModMgt
import testcases.TestFDSSysMgt
import testcases.TestFDSSysLoad
import NodeWaitSuite


def suiteConstruction():
    """
    Construct the ordered set of test cases that comprise the
    Build Smoke Test suite.
    """
    suite = unittest.TestSuite()

    # Build the necessary FDS installation directory structure.
    suite.addTest(testcases.TestFDSEnvMgt.TestFDSCreateInstDir())

    # Start the node(s) according to configuration supplied with the -q cli option.
    suite.addTest(testcases.TestFDSModMgt.TestPMBringUp())
    suite.addTest(testcases.TestFDSModMgt.TestPMWait())
    suite.addTest(testcases.TestFDSModMgt.TestOMBringUp())
    suite.addTest(testcases.TestFDSModMgt.TestOMWait())
    suite.addTest(testcases.TestFDSSysMgt.TestNodeActivate())

    # Check that all nodes are up.
    nodeUpSuite = NodeWaitSuite.suiteConstruction()
    suite.addTest(nodeUpSuite)

    # Load test.
    suite.addTest(testcases.TestFDSSysLoad.TestSmokeLoad())

    suite.addTest(testcases.TestFDSSysMgt.TestNodeShutdown())

    # Cleanup FDS installation directory.
    suite.addTest(testcases.TestFDSEnvMgt.TestFDSDeleteInstDir())

    return suite

if __name__ == '__main__':

    # Handle FDS specific commandline arguments.
    log_dir, failfast = testcases.TestCase.FDSTestCase.fdsGetCmdLineConfigs(sys.argv)

    # If a test log directory was not supplied on the command line (with option "-l"),
    # then default it.
    if log_dir is None:
        log_dir = 'test-reports'

    # Get a test runner that will output an xUnit XML report for Jenkins
    # TODO(Greg) I've tried everything I can think of, but failfast does not
    # stop the suite upon first failure.
    runner = xmlrunner.XMLTestRunner(output=log_dir, failfast=failfast)

    test_suite = suiteConstruction()

    runner.run(test_suite)

