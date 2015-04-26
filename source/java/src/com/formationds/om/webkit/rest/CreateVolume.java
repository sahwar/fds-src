/*
 * Copyright (c) 2015 Formation Data Systems. All rights Reserved.
 */

package com.formationds.om.webkit.rest;

import com.formationds.protocol.ApiException;
import com.formationds.protocol.ErrorCode;
import com.formationds.apis.ConfigurationService;
import com.formationds.apis.VolumeSettings;
import com.formationds.apis.VolumeType;
import com.formationds.commons.model.ConnectorAttributes;
import com.formationds.commons.model.Volume;
import com.formationds.commons.model.helper.ObjectModelHelper;
import com.formationds.commons.model.type.ConnectorType;
import com.formationds.security.AuthenticationToken;
import com.formationds.security.Authorizer;
import com.formationds.util.SizeUnit;
import com.formationds.web.toolkit.JsonResource;
import com.formationds.web.toolkit.RequestHandler;
import com.formationds.web.toolkit.Resource;
import com.formationds.web.toolkit.TextResource;
import org.apache.thrift.TException;
import org.eclipse.jetty.server.Request;
import org.json.JSONObject;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import javax.servlet.http.HttpServletResponse;
import java.io.InputStreamReader;
import java.io.Reader;
import java.util.Map;
import java.util.concurrent.TimeUnit;

public class CreateVolume
  implements RequestHandler {
  private static final Logger logger =
    LoggerFactory.getLogger( CreateVolume.class );

  // TODO pull these values from the platform.conf file.
  private static final String URL = "http://localhost:7777/api/stats";
  private static final String METHOD = "POST";
  private static final Long DURATION = TimeUnit.MINUTES.toSeconds( 2 );
  private static final Long FREQUENCY = TimeUnit.MINUTES.toSeconds( 1 );

  private static final Integer DEF_BLOCK_SIZE = ( 1024 * 128 );
  private static final Integer DEF_OBJECT_SIZE = ( ( 1024 * 1024 ) * 2 );

  private final Authorizer authorizer;
  private final ConfigurationService.Iface configApi;
  private final AuthenticationToken token;

  public CreateVolume( final Authorizer authorizer,
                       final ConfigurationService.Iface configApi,
                       final AuthenticationToken token ) {
    this.authorizer = authorizer;
    this.configApi = configApi;
    this.token = token;
  }

  @Override
  public Resource handle( Request request, Map<String, String> routeParameters )
      throws Exception {

      Volume volume;
      long volumeId = -1;
      final String domainName = "";
      final int unused_argument = 0;

      try( final Reader reader =
               new InputStreamReader( request.getInputStream(), "UTF-8" ) ) {
          volume =
              ObjectModelHelper.toObject( reader, Volume.class );

          // validate model object has all required fields.
          if( ( volume == null ) ||
              ( volume.getName() == null ) ||
              ( volume.getData_connector() == null ) ||
              ( volume.getData_connector().getType() == null ) ) {

              return new JsonResource(
                  new JSONObject().put( "message", "missing mandatory field" ),
                  HttpServletResponse.SC_BAD_REQUEST );
          }
      }

      VolumeSettings settings;
      final ConnectorType type = volume.getData_connector()
                                       .getType();
      switch( type ) {
          case BLOCK:
              if( volume.getData_connector()
                        .getAttributes() == null ) {

                  return new JsonResource(
                      new JSONObject().put(
                          "message",
                          "missing data connector attributes for BLOCK." ),
                      HttpServletResponse.SC_BAD_REQUEST );
              }

	      if (volume.getMaxObjectSize() == 0) {
		      volume.setMaxObjectSize(DEF_BLOCK_SIZE);
	      }

              final ConnectorAttributes attrs =
                  volume.getData_connector()
                        .getAttributes();
              settings = new VolumeSettings( volume.getMaxObjectSize(),
                                             VolumeType.BLOCK,
                                             SizeUnit.valueOf(
                                                 attrs.getUnit()
                                                      .name() )
                                                     .totalBytes( attrs.getSize() ), 
                                             0,
                                             volume.getMediaPolicy());

              break;
          case OBJECT:
	      if (volume.getMaxObjectSize() == 0) {
		      volume.setMaxObjectSize(DEF_OBJECT_SIZE);
	      }
              settings = new VolumeSettings( volume.getMaxObjectSize(),
                                             VolumeType.OBJECT,
                                             0 , 0, volume.getMediaPolicy() );
              break;
          default:
              throw new IllegalArgumentException(
                  String.format( "Unsupported data connector type '%s'",
                                 type ) );
      }

      // add the commit log retention value
      settings.setContCommitlogRetention( volume.getCommit_log_retention() );

      try {
          configApi.createVolume(domainName,
                                 volume.getName(),
                                 settings,
                                 authorizer.tenantId(token));
      } catch( ApiException e ) {

          if ( e.getErrorCode().equals(ErrorCode.RESOURCE_ALREADY_EXISTS)) {
              volumeId = configApi.getVolumeId( volume.getName() );
              if( volumeId > 0 ) {
                  volume.setId(String.valueOf(volumeId));
              }
              return new TextResource(volume.toJSON());
          }

          logger.error( "CREATE::FAILED::" + e.getMessage(), e );

          // allow dispatcher to handle
          throw e;
      } catch ( TException | SecurityException se ) {
          logger.error( "CREATE::FAILED::" + se.getMessage(), se );

          // allow dispatcher to handle
          throw se;
      }

      volumeId = configApi.getVolumeId( volume.getName() );
      if( volumeId > 0 ) {
          volume.setId( String.valueOf( volumeId ) );

          Thread.sleep( 200 );
          SetVolumeQosParams.setVolumeQos( configApi,
                                           volume.getName(),
                                           ( int ) volume.getSla(),
                                           volume.getPriority(),
                                           ( int ) volume.getLimit(),
                                           volume.getCommit_log_retention(),
                                           volume.getMediaPolicy() );

          return new TextResource( volume.toJSON() );
      }

      throw new Exception( "no volume id after createVolume call!" );
  }
}

