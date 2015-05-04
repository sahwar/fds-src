import unittest
from mock_auth import MockFdsAuth
from fds import fdscli

class BaseCliTest( unittest.TestCase ):
    '''
    Created on Apr 23, 2015
    
    provides basic functionality to the cli tests so we don't have to do boilerplate
    repeatedly
    
    @author: nate
    '''
    
    @classmethod
    def setUpClass(self):
        print "Setting up the test..."
        
        auth = MockFdsAuth()
        auth.login()
        self.__auth = auth
        self.__cli = fdscli.FDSShell( auth ) 
        print "Done with setup\n\n"
        
    def callMessageFormatter(self, args):
        
        message = ""
        
        for arg in args:
            message += arg + " "
            
        print "Making call: " + message
        
    @property
    def cli(self):
        return self.__cli
    
    @property
    def auth(self):
        return self.__auth
        
if __name__ == '__main__':
    unittest.main()        