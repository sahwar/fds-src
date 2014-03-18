package com.formationds.web.om;

import com.formationds.fdsp.ClientFactory;
import com.formationds.om.NativeApi;
import com.formationds.web.toolkit.HttpMethod;
import com.formationds.web.toolkit.WebApp;
import org.apache.log4j.Logger;

import java.lang.management.ManagementFactory;

/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

public class Main {
    private static Logger LOG = Logger.getLogger(Main.class);

    public void start(String[] args) throws Exception {
        ClientFactory clientFactory = new ClientFactory();
        new NativeApi();
        NativeApi.startOm(args);
        LOG.info("Process info: " + ManagementFactory.getRuntimeMXBean().getName());
        WebApp webApp = new WebApp("../lib/admin-webapp/");

        webApp.route(HttpMethod.get, "", () -> new LandingPage());
        webApp.route(HttpMethod.get, "nodes", () -> new ListNodes(clientFactory.configPathClient("localhost", 8903)));
        webApp.start(4242);
    }

    public static void main(String[] args) throws Exception {
            new Main().start(args);
    }
}

