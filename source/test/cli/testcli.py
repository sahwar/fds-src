#!/usr/bin/python
import sys
import os
import logging
sys.path.append('../../test')
sys.path.append('../fdslib/pyfdsp')
sys.path.append('../fdslib')

from argh import *
import cmd
from tabulate import tabulate


import time
from multiprocessing import Process
import json

from SvcHandle import *
import struct
import socket
from FDS_ProtocolInterface.ttypes import *
from snapshot.ttypes import *

from fdslib import process
from fdslib import IOGen
from fdslib import thrift_json

ip='127.0.0.1'
port=7020
svc_map = SvcMap(ip, port)
client=svc_map.omConfig()

volumeid=client.getVolumeId("smoke_volume0")
print volumeid
print client.getVolumeName(volumeid)


