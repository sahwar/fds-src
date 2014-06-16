package com.formationds.am;
/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

import com.formationds.apis.AmService;
import com.formationds.apis.ConfigurationService;
import com.formationds.security.Authenticator;
import com.formationds.security.JaasAuthenticator;
import com.formationds.spike.nbd.FdsServerOperations;
import com.formationds.spike.nbd.NbdHost;
import com.formationds.util.Configuration;
import com.formationds.util.libconfig.ParsedConfig;
import com.formationds.xdi.CachingConfigurationService;
import com.formationds.xdi.FakeAmService;
import com.formationds.xdi.Xdi;
import com.formationds.xdi.XdiClientFactory;
import com.formationds.xdi.s3.S3Endpoint;
import com.formationds.xdi.swift.SwiftEndpoint;
import org.apache.thrift.server.TServer;
import org.apache.thrift.server.TThreadPoolServer;
import org.apache.thrift.transport.TServerSocket;

class FakeAmServer {
    public static void main(String[] args) throws Exception {
        AmService.Processor<AmService.Iface> processor = new AmService.Processor<>(new FakeAmService());
        TServerSocket transport = new TServerSocket(4242);
        TServer server = new TThreadPoolServer(new TThreadPoolServer.Args(transport).processor(processor));
        server.serve();
    }
}

public class Main {

    public static void main(String[] args) throws Exception {
        try {
            Configuration configuration = new Configuration("xdi", args);
            ParsedConfig amParsedConfig = configuration.getPlatformConfig();
            NativeAm.startAm(args);
            Thread.sleep(200);

            XdiClientFactory clientFactory = new XdiClientFactory();

            boolean useFakeAm = amParsedConfig.lookup("fds.am.memory_backend").booleanValue();
            String omHost = amParsedConfig.lookup("fds.am.om_ip").stringValue();
            int omPort = 9090;

            AmService.Iface am = useFakeAm ? new FakeAmService() :
                    clientFactory.remoteAmService("localhost", 9988);
                    //clientFactory.remoteAmService("localhost", 4242);

            ConfigurationService.Iface config = new CachingConfigurationService(clientFactory.remoteOmService(omHost, omPort));

            int nbdPort = amParsedConfig.lookup("fds.am.nbd_server_port").intValue();
            NbdHost nbdHost = new NbdHost(nbdPort, new FdsServerOperations(am, config));
            
            new Thread(() -> nbdHost.run()).start();

            Authenticator authenticator = new JaasAuthenticator();
            Xdi xdi = new Xdi(am, config, authenticator);

            boolean enforceAuthorization = amParsedConfig.lookup("fds.am.enforce_authorization").booleanValue();
            int s3Port = amParsedConfig.lookup("fds.am.s3_port").intValue();
            new Thread(() -> new S3Endpoint(xdi, enforceAuthorization).start(s3Port)).start();

            int swiftPort = amParsedConfig.lookup("fds.am.swift_port").intValue();
            new SwiftEndpoint(xdi, enforceAuthorization).start(swiftPort);
        } catch (Throwable throwable) {
            System.out.println(throwable.getMessage());
            System.out.flush();
            System.exit(-1);
        }
    }

}

