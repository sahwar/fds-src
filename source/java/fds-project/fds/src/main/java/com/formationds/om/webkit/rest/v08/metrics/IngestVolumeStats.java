/*
 * Copyright (c) 2015, Formation Data Systems, Inc. All Rights Reserved.
 */

package com.formationds.om.webkit.rest.v08.metrics;

import com.formationds.client.v08.model.SizeUnit;
import com.formationds.commons.model.entity.IVolumeDatapoint;
import com.formationds.commons.model.entity.VolumeDatapoint;
import com.formationds.commons.model.helper.ObjectModelHelper;
import com.formationds.commons.model.type.Metrics;
import com.formationds.commons.togglz.feature.flag.FdsFeatureToggles;
import com.formationds.om.helper.SingletonConfigAPI;
import com.formationds.om.redis.RedisSingleton;
import com.formationds.om.repository.SingletonRepositoryManager;
import com.formationds.util.thrift.ConfigurationApi;
import com.formationds.web.toolkit.JsonResource;
import com.formationds.web.toolkit.RequestHandler;
import com.formationds.web.toolkit.RequestLog;
import com.formationds.web.toolkit.RequestLog.LoggingRequestWrapper;
import com.formationds.web.toolkit.Resource;
import com.google.gson.reflect.TypeToken;

import org.eclipse.jetty.server.Request;
import org.json.JSONObject;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.InputStreamReader;
import java.lang.reflect.Type;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

import javax.servlet.http.HttpServletRequest;

/**
 * @author ptinius
 */
public class IngestVolumeStats implements RequestHandler {

    private static final Logger logger =
            LoggerFactory.getLogger( IngestVolumeStats.class );

    private static final Type TYPE = new TypeToken<List<VolumeDatapoint>>(){}.getType();

    private final ConfigurationApi config;

    public IngestVolumeStats(final ConfigurationApi config) {
        this.config = config;
    }

    @Override
    public Resource handle(Request request, Map<String, String> routeParameters)
            throws Exception {
    	
    	if ( !FdsFeatureToggles.INFLUX_WRITE_METRICS.isActive() ){
    		logger.warn( "The toggle to write metrics to InfluxDB is turned off so no metrics will be sent to that persistent store." );
    		return new JsonResource(new JSONObject().put("status", "OK"));
    	}

        HttpServletRequest httpRequest = (HttpServletRequest)request;
        if ( FdsFeatureToggles.WEB_LOGGING_REQUEST_WRAPPER.isActive() ) {
            // Use the INGEST_STATS_MARKER to only log the payload if the marker is enabled.
            // Override the parent.
            httpRequest = RequestLog.newRequestLogger( request );
            ((LoggingRequestWrapper<?>)httpRequest).setPayloadMarker( RequestLog.REQUEST_PAYLOAD_INGEST_STATS_MARKER );
        }

        try ( final InputStreamReader reader = new InputStreamReader( httpRequest.getInputStream(), "UTF-8" ) ) {

            final List<IVolumeDatapoint> volumeDatapoints = ObjectModelHelper.toObject(reader, TYPE);

            final List<String> volumeNames = new ArrayList<>( );
            volumeDatapoints.forEach( vdp -> {
                long volid;

                try {
                    if( !volumeNames.contains( vdp.getVolumeName() ) )
                    {
                        volumeNames.add( vdp.getVolumeName() );
                    }

                    volid = SingletonConfigAPI.instance().api().getVolumeId( vdp.getVolumeName() );
                } catch (Exception e) {
                    throw new IllegalStateException( "Volume " + vdp.getVolumeName() + " does not " +
                            "have an ID associated with the name." );
                }

                vdp.setVolumeId( String.valueOf( volid ) );
            });

            if( volumeNames.isEmpty() )
            {
                // prevent divide by zero
                volumeNames.add( "DummyVolumeName" );
            }

            /**
             * HACK ALERT!!
             *
             *  The stat stream is per volume; we need to have used bytes (UBYTES) included with every
             *  volume, we divide the used capacity across all volumes so when we sum it later we should
             *  have a total used capacity be correct, well at least close.
             *
             *  Also, make sure that UBYTES is added to all time series data points
             */
            final Double usedCapacity = RedisSingleton.INSTANCE.api( )
                    .getDomainUsedCapacity( )
                    .getValue( SizeUnit.B )
                    .doubleValue( ) / volumeNames.size();

            List<IVolumeDatapoint> newList = new ArrayList<>( );
            for( final IVolumeDatapoint vdp : volumeDatapoints )
            {
                newList.add( vdp );
                if( Metrics.LBYTES.matches( vdp.getKey() ) )
                {
                    newList.add( new VolumeDatapoint( vdp.getTimestamp(),
                                                      vdp.getVolumeId( ),
                                                      vdp.getVolumeName( ),
                                                      Metrics.UBYTES.key( ),
                                                      usedCapacity ) );
               }
            }

            SingletonRepositoryManager.instance().getMetricsRepository().save( newList );
        }

        return new JsonResource(new JSONObject().put("status", "OK"));
    }
}
