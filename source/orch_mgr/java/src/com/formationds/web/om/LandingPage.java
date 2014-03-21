package com.formationds.web.om;

import com.formationds.web.toolkit.RequestHandler;
import com.formationds.web.toolkit.Resource;
import com.formationds.web.toolkit.StreamResource;
import org.eclipse.jetty.server.Request;

import java.io.File;
import java.io.FileInputStream;

/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
public class LandingPage implements RequestHandler {
    private String webDir;

    public LandingPage(String webDir) {
        this.webDir = webDir;
    }

    @Override
    public Resource handle(Request request) throws Exception {
        return new StreamResource(new FileInputStream(new File(webDir, "index.html")), "text/html");
    }

}
