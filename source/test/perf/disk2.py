#!/usr/bin/python
import os,re,sys
import numpy as np
import matplotlib.pyplot as plt
print "...."
#disks=["sda1", "sdb3", "sdc3", "sdd3", "sde3", "sdf3", "sdg1", "sdh1"]
disks=[
"sda",
"sdb",
"sdc",
"sdd",
"sde",
"sdf",
"sdg",
"sdh",
"sdi",
"sdj",
"sdk",
#"sdl",
#"sdm",
]
def plot_series(series, title = None, ylabel = None, xlabel = "Time [s]", legend_label = None):
    #plt.figure()
    plt.plot(series, label=legend_label)
    if title:
        plt.title(title)
    if ylabel:
        plt.ylabel(ylabel)
    if xlabel:
        plt.xlabel(xlabel)
    #plt.show()
    #plt.savefig('/tmp/%s.png' % title)


#cols = {'iops' : 2, 'kbreads' : 3, 'kbwrites' : 4}
cols = {'w_s' : 5, 'wkb_s' : 7 ,'avgrqsz' : 8, 'avgqusz' : 9, 'await' : 10, 
        'r_s' : 4, 'rkb_s' : 6, 'await_r' : 10, 'await_w' : 11, 'util' : 13}

iops = {}
for d in disks:
    cmd="grep %s %s|awk '{print $%d}'" % (d,sys.argv[1], cols[sys.argv[2]])
    #cmd="grep %s %s|awk '{print $%d}'" % (d,sys.argv[1], int(sys.argv[2]))
    try:
        iop = [float(x) for x in os.popen(cmd).read().rstrip('\n').split()]
    except:
        iop = 0
    iops[d] = iop

total = [0,]*len(iops["sda"])
ndisks = [0,]*len(iops["sda"])
for d in disks:
    print d,",",
    #for e in iops[d]:
    #    print e,",",
    for i in range(len(iops[d])):
        total[i] += iops[d][i]
    for i in range(len(iops[d])):
        if iops[d][i] > 0:
            ndisks[i] += 1
    array = np.asarray(iops[d])
    #plot_series(array, sys.argv[2], legend_label = d)
    print np.mean(array),",",
    print np.min(array),",",
    print np.max(array),",",
    print np.median(array),",",
    print 100*float(np.count_nonzero(array))/array.size,",",
    print ""
plt.legend() 
#plt.savefig('/tmp/%s-%s.png' % (sys.argv[3], sys.argv[2]))
#plt.show()

print total 
plot_series(total, sys.argv[2])
plt.savefig('/tmp/total-%s.png' % sys.argv[3])
#print ndisks
#plot_series(ndisks, sys.argv[2])
#plt.savefig('/tmp/ndisks-%s.png' % sys.argv[3])
