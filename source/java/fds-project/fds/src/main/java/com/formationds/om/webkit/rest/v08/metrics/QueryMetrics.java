/*
 * Copyright (c) 2015, Formation Data Systems, Inc. All Rights Reserved.
 */
package com.formationds.om.webkit.rest.v08.metrics;

import com.formationds.client.v08.model.stats.Statistics;
import com.formationds.client.v08.model.stats.query.MetricQueryCriteria;
import com.formationds.commons.model.helper.ObjectModelHelper;
import com.formationds.commons.togglz.feature.flag.FdsFeatureToggles;
import com.formationds.om.repository.helper.QueryHelper;
import com.formationds.om.webkit.rest.v08.users.GetUser;
import com.formationds.protocol.ApiException;
import com.formationds.protocol.ErrorCode;
import com.formationds.security.AuthenticationToken;
import com.formationds.security.Authenticator;
import com.formationds.security.Authorizer;
import com.formationds.stats.client.QueryHandler;
import com.formationds.stats.client.StatsConnection;
import com.formationds.web.toolkit.JsonResource;
import com.formationds.web.toolkit.RequestHandler;
import com.formationds.web.toolkit.Resource;
import com.google.gson.reflect.TypeToken;

import org.eclipse.jetty.server.Request;
import org.json.JSONObject;
import org.apache.logging.log4j.Logger;
import org.apache.logging.log4j.LogManager;

import java.io.InputStreamReader;
import java.io.Reader;
import java.lang.reflect.Type;
import java.util.Map;

import javax.crypto.SecretKey;

/**
 * @author ptinius
 */
public class QueryMetrics implements RequestHandler, QueryHandler {
    private static final Logger logger =
            LogManager.getLogger( QueryMetrics.class );

    private static final Type TYPE_08 = new TypeToken<MetricQueryCriteria>() { }.getType();
    private static final Type TYPE_COMMON = new TypeToken<com.formationds.om.repository.query.MetricQueryCriteria>() { }.getType();
    private final AuthenticationToken token;
    private final SecretKey key;
    private final Authorizer authorizer;
    
    private static final long TIMEOUT = 300000;
    private Statistics returnStats = null;

    public QueryMetrics( final Authorizer authorizer, final AuthenticationToken token, final SecretKey key ) {
        super();

        this.token = token;
        this.key = key;
        this.authorizer = authorizer;
    }

    @Override
    public Resource handle( Request request, Map<String, String> routeParameters )
            throws Exception {

        try( final Reader reader = new InputStreamReader( request.getInputStream(), "UTF-8" ) ) {

            if ( FdsFeatureToggles.STATS_SERVICE_QUERY.isActive() ){
            	
            	StatsConnection statsConn = StatsConnection.newConnection( "localhost", 11011, "admin", getToken().signature( getSecretKey() ) );
            	statsConn.query( ObjectModelHelper.toObject( reader, TYPE_08 ), this );
            	
            	long waited = 0;
            	
            	try {
            		while ( this.returnStats == null && waited < TIMEOUT ){
            			Thread.sleep( 5 );
            			waited += 5;
            		}

            		Statistics stats = this.returnStats;
            		
            		return new JsonResource( new JSONObject( stats ) );
            	}
            	catch( Exception e ){
            		logger.warn( "Metric query failed with exception message: " + e.getLocalizedMessage() );
            		logger.debug( "Metric query exception", e );
            		
            		throw new ApiException( "Failure during a metrics query using the stats service.", ErrorCode.INTERNAL_SERVER_ERROR );
            	}
            	finally {
            		
            		if ( statsConn != null ){
            			statsConn.close();
            		}
            	}
            }
            else {

            	com.formationds.commons.model.Statistics stats = QueryHelper.instance().execute( ObjectModelHelper.toObject( reader, TYPE_COMMON ), authorizer, token );
            	return new JsonResource( new JSONObject( stats ) );
            }
        }
    }

	@Override
	public void queryResults( Statistics stats) {
		returnStats = stats;
	}
	
	private AuthenticationToken getToken(){
		return this.token;
	}
	
	private SecretKey getSecretKey(){
		return this.key;
	}
}
