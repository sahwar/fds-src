package com.formationds.om.webkit;

import com.formationds.om.webkit.rest.HttpAuthenticator;
import com.formationds.om.webkit.rest.HttpErrorHandler;
import com.formationds.security.AuthenticationToken;
import com.formationds.security.Authenticator;
import com.formationds.security.Authorizer;
import com.formationds.web.toolkit.HttpMethod;
import com.formationds.web.toolkit.JsonResource;
import com.formationds.web.toolkit.RequestHandler;
import com.formationds.web.toolkit.WebApp;
import org.json.JSONObject;
import org.apache.logging.log4j.Logger;
import org.apache.logging.log4j.LogManager;

import javax.servlet.http.HttpServletResponse;
import java.util.function.Function;

public abstract class AbstractApiDefinition {

    private static final Logger logger = LogManager.getLogger( AbstractApiDefinition.class );
    
	public abstract void configure();
	protected abstract WebApp getWebApp();
	protected abstract Authorizer getAuthorizer();
	protected abstract Authenticator getAuthenticator();
	
    /**
     * Helper method in order to make sure only an FDS admin
     * can hit the requested URL
     * 
     * @param method
     * @param route
     * @param reqHandler
     */
    protected void fdsAdminOnly( HttpMethod method, String route, Function<AuthenticationToken, RequestHandler> reqHandler ) {
        
    	authenticate( method, route, ( t ) -> {
            
            try {
                
            	if( getAuthorizer().userFor( t )
                              .isIsFdsAdmin() ) {
                    return reqHandler.apply( t );
                } else {
                    return ( r, p ) -> {
                    	
                		JSONObject jObject = new JSONObject();
                		jObject.put( "message",  "Invalid permissions" );
                        
                		return new JsonResource( jObject, HttpServletResponse.SC_UNAUTHORIZED );
                    };
                }
            	
            } catch( SecurityException e ) {
                
            	logger.error( "Error authorizing request, userId = " + t.getUserId(), e );
                
            	return ( r, p ) -> {
            		
            		JSONObject jObject = new JSONObject();
            		jObject.put( "message",  "Invalid permissions" );
            		
                    return new JsonResource( jObject, HttpServletResponse.SC_UNAUTHORIZED );
            	};
            }
        } );
    }

    /**
     * Helper to make sure the call is authenticated
     * before passing to the requested URL
     *  
     * @param method
     * @param route
     * @param reqHandler
     */
    protected void authenticate( HttpMethod method, String route, Function<AuthenticationToken,RequestHandler> reqHandler ) {
        
    	HttpAuthenticator httpAuth = new HttpAuthenticator( reqHandler, getAuthenticator() );
    	
    	HttpErrorHandler httpErrorHandler = new HttpErrorHandler( httpAuth );
        getWebApp().route( method, route, () -> httpErrorHandler );
    }
}
