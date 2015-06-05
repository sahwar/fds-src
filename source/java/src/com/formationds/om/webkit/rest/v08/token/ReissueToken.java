/*
 * Copyright (c) 2015, Formation Data Systems, Inc. All Rights Reserved.
 */
package com.formationds.om.webkit.rest.v08.token;

import com.formationds.apis.User;
import com.formationds.om.helper.SingletonConfigAPI;
import com.formationds.security.AuthenticationToken;
import com.formationds.util.thrift.ConfigurationApi;
import com.formationds.web.toolkit.JsonResource;
import com.formationds.web.toolkit.RequestHandler;
import com.formationds.web.toolkit.Resource;

import org.eclipse.jetty.server.Request;
import org.json.JSONObject;

import javax.crypto.SecretKey;

import java.util.Map;
import java.util.UUID;

public class ReissueToken implements RequestHandler {
    
	private static final String USER_ARG = "user_id";
	private ConfigurationApi configApi;
    private SecretKey        secretKey;

    public ReissueToken( SecretKey secretKey ) {
        this.secretKey = secretKey;
    }

    @Override
    public Resource handle(Request request, Map<String, String> routeParameters) throws Exception {
        
    	long userId = requiredLong(routeParameters, USER_ARG );
        User user = getConfigApi().getUser(userId);
        
        if (user == null) {
            return new JsonResource(new JSONObject().put("status", "not found"), 404);
        }

        String secret = UUID.randomUUID().toString();
        getConfigApi().updateUser(user.getId(), user.getIdentifier(), user.getPasswordHash(), secret, user.isIsFdsAdmin());
        String token = new AuthenticationToken( user.getId(), secret).signature( getSecretKey() );
        return new JsonResource(new JSONObject().put("token", token));
    }
    
    private SecretKey getSecretKey(){
    	return this.secretKey;
    }
    
    private ConfigurationApi getConfigApi(){
    	
    	if ( configApi == null ){
    		
    		configApi = SingletonConfigAPI.instance().api();
    	}
    	
    	return configApi;
    }
}
