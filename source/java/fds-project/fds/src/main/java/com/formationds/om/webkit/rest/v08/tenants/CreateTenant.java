/*
 * Copyright 2015 Formation Data Systems, Inc.
 */
package com.formationds.om.webkit.rest.v08.tenants;

import com.formationds.client.v08.converters.ExternalModelConverter;
import com.formationds.client.v08.model.Tenant;
import com.formationds.commons.model.helper.ObjectModelHelper;
import com.formationds.om.helper.SingletonConfigAPI;
import com.formationds.util.thrift.ConfigurationApi;
import com.formationds.web.toolkit.RequestHandler;
import com.formationds.web.toolkit.Resource;
import com.formationds.web.toolkit.TextResource;

import org.eclipse.jetty.server.Request;
import org.apache.logging.log4j.Logger;
import org.apache.logging.log4j.LogManager;

import java.io.InputStreamReader;
import java.util.List;
import java.util.Map;

public class CreateTenant implements RequestHandler {

    private static final Logger logger = LogManager.getLogger( CreateTenant.class);

    private ConfigurationApi configApi;

    public CreateTenant() {}

    @Override
    public Resource handle(Request request, Map<String, String> routeParameters) throws Exception {

        Tenant tenant = null;
        try ( final InputStreamReader reader = new InputStreamReader( request.getInputStream() ) ) {
            tenant = ObjectModelHelper.toObject( reader, Tenant.class );
        }

        logger.debug( "Trying to create tenant: " + tenant.getName() );

        long tenantId = getConfigApi().createTenant( tenant.getName() );

        List<com.formationds.apis.Tenant> internalTenants = getConfigApi().listTenants(0);
        Tenant newTenant = null;

        for ( com.formationds.apis.Tenant internalTenant : internalTenants ){

            if ( internalTenant.getId() == tenantId ){
                newTenant = ExternalModelConverter.convertToExternalTenant( internalTenant );
                break;
            }
        }

        String jsonString = ObjectModelHelper.toJSON( newTenant );

        return new TextResource( jsonString );
    }

    private ConfigurationApi getConfigApi(){

        if ( configApi == null ){
            configApi = SingletonConfigAPI.instance().api();
        }

        return configApi;
    }
}
