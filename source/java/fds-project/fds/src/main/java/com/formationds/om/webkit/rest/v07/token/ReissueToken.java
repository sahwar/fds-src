package com.formationds.om.webkit.rest.v07.token;
/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

import com.formationds.apis.User;
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
    private ConfigurationApi configCache;
    private SecretKey        secretKey;

    public ReissueToken(ConfigurationApi configCache, SecretKey secretKey) {
        this.configCache = configCache;
        this.secretKey = secretKey;
    }

    @Override
    public Resource handle(Request request, Map<String, String> routeParameters) throws Exception {
        long userId = requiredLong(routeParameters, "userid");
        User user = configCache.getUser(userId);
        if (user == null) {
            return new JsonResource(new JSONObject().put("status", "not found"), 404);
        }

        String secret = UUID.randomUUID().toString();
        configCache.updateUser(user.getId(), user.getIdentifier(), user.getPasswordHash(), secret, user.isIsFdsAdmin());
        String token = new AuthenticationToken(user.getId(), secret).signature(secretKey);
        return new JsonResource(new JSONObject().put("token", token));

        // returns {"token": "ICwGwPbTuMYWX7O9pyq+H53+S1I0L2iGI66Ca4Pf13k="}
    }
}
