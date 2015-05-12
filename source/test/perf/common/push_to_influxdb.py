#!/usr/bin/python

import os,re,sys
from influxdb import client as influxdb

def is_float(x):
    try:
        float(x)
    except (ValueError, TypeError):
        return False
    else:
        return True

def is_int(x):
    try:
        int(x)
    except (ValueError, TypeError):
        return False
    else:
        return True

def dyn_cast(val):
    if is_float(val):
        return float(val)
    elif is_int(val):
        return int(val)
    else:
        return val

class InfluxDb:
    def __init__(self, config):
        self.ip = config["ip"]
        self.port = config["port"]
        self.user = config["user"]
        self.password = config["password"]
        self.dbname = config["db"]
        self.db = influxdb.InfluxDBClient(self.ip, self.port, self.user, self.password, self.dbname)

    def write_records(self, series, records):
        cols, vals = [list(x) for x in  zip(*records)]
        vals = [dyn_cast(x) for x in vals]
        data = [
          {
           "name" : series,
            "columns" : cols,
            "points" : [
              vals 
            ]
          }
        ]
        self.db.write_points(data)

influx_db_config = {
    "ip" : "metrics.formationds.com",
    "port" : 8086,
    "user" : "perf",
    "password" : "perf",
    "db" : "perf",
}

if __name__ == "__main__":
    series = sys.argv[1]
    filein = sys.argv[2]
    with open(filein, "r") as f:
        records = [ [re.sub(' ','',y) for y in x.split('=')] for x in filter(lambda x : x != "", re.split("[\n;,]+", f.read()))]
            
    print "InfluxDB Config:", influx_db_config
    db = InfluxDb(influx_db_config)
    db.write_records(series, records)

