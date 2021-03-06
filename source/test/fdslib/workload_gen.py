#!/usr/bin/python
try:
    import sys
    import os
    import time
    sys.path.append(os.getcwd()+'/test/fdslib/pyfdsp')

    from svc_api import ttypes
    from thrift import TSerialization
#from thrift.protocol import TJSONProtocol
except Exception as e:
    print e
    print 'PYTHONPATH: ', sys.path
import json
import jsonpickle
import random
from requests_futures.sessions import FuturesSession
import requests
from tabulate import tabulate
import hashlib

class GlobalStats(object):
    CALC_HASHES = True
    DEBUG_TIMEOUT = 240
    MAX_CONCURRENCY_PUT = 999
    MAGIC_WORD = 'magic_word'

class DataObject(object):
    """ Contains information obtained from data header as well as actual data itself."""
    def __init__(self, oid='', size=0, dedup=0, data='', obj_hash=''):
        self.objId = oid
        self.size = size
        self.dedup = dedup
        self.data = data
        self.obj_hash = obj_hash

    def check_data(self):
        """ Sanity check on data fields. """
        assert(self.objId != '')
        assert(self.size != 0)
        assert(self.data != '')
        assert(self.dedup <= 100)
        assert(self.dedup >= 0)
        assert(self.obj_hash != '')

class Processor(object):
    """ Converts the primitive operations generated by workload generator into sendable messages."""
    def __init__(self, list_, readData_=None, read_arg=None):
        self.list_      = list_
        self.readData_  = readData_
        self.read_arg   = read_arg

        if self.readData_ is None:
            self.readData_ = Processor.readData

    def readData(self, input_=sys.stdin):
        """ Reads a single object from standard input. """
        _data_obj = DataObject()

        # Populate header data
        while True:
            line = input_.readline()
            if not line: break

            # strip newline char
            line = line.rstrip()

            # start parsing once magic word is detected
            if line == GlobalStats.MAGIC_WORD:
                _data_obj.objId = input_.readline().rstrip()
                _data_obj.size = int(input_.readline())
                _data_obj.dedup = int(input_.readline())
                _data_obj.data = input_.read(_data_obj.size)
                break

        # Generate hash for object
        if GlobalStats.CALC_HASHES:
            _data_obj.obj_hash = hashlib.sha1(_data_obj.data).hexdigest()
        else:
            _data_obj.obj_hash = 'CALC_HASHES not enabled'

        # Sanity check
        _data_obj.check_data()
        return _data_obj

    def to_dm_json(self):
        """ Returns a list of messages usable by JSon decoders."""
        json_output = ''
        for operation in self.list_:
            if operation == 'put':
                pass
            elif operation == 'get':
                pass
            elif operation == 'delete':
                pass

    def toHTTP(self, fds_url='http://localhost:8000/bucket1/'):
        """ Handles sending async HTTP requests to AM. """
        session = FuturesSession(max_workers=GlobalStats.MAX_CONCURRENCY_PUT)
        data_list = []
        futures = []

        _get_counter = 0
        _del_counter = 0
        _fake_del_obj = 'fake1'
        _fake_del_ctr = 1
        _fake_get_obj = 'fake1'
        _fake_get_ctr = 1

        # data_list[0] : obj id
        # data_list[1] : dedup
        # data_list[2] : hash (sha1)
        #
        # futures[0] : session
        # futures[1] : obj id
        # futures[2] : operation
        #
        start=time.clock()
        # Send HTTP requests asynchronously
        for operation in self.list_:
            if operation == 'put':
                _data = self.readData_(self.read_arg)

                # Generate hash for object
                if GlobalStats.CALC_HASHES:
                    _data.obj_hash = hashlib.sha1(_data.data).hexdigest()
                else:
                    _data.obj_hash = 'CALC_HASHES not enabled'

                data_list.append((_data.objId, _data.dedup, _data.obj_hash))
                futures.append((
                    session.put(fds_url+_data.objId,
                        data=_data.data, timeout=GlobalStats.DEBUG_TIMEOUT),
                    _data.objId,
                    'put',
                    _data.obj_hash))
            elif operation == 'get':
                try:
                    futures.append((
                        session.get(fds_url+data_list[_get_counter][0],
                            timeout=GlobalStats.DEBUG_TIMEOUT),
                        data_list[_get_counter][0],
                        'get'))
                except IndexError as y:
                    futures.append((
                        session.get(fds_url+_fake_get_obj, timeout=GlobalStats.DEBUG_TIMEOUT),
                        _fake_get_obj,
                        'get'))
                    _fake_get_ctr += 1
                    _fake_get_obj = 'fake'+str(_fake_get_ctr)
                else:
                    _get_counter += 1
            elif operation == 'delete':
                try:
                    futures.append((
                        session.delete(fds_url+data_list[_del_counter][0],
                            timeout=GlobalStats.DEBUG_TIMEOUT),
                        data_list[_del_counter][0],
                        'del'))
                except IndexError as y:
                    futures.append((
                        session.delete(fds_url+_fake_del_obj, timeout=GlobalStats.DEBUG_TIMEOUT),
                        _fake_del_obj,
                        'del'))
                    _fake_del_ctr += 1
                    _fake_del_obj = 'fake'+str(_fake_del_ctr)
                else:
                    _del_counter += 1
        elapsed=time.clock()-start

        # Process the responses.  This is blocking
        _tab = []
        _header = ['Obj Name','Op','HTTP Error Code','Error Text', 'Hash', 'Hashes Match']

        # future[0] : future requests object
        # future[1] : obj id
        # future[2] : operation
        #
        _failed_hashes=0
        for future in futures:
            try:
                if GlobalStats.CALC_HASHES:
                    # Check to see if hashes match for real get ops
                    _hash = hashlib.sha1(future[0].result().content).hexdigest()
                    _matches = 'n/a'
                    if future[2] == 'get':
                        # Find matching PUT
                        for put in data_list:
                            if put[2] == _hash:
                                _matches = 'yes'
                        if _matches != 'yes':
                            _matches = 'no'
                            _failed_hashes += 1
                else:
                    _hash = ''
                    _matches = 'CALC_HASHES not enabled'

                if future[2] == 'get':
                    _tmp = [future[1],
                            future[2],
                            str(future[0].result()),
                            #str(future[0].result().text),
                            str(_hash),
                            _matches]
                else:
                    _tmp = [future[1],
                            future[2],
                            str(future[0].result()),
                            #str(future[0].result().text),
                            '',
                            _matches]

            except requests.RequestException as e:
                _tmp = [future[1],future[2],str(e)]

            _tab.append(_tmp)
        print tabulate(_tab, _header)
        print 'Elapsed time:', elapsed*1000, 'ms'
        print 'Failed hashes:', _failed_hashes
        print tabulate(data_list)
        return _failed_hashes

