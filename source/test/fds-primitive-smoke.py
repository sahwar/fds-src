#!/usr/bin/env python
#
# Copyright 2014 by Formation Data Systems, Inc.
#
import os, sys
import inspect
import argparse
import subprocess
from multiprocessing import Process
import ServiceMgr
import ServiceConfig
import pdb
import time

###
# Fds test environment
#
class FdsEnv:
    def __init__(self, _root):
        self.env_cdir        = os.getcwd()
        self.srcdir          = ''
        self.env_root        = _root
        self.env_exit_on_err = True
        
        # assuming that this script is located in the test dir
        self.testdir  = os.path.dirname(
                            os.path.abspath(inspect.getfile(inspect.currentframe())))
        self.srcdir   = os.path.abspath(self.testdir + "/..")
        self.toolsdir = os.path.abspath(self.srcdir + "/tools")
        self.fdsctrl  = self.toolsdir + "/fds"

        if self.srcdir == "":
            print "Can't determine FDS root from the current dir ", self.env_cdir
            sys.exit(1)

    def shut_down(self):
         subprocess.call([self.fdsctrl, 'stop', '--fds-root', self.env_root])

    def cleanup(self):
        self.shut_down()
        subprocess.call([self.fdsctrl, 'clean', '--fds-root', self.env_root])
        subprocess.call([self.fdsctrl, 'clogs', '--fds-root', self.env_root])

    def getRoot(self):
        return self.env_root

###
# setup test environment, directories, etc
#
class FdsSetupNode:
    def __init__(self, fdsSrc, fds_data_path):
        try:
            os.mkdir(fds_data_path)
            os.mkdir(fds_data_path + '/hdd')
            os.mkdir(fds_data_path + '/ssd')
        except:
            pass
        subprocess.call(['cp', '-rf', fdsSrc + '/config/etc', fds_data_path])

###
# dataset and bucket information
#
class FdsDataSet:
    def __init__(self, bucket='abc', data_dir='', dest_dir='/tmp', file_ext='*'):
        self.ds_bucket   = bucket
        self.ds_data_dir = data_dir
        self.ds_file_ext = file_ext
        self.ds_dest_dir = dest_dir

        if not self.ds_data_dir or self.ds_data_dir == '':
            self.ds_data_dir = os.getcwd()

        if not self.ds_dest_dir or self.ds_dest_dir == '':
            self.ds_dest_dir = '/tmp'
        ret = subprocess.call(['mkdir', '-p', self.ds_dest_dir])

###
# global test stats
#
class FdsStats:
    def __init__(self):
        self.total_puts = 0
        self.total_gets = 0
        self.total_dels = 0

