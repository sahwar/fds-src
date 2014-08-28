package com.formationds.om;

import FDS_ProtocolInterface.FDSP_ConfigPathReq;
import com.formationds.apis.AmService;
import com.formationds.apis.ConfigurationService;
import com.formationds.fdsp.LegacyClientFactory;
import com.formationds.om.plotter.DisplayVolumeStats;
import com.formationds.om.plotter.ListActiveVolumes;
import com.formationds.om.plotter.RegisterVolumeStats;
import com.formationds.om.plotter.VolumeStatistics;
import com.formationds.om.rest.*;
import com.formationds.security.*;
import com.formationds.util.Configuration;
import com.formationds.util.libconfig.ParsedConfig;
import com.formationds.web.toolkit.*;
import com.formationds.xdi.CachingConfigurationService;
import com.formationds.xdi.Xdi;
import com.formationds.xdi.XdiClientFactory;
import org.apache.log4j.Logger;
import org.eclipse.jetty.server.Request;
import org.joda.time.Duration;
import org.json.JSONObject;

import java.util.Map;
import java.util.UUID;
import java.util.function.Function;

/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

public class Main {
    private static final Logger LOG = Logger.getLogger(Main.class);

    private WebApp webApp;
    private Configuration configuration;
    private Xdi xdi;

    public static void main(String[] args) throws Exception {
        new Main().start(args);
    }

    public void start(String[] args) throws Exception {
        configuration = new Configuration("om-xdi", args);
        NativeOm.startOm(args);

        ParsedConfig platformConfig = configuration.getPlatformConfig();
        XdiClientFactory clientFactory = new XdiClientFactory();
        CachingConfigurationService configApi = new CachingConfigurationService(clientFactory.remoteOmService("localhost", 9090));
        AmService.Iface amService = clientFactory.remoteAmService("localhost", 9988);

        String omHost = "localhost";
        int omPort = platformConfig.lookup("fds.om.config_port").intValue();
        String webDir = platformConfig.lookup("fds.om.web_dir").stringValue();

        LegacyClientFactory legacyClientFactory = new LegacyClientFactory();
        FDSP_ConfigPathReq.Iface legacyConfigClient = legacyClientFactory.configPathClient(omHost, omPort);
        VolumeStatistics volumeStatistics = new VolumeStatistics(Duration.standardMinutes(20));

        boolean enforceAuthentication = platformConfig.lookup("fds.om.authentication").booleanValue();
        Authenticator authenticator = enforceAuthentication ? new FdsAuthenticator(configApi) : new NullAuthenticator();

        Authorizer authorizer = new DumbAuthorizer();
        xdi = new Xdi(amService, configApi, authenticator, authorizer, legacyConfigClient);

        webApp = new WebApp(webDir);

        webApp.route(HttpMethod.GET, "", () -> new LandingPage(webDir));

        webApp.route(HttpMethod.POST, "/api/auth/token", () -> new IssueToken(xdi));
        webApp.route(HttpMethod.GET, "/api/auth/token", () -> new IssueToken(xdi));

        authenticate(HttpMethod.GET, "/api/auth/currentUser", (t) -> new CurrentUser(xdi, t));
        authenticate(HttpMethod.GET, "/api/config/services", (t) -> new ListServices(legacyConfigClient));
        authenticate(HttpMethod.POST, "/api/config/services/:node_uuid", (t) -> new ActivatePlatform(legacyConfigClient));

        authenticate(HttpMethod.GET, "/api/config/volumes", (t) -> new ListVolumes(configApi, amService, legacyConfigClient));
        authenticate(HttpMethod.POST, "/api/config/volumes", (t) -> new CreateVolume(configApi, legacyConfigClient));
        authenticate(HttpMethod.DELETE, "/api/config/volumes/:name", (t) -> new DeleteVolume(legacyConfigClient));
        authenticate(HttpMethod.PUT, "/api/config/volumes/:uuid", (t) -> new SetVolumeQosParams(legacyConfigClient, configApi, amService));

        authenticate(HttpMethod.GET, "/api/config/globaldomain", (t) -> new ShowGlobalDomain());
        authenticate(HttpMethod.GET, "/api/config/domains", (t) -> new ListDomains());

        authenticate(HttpMethod.POST, "/api/config/streams", (t) -> new RegisterStream(configApi));
        authenticate(HttpMethod.GET, "/api/config/streams", (t) -> new ListStreams(configApi));

        webApp.route(HttpMethod.GET, "/api/stats/volumes", () -> new ListActiveVolumes(volumeStatistics));
        webApp.route(HttpMethod.POST, "/api/stats", () -> new RegisterVolumeStats(volumeStatistics));
        webApp.route(HttpMethod.GET, "/api/stats/volumes/:volume", () -> new DisplayVolumeStats(volumeStatistics));

        // [fm] Temporary
        webApp.route(HttpMethod.GET, "/api/config/user/:login/:password", () -> new CreateAdminUser(configApi));


        new Thread(() -> {
            try {
                new com.formationds.demo.Main().start(configuration.getDemoConfig());
            } catch (Exception e) {
                LOG.error("Couldn't start demo app", e);
            }
        }).start();

        int adminWebappPort = platformConfig.lookup("fds.om.admin_webapp_port").intValue();
        webApp.start(adminWebappPort);
    }

    private void authenticate(HttpMethod method, String route, Function<AuthenticationToken, RequestHandler> f) {
        webApp.route(method, route, () -> new HttpAuthenticator(f, xdi.getAuthenticator()));
    }
}


class CreateAdminUser implements RequestHandler {
    private ConfigurationService.Iface config;

    public CreateAdminUser(ConfigurationService.Iface config) {
        this.config = config;
    }

    @Override
    public Resource handle(Request request, Map<String, String> routeParameters) throws Exception {
        String login = requiredString(routeParameters, "login");
        String password = requiredString(routeParameters, "password");

        long id = config.createUser(login, new HashedPassword().hash(password), UUID.randomUUID().toString(), true);
        return new JsonResource(new JSONObject().put("id", id));
    }
}
