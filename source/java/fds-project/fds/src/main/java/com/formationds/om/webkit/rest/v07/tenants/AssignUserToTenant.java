package com.formationds.om.webkit.rest.v07.tenants;
/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

import com.formationds.web.toolkit.JsonResource;
import com.formationds.web.toolkit.RequestHandler;
import com.formationds.web.toolkit.Resource;
import org.eclipse.jetty.server.Request;
import org.json.JSONObject;

import javax.crypto.SecretKey;
import java.util.Map;

public class AssignUserToTenant implements RequestHandler {
    private com.formationds.util.thrift.ConfigurationApi configCache;
    private SecretKey                                    secretKey;

    public AssignUserToTenant(com.formationds.util.thrift.ConfigurationApi configCache, SecretKey secretKey) {
        this.configCache = configCache;
        this.secretKey = secretKey;
    }

    @Override
    public Resource handle(Request request, Map<String, String> routeParameters) throws Exception {
        long tenantId = requiredLong(routeParameters, "tenantid");
        long userId = requiredLong(routeParameters, "userid");
        configCache.assignUserToTenant(userId, tenantId);
        return new JsonResource(new JSONObject().put("status", "ok"));
    }
}