#########################################################################################
###
# put a directory and get it back
#
class CopyS3Dir:
    global_obj_prefix = 0
    def __init__(self, dataset):
        self.ds         = dataset
        self.verbose    = False
        self.bucket     = dataset.ds_bucket
        self.exit_on_err= True
        self.files_list = []
        self.cur_put    = 0
        self.cur_get    = 0
        self.perr_cnt   = 0
        self.gerr_cnt   = 0
        self.gerr_chk   = 0
        self.obj_prefix = str(CopyS3Dir.global_obj_prefix)
        CopyS3Dir.global_obj_prefix += 1
        self.bucket_url = 'http://localhost:8000/' + self.bucket

        os.chdir(self.ds.ds_data_dir)
        files = subprocess.Popen(
            ['find', '.', '-name', self.ds.ds_file_ext, '-type', 'f', '-print'],
            stdout=subprocess.PIPE
        ).stdout
        for rec in files:
            rec = rec.rstrip('\n')
            self.files_list.append(rec)
        if len(self.files_list) == 0:
            print "WARNING: Data directory is empty"

    def reset_test(self):
        self.cur_put    = 0
        self.cur_get    = 0

    def run_test(self, burst_cnt=0):
        self.create_bucket()
        total = len(self.files_list)
        if burst_cnt == 0 or burst_cnt > total:
            burst_cnt = total
        cnt = 0
        while cnt < total:
            self.put_test(burst_cnt)
            self.get_test(burst_cnt)
            cnt += burst_cnt

    def create_bucket(self):
        ret = subprocess.call(['curl', '-X' 'PUT', self.bucket_url])
        if ret != 0:
            print "Bucket create failed"
            sys.exit(1)
        subprocess.call(['sleep', '3'])

    def delete_bucket(self):
        ret = subprocess.call(['curl', '-X' 'DELETE', self.bucket_url])
        if ret != 0:
            print "Bucket delete failed"
            sys.exit(1)
        subprocess.call(['sleep', '3'])

    def print_debug(self, message="INFO: "):
        if self.verbose:
            print message

    def put_test(self, burst=0):
        global stat
        os.chdir(self.ds.ds_data_dir)
        cnt = 0
        err = 0
        if burst <= 0:
            burst = len(self.files_list)
        for put in self.files_list[self.cur_put:]:
            cnt = cnt + 1
            obj = self.obj_prefix + put.replace("/", "_")
            self.print_debug("curl PUT: " + put + " -> " + obj)
            ret = subprocess.call(['curl', '-X', 'PUT', '--data-binary',
                                   '@' + put, self.bucket_url + '/' + obj])
            if ret != 0:
                err = err + 1
                print "curl error PUT: " + put + ": " + obj
                if self.exit_on_err == True:
                    sys.exit(1)

            if (cnt % 10) == 0:
                print "Put ", cnt + self.cur_put , " objects... errors: [", err, "]"
            if cnt == burst:
                break

        print "Put ", cnt + self.cur_put, " objects... errors: [", err, "]"
        self.cur_put  += cnt
        self.perr_cnt += err
        stat.total_puts += cnt

    def get_test(self, burst=0):
        global stat
        os.chdir(self.ds.ds_data_dir)
        cnt = 0
        err = 0
        chk = 0
        if burst <= 0:
            burst = len(self.files_list)
        for get in self.files_list[self.cur_get:]:
            cnt = cnt + 1
            obj = self.obj_prefix + get.replace("/", "_");
            tmp = self.ds.ds_dest_dir + '/' + obj
            self.print_debug("curl GET: " + obj + " -> " + tmp)
            ret = subprocess.call(['curl', '-s' '1' ,
                                   self.bucket_url + '/' + obj, '-o', tmp])
            if ret != 0:
                err = err + 1
                print "curl error GET: " + get + ": " + obj + " -> " + tmp
                if self.exit_on_err == True:
                    sys.exit(1)

            ret = subprocess.call(['diff', get, tmp])
            if ret != 0:
                chk = chk + 1
                print "Get ", get, " -> ", tmp, ": ", cnt,  \
                      " objects... errors: [", err, "], verify failed [", chk, "]"
                if self.exit_on_err == True:
                    sys.exit(1)
            else:
                subprocess.call(['rm', tmp])
            if (cnt % 10) == 0:
                print "Get ", cnt + self.cur_get, \
                      " objects... errors: [", err, "], verify failed [", chk, "]"
            if cnt == burst:
                break

        print "Get ", cnt + self.cur_get, " objects... errors: [", err, \
              "], verify failed [", chk, "]"
        self.cur_get    += cnt
        self.gerr_cnt   += err
        self.gerr_chk   += chk
        stat.total_gets += cnt

    def exit_on_error(self):
        if self.perr_cnt != 0 or self.gerr_cnt != 0 or self.gerr_chk != 0:
            print "Test Failed: put errors: [", self.perr_cnt,  \
                  "], get errors: [", self.gerr_cnt,            \
                  "], verify failed [", self.err_chk, "]"
            sys.exit(1)

###
# interleave put and get
#
class CopyS3Dir_Pattern(CopyS3Dir):
    def run_test(self, write=1, read=1):
        self.create_bucket()
        total = len(self.files_list)
        if write == 0 or write > total:
            write = total

        if read == 0 or read > total:
            read = total

        wr_cnt = 0
        rd_cnt = 0
        while wr_cnt < total and rd_cnt < total:
            self.put_test(write)
            self.get_test(read)
            wr_cnt += write
            rd_cnt += read

        if wr_cnt < total:
            self.put_test()
        if rd_cnt < total:
            self.get_test()

