/**
 * Copyright (c) 2015 Formation Data Systems, All rights reserved.
 */

package com.formationds.om.webkit.rest.v07.snapshot;

import com.formationds.commons.model.SnapshotPolicy;
import com.formationds.commons.model.builder.SnapshotPolicyBuilder;
import com.formationds.commons.model.helper.ObjectModelHelper;
import com.formationds.util.thrift.ConfigurationApi;
import com.formationds.web.toolkit.JsonResource;
import com.formationds.web.toolkit.RequestHandler;
import com.formationds.web.toolkit.Resource;
import org.eclipse.jetty.server.Request;
import org.json.JSONObject;
import org.apache.logging.log4j.Logger;
import org.apache.logging.log4j.LogManager;

import java.io.InputStreamReader;
import java.io.Reader;
import java.util.Map;

public class CreateSnapshotPolicy
  implements RequestHandler {
    private static final Logger logger =
      LogManager.getLogger( CreateSnapshotPolicy.class );

    private ConfigurationApi config;

    public CreateSnapshotPolicy(ConfigurationApi config) {
        this.config = config;
    }

    @Override
    public Resource handle(final Request request,
                           final Map<String, String> routeParameters)
        throws Exception {
        long policyId;
        try (final Reader reader =
                 new InputStreamReader(request.getInputStream(), "UTF-8")) {
            final SnapshotPolicy policy =
                ObjectModelHelper.toObject(reader, SnapshotPolicy.class);
            logger.trace("SNAPSHOT POLICY:: " + policy);

            policyId = config.createSnapshotPolicy(policy.getName(),
                                                   policy.getRecurrenceRule().toString(),
                                                   policy.getRetention(),
                                                   policy.getTimelineTime());
            
        }

        final SnapshotPolicy snapshotPolicy =
            new SnapshotPolicyBuilder().withId(policyId ).build();
      return new JsonResource( new JSONObject( snapshotPolicy ) );
    }
}
