package com.formationds.xdi.s3;
/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

import com.formationds.am.Main;
import com.formationds.apis.VolumePolicy;
import com.formationds.web.toolkit.RequestHandler;
import com.formationds.web.toolkit.Resource;
import com.formationds.web.toolkit.TextResource;
import com.formationds.xdi.Xdi;
import org.eclipse.jetty.server.Request;

import java.util.Map;

public class CreateBucket implements RequestHandler {
    private Xdi xdi;

    public CreateBucket(Xdi xdi) {
        this.xdi = xdi;
    }

    @Override
    public Resource handle(Request request, Map<String, String> routeParameters) throws Exception {
        String bucketName = requiredString(routeParameters, "bucket");
        xdi.createVolume(Main.FDS_S3, bucketName, new VolumePolicy(1024 * 1024 * 2));
        return new TextResource("");
    }
}
