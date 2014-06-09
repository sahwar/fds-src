package com.formationds.util;
/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

import com.formationds.util.libconfig.ParsedConfig;
import joptsimple.OptionParser;
import joptsimple.OptionSet;
import org.apache.log4j.PropertyConfigurator;

import java.io.File;
import java.io.IOException;
import java.net.URL;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Properties;

public class Configuration {
    Properties properties = new Properties();
    private File fdsRoot;
    private static final String[] LOGLEVELS = new String[] {
            "TRACE",
            "DEBUG",
            "INFO",
            "WARN",
            "ERROR",
            "FATAL"
    };

    public Configuration(String commandName, String[] commandLineArgs) throws Exception {
        OptionParser parser = new OptionParser();
        parser.allowsUnrecognizedOptions();
        parser.accepts("fds-root").withRequiredArg();
        parser.accepts("console");
        OptionSet options = parser.parse(commandLineArgs);
        if (options.has("fds-root")) {
            fdsRoot = new File((String) options.valueOf("fds-root"));
        } else {
            fdsRoot = new File("/fds");
        }

        int logLevel = getPlatformConfig().defaultInt("fds.plat.log_severity", 0);

        if (options.has("console")) {
            initConsoleLogging(LOGLEVELS[logLevel]);
        } else {
            initFileLogging(commandName, fdsRoot, LOGLEVELS[logLevel]);
        }

        initJaas(fdsRoot);
    }

    private void initConsoleLogging(String loglevel) {
        properties.put("log4j.rootCategory", "FATAL, console");
        properties.put("log4j.appender.console", "org.apache.log4j.ConsoleAppender");
        properties.put("log4j.appender.console.layout", "org.apache.log4j.PatternLayout");
        properties.put("log4j.appender.console.layout.ConversionPattern", "%-4r [%t] %-5p %c %x - %m%n");
        properties.put("log4j.logger.com.formationds", loglevel);
        PropertyConfigurator.configure(properties);
    }

    private void initFileLogging(String commandName, File fdsRoot, String loglevel) {
        Path logPath = Paths.get(fdsRoot.getAbsolutePath(), "var", "logs", commandName + ".log").toAbsolutePath();
        properties.put("log4j.rootLogger", "FATAL, rolling");
        properties.put("log4j.appender.rolling", "org.apache.log4j.RollingFileAppender");
        properties.put("log4j.appender.rolling.File", logPath.toString());
        properties.put("log4j.appender.rolling.MaxFileSize", "5120KB");
        properties.put("log4j.appender.rolling.MaxBackupIndex", "10");
        properties.put("log4j.appender.rolling.layout", "org.apache.log4j.PatternLayout");
        properties.put("log4j.appender.rolling.layout.ConversionPattern", "[%t] %-5p %l - %m%n");
        properties.put("log4j.appender.rolling.layout.ConversionPattern", "%d{ISO8601} - %p %c - %m%n");
        properties.put("log4j.logger.com.formationds", loglevel);
        PropertyConfigurator.configure(properties);
    }

    private void initJaas(File fdsRoot) throws Exception {
        Path jaasConfig = Paths.get(fdsRoot.getCanonicalPath(), "etc", "auth.conf");
        URL configUrl = jaasConfig.toFile().toURL();
        System.setProperty("java.security.auth.login.config", configUrl.toExternalForm());
    }

    public boolean enforceRestAuth() {
        return getOmConfig().lookup("fds.om.enforce_rest_auth").booleanValue();
    }

    public String getFdsRoot() {
        return fdsRoot.getAbsolutePath();
    }

    public ParsedConfig getPlatformConfig() {
        Path path = Paths.get(getFdsRoot(), "etc", "platform.conf");
        return getParserFacade(path);
    }

    public ParsedConfig getOmConfig() {
        Path path = Paths.get(getFdsRoot(), "etc", "orch_mgr.conf");
        return getParserFacade(path);
    }

    public ParsedConfig getDemoConfig() {
        Path path = Paths.get(getFdsRoot(), "etc", "demo.conf");
        return getParserFacade(path);
    }

    private ParsedConfig getParserFacade(Path path) {
        try {
            return new ParsedConfig(Files.newInputStream(path));
        } catch (IOException e) {
            throw new RuntimeException(e);
        }
    }
}