###
# overwrite existing objects
#
class CopyS3Dir_Overwrite(CopyS3Dir):
    def run_test(self):
        CopyS3Dir.run_test(self)

        self.put_test()
        self.get_test()

    def put_test(self, burst=0):
        os.chdir(self.ds.ds_data_dir)
        cnt = 0
        err = 0
        if len(self.files_list) > 0:
            wr_file = self.files_list[0]
        for put in self.files_list:
            cnt = cnt + 1
            obj = self.obj_prefix + put.replace("/", "_")
            self.print_debug("curl PUT: " + put + " -> " + obj)
            ret = subprocess.call(['curl', '-X', 'PUT', '--data-binary',
                    '@' + wr_file, self.bucket_url + '/' + obj])
            if ret != 0:
                err = err + 1
                print "curl error PUT: " + put + ": " + obj
                if self.exit_on_err == True:
                    sys.exit(1)

            if (cnt % 10) == 0:
                print "Put ", cnt + self.cur_put , " objects... errors: [", err, "]"

        print "Put ", cnt + self.cur_put, " objects... errors: [", err, "]"
        self.cur_put  += cnt
        self.perr_cnt += err

    def get_test(self, burst=0):
        global stat
        os.chdir(self.ds.ds_data_dir)
        cnt = 0
        err = 0
        chk = 0
        if len(self.files_list):
            rd_file = self.files_list[0]
        for get in self.files_list:
            cnt = cnt + 1
            obj = self.obj_prefix + get.replace("/", "_");
            tmp = self.ds.ds_dest_dir + '/' + obj
            self.print_debug("curl GET: " + obj + " -> " + tmp)
            ret = subprocess.call(['curl', '-s' '1' ,
                                   self.bucket_url + '/' + obj, '-o', tmp])
            if ret != 0:
                err = err + 1
                print "curl error GET: " + get + ": " + obj + " -> " + tmp
                if self.exit_on_err == True:
                    sys.exit(1)

            ret = subprocess.call(['diff', rd_file, tmp])
            if ret != 0:
                chk = chk + 1
                print "Get ", get, " -> ", tmp, ": ", cnt,  \
                      " objects... errors: [", err, "], verify failed [", chk, "]"
                if self.exit_on_err == True:
                    sys.exit(1)
            else:
                subprocess.call(['rm', tmp])
            if (cnt % 10) == 0:
                print "Get ", cnt + self.cur_get, \
                      " objects... errors: [", err, "], verify failed [", chk, "]"
            if cnt == burst:
                break

        print "Get ", cnt + self.cur_get, " objects... errors: [", err, \
              "], verify failed [", chk, "]"
        self.cur_get    += cnt
        self.gerr_cnt   += err
        self.gerr_chk   += chk
        stat.total_gets += cnt

###
# put a directory, get, then delete
#
class DelS3Dir(CopyS3Dir):
    def __init__(self, dataset):
        CopyS3Dir.__init__(self, dataset)
        self.cur_del = 0

    def reset_test(self):
        CopyS3Dir.reset_test(self)
        self.cur_del = 0

    def run_test(self, burst_cnt=0):
        self.create_bucket()
        total = len(self.files_list)
        if burst_cnt == 0 or burst_cnt > total:
            burst_cnt = total
        cnt = 0
        while cnt < total:
            self.put_test(burst_cnt)
            self.get_test(burst_cnt)
            self.del_test(burst_cnt)
            cnt += burst_cnt

    def del_test(self, burst=0):
        global stat
        os.chdir(self.ds.ds_data_dir)
        cnt = 0
        err = 0
        if burst <= 0:
            burst = len(self.files_list)
        for delete in self.files_list[self.cur_del:]:
            cnt = cnt + 1
            obj = self.obj_prefix + delete.replace("/", "_")
            self.print_debug("curl DEL: " + delete + " -> " + obj)
            ret = subprocess.call(['curl', '-X', 'DELETE', self.bucket_url + '/' + obj])
            if ret != 0:
                err = err + 1
                print "curl error DEL: " + delete + ": " + obj
                if self.exit_on_err == True:
                    sys.exit(1)

            tmp = self.ds.ds_dest_dir + '/' + obj
            ret = subprocess.call(['curl', '-s' '1' ,
                                   self.bucket_url + '/' + obj, '-o', tmp])
            if ret != 0:
                err = err + 1
                print "curl error DEL-GET: " + get + ": " + obj + " -> " + tmp
                if self.exit_on_err == True:
                    sys.exit(1)

            if (cnt % 10) == 0:
                print "Del ", cnt + self.cur_del , " objects... errors: [", err, "]"
            if cnt == burst:
                break

        print "Del ", cnt + self.cur_del, " objects... errors: [", err, "]"
        self.cur_del    += cnt
        self.perr_cnt   += err
        stat.total_dels += cnt

