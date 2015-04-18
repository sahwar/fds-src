import abstract_plugin

'''
Created on Apr 13, 2015

A plugin to return information about the global domain.  This will
set up both the parsing and the routing to the appropriate functions or
REST calls

@author: nate
'''
class GlobalDomainPlugin( abstract_plugin.AbstractPlugin):
    
    '''
    @see: AbstractPlugin
    '''
    def build_parser(self, parentParser, session): 
        
        self.__parser = parentParser.add_parser( "global_domain", help="Interact with the global domain" )
        self.__subparser = self.__parser.add_subparsers( help="The sub-commands that are available")
    
    '''
    @see: AbstractPlugin
    '''    
    def detect_shortcut(self, args):
        
        return None
