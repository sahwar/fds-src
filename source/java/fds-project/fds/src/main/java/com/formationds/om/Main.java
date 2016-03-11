/*
 * Copyright (c) 2015, Formation Data Systems, Inc. All Rights Reserved.
 */
package com.formationds.om;

import com.formationds.apis.ConfigurationService;
import com.formationds.util.thrift.ThriftClientFactory;
import com.formationds.commons.libconfig.Assignment;
import com.formationds.commons.libconfig.ParsedConfig;
import com.formationds.commons.togglz.feature.flag.FdsFeatureToggles;
import com.formationds.nfs.XdiStaticConfiguration;
import com.formationds.om.events.EventManager;
import com.formationds.om.helper.SingletonAmAPI;
import com.formationds.om.helper.SingletonConfigAPI;
import com.formationds.om.helper.SingletonConfiguration;
import com.formationds.om.repository.SingletonRepositoryManager;
import com.formationds.om.snmp.SnmpManager;
import com.formationds.om.snmp.TrapSend;
import com.formationds.om.webkit.WebKitImpl;
import com.formationds.platform.svclayer.OmSvcHandler;
import com.formationds.platform.svclayer.SvcMgr;
import com.formationds.platform.svclayer.SvcServer;
import com.formationds.platform.svclayer.ThriftServiceDescriptor;
import com.formationds.protocol.ApiException;
import com.formationds.protocol.commonConstants;
import com.formationds.protocol.svc.types.FDSP_MgrIdType;
import com.formationds.protocol.om.OMSvc.Iface;
import com.formationds.security.Authenticator;
import com.formationds.security.Authorizer;
import com.formationds.security.DumbAuthorizer;
import com.formationds.security.FdsAuthenticator;
import com.formationds.security.FdsAuthorizer;
import com.formationds.security.NullAuthenticator;
import com.formationds.util.Configuration;
import com.formationds.util.ServerPortFinder;
import com.formationds.util.thrift.ConfigServiceClientFactory;
import com.formationds.xdi.AsyncAm;
import com.formationds.xdi.FakeAsyncAm;
import com.formationds.xdi.RealAsyncAm;
import com.nurkiewicz.asyncretry.AsyncRetryExecutor;
import org.apache.commons.codec.binary.Hex;
import org.apache.thrift.TMultiplexedProcessor;
import org.apache.thrift.TProcessor;
import org.apache.thrift.transport.TTransportException;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import javax.crypto.SecretKey;
import javax.crypto.spec.SecretKeySpec;
import java.net.ConnectException;
import java.util.HashMap;
import java.util.Map;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;

public class Main {
    private static final Logger logger = LoggerFactory.getLogger( Main.class );

    // key for managing the singleton EventManager.
    private final Object eventMgrKey = new Object();

    // OM Service Layer proxy server is enabled based on the OM_SERVICE_PROXY
    // feature toggle, but also depends on the platform.conf feature toggle (that
    // the C++ side uses).
    private Optional<SvcServer<Iface>> proxyServer = Optional.empty();

    public static void main( String[] args ) {

        try {

            Configuration cfg = new Configuration( "om-xdi", args );
            new Main( cfg ).start( args );
        } catch ( Throwable t ) {

            logger.error( "Error starting OM", t );
            System.out.println( t.getMessage() );
            System.out.flush();
            System.exit( -1 );
        }
    }

    public Main( Configuration cfg ) {
        SingletonConfiguration.instance().setConfig( cfg );
    }

    protected boolean isOMProxyServerEnabled( ) {
        return FdsFeatureToggles.OM_SERVICE_PROXY.isActive();
    }

