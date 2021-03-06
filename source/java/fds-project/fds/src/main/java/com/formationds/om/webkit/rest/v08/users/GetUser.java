/*
 * Copyright (c) 2015, Formation Data Systems, Inc. All Rights Reserved.
 */
package com.formationds.om.webkit.rest.v08.users;

import java.util.Map;

import org.eclipse.jetty.server.Request;
import org.apache.logging.log4j.Logger;
import org.apache.logging.log4j.LogManager;

import com.formationds.client.v08.converters.ExternalModelConverter;
import com.formationds.client.v08.model.User;
import com.formationds.commons.model.helper.ObjectModelHelper;
import com.formationds.om.helper.SingletonConfigAPI;
import com.formationds.util.thrift.ConfigurationApi;
import com.formationds.web.toolkit.RequestHandler;
import com.formationds.web.toolkit.Resource;
import com.formationds.web.toolkit.TextResource;

public class GetUser implements RequestHandler{

	private static final Logger logger = LogManager.getLogger( GetUser.class );
	private final static String USER_ARG = "user_id";
	
	public static final String STATS_USERNAME = "stats-service";
	
    private ConfigurationApi configApi;
    
    public GetUser() {}

    @Override
    public Resource handle(Request request, Map<String, String> routeParameters) throws Exception {
        
    	long userId = requiredLong( routeParameters, USER_ARG );
    	
    	User externalUser = getUser( userId );
    	
    	String jsonString = ObjectModelHelper.toJSON( externalUser );
    	
    	return new TextResource( jsonString );
    }
    
    public User getUser( long userId ){
    	
    	logger.debug( "Trying to find user: {}.", userId );
    	
    	com.formationds.apis.User internalUser = getConfigApi().getUser( userId );
    	
    	if ( internalUser.getIdentifier().equalsIgnoreCase( STATS_USERNAME ) ){
    		logger.debug( "Trying to find the stats user, returning nothing." );
    		return null;
    	}
    	
    	User externalUser = ExternalModelConverter.convertToExternalUser( internalUser );
    	
    	return externalUser;
    }
    
    public User getUser( String username ) {
    	
    	logger.debug( "Trying to find user: {}.", username );
    	
    	com.formationds.apis.User internalUser = getConfigApi().getUser( username );
    	
    	if ( internalUser.getIdentifier().equalsIgnoreCase( STATS_USERNAME ) ){
    		logger.debug( "Trying to find the stats user, returning nothing." );
    		return null;
    	}
    	
    	User externalUser = ExternalModelConverter.convertToExternalUser( internalUser );
    	
    	return externalUser;
    }
    
    private ConfigurationApi getConfigApi(){
    	
    	if ( configApi == null ){
    		configApi = SingletonConfigAPI.instance().api();
    	}
    	
    	return configApi;
    }
}
