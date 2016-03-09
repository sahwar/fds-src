/*
 * Copyright 2015 Formation Data Systems, Inc.
 */
package com.formationds.security;

import com.formationds.apis.User;
import com.formationds.apis.VolumeDescriptor;
import com.formationds.protocol.ApiException;
import com.formationds.protocol.ErrorCode;
import com.formationds.util.thrift.ConfigurationApi;
import org.apache.thrift.TException;
import org.apache.logging.log4j.Logger;
import org.apache.logging.log4j.LogManager;

public class FdsAuthorizer implements Authorizer {
    private static final Logger logger = LogManager.getLogger(FdsAuthorizer.class);

    private ConfigurationApi config;

    public FdsAuthorizer(ConfigurationApi config) {
        this.config = config;
    }

    @Override
    public long tenantId(AuthenticationToken token) throws SecurityException {
        User user = userFor(token);
        if (user.isIsFdsAdmin()) {
            return 0;
        }
        return config.tenantId(user.getId());
    }

  /**
   * @param token the authentication token
   * @param snapshotName the name of the snapshot
   * @return Returns {@code true} is snapshot is owned by; Otherwise {@code false}
   */
  @Override
  public boolean ownsSnapshot( final AuthenticationToken token,
                               final String snapshotName )
  {
    return !AuthenticationToken.ANONYMOUS.equals( token ) && isAdmin( token );
  }

  private boolean isAdmin( final AuthenticationToken token ) {
      return ( token != null ) && userFor( token ).isIsFdsAdmin( );
  }

  @Override
    public boolean ownsVolume(AuthenticationToken token, String volume) throws SecurityException {
        if (AuthenticationToken.ANONYMOUS.equals(token)) {
            return false;
        }
        try {
            VolumeDescriptor v = config.statVolume("", volume);
            if (v == null) {
                if (logger.isTraceEnabled()) {
                    logger.trace("AUTHZ::HASACCESS::DENIED " + volume + " not found.");
                }
                return false;
            }

            User user = userFor(token);
            if (user.isIsFdsAdmin()) {
                if (logger.isTraceEnabled()) {
                    logger.trace("AUTHZ::HASACCESS::GRANTED " + volume + " (admin)");
                }
                return true;
            }

            long volTenantId = v.getTenantId();
            long userTenantId = tenantId(token);

            if (userTenantId == volTenantId) {
                if (logger.isTraceEnabled()) {
                    logger.trace("AUTHZ::HASACCESS::GRANTED volume={} userid={}", volume, token.getUserId());
                }

                return true;
            } else {
                if (logger.isTraceEnabled()) {
                    logger.trace("AUTHZ::HASACCESS::DENIED volume={} userid={}", volume, token.getUserId());
                }

                return false;
            }
        } catch (ApiException apie) {
            if (apie.getErrorCode().equals(ErrorCode.MISSING_RESOURCE)) {
                if (logger.isTraceEnabled()) {
                    logger.trace("AUTHZ::HASACCESS::DENIED " + volume + " not found.");
                }
                return false;
            }
            throw new IllegalStateException("Failed to access config service.", apie);

        } catch (TException e) {
            throw new IllegalStateException("Failed to access config service.", e);
        }
    }

    @Override
    public User userFor(AuthenticationToken token) throws SecurityException {
        User user = config.getUser(token.getUserId());
        if (user == null) {
            if (logger.isTraceEnabled()) {
                logger.trace("AUTHZ::HASACCESS::DENIED user userid={} not found.", token.getUserId());
            }

            throw new SecurityException();
        } else if (!user.getSecret().equals(token.getSecret())) {
            throw new SecurityException();
        }
        return user;
    }
}