class Spec(object):
    """ Encapsulates the order in which operations should occur."""
    put = 0
    get = 0
    delete = 0
    io = 0
    rand_seed = 0
    order = ''
    node = ''

class Workload(object):
    """ Handles loading and creation of test workloads."""

    def __init__(self, pathToConfig):
        """ Reads in config file and sets Spec variables."""
        try:
            f = open(pathToConfig,'r')
        except EnvironmentError as e:
            print e
        else:
            with f:
                self.specs=[]
                j = json.load(f)
                for node in j['nodes']:
                    _spec = Spec()
                    _spec.put = node['put']
                    _spec.get = node['get']
                    _spec.delete = node['del']
                    _spec.rand_seed = node['rand_seed']
                    _spec.io = node['total_io']
                    _spec.order = node['order']
                    _spec.node = node['node']
                    self.specs.append(_spec)

                # Temporary measure
                self.spec = self.specs[0]

    def print_spec(self):
        """ Prints test workload spec."""
        for spec in self.specs:
            print 'Node', spec.node
            print 'PUTS:',spec.put
            print 'GETS:',spec.get
            print 'DELS:',spec.delete
            print 'ORDER:',spec.order
            print 'TOTAL IO:',spec.io
            print 'RAND SEED:',spec.rand_seed

    def get_op_from_order(self, y):
        """ Maps order string to operation strings."""
        if y == 'p':
            return 'put'
        elif y == 'g':
            return 'get'
        elif y == 'd':
            return 'delete'

    def generate(self):
        """ Generates list of commands to be executed in order."""
        self.load = []
        random.seed(self.spec.rand_seed)
        _order = 0
        _spec = Spec()
        _spec.__dict__.update(self.spec.__dict__)
        _repeat = False
        _list = list(_spec.order)
        _NUMBER_OF_OPS = len(self.spec.order)

        if self.spec.rand_seed != 0:
            random.shuffle(_list)
            _spec.order = ''.join(_list)
        for i in range(self.spec.io):
            # Add a command based on order.  order resets after three ops
            self.load.append(self.get_op_from_order(_spec.order[_order]))
            _spec.__dict__[self.get_op_from_order(_spec.order[_order])] -= 1

            # If there are still ops on current op type repeat this op type
            if _spec.__dict__[self.get_op_from_order(_spec.order[_order])] > 0:
                _repeat = True
            else:
                _repeat = False

            # If there are no ops left proceed to the next op and reset _spec
            if _repeat is False:
                _order = (_order + 1) % _NUMBER_OF_OPS
                if _order is _NUMBER_OF_OPS-1:
                    _spec.__dict__.update(self.spec.__dict__)
                    random.shuffle(_list)
                    _spec.order = ''.join(_list)


def main():
    """ Runs workload generator with JSon spec as first argument."""
    # Create a test volume
    try:
        print requests.put('http://localhost:8000/bucket1')
    except Exception as e:
        print 'Bucket create failed!'

    w = Workload(sys.argv[1])
    w.generate()
    w.print_spec()
    print w.load
    p = Processor(w.load)
    print tabulate(p.toHTTP())

    #try:
    #    print requests.delete('http://localhost:8000/bucket1')
    #except Exception as e:
    #    print 'Bucket delete failed!'

if __name__ == '__main__':
    main()