#########################################################################################
###
# cluster bring up
#
def bringup_cluster(env, verbose, debug):
    env.cleanup()
    root1 = env.env_root
    print "\nSetting up private fds-root in " + root1 + '[1-4]'
    FdsSetupNode(env.srcdir, root1)
    
    os.chdir(env.srcdir + '/Build/linux-x86_64.debug/bin')

    print "\n\nStarting fds on ", root1
    subprocess.call([env.fdsctrl, 'start', '--fds-root', root1])
    os.chdir(env.srcdir)

###
# exit the test, shutdown if requested
#
def exit_test(env, shutdown):
    global stat
    print "Total PUTs: ", stat.total_puts
    print "Total GETs: ", stat.total_gets
    print "Total DELs: ", stat.total_dels
    print "Test Passed, cleaning up..."
    if shutdown:
        env.cleanup()
    sys.exit(0)
#########################################################################################
###
# pre-commit test
#
def pre_commit(volume_name, data_dir, res_dir):
    # basic PUTs and GETs of fds-src cpp files
    time.sleep(20)
    smoke_ds0 = FdsDataSet(volume_name, data_dir, res_dir, '*.cpp')

    smoke0 = CopyS3Dir(smoke_ds0)
    smoke0.run_test()
    smoke0.reset_test()
    smoke0.exit_on_error()

    smoke1 = DelS3Dir(smoke_ds0)
    smoke1.run_test()
    smoke1.reset_test()
    smoke1.exit_on_error()


###
# I/O stress test
#
def stress_io(volume_name, data_dir, res_dir):

    # seq write, then seq read
    smoke_ds1 = FdsDataSet(volume_name, data_dir, res_dir, '*')
    smoke1 = DelS3Dir(smoke_ds1)

    smoke1.run_test()
    smoke1.reset_test()

    smoke1.exit_on_error()

    # write from in burst of 10
    smoke_ds2 = FdsDataSet(volume_name, data_dir, res_dir, '*')
    smoke2 = DelS3Dir(smoke_ds2)
    smoke2.run_test(10)
    smoke2.exit_on_error()

    # write from in burst of 10-5 (W-R)
    smoke_ds3 = FdsDataSet(volume_name, data_dir, res_dir, '*')
    smoke3 = CopyS3Dir_Pattern(smoke_ds3)
    smoke3.run_test(10, 5)
    smoke3.exit_on_error()

    # write from in burst of 10-3 (W-R)
    smoke_ds4 = FdsDataSet(volume_name, data_dir, res_dir, '*')
    smoke4 = CopyS3Dir_Pattern(smoke_ds4)
    smoke4.run_test(10, 3)
    smoke4.exit_on_error()

    # write from in burst of 1-1 (W-R)
    smoke_ds5 = FdsDataSet(volume_name, data_dir, res_dir, '*')
    smoke5 = CopyS3Dir_Pattern(smoke_ds5)
    smoke5.run_test(1, 1)
    smoke5.exit_on_error()

###
# overwrite test
#
def blob_overwrite(volume_name, data_dir, res_dir):

    # seq write, then seq read
    smoke_ds1 = FdsDataSet(volume_name, data_dir, res_dir, '*')
    smoke1 = CopyS3Dir_Overwrite(smoke_ds1)
    smoke1.run_test()
    smoke1.exit_on_error()

###
# negative test
#
def neg_test(volume_name, data_dir, es_dir):
    print "Negative test..."
    print "WIN-267, curl PUT object with invalid filename crashed AM, expect non zero"
    print "WIN-235, list bucket on non-existing volume"