    public void start( String[] args )
        throws Exception {

        final Configuration configuration = SingletonConfiguration.getConfig();

        // Configuration class ensures that fds-root system property is defined.
        logger.trace( "FDS-ROOT: " + configuration.getFdsRoot() );
        logger.trace( "OM UUID: " + configuration.getOMUuid() );

        logger.trace( "Loading platform configuration." );
        ParsedConfig platformConfig = configuration.getPlatformConfig();

        // the base platform port that is used for determining all of the service default ports except for the OM
        int pmPort = platformConfig.defaultInt( "fds.pm.platform_port", 7000 );

        // OM port is handled differently.  The default value is still PM + SvcId(4) = 7004, but it
        // is controlled through the fds.common.om_port setting.
        int omPort = platformConfig.defaultInt( "fds.common.om_port",
                                                SvcMgr.mapToServicePort( pmPort,
                                                                         FDSP_MgrIdType.FDSP_ORCH_MGR ) );

        if ( isOMProxyServerEnabled( ) ) {

            int proxyPortOffset = platformConfig.defaultInt( "fds.om.java_svc_proxy_port_offset", 1900 );
            int proxyToPort = omPort + proxyPortOffset;  // 8904 by default
            logger.trace( "Starting OM Service Proxy {} -> {}", omPort, proxyToPort );

            /**
             * Empty, unless the server is multiplexed.
             * Note on Thrift service compatibility:
             *
             * For service that extends PlatNetSvc, add the processor twice using
             * Thrift service name as the key and again using 'PlatNetSvc' as the
             * key. Only ONE major API version is supported for PlatNetSvc.
             *
             * All other services:
             * Add Thrift service name and a processor for each major API version
             * supported.
             */
            Map<String, TProcessor> processors = new HashMap<String, TProcessor>();

            /**
             * FEATURE TOGGLE: enable multiplexed services
             * Tue Feb 23 15:04:17 MST 2016
             */
            if ( FdsFeatureToggles.THRIFT_MULTIPLEXED_SERVICES.isActive() ) {
                // Multiplexed
                OmSvcHandler handler = new OmSvcHandler( "localhost", proxyToPort, commonConstants.OM_SERVICE_NAME );
                ThriftServiceDescriptor serviceDescriptor = ThriftServiceDescriptor.newDescriptor(
                    handler.getClass().getInterfaces()[0].getEnclosingClass() );
                TProcessor processor = (TProcessor) serviceDescriptor.getProcessorFactory().apply( handler );
                // Handles requests from OMSvcClient
                processors.put( commonConstants.OM_SERVICE_NAME, processor );
                // It is common for SvcLayer to route asynchronous requets using an
                // instance of PlatNetSvcClient. When using a multiplexed server, the
                // processor map must have a key for PlatNetSvc.
                processors.put( commonConstants.PLATNET_SERVICE_NAME, processor );
                // The type variable of SvcServer will be a class or interface
                // that extends PlatNetSvc.Iface. This fact may not make sense
                // for a multiplexed server, but we will not redesign SvcServer
                // at this time.
                proxyServer = Optional.of( new SvcServer<>( omPort, handler, processors, true ) );
            } else {
                // Non-multiplexed
                proxyServer = Optional.of( new SvcServer<>( omPort,
                                                new OmSvcHandler( "localhost", proxyToPort, "" ),
                                                processors, false ) );
            }
            proxyServer.get().startAndWait( 5, TimeUnit.MINUTES );

        }

        logger.trace( "Starting native OM" );
        NativeOm.startOm( args );

        logger.trace( "Initializing the repository manager." );
        SingletonRepositoryManager.instance().initializeRepositories();

        logger.trace( "Initializing repository event notifier." );
        EventManager.INSTANCE
            .initEventNotifier(
                                  eventMgrKey,
                                  ( e ) -> {

                                      Assignment snmpTarget =
                                          platformConfig.lookup( TrapSend.SNMP_TARGET_KEY );
                                      if ( snmpTarget.getValue().isPresent() ) {
                                          System.setProperty( TrapSend.SNMP_TARGET_KEY,
                                                                    snmpTarget.stringValue() );
                                                SnmpManager.instance()
                                                           .notify( e );
                                            } else {
                                                SnmpManager.instance()
                                                           .disable( SnmpManager.DisableReason
                                                                         .MISSING_CONFIG );
                                            }

                                            return (SingletonRepositoryManager.instance()
                                                                              .getEventRepository()
                                                                              .save( e ) != null);
                                        }
                    );

        // initialize the firebreak event listener (callback from repository persist)
        EventManager.INSTANCE.initEventListeners();

        int omConfigPort = platformConfig.defaultInt( "fds.om.config_port",
                                                      9090 );

        String omHost = configuration.getOMIPAddress( );

        ThriftClientFactory<ConfigurationService.Iface> configApiFactory =
            ConfigServiceClientFactory.newConfigService( omHost, omConfigPort );

        logger.debug( "Attempting to connect to configuration service, on {}:{}.",
                      omHost, omConfigPort );
        final AsyncRetryExecutor asyncRetryExecutor =
            new AsyncRetryExecutor(
                Executors.newSingleThreadScheduledExecutor( ) );

        asyncRetryExecutor.withExponentialBackoff( 500, 2 )
                          .withMaxDelay( 10_000 )
                          .withUniformJitter( )
                          .retryInfinitely()
                          .retryOn( ExecutionException.class )
                          .retryOn( RuntimeException.class )
                          .retryOn( TTransportException.class )
                          .retryOn( ConnectException.class )
            /*
             * retry on ApiException, because we try and get the configuration
             * version number. Which can fail because the native OM isn't
             * ready yet.
             */
                          .retryOn( ApiException.class );

        final CompletableFuture<OmConfigurationApi> completableFuture =
            asyncRetryExecutor.getWithRetry(
                ( ) -> new OmConfigurationApi( configApiFactory ) );

        final OmConfigurationApi configCache = completableFuture.get( );

        if( configCache != null )
        {
            logger.debug( "Successful connected to configuration service, on {}:{}.",
                          omHost, omConfigPort );
        }
        else
        {
            throw new RuntimeException(
                "FAILED: not able to connect to configuration service, on" +
                omHost + ":" + omConfigPort );
        }

        final int configUpdateIntervalMS = platformConfig.defaultInt( "fds.om.config_cache_update_interval_ms",
                                                                      10000 );
        configCache.startConfigurationUpdater( configUpdateIntervalMS );
        SingletonConfigAPI.instance().api( configCache );

        EnsureAdminUser.bootstrapAdminUser( configCache );

        /**
         * if volume grouping is disabled, use the statVolume call against the am host defined in
         * the platform.conf file.
         */
        if( !FdsFeatureToggles.USE_VOLUME_GROUPING.isActive() )
        {
            // TODO: should there be an OM property for the am host?
            String amHost = platformConfig.defaultString( "fds.xdi.am_host", "localhost" );

            int xdiServicePortOffset = platformConfig.defaultInt( "fds.am.xdi_service_port_offset",
                                                                  1899 );
            int xdiServicePort = pmPort + xdiServicePortOffset;

            int xdiResponsePortOffset = platformConfig.defaultInt( "fds.om.xdi_response_port_offset", 2988 );
            int xdiResponsePort = new ServerPortFinder( ).findPort( "Async XDI response port",
                                                                    pmPort + xdiResponsePortOffset );

            XdiStaticConfiguration xdiStaticConfig = configuration.getXdiStaticConfig( pmPort );

            boolean useFakeAm = platformConfig.defaultBoolean( "fds.am.memory_backend", false );
            AsyncAm asyncAm = useFakeAm ? new FakeAsyncAm( ) : new RealAsyncAm( amHost,
                                                                                xdiServicePort,
                                                                                xdiResponsePort,
                                                                                xdiStaticConfig.getAmTimeout( ) );
            SingletonAmAPI.instance( )
                          .api( asyncAm );
        }

        /*
         * TODO(Tinius) currently we only support a single OM
         */

        String webDir = platformConfig.defaultString( "fds.om.web_dir",
                                                      "../lib/admin-webapp" );



        char[] aesKey = platformConfig.defaultString( "fds.aes_key", "" ).toCharArray();
        if (aesKey.length == 0) {
            throw new RuntimeException( "The required configuration key 'fds.aes.key' was not found." );
        }
        byte[] keyBytes = Hex.decodeHex( aesKey );
        SecretKey secretKey = new SecretKeySpec( keyBytes, "AES" );

        final boolean enforceAuthentication = platformConfig.defaultBoolean( "fds.authentication", true );
        Authenticator authenticator =
            enforceAuthentication ? new FdsAuthenticator( configCache,
                                                          secretKey )
                                  : new NullAuthenticator();

        Authorizer authorizer = enforceAuthentication
            ? new FdsAuthorizer( configCache )
            : new DumbAuthorizer();

        int httpPort = platformConfig.defaultInt( "fds.om.http_port", 7777 );
        int httpsPort = platformConfig.defaultInt( "fds.om.https_port", 7443 );

        // TODO: pass om host/port (or url) to stat stream registration handler
        // create and start the statistics stream registration handler to manage stat
        // stream register/deregister requests
        /*
         * TODO(Tinius) should be using the https port here, but requires more SSL certs ( AM service )
         */
        configCache.createStatStreamRegistrationHandler( omHost,
                                                         httpPort,
                                                         configuration );

        logger.info( "Starting Web toolkit" );

        WebKitImpl originalImpl = new WebKitImpl( authenticator,
                    authorizer,
                    webDir,
                    httpPort,
                    httpsPort,
                    secretKey );
        originalImpl.start();
    }
}

