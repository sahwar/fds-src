from fds.model.qos_preset import QosPreset
from fds.model.timeline_preset import TimelinePreset
from fds.utils.snapshot_policy_converter import SnapshotPolicyConverter
import json

class PresetConverter(object):
    '''
    Created on May 6, 2015
    
    @author: nate
    '''

    @staticmethod
    def build_qos_preset_from_json( jsonString ):
        '''
        Take a JSON string and build a QoS preset from it
        '''
        
        qos = QosPreset()
        qos.id = jsonString.pop("uuid", qos.id)
        qos.name = jsonString.pop("name", "UNKNOWN")
        qos.iops_guarantee = jsonString.pop("sla", qos.iops_guarantee)
        qos.iops_limit = jsonString.pop("limit", qos.iops_limit)
        qos.priority = jsonString.pop("priority", qos.priority)
        
        return qos
    
    @staticmethod
    def build_timeline_from_json( jsonString ):
        '''
        Take a JSON string for a timeline preset and turn it into an object
        '''
        
        timeline = TimelinePreset()
        timeline.id = jsonString.pop("uuid", timeline.id)
        timeline.name = jsonString.pop("name", "UNKNOWN")
        timeline.continuous_protection = jsonString.pop("commitLogRetention", timeline.continuous_protection)
        
        #If I don't do this, the list is still populated with previous list... no idea why
        timeline.policies = list()
        
        policies = jsonString.pop("policies", [])
        
        for policy in policies:
            timeline.policies.append( SnapshotPolicyConverter.build_snapshot_policy_from_json( policy ))
            
        return timeline
    
    @staticmethod
    def qos_to_json( preset ):
        '''
        Turn a qos preset into JSON
        '''
        d = dict()
        
        d["uuid"] = preset.id
        d["name"] = preset.name
        d["priority"] = preset.priority
        d["sla"] = preset.iops_guarantee
        d["limit"] = preset.iops_limit
        
        j_str = json.dumps( d )
        
        return j_str
    
    @staticmethod
    def timeline_to_json(preset):
        '''
        Turn a timeline policy preset into a JSON string
        '''