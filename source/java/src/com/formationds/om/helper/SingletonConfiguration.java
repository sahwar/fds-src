/*
 * Copyright (c) 2014, Formation Data Systems, Inc. All Rights Reserved.
 */

package com.formationds.om.helper;

import com.formationds.util.Configuration;

import java.io.Serializable;

/**
 * @author ptinius
 */
public class SingletonConfiguration
    implements Serializable {

    private static final long serialVersionUID = 493525199822398655L;

    /**
     * private constructor
     */
    private SingletonConfiguration() {
    }

    private static class SingletonConfigurationHolder {
        protected static final SingletonConfiguration INSTANCE = new SingletonConfiguration();
    }

    /**
     * @return Returns SingletonConfiguration instance
     */
    public static SingletonConfiguration instance() {
        return SingletonConfigurationHolder.INSTANCE;
    }

    /**
     * @return Returns the {@link com.formationds.om.helper.SingletonConfiguration
     */
    protected Object readResolve() {
        return instance();
    }

    private Configuration config;

    /**
     * @return Returns the {@link com.formationds.util.Configuration}
     */
    public Configuration getConfig() {
        return config;
    }

    /**
     * @param config the {@link com.formationds.util.Configuration}
     */
    public void setConfig( final Configuration config ) {
        this.config = config;
    }
}
