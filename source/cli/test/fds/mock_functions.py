from fds.model.volume import Volume
from fds.model.snapshot import Snapshot
from fds.model.node import Node
from fds.model.service import Service
from fds.model.domain import Domain
from fds.model.snapshot_policy import SnapshotPolicy

'''
Created on Apr 22, 2015

@author: nate
'''

responseOk = dict()
responseOk["status"] = "OK"

response200 = dict()
response200["status"] = 200

def writeJson( data ):
    return

def listVolumes():
    volume = Volume()
    vols = []

    vols.append( volume )
    
    return vols

def createVolume(volume):
    return volume

def editVolume(volume):
    return volume

def cloneFromTimelineTime( a_time, volume ):
    
    volume.id = 354
    volume.timeline_time = a_time
    return volume

def cloneFromSnapshotId( snapshot_id, volume):
    volume.id = 789
    return volume

def deleteVolume(name):
    return responseOk

def findVolumeById( an_id ):
    volume = Volume()
    volume.name = "VolumeName"
    volume.id = an_id
    
    return volume

def findVolumeByName( name ):
    volume = Volume()
    volume.name = name
    volume.id = 100
    
    return volume

def findVolumeBySnapId( an_id ):
    volume = Volume()
    volume.name = "SnapVol"
    volume.id = 300
    
    return volume

def createSnapshot( snapshot ):
    return responseOk

def listSnapshots( volumeName ):
    snapshot = Snapshot()
    snaps = []
    snaps.append( snapshot )
    return snaps

def listNodes():
    node = Node()
    node.name = "FakeNode"
    node.ip_v4_address = "10.12.14.15"
    node.id = "21ABC"
    node.state = "ACTIVE"
    
    node.services["AM"]  = [Service(a_type="FDSP_ACCESS_MGR",auto_name="AM")]
    node.services["DM"]  = [Service(a_type="FDSP_DATA_MGR",auto_name="DM")]
    node.services["PM"]  = [Service(a_type="FDSP_PLATFORM",auto_name="PM")]
    node.services["SM"]  = [Service(a_type="FDSP_STOR_MGR",auto_name="SM")]                
 
    nodes = [node]
    return nodes

def addNode( node_id, state ):
    return response200
    
def removeNode( node_id ):
    return response200

def stopNode( node_id ):
    return response200

def startNode( node_id ):
    return response200

def startService(node_id, service_id):
    return response200

def stopService(node_id, service_id):
    return response200

def removeService(node_id, service_id):
    return response200

def addService(node_id, service):
    return response200

def shutdownDomain( domain_name ):
    return responseOk

def startDomain( domain_name ):
    return responseOk

def listLocalDomains():
    
    domain = Domain()
    domains = []
    domains.append(domain)
    
    return domains

def createSnapshotPolicy( policy ):
    policy.id = 100
    return policy

def editSnapshotPolicy( policy ):
    return policy

def listSnapshotPolicies( volume_id=None ):
    policies = []
    policy = SnapshotPolicy()
    policies.append( policy )
    return policies

def attachPolicy( policy_id, volume_id ):
    return responseOk

def detachPolicy( policy_id, volume_id ):
    return responseOk

def deleteSnapshotPolicy( policy_id ):
    return responseOk 