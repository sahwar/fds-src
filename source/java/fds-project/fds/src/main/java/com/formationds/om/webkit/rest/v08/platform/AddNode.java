/*
 * Copyright (c) 2015, Formation Data Systems, Inc. All Rights Reserved.
 */
package com.formationds.om.webkit.rest.v08.platform;

import com.formationds.protocol.FDSP_Uuid;
import com.formationds.protocol.svc.types.SvcInfo;
import com.formationds.protocol.pm.NotifyAddServiceMsg;
import com.formationds.apis.FDSP_ActivateOneNodeType;
import com.formationds.client.v08.model.Node;
import com.formationds.client.v08.model.Service;
import com.formationds.client.v08.model.ServiceType;
import com.formationds.client.v08.converters.PlatformModelConverter;
import com.formationds.commons.model.helper.ObjectModelHelper;
import com.formationds.om.events.EventManager;
import com.formationds.om.events.OmEvents;
import com.formationds.om.helper.SingletonConfigAPI;
import com.formationds.util.thrift.ConfigurationApi;
import com.formationds.web.toolkit.RequestHandler;
import com.formationds.web.toolkit.Resource;
import com.formationds.web.toolkit.TextResource;

import org.eclipse.jetty.server.Request;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import javax.servlet.http.HttpServletResponse;

import java.io.InputStreamReader;
import java.util.List;
import java.util.Map;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.Optional;


public class AddNode
    implements RequestHandler {

	private static final String NODE_ARG = "node_id";
	
    private static final Logger logger =
        LoggerFactory.getLogger( AddNode.class );

    private ConfigurationApi configApi;

    public AddNode() {}

    @Override
    public Resource handle( final Request request,
                            final Map<String, String> routeParameters)
        throws Exception {

        long nodeUuid = requiredLong(routeParameters, NODE_ARG );
        
        logger.debug( "Trying to add node: " + nodeUuid );
        
        final InputStreamReader reader = new InputStreamReader( request.getInputStream() );
        //Node node = ObjectModelHelper.toObject( reader, Node.class );
        Node node = (new GetNode()).getNode(nodeUuid);
        List<SvcInfo> svcInfList = new ArrayList<SvcInfo>();
        boolean pmPresent = false;
        
        for(List<Service> svcList : node.getServices().values())
        {
        	for(Service svc : svcList)
        	{
        		SvcInfo svcInfo = PlatformModelConverter.convertServiceToSvcInfoType(svc);
        		svcInfList.add(svcInfo);
        		
        		pmPresent = (svc.getType() == ServiceType.PM);
        		
        	}
        }
        
//        int status =
//        		getConfigApi().AddService(new NotifyAddServiceMsg(svcInfList));
        int status =
            getConfigApi().ActivateNode( new FDSP_ActivateOneNodeType(
                                     0,
                                     new FDSP_Uuid( nodeUuid ),
                                     activateService(ServiceType.SM,node),
                                     activateService(ServiceType.AM,node),
                                     activateService(ServiceType.DM,node) ) );

        if( status != 0 ) {

            status= HttpServletResponse.SC_BAD_REQUEST;
            EventManager.notifyEvent( OmEvents.ADD_NODE_ERROR,
                                      nodeUuid );

        } else {

            EventManager.notifyEvent( OmEvents.ADD_NODE,
                                      nodeUuid );

        }

        Node newNode = (new GetNode()).getNode( nodeUuid );
        
        String jsonString = ObjectModelHelper.toJSON( newNode );
        
        return new TextResource( jsonString );
    }
    
    private Boolean activateService( ServiceType type, Node node ){
    	
    	List<Service> services = node.getServices().get( type );
    	
    	if ( services.size() == 0 ){
    		return false;
    	}
    	
    	return true;
    }
    
    private ConfigurationApi getConfigApi(){
    	
    	if ( configApi == null ){
    		configApi = SingletonConfigAPI.instance().api();
    	}
    	
    	return configApi;
    }
}
