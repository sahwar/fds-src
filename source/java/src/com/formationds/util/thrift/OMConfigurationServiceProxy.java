/*
 * Copyright (c) 2015 Formation Data Systems. All rights Reserved.
 */
package com.formationds.util.thrift;

import com.formationds.apis.VolumeSettings;
import com.formationds.security.AuthenticatedRequestContext;
import com.formationds.security.AuthenticationToken;
import org.apache.log4j.Logger;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;
import java.lang.reflect.Proxy;
import java.util.Arrays;

/**
 * Proxy calls to the ConfigurationService interface in the OM to intercept
 * create/delete operations that must go through the Java OM for cluster management
 * and consensus.
 */
// TODO: ultimately this needs to be modified to ensure it goes through the cluster leader.
public class OMConfigurationServiceProxy implements InvocationHandler {

    private static final Logger logger = Logger.getLogger(OMConfigurationServiceProxy.class);

    /**
     *
     * @param omConfigServiceClient
     * @param configApi
     * @return
     */
    public static ConfigurationApi newOMConfigProxy(OMConfigServiceClient omConfigServiceClient,
                                                    ConfigurationApi configApi) {
        return (ConfigurationApi) Proxy.newProxyInstance(OMConfigurationServiceProxy.class.getClassLoader(),
                                              new Class<?>[] {ConfigurationApi.class},
                                              new OMConfigurationServiceProxy(omConfigServiceClient,
                                                                              configApi));
    }

    private final ConfigurationApi configurationApi;
    private OMConfigServiceClient omConfigServiceClient;

    private OMConfigurationServiceProxy( OMConfigServiceClient omConfigServiceClient,
                                         ConfigurationApi configurationApi) {

        this.configurationApi = configurationApi;
        this.omConfigServiceClient = omConfigServiceClient;
    }

    @Override
    public Object invoke(Object proxy, Method method, Object[] args) throws Throwable {

        long start = System.currentTimeMillis();
        try {
            if (logger.isTraceEnabled()) {
                logger.trace(String.format("CONFIGPROXY::START: %s(%s)",
                                           method.getName(),
                                           Arrays.toString(args)));
            }
            switch (method.getName()) {

                // METHODS That should be redirected to the OM Rest API
                case "createVolume":
                    doCreateVolume(args);
                    return null;

                case "deleteVolume":
                    doDeleteVolume(args);
                    return null;

                // METHODS that are supported by OM Rest Client, but we are allowing
                // to go direct to the configuration api
                case "getVolumeId":
                case "statVolume":
                case "listVolumes":

                    // METHODS that *should* probably be redirected but are not yet
                    // supported by OM REST Client
                case "createTenant":
                case "createUser":
                case "assignUserToTenant":
                case "revokeUserFromTenant":
                case "updateUser":
                case "cloneVolume":
                case "createSnapshot":
                case "restoreClone":

                    // Other methods can go direct to OM Thrift Server
                case "listTenants":
                case "allUsers":
                case "listUsersForTenant":
                case "configurationVersion":
                case "getVolumeName":
                case "registerStream":
                case "getStreamRegistrations":
                case "deregisterStream":
                case "createSnapshotPolicy":
                case "listSnapshotPolicies":
                case "deleteSnapshotPolicy":
                case "attachSnapshotPolicy":
                case "listSnapshotPoliciesForVolume":
                case "detachSnapshotPolicy":
                case "listVolumesForSnapshotPolicy":
                case "listSnapshots":
                default:
                    return method.invoke(configurationApi, args);
            }
        } finally {
            if (logger.isTraceEnabled()) {
                logger.trace(String.format("CONFIGPROXY::COMPLETED: %s(%s) in %s ms",
                                           method.getName(),
                                           Arrays.toString(args),
                                           System.currentTimeMillis() - start));
            }
        }
    }

    private void doCreateVolume(Object... args) throws OMConfigException {
        AuthenticationToken token = AuthenticatedRequestContext.getToken();
        String domain = (String)args[0];
        String name = (String)args[1];
        VolumeSettings volumeSettings = (VolumeSettings)args[2];
        long tenantId = (Long)args[3];
        omConfigServiceClient.createVolume(token,
                                           domain,
                                           name,
                                           volumeSettings,
                                           tenantId);
    }

    private void doDeleteVolume(Object... args) throws OMConfigException {
        AuthenticationToken token = AuthenticatedRequestContext.getToken();
        String domain = (String)args[0];
        String name = (String)args[1];
        omConfigServiceClient.deleteVolume(token, domain, name);
    }
}