###
# helper function to start I/O
#
def start_io_helper(volume_name, data_set_dir, res_dir, loop):
    i = 0
    while i < loop or loop == 0:
        # basic PUTs and GETs of fds-src cpp files
        pre_commit(volume_name, data_set_dir, res_dir)

        # stress I/O
        stress_io(volume_name, data_set_dir, res_dir)

        # blob over-write
        blob_overwrite(volume_name, data_set_dir, res_dir)
        i += 1

###
#  spawn process for I/O
#
def start_io_helper_thrd(volume_name, data_set_dir, res_dir, loop):
    sys.stdout = open('/tmp/smoke_io-' + str(os.getpid()) + ".out", "w")
    start_io_helper(volume_name, data_set_dir, res_dir, loop)

###
# main I/O function
#
def start_io_main(volume_name, data_set_dir, res_dir, async, loop):
    if async == True:
        startIOProcess = Process(target=start_io_helper_thrd,
                                 args=(volume_name, data_set_dir, res_dir, loop))
        startIOProcess.start()
    else:
        start_io_helper(volume_name, data_set_dir, res_dir, loop)
        startIOProcess = None
    return startIOProcess

###
# wait for I/O completion
#
def wait_io_done(p):
    if p != None:
        while p.is_alive():
            print "Waiting for process to complete: "
            p.join(60)
        if p.exitcode != 0:
            print "Process terminated with Failure: "
            sys.exit(p.exitcode)
        print "Async Process terminated: "
    print "Sync Process terminated: "

#########################################################################################

global stat
stat = FdsStats()
data_set_dir = None

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Start FDS Processes...')
    parser.add_argument('--cfg_file', default='./test/smoke_test.cfg',
                        help='Set FDS test cfg')
    parser.add_argument('--up', default='true',
                        help='Bringup system [true]')
    parser.add_argument('--down', default='true',
                        help ='Shutdown/cleanup system after passed test run [true]')
    parser.add_argument('--smoke_test', default='false',
                        help='Run full smoke test [false]')
    parser.add_argument('--vol_prefix', default='smoke',
                        help='prefix for bucket name')
    parser.add_argument('--data_set', default='/smoke_test',
                        help='Smoke test dataset [/smoke_test]')
    parser.add_argument('--thread', default=4,
                        help='Number of I/O threads')
    parser.add_argument('--async', default='true',
                        help ='Run I/O Async [true]')
    parser.add_argument('--verbose', default='false',
                        help ='Print verbose [false]')
    parser.add_argument('--debug', default='false',
                        help ='pdb debug on [false]')
    args = parser.parse_args()

    cfgFile      = args.cfg_file
    smokeTest    = args.smoke_test
    vol_prefix   = args.vol_prefix
    data_set_dir = args.data_set
    verbose      = args.verbose
    debug        = args.debug
    start_sys    = args.up
    thread_cnt   = args.thread
    if args.async == 'true':
        async_io = True
    else:
        async_io = False
    loop = 1

    if args.down == 'true':
        shutdown = True
    else:
        shutdown = False

    #    3) pass in IP address for AM, currently its localhost
    #    5) pass in the work-load that you want to run, default to all.
    #       (CopyS3Dir, DelS3Dir, etc)

    ###
    # load the configuration file and build test environment
    #
    env = FdsEnv('/fds/node1')

    ###
    # bring up the cluster
    #
    if start_sys == 'true':
        bringup_cluster(env, verbose, debug)
        time.sleep(2)

    if args.smoke_test == 'false':
        pre_commit(vol_prefix + '_volume1', env.srcdir, '/tmp/pre_commit')
        exit_test(env, shutdown)

    ###
    # start IO
    #
    thr = 0
    assert(thread_cnt >= 0)
    process_list = []
    while thr < thread_cnt:
        p1 = start_io_main(vol_prefix + 'volume' + str(thr), data_set_dir,
                           '/tmp/res' + str(thr), async_io, loop)
        process_list.append(p1)
        thr += 1

    ###
    # bring up more node/delete node test
    #

    ###
    # wait for async I/O to complete
    #
    for p1 in process_list:
        wait_io_done(p1)
    exit_test(env, shutdown)
