/*
 * Copyright (c) 2015, Formation Data Systems, Inc. All Rights Reserved.
 */
package com.formationds.om.webkit.rest.v08.platform;

import java.util.Map;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.List;
import java.io.Reader;
import javax.servlet.http.HttpServletResponse;

import org.eclipse.jetty.server.Request;
import org.json.JSONObject;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.formationds.client.v08.model.Node;
import com.formationds.client.v08.model.Service;
import com.formationds.client.v08.model.ServiceType;
import com.formationds.client.v08.converters.PlatformModelConverter;
import com.formationds.commons.model.helper.ObjectModelHelper;
import com.formationds.om.events.EventManager;
import com.formationds.om.events.OmEvents;
import com.formationds.protocol.pm.NotifyAddServiceMsg;
import com.formationds.protocol.pm.NotifyStartServiceMsg;
import com.formationds.protocol.svc.types.SvcInfo;
import com.formationds.apis.ConfigurationService;
import com.formationds.om.helper.SingletonConfigAPI;
import com.formationds.util.thrift.ConfigurationApi;
import com.formationds.web.toolkit.JsonResource;
import com.formationds.web.toolkit.RequestHandler;
import com.formationds.web.toolkit.Resource;
import com.formationds.web.toolkit.TextResource;

public class AddService implements RequestHandler {

	private static final String NODE_ARG = "node_id";
	
    private static final Logger logger =
            LoggerFactory.getLogger( AddService.class );

    private ConfigurationApi configApi;
	
	public AddService(){}
	
	@Override
	public Resource handle(Request request, Map<String, String> routeParameters)
			throws Exception {

		long nodeId = requiredLong( routeParameters, NODE_ARG );
        
		logger.debug( "Adding service to node: " + nodeId );
        
        final Reader reader = new InputStreamReader( request.getInputStream(), "UTF-8" );
        
        Service service = ObjectModelHelper.toObject( reader, Service.class );
        
        Node node = (new GetNode()).getNode(nodeId);
          	
        List<SvcInfo> svcInfList = new ArrayList<SvcInfo>();
        
        SvcInfo svcInfo = PlatformModelConverter.convertServiceToSvcInfoType
        				               (node.getAddress().getHostAddress(), service);
        svcInfList.add(svcInfo);
        		
        Service pmSvc = (new GetService()).getService(nodeId, nodeId);
        SvcInfo pmSvcInfo = PlatformModelConverter.convertServiceToSvcInfoType
        		                         (node.getAddress().getHostAddress(), pmSvc);
        svcInfList.add(pmSvcInfo);
        
        // Add the new service
        int status =
        		getConfigApi().AddService(new NotifyAddServiceMsg(svcInfList));
                
        if( status != 0 )
        {
            status= HttpServletResponse.SC_BAD_REQUEST;
            EventManager.notifyEvent( OmEvents.ADD_SERVICE_ERROR, 0 );
        }
        else
        {   
            // Now that we have added the services, go start them
            status = getConfigApi().StartService(new NotifyStartServiceMsg(svcInfList));
       
            if( status != 0 )
            {
                status= HttpServletResponse.SC_BAD_REQUEST;
                EventManager.notifyEvent( OmEvents.START_SERVICE_ERROR, 0 );
            }
            else
            {
                EventManager.notifyEvent( OmEvents.START_SERVICE, 0 );
            }
        }
        
        List<Service> svcList = node.getServices().get(service.getType());
        Service newService = svcList.get(0);
		String jsonString = ObjectModelHelper.toJSON( newService );
		
		return new TextResource( jsonString );
	}

    private ConfigurationApi getConfigApi(){
    	
    	if ( configApi == null ){
    		configApi = SingletonConfigAPI.instance().api();
    	}
    	
    	return configApi;
    }

}
