from abstract_service import AbstractService
from fds.utils.node_state_converter import NodeStateConverter

class NodeService( AbstractService ):
    '''
    Created on Apr 23, 2015
    
    @author: nate
    '''

    def __init__(self, session):
        AbstractService.__init__(self, session)
        
    def list_nodes(self):
        '''
        Get a list of nodes known to the system.  This will include active attached nodes as well as nodes
        that are in the "DISCOVERED" State
        
        The nodes will also have all of their services and states within the returned object
        '''
        
        url = "{}{}".format( self.get_url_preamble(), "/api/config/services" )
        return self.rest_helper().get( self.session, url )
    
    def activate_node(self, node_id, node_state):
        '''
        This method will activate a node and put the services in the desired state.
        '''
        
        url = "{}{}{}".format( self.get_url_preamble(), "/api/config/services/", node_id)
        data = NodeStateConverter.to_json( node_state )
        return self.rest_helper().post( self.session, url, data )
    
    def deactivate_node(self, node_id, node_state):
        '''
        This method will deactivate the node and remember the state that is sent in
        '''
        
        url = "{}{}{}".format( self.get_url_preamble(), "/api/config/services/", node_id )
        data = NodeStateConverter.to_json( node_state )
        return self.rest_helper().put( self.session, url, data )
        