package com.formationds.util;
/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

import com.formationds.util.libconfig.ParsedConfig;
import com.sun.management.HotSpotDiagnosticMXBean;
import joptsimple.OptionParser;
import joptsimple.OptionSet;
import org.apache.log4j.PropertyConfigurator;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.File;
import java.io.IOException;
import java.lang.management.ManagementFactory;
import java.net.URL;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.text.ParseException;
import java.util.HashMap;
import java.util.Map;
import java.util.Properties;

public class Configuration {
    public static final String KEYSTORE_PATH = "fds.ssl.keystore_path";
    public static final String KEYSTORE_PASSWORD = "fds.ssl.keystore_password";
    public static final String KEYMANAGER_PASSWORD = "fds.ssl.keymanager_password";
    private static final Map<String, String> LOGLEVELS = new HashMap<>();

    //trace/debug/normal/info/notify/notification/crit/critical,warn/warning/error
    static {
        LOGLEVELS.put("trace", "TRACE");
        LOGLEVELS.put("debug", "DEBUG");
        LOGLEVELS.put("normal", "INFO");
        LOGLEVELS.put("notification", "INFO");
        LOGLEVELS.put("warning", "WARN");
        LOGLEVELS.put("error", "ERROR");
        LOGLEVELS.put("critical", "FATAL");
    }

    private final String commandName;
    private Properties properties = new Properties();
    private File fdsRoot;

    public Configuration(String commandName, String[] commandLineArgs) {
        this.commandName = commandName;

        OptionParser parser = new OptionParser();
        parser.allowsUnrecognizedOptions();
        parser.accepts("fds-root").withRequiredArg();
        parser.accepts("console");
        parser.accepts("fds.am.instanceId").withRequiredArg().ofType(Integer.class);
        OptionSet options = parser.parse(commandLineArgs);
        if (options.has("fds-root")) {
            fdsRoot = new File((String) options.valueOf("fds-root"));
        } else {
            fdsRoot = new File("/fds");
        }

        String logLevel = getPlatformConfig().defaultString("fds.plat.log_severity", "normal").toLowerCase();
        // Get the instance ID from either the config file or cmd line
        int amInstanceId = getPlatformConfig().defaultInt("fds.am.instanceId", 0);
        if (options.has("fds.am.instanceId")) {
            amInstanceId = (int)options.valueOf("fds.am.instanceId");
        }

        if (options.has("console")) {
            //initConsoleLogging(LOGLEVELS.getOrDefault(logLevel, "INFO"));
            initConsoleLogging("DEBUG");
        } else {
            // only append instance name on the am (xdi)
            String logName = (commandName.startsWith("om") ?
                              commandName :
                              commandName + Integer.toString( amInstanceId ));
            initFileLogging(logName, fdsRoot, LOGLEVELS.getOrDefault(logLevel, "INFO"));
        }

        initDiagnostics(fdsRoot);

        initJaas(fdsRoot);
    }

    private void initDiagnostics(File fdsRoot)
    {
        Logger logger = LoggerFactory.getLogger( Configuration.class );
        if ( ManagementFactory.getRuntimeMXBean().getVmVendor().toLowerCase().contains( "oracle" ) )
        {
            try
            {
                String vmName = ManagementFactory.getRuntimeMXBean().getName();
                String pidS = vmName.split( "@" )[0];
                HotSpotDiagnosticMXBean hotspot = ManagementFactory.getPlatformMXBean( HotSpotDiagnosticMXBean.class );
                hotspot.setVMOption( "HeapDumpOnOutOfMemoryError", "true" );
                hotspot.setVMOption( "HeapDumpPath",
                                     String.format( "%s/%s_java_pid%s.hprof",
                                                    fdsRoot,
                                                    commandName,
                                                    pidS ) );
            }
            catch (Exception e)
            {
                logger.warn( "Failed to set the JVM diagnostic options.  Running with default diagnostics", e );
            }
        }
        else
        {
            logger.warn( "Unexpected JVM Vendor name '{}'.  Oracle JVM is expected in order to init JVM diagnostics." );
        }

    }

    private void initConsoleLogging(String loglevel) {
        properties.put("log4j.rootCategory", "INFO, console");
        properties.put("log4j.appender.console", "org.apache.log4j.ConsoleAppender");
        properties.put("log4j.appender.console.layout", "org.apache.log4j.PatternLayout");
        properties.put("log4j.appender.console.layout.ConversionPattern", "%-4r [%t] %-5p %c %x - %m%n");
        properties.put("log4j.logger.com.formationds", loglevel);
        //properties.put("log4j.logger.com.formationds.web.toolkit.Dispatcher", "WARN");
        PropertyConfigurator.configure(properties);
    }

    private void initFileLogging(String commandName, File fdsRoot, String loglevel) {
        Path logPath = Paths.get(fdsRoot.getAbsolutePath(), "var", "logs", commandName + ".log").toAbsolutePath();
        properties.put("log4j.rootLogger", "FATAL, rolling");
        properties.put("log4j.appender.rolling", "org.apache.log4j.RollingFileAppender");
        properties.put("log4j.appender.rolling.File", logPath.toString());
        properties.put("log4j.appender.rolling.MaxFileSize", "50MB");
        properties.put("log4j.appender.rolling.MaxBackupIndex", "10");
        properties.put("log4j.appender.rolling.layout", "org.apache.log4j.PatternLayout");
//        properties.put("log4j.appender.rolling.layout.ConversionPattern", "[%t] %-5p %l - %m%n");
//        properties.put("log4j.appender.rolling.layout.ConversionPattern", "%d{ISO8601} - %p %c - %m%n");
        properties.put("log4j.appender.rolling.layout.ConversionPattern", "%d{dd MMM yyyy HH:mm:ss.SSS z} - %p %c - %m%n");
        properties.put("log4j.logger.com.formationds", loglevel);
        //properties.put("log4j.logger.com.formationds.web.toolkit.Dispatcher", "WARN");
        PropertyConfigurator.configure(properties);
    }

    private void initJaas(File fdsRoot) {
        try {

            Path jaasConfig = Paths.get( fdsRoot.getCanonicalPath(),
                                         "etc",
                                         "auth.conf" );
            URL configUrl = jaasConfig.toFile().toURI().toURL();
            System.setProperty( "java.security.auth.login.config",
                                configUrl.toExternalForm() );

        } catch( IOException e ) {

            throw new RuntimeException(e);

        }
    }

    public String getFdsRoot() {
        return fdsRoot.getAbsolutePath();
    }

    public ParsedConfig getPlatformConfig() {

        Path path = Paths.get(getFdsRoot(), "etc", "platform.conf");
        return getParserFacade(path);

    }

    /**
     * @return Returns the demo configuration
     * @deprecated Will be removed very soon.
     */
    @Deprecated
    public ParsedConfig getDemoConfig() {

        Path path = Paths.get( getFdsRoot(), "etc", "demo.conf" );
        return getParserFacade(path);

    }

    private ParsedConfig getParserFacade( final Path path ) {
        try {

            return new ParsedConfig(Files.newInputStream(path));

        } catch( ParseException | IOException e ) {

            throw new RuntimeException( e );

        }
    }
}
