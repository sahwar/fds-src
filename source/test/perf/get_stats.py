#!/usr/bin/python
import os,re,sys
from optparse import OptionParser
import numpy as np
import matplotlib.pyplot as plt
import time
from parse_cntrs import Counters

g_pidmap = {}

g_drives = [ "sdg ",
"sdc",
"sdb", 
"sdd", 
"sde", 
"sdh", 
"sda", 
"sdf", 
"nb0", 
"nb16" ]

nodes = {
    #"node1" : "10.1.10.16",
    #"node2" : "10.1.10.17",
    #"node3" : "10.1.10.18",
    "tiefighter" : "10.1.10.102",
}

def plot_series(series, title = None, ylabel = None, xlabel = "Time [ms]"):
    data = np.asarray(series)
    plt.plot(data)
    if title:
        plt.title(title)
    if ylabel:
        plt.ylabel(ylabel)
    if xlabel:
        plt.xlabel(xlabel)
    plt.show()

def get_avglat():
    outfiles = get_outfiles()
    lats = []
    for of in outfiles:        
        if options.ab_enable:
            cmd = "grep 'Time per request' %s |head -1| awk '{print $4}'" % (of)
        elif options.block_enable:
            return None
        else:
            cmd = "cat %s | grep Summary |awk '{print $17}'" % (of)
        lat = float(os.popen(cmd).read().rstrip('\n'))
        lats.append(lat)
    return sum(lats)/len(lats)


def get_outfiles():
    outfiles = ["%s/test.out" % (options.directory)]
    if not os.path.exists(outfiles[0]):
        outfiles.pop()
        i = 1
        candidate = "%s/test.out.%d" % (options.directory, i)
        while os.path.exists(candidate) == True: 
            outfiles.append(candidate)
            i += 1
            candidate = "%s/test.out.%d" % (options.directory, i)
    return outfiles


def get_avgth():
    outfiles = get_outfiles()
    ths = []
    for of in outfiles:        
        if options.ab_enable:
            cmd = "grep 'Requests per second' %s | awk '{print $4}'" % (of)
        elif options.block_enable:
            cmd = "cat %s | grep 'Experiment result' | awk '{print $11}'" % (of)
        else:        
            cmd = "cat %s | grep Summary |awk '{print $15}'" % (of)
        th = float(os.popen(cmd).read().rstrip('\n'))
        ths.append(th)
    return sum(ths)/len(ths)    

def exist_exp():
    if os.path.exists("%s/test.out" % (options.directory)):
        return True
    else:
        return False

def compute_pidmap(node):
    file = open("%s/pidmap" % (options.directory),"r")
    for l in file:
        m = re.search("(am|om|dm|sm)\s*:\s*(\d+)",l)
        if m is not None:
            agent = m.group(1)
            pid = int(m.group(2))
            if not node in g_pidmap:
                g_pidmap[node] = {}
            g_pidmap[node][agent] = pid
                
def convert_mb(x):
    if x[len(x) - 1] == "m":
        return float(x.rstrip('m')) * 1024
    elif x[len(x) - 1] == "g":
        return float(x.rstrip('g')) * 1024 * 1024
    else:
        return float(x)    

def get_mem(node, agent, pid):
    cmd = "egrep '^[ ]*%d' %s/%s/top/stdout | awk '{print $6}'" % (pid, options.directory, node)
    series = os.popen(cmd).read().split()
    return [convert_mb(x)/1024 for x in series]

def get_cpu(node, agent, pid):
    cmd = "egrep '^[ ]*%d' %s/%s/top/stdout | awk '{print $9}'" % (pid, options.directory, node)
    series = os.popen(cmd).read().split()
    return [float(x) for x in series]

def get_idle(node):
    cmd = "cat %s/%s/top/stdout |grep Cpu | awk '{print $8}'" % (options.directory, node)
    series = os.popen(cmd).read().split()
    return [float(x) for x in series]

def ma(v, w):
    _ma = []
    for i in range(w,len(v)):
        _ma.append(1.0*sum(v[i-w:i])/w)
    return _ma

def getmax(series, window = 5, skipbeg = 2, skipend = 2):
    _s = series[skipbeg:len(series)-skipend]
    if len(_s) == 0:
        return None
    _ma = ma(_s, window)
    if len(_ma) == 0:
        return None
    return max(_ma)

def getavg(series, window = 5, skipbeg = 2, skipend = 2):
    _s = series[skipbeg:len(series)-skipend]
    if len(_s) == 0:
        return None
    _ma = ma(_s, window)
    if len(_ma) == 0:
        return None
    return sum(_ma)/len(_ma)

#g_drives
def get_iops(node):
    iops_map = {}
    iops_series = []
    _file = open("%s/%s/ios/stdout" % (options.directory, node),"r")
    for l in _file:
        tokens = l.split()
        if len(tokens) >= 2:
            name = tokens[0]
            value = tokens[1]
            if name in g_drives:
                if name in iops_map:
                    iops_map[name].append(float(value))
                else:
                    iops_map[name] = [float(value)]
    _file.close()
    all_values = []
    for n,values in iops_map.items():
        all_values.append(values)
    #for e in zip(*all_values):
    #    iops_series.append(sum(e)/len(e))
    iops_series = [sum(e) for e in zip(*all_values)]
    return iops_series

def get_test_parms():
    #results_2014-08-05_nvols\:1.threads\:1.type\:GET.nfiles\:10000.test_type\:tgen.fsize\:4096.nreqs\:100000.test
    m = re.match("(\w+)_(\d+-)_nvols:(\d+).threads:(\d+).type:(\w+).nfiles:(\d+).test_type:(\w+).fsize:(\d+).nreqs:(\d+).test", options.directory)


def main():
    compute_pidmap("tiefighter")
    # counters = Counters()
    # counters.parse()
    # counters.get_cntr()
    print options.name,",",
    time.sleep(1)
    print "th,", get_avgth(),",",
    print "lat,", get_avglat(),",",
    for node in nodes.keys():
        for agent,pid in g_pidmap[node].items():
            print "Agent,",agent,",",
            if options.plot_enable:
                plot_series(get_mem(node, agent, pid), "Memory - " + agent + " - " + options.name, "megabatyes")
                plot_series(get_cpu(node, agent, pid), "CPU - " + agent + " - " + options.name, "CPU utilization [%]")
            else:
                print "maxmem,",getmax(get_mem(node, agent, pid)),",",
                print "maxcpu,",getmax(get_cpu(node, agent, pid)),",",
        if options.plot_enable:
            plot_series(get_iops(node), "IOPS - " + options.name, "IOPS (disk)")
            plot_series(get_idle(node), "Avg Idle - " + options.name, "Idle CPU Time [%]")
        else:
            print "maxiops,",getmax(get_iops(node)),",",
            print "avgidle,",getavg(get_idle(node)),",",
    print "" 

if __name__ == "__main__":
    parser = OptionParser()
    parser.add_option("-d", "--directory", dest = "directory", help = "Directory")
    parser.add_option("-n", "--name", dest = "name", help = "Name")
    #parser.add_option("-c", "--column", dest = "column", help = "Column")
    parser.add_option("-p", "--plot-enable", dest = "plot_enable", action = "store_true", default = False, help = "Plot graphs")
    parser.add_option("-A", "--ab-enable", dest = "ab_enable", action = "store_true", default = False, help = "AB mode")
    parser.add_option("-B", "--block-enable", dest = "block_enable", action = "store_true", default = False, help = "Block mode")
    (options, args) = parser.parse_args()
    main()
