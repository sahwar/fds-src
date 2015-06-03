/*
 * Copyright (c) 2015, Formation Data Systems, Inc. All Rights Reserved.
 */
package com.formationds.om.webkit.rest.v08.users;

import java.util.Map;
import java.util.UUID;

import javax.crypto.SecretKey;

import org.eclipse.jetty.server.Request;
import org.json.JSONObject;

import com.formationds.security.HashedPassword;
import com.formationds.util.thrift.ConfigurationApi;
import com.formationds.web.toolkit.JsonResource;
import com.formationds.web.toolkit.RequestHandler;
import com.formationds.web.toolkit.Resource;

public class GetUser implements RequestHandler{

    private ConfigurationApi configCache;
    private SecretKey                                    secretKey;

    public GetUser(ConfigurationApi configCache, SecretKey secretKey) {
        this.configCache = configCache;
        this.secretKey = secretKey;
    }

    @Override
    public Resource handle(Request request, Map<String, String> routeParameters) throws Exception {
        String login = requiredString(routeParameters, "login");
        String password = requiredString(routeParameters, "password");

        String hashed = new HashedPassword().hash(password);
        String secret = UUID.randomUUID().toString();
        long id = configCache.createUser(login, hashed, secret, false);
        return new JsonResource(new JSONObject().put("id", Long.toString(id)));
    }
}
