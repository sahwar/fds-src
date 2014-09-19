/*
 * Copyright (C) 2014, All Rights Reserved, by Formation Data Systems, Inc.
 *
 * This software is furnished under a license and may be used and copied only
 * in  accordance  with  the  terms  of such  license and with the inclusion
 * of the above copyright notice. This software or  any  other copies thereof
 * may not be provided or otherwise made available to any other person.
 * No title to and ownership of  the  software  is  hereby transferred.
 *
 * The information in this software is subject to change without  notice
 * and  should  not be  construed  as  a commitment by Formation Data Systems.
 *
 * Formation Data Systems assumes no responsibility for the use or  reliability
 * of its software on equipment which is not supplied by Formation Date Systems.
 */

package com.formationds.om.rest.snapshot;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.formationds.commons.model.ObjectFactory;
import com.formationds.commons.model.Status;
import com.formationds.commons.togglz.feature.flag.FdsFeatureToggles;
import com.formationds.om.rest.OMRestBase;
import com.formationds.web.toolkit.JsonResource;
import com.formationds.web.toolkit.Resource;
import com.formationds.xdi.ConfigurationServiceCache;
import io.netty.handler.codec.http.HttpResponseStatus;
import org.apache.log4j.Logger;
import org.eclipse.jetty.server.Request;
import org.json.JSONObject;

import java.util.Map;

/**
 * @author ptinius
 */
public class DetachSnapshotPolicyIdToVolumeId
        extends OMRestBase {
  private static final Logger LOG =
          Logger.getLogger(DetachSnapshotPolicyIdToVolumeId.class);

  private static final String REQ_PARAM_VOLUME_ID = "volumeId";
  private static final String REQ_PARAM_POLICY_ID = "policyId";

  /**
   * @param config the {@link com.formationds.xdi.ConfigurationServiceCache}
   */
  public DetachSnapshotPolicyIdToVolumeId(final ConfigurationServiceCache config) {
    super(config);
  }

  /**
   * @param request         the {@link Request}
   * @param routeParameters the {@link Map} of route parameters
   * @return Returns the {@link Resource}
   * @throws Exception any unhandled error
   */
  @Override
  public Resource handle(final Request request,
                         final Map<String, String> routeParameters)
          throws Exception {
    if (!FdsFeatureToggles.USE_CANNED.isActive()) {
      getConfigurationServiceCache().detachSnapshotPolicy(
              requiredLong(routeParameters, REQ_PARAM_VOLUME_ID),
              requiredLong(routeParameters, REQ_PARAM_POLICY_ID));
    }

    final ObjectMapper mapper = new ObjectMapper();
    final Status status = ObjectFactory.createStatus();
    status.setStatus(HttpResponseStatus.OK.reasonPhrase());
    status.setCode(HttpResponseStatus.OK.code());

    return new JsonResource(new JSONObject(mapper.writeValueAsString(status)));
  }
}
