
class Service(object):
    '''
    Created on Apr 28, 2015
    
    @author: nate
    '''

    def __init__(self, status="UNKNOWN", port=0, auto_name="UNKNOWN", a_type="FDSP_PLATFORM", an_id=-1):
        self.__status = status
        self.__port = port
        self.__auto_name = auto_name
        self.__id = an_id
        self.__type = a_type
        return
    
    @property
    def auto_name(self):
        return self.__auto_name
    
    @auto_name.setter
    def auto_name(self, a_name):
        
        if ( a_name not in ("AM", "DM", "SM", "PM", "OM") ):
            return
        
        self.__auto_name = a_name
        
    @property
    def port(self):
        return self.__port
    
    @port.setter
    def port(self, port):
        self.__port = port
        
    @property
    def status(self):
        return self.__status
    
    @status.setter
    def status(self, status):
        
        if ( status not in ("ACTIVE", "INACTIVE", "INVALID", "ERROR")):
            return
        
        self.__status = status
      
    @property  
    def type(self):
        return self.__type
    
    @type.setter
    def type(self, a_type):
        
        if ( a_type not in ("FDSP_ACCESS_MGR", "FDSP_STOR_MGR", "FDSP_ORCH_MGR", "FDSP_PLATFORM", "FDSP_DATA_MGR")):
            return
        
        self.__type = a_type
        
    @property
    def id(self):
        return self.__id
    
    @id.setter
    def id(self, an_id):
        self.__id = an_id