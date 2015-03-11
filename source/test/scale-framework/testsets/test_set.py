#!/usr/bin/python
# Copyright 2014 by Formation Data Systems, Inc.
# Written by Philippe Ribeiro
# philippe@formationds.com
import concurrent.futures as concurrent
import importlib
import logging
import os
import unittest

import config
import graph
import testcase
import testcases


class TestSet(object):

    '''
    In our definition, a TestSet object is a set of test cases for a particular
    feature.

    Attributes:
    -----------
    name : str
        the name of the test set being executed
    test_cases: list
        a list of all the test cases for this test set.
    '''
    logging.basicConfig(level=logging.INFO)
    log = logging.getLogger(__name__)

    def __init__(self, name, test_cases, om_ip_address, inventory_file, **kwargs):
        self.name = name
        self.graph = {}
        self.lookup_table = {}
        self.execution_order = []
        self._tests = []
        self.suite = unittest.TestSuite()
        self.inventory_file = inventory_file
        # create all the test cases that belong to this test set.
        for tc in test_cases:
            name = tc['name']
            # give each test case one ip address
            if tc['depends'] == []:
                self.execution_order.append(name)
                self.__instantiate_module(tc, om_ip_address)
            else:
                self.lookup_table[name] = tc
            self.graph[name] = tc['depends']
            
        for k, v in self.graph.iteritems():
            # if the test 
            if k not in self.execution_order and v != []:
                resolved = []
                graph.dependency_resolve(self.graph, k, resolved, [])
                for node in resolved:
                    if node not in self.execution_order:
                        current_tc = self.lookup_table[node]
                        self.execution_order.append(node)
                        self.__instantiate_module(current_tc, om_ip_address)

    def __instantiate_module(self, test_cases, om_ip_address):
        fmodule = self.__process_module(test_cases['file'])
        module = importlib.import_module("testsets.testcases.%s" % fmodule)
        my_class = getattr(module, test_cases['name'])
        if 'parameters' in test_cases:
            params = test_cases['parameters']
        else:
            params = {}
        if 'config' in test_cases:
            cnf = test_cases['config']
        else:
            cnf = None
        params['inventory_file'] = self.inventory_file
        instance = my_class(parameters=params, 
                            config_file=cnf,
                            om_ip_address=om_ip_address,
                            )

        self.log.info("Adding test case: %s" % test_cases['name'])
        self.log.info("Adding file name: %s" % test_cases['file'])
        self.suite.addTest(instance)
            
    def __process_module(self, module=""):
        '''
        This method checks if a file ends with .py extension (for now, but it
        could be extended to include a .java, .c or .cpp). If it does, then
        returns the module name (in Python file names are modules).
        
        Attributes:
        -----------
        module : str
            the file(module) name
            
        Returns:
        --------
        str : the file name stripped of the extension
        '''
        if module.endswith(".py"):
            return os.path.splitext(module)[0]
        else:
            raise ValueError("Only .py modules are supported.")
            sys.exit(1)