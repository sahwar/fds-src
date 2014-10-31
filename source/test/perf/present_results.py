#!/usr/bin/python

import os,re,sys
import tabulate
import dataset
import operator 
from influxdb import client as influxdb
import numpy
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

class FigureUtils:
    @staticmethod
    def new_figure():
        return plt.figure()
     
    @staticmethod
    def plot_series(series, fig, _label = ""):
        plt.figure(fig.number)
        data = numpy.asarray(series)
        plt.plot(data, label=_label)

    @staticmethod
    def plot_scatter(point, fig):
        plt.figure(fig.number)
        #data = numpy.asarray(series)
        # plt.scatter(x, y, s=area, c=colors, alpha=0.5)
        plt.scatter(point[0], point[1])
    
    @staticmethod
    def save_figure(filename, fig, title = None, ylabel = None, xlabel = None):
        plt.figure(fig.number)
        if title:
            plt.title(title)
        if ylabel:
            plt.ylabel(ylabel)
        if xlabel:
            plt.xlabel(xlabel)
        plt.ylim(ymin = 0)
        #plt.legend()
        ax = plt.subplot(111)
        box = ax.get_position()
        #ax.set_position([box.x0, box.y0, box.width * 0.8, box.height])
        ax.set_position([0.1, 0.1, 0.5, 0.8])
        ax.legend(loc='center left', bbox_to_anchor=(1, 0.5))
        ax.set_aspect("auto")
        plt.savefig(filename)


def generate_scaling_iops(conns, iops):
    filename = "scaling_iops.png"
    title = "IOPs"
    xlabel = "Connections"
    ylabel = "IOPs"
    plt.figure()
    plt.plot(conns, iops)
    plt.xlabel(xlabel)
    plt.ylabel(ylabel)
    plt.title(title)
    plt.savefig(filename)
    return filename

def generate_scaling_lat(conns, lat):
    filename = "scaling_lat.png"
    title = "Latency"
    xlabel = "Connections"
    ylabel = "Latency [ms]"
    plt.figure()
    plt.plot(conns, lat)
    plt.xlabel(xlabel)
    plt.ylabel(ylabel)
    plt.title(title)
    plt.savefig(filename)
    return filename

def mail_success(images):
    # githead = get_githead(directory)
    # gitbranch = get_gitbranch(directory)
    # hostname = get_hostname()
#    preamble = "\
#Performance regression\n\
#Regression type: %s\n\
#Number of nodes: 1\n\
#Branch: %s\n\
#hostname: %s\n\
#git version: %s\
#" % (regr_type, branch, hostname, githead)
#    epilogue = "\
#Data uploaded to Influxdb @ 10.1.10.222:8083 - login guest - guest - database: perf\n\
#Confluence: https://formationds.atlassian.net/wiki/display/ENG/InfluxDB+for+Performance"
    preamble = "START"
    epilogue = "END"
    text = "Performance Regressions"
    with open(".mail", "w") as _f:
        _f.write(preamble + "\n")
        _f.write("\n")
        _f.write(text + "\n")
        _f.write("\n")
        _f.write(epilogue + "\n")
    # mail_address = "engineering@formationds.com"
    
    mail_address = "matteo@formationds.com"
    attachments = ""
    for e in images:
        attachments += "-a %s " % e
    cmd = "cat .mail | mail -s 'performance regression' %s %s" % (attachments, mail_address)
    print cmd
    os.system(cmd)


if __name__ == "__main__":
    test_db = sys.argv[1]
    # directory = sys.argv[1]
    # test_id = int(sys.argv[2])
    # mode = sys.argv[3]
    # branch = sys.argv[4]
 
    db = dataset.connect('sqlite:///%s' % test_db)
    experiments = db["experiments"].all()
    #for e in experiments:
    #    print e
    experiments = [ x for x in experiments]
    experiemnts = filter(lambda x : x["type"] == "GET", experiments)
    experiments = sorted(experiemnts, key = lambda k : int(k["threads"]))
    iops = [x["th"] for x in experiments]    
    lat = [x["lat"] for x in experiments]    
    #print [x["nreqs"] for x in experiments]    
    conns = [x["threads"] for x in experiments]    
    #print [x["type"] for x in experiments]    
    # iops = [x["am:am_get_obj_req:count"] for x in experiments]    
    
    images = [] 
    images.append(generate_scaling_iops(conns, iops))
    images.append(generate_scaling_lat(conns, lat))
    mail_success(images)
    

