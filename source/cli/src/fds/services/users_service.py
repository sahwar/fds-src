from abstract_service import AbstractService

class UsersService( AbstractService ):
    '''
    Created on Apr 23, 2015
    
    @author: nate
    '''
    
    def __init__(self, session):
        AbstractService.__init__(self, session)
        
    def list_users(self):
        ''' 
        get a list of all the users you're allowed to see
        '''
        url = "{}{}".format( self.get_url_preamble(), "/api/system/users" )
        return self.rest_helper().get( self.session, url )
    
    def create_user(self, login, password ):
        '''
        create a new user
        '''
        
        url = "{}{}{}{}{}".format( self.get_url_preamble(), "/api/system/users/", login, "/", password )
        return self.rest_helper().post( self.session, url )
    
    def change_password(self, user_id, password ):
        '''
        Change a users password
        '''
        
        url = "{}{}{}{}{}".format( self.get_url_preamble(), "/api/system/users/", user_id, "/", password )
        return self.rest_helper().put( self.session, url )
    
    def reissue_user_token(self, user_id):
        '''
        Re-issue a users token.  This will effectively cause the effected user to have to revalidate themselves
        '''
        url = "{}{}{}".format( self.get_url_preamble(), "/api/system/token/", user_id )
        return self.rest_helper().post( self.session, url )
    
    def get_user_token(self, user_id ):
        '''
        Retrieve the token for a specified user
        '''
        
        url = "{}{}{}".format( self.get_url_preamble(), "/api/system/token/", user_id )
        return self.rest_helper().get( self.session, url )
    
    def who_am_i(self):
        '''
        Retrieve the user associated with this session/token
        '''
        
        url = "{}{}".format( self.get_url_preamble(), "/api/auth/currentUser" )
        return self.rest_helper().get( self.session, url )