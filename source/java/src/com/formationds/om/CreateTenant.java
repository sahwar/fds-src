package com.formationds.om;
/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

import com.formationds.web.toolkit.JsonResource;
import com.formationds.web.toolkit.RequestHandler;
import com.formationds.web.toolkit.Resource;
import com.formationds.xdi.ConfigurationServiceCache;
import org.eclipse.jetty.server.Request;
import org.json.JSONObject;

import javax.crypto.SecretKey;
import java.util.Map;

public class CreateTenant implements RequestHandler {
    private ConfigurationServiceCache config;
    private SecretKey secretKey;

    public CreateTenant(ConfigurationServiceCache config, SecretKey secretKey) {
        this.config = config;
        this.secretKey = secretKey;
    }

    @Override
    public Resource handle(Request request, Map<String, String> routeParameters) throws Exception {
        String tenantName = requiredString(routeParameters, "tenant");
        long tenantId = config.createTenant(tenantName);
        return new JsonResource(new JSONObject().put("id", Long.toString(tenantId)));

        // returns {"id": "42"}
    }
}
