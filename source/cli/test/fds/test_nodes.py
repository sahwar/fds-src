from base_cli_test import BaseCliTest
import mock_functions
from mock import patch
from fds.utils.node_converter import NodeConverter

def filter_for_discovered(nodes):
    return nodes

def filter_for_added(nodes):
    return nodes

def writeJson(data):
    return

class TestNodes( BaseCliTest ):
    '''
    Created on Apr 27, 2015
    
    @author: nate
    '''
    
    @patch( "fds.plugins.node_plugin.NodePlugin.filter_for_added_nodes", side_effect=filter_for_added )
    @patch( "fds.plugins.node_plugin.NodePlugin.filter_for_discovered_nodes", side_effect=filter_for_discovered )
    @patch( "fds.services.node_service.NodeService.list_nodes", side_effect=mock_functions.listNodes)
    def test_list_nodes(self, mockList, mockDisc, mockAdd):
        '''
        Test that list nodes gets called correctly with the args
        '''
        
        args = ["node", "list"]
        self.callMessageFormatter(args)
        self.cli.run(args)
        
        assert mockList.call_count == 1
        assert mockDisc.call_count == 0
        assert mockAdd.call_count == 0
        
        args = ["node", "list", "-state=discovered"]
        self.callMessageFormatter(args)
        self.cli.run( args )
        
        assert mockList.call_count == 2
        assert mockDisc.call_count == 1
        assert mockAdd.call_count == 0
        
        args = ["node", "list", "-state=added"]
        self.callMessageFormatter(args)
        self.cli.run( args )
        
        assert mockList.call_count == 3
        assert mockDisc.call_count == 1
        assert mockAdd.call_count == 1
        
    @patch( "fds.services.node_service.NodeService.add_node", side_effect=mock_functions.addNode)
    @patch( "fds.services.node_service.NodeService.list_nodes", side_effect=mock_functions.listNodes)
    def test_add_node(self, mockList, mockAdd):
        '''
        Test that activate is called with all three services set to true
        '''
        
        #failure call first
        args = ["node", "add"]
        self.callMessageFormatter(args)
        self.cli.run(args)
         
        assert mockAdd.call_count == 0
        
        args = ["node", "add", "-node_ids", "1", "2", "3"]
        self.callMessageFormatter(args)
        self.cli.run(args)
        
        assert mockAdd.call_count == 3
        
        first = mockAdd.call_args_list[0]
        second = mockAdd.call_args_list[1]
        third = mockAdd.call_args_list[2]
        
        assert first[0][0] == "1"
        assert second[0][0] == "2"
        assert third[0][0] == "3"
        
        assert first[0][1].am == second[0][1].am == third[0][1].am
        assert first[0][1].dm == second[0][1].dm == third[0][1].dm
        assert first[0][1].sm == second[0][1].sm == third[0][1].sm
        
    @patch( "fds.services.node_service.NodeService.remove_node", side_effect=mock_functions.removeNode)
    @patch( "fds.services.node_service.NodeService.list_nodes", side_effect=mock_functions.listNodes)
    def test_remove_node(self, mockList, mockRemove):
        '''
        Test that activate is called with all three services set to true
        '''
        
        #failure call first
        args = ["node", "add"]
        self.callMessageFormatter(args)
        self.cli.run(args)
         
        assert mockRemove.call_count == 0
        
        args = ["node", "remove", "-node_id=1"]
        self.callMessageFormatter(args)
        self.cli.run(args)
        
        assert mockRemove.call_count == 1
        
        first = mockRemove.call_args_list[0]
        
        assert first[0][0] == "1"
          
    
    @patch( "fds.services.node_service.NodeService.list_nodes", side_effect=mock_functions.listNodes) 
    @patch( "fds.services.response_writer.ResponseWriter.writeJson", side_effect=writeJson)
    def test_list_services(self, mockWriteJson, mockList):
        '''
        Test the list services functionality.  This one is a little more intense because
        there isn't a list services ability in the REST endpoints yet, so the plugin function
        really just plays games with the results from list nodes in order to filter the results.
        
        Therefore, we need to inspect the objects sent to the writer directly.
        '''
        
        #all services
        args = ["node", "list_services", "-format=json", "-node_id=21ABC"]
    
        self.callMessageFormatter(args)
        self.cli.run(args)
        
        assert mockWriteJson.call_count == 1
        
        j_node = mockWriteJson.call_args[0][0]
        node = NodeConverter.build_node_from_json( j_node[0] )
        
        assert len( node.services["AM"] ) == 1
        assert len( node.services["PM"] ) == 1
        assert len( node.services["SM"] ) == 1
        assert len( node.services["DM"] ) == 1
    
    @patch( "fds.services.node_service.NodeService.stop_node", side_effect=mock_functions.stopNode)
    @patch( "fds.services.node_service.NodeService.list_nodes", side_effect=mock_functions.listNodes)
    def test_stop_node(self, mockList, mockStop):
        '''
        Test to see that the stop node functionality makes the right call
        '''
        
        #no node ID
        args = ["node", "shutdown"]
        
        self.callMessageFormatter(args)
        self.cli.run( args )
        
        assert mockStop.call_count == 0
        
        args.append( "-node_id=21ABC" )
        self.callMessageFormatter(args)
        self.cli.run( args )
        
        assert mockStop.call_count == 1
        
        node_id = mockStop.call_args[0][0]
        
        assert node_id == "21ABC"
        assert mockList.call_count == 1
        
    @patch( "fds.services.node_service.NodeService.start_node", side_effect=mock_functions.startNode)
    @patch( "fds.services.node_service.NodeService.list_nodes", side_effect=mock_functions.listNodes)
    def test_start_node(self, mockList, mockStart):
        '''
        Test to see that the stop node functionality makes the right call
        '''
        
        #no node ID
        args = ["node", "start"]
        
        self.callMessageFormatter(args)
        self.cli.run( args )
        
        assert mockStart.call_count == 0
        
        args.append( "-node_id=21ABC" )
        self.callMessageFormatter(args)
        self.cli.run( args )
        
        assert mockStart.call_count == 1
        
        node_id = mockStart.call_args[0][0]
        
        assert node_id == "21ABC"
        assert mockList.call_count == 1        