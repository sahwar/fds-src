/*
 * Copyright (c) 2014, Formation Data Systems, Inc. All Rights Reserved.
 */

package com.formationds.om.rest.snapshot;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.formationds.commons.model.Snapshot;
import com.formationds.web.toolkit.JsonResource;
import com.formationds.web.toolkit.RequestHandler;
import com.formationds.web.toolkit.Resource;
import com.formationds.xdi.ConfigurationApi;
import org.eclipse.jetty.server.Request;
import org.json.JSONArray;

import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Map;

public class ListSnapshotsByVolumeId
  implements RequestHandler {

  private static final String REQ_PARAM_VOLUME_ID = "volumeId";
  private ConfigurationApi config;

  public ListSnapshotsByVolumeId( final ConfigurationApi config ) {
    this.config = config;
  }

  @Override
  public Resource handle( final Request request,
                          final Map<String, String> routeParameters )
    throws Exception {
    final ObjectMapper mapper = new ObjectMapper();
    final List<Snapshot> snapshots = new ArrayList<>();

    final long volumeId = requiredLong( routeParameters,
                                        REQ_PARAM_VOLUME_ID );
    final List<com.formationds.apis.Snapshot> _snapshots =
      config.listSnapshots( volumeId );
    if( _snapshots == null || _snapshots.isEmpty() ) {
      return new JsonResource( new JSONArray( snapshots ) );
    }

    for( final com.formationds.apis.Snapshot snapshot : _snapshots ) {
      final Snapshot mSnapshot = new Snapshot();

      mSnapshot.setId( String.valueOf( snapshot.getSnapshotId() ) );
      mSnapshot.setName( snapshot.getSnapshotName() );
      mSnapshot.setVolumeId( String.valueOf( snapshot.getVolumeId() ) );
      mSnapshot.setCreation( new Date( snapshot.getCreationTimestamp() ) );

      snapshots.add( mSnapshot );
    }

    return new JsonResource( new JSONArray( mapper.writeValueAsString( snapshots ) ) );
  }
}
