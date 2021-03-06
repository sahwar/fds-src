/*
 * Copyright (c) 2015, Formation Data Systems, Inc. All Rights Reserved.
 */

package com.formationds.om.webkit.rest.v07.snapshot;

import com.formationds.commons.model.Volume;
import com.formationds.commons.model.builder.VolumeBuilder;
import com.formationds.om.webkit.rest.v07.volumes.SetVolumeQosParams;
import com.formationds.util.thrift.ConfigurationApi;
import com.formationds.web.toolkit.JsonResource;
import com.formationds.web.toolkit.RequestHandler;
import com.formationds.web.toolkit.Resource;
import com.google.gson.GsonBuilder;
import org.eclipse.jetty.server.Request;
import org.json.JSONObject;
import org.apache.logging.log4j.Logger;
import org.apache.logging.log4j.LogManager;

import java.io.InputStreamReader;
import java.io.Reader;
import java.util.Map;

public class CloneSnapshot
  implements RequestHandler {
  private static final Logger logger =
    LogManager.getLogger( CloneSnapshot.class );

  private static final String REQ_PARAM_SNAPSHOT_ID = "snapshotId";
  private static final String REQ_PARAM_CLONE_VOLUME_NAME = "cloneVolumeName";
  private ConfigurationApi config;

    public CloneSnapshot(final ConfigurationApi config) {
        this.config = config;
    }

    @Override
    public Resource handle(final Request request,
                           final Map<String, String> routeParameters)
        throws Exception {

        long clonedSnapshotId;
        try (final Reader reader =
                 new InputStreamReader(request.getInputStream(), "UTF-8")) {

            final Volume volume = new GsonBuilder().create()
                                                   .fromJson(reader,
                                                             Volume.class);
            logger.trace("CLONE SNAPSHOT:VOLUME: {}", volume);

            final String name = requiredString(routeParameters,
                                               REQ_PARAM_CLONE_VOLUME_NAME);
            final long snapshotId = requiredLong(routeParameters,
                                                 REQ_PARAM_SNAPSHOT_ID);
            logger.trace("CLONE SNAPSHOT:NAME {} ID {}", name, snapshotId);

            clonedSnapshotId = config.cloneVolume(
                                                     snapshotId,
        0L,       // optional parameter so setting it to zero!
        name, 0L);

      Thread.sleep( 200 );
      SetVolumeQosParams.setVolumeQos( config,
                                       name,
                                       volume.getSla(),
                                       volume.getPriority(),
                                       volume.getLimit(),
                                       volume.getCommit_log_retention(),
                                       volume.getMediaPolicy());
    }

    // TODO audit and/or alert/event

    return new JsonResource(
      new JSONObject(
        new VolumeBuilder().withId( String.valueOf( clonedSnapshotId ) )
                           .build() ) );
  }
}
