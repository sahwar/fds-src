package com.formationds.xdi.swift;
/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

import com.formationds.security.AuthenticationToken;
import com.formationds.security.AuthenticationTokenTest;
import com.formationds.web.toolkit.RequestHandler;
import com.formationds.web.toolkit.Resource;
import com.formationds.web.toolkit.TextResource;
import com.formationds.xdi.Xdi;
import com.google.common.collect.Maps;
import org.eclipse.jetty.server.Request;
import org.junit.Test;

import javax.security.auth.login.LoginException;
import javax.servlet.http.HttpServletResponse;
import java.util.Map;

import static org.junit.Assert.assertEquals;
import static org.mockito.Matchers.anyString;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

public class SwiftAuthenticatorTest {
    @Test
    public void testNoHeader() throws Exception {
        Xdi xdi = mock(Xdi.class);
        SwiftAuthenticator authorizer = new SwiftAuthenticator(() -> new StubHandler(), xdi);

        Resource result = authorizer.handle(mock(Request.class), Maps.newHashMap());
        assertEquals(HttpServletResponse.SC_UNAUTHORIZED, result.getHttpStatus());
    }

    @Test
    public void testMalformedAuthHeader() throws Exception {
        Xdi xdi = mock(Xdi.class);
        when(xdi.resolveToken(anyString())).thenThrow(LoginException.class);
        SwiftAuthenticator authorizer = new SwiftAuthenticator(() -> new StubHandler(), xdi);
        Request request = mock(Request.class);
        when(request.getHeader(GrantAuthenticationToken.X_AUTH_TOKEN)).thenReturn("poop");
        Resource result = authorizer.handle(request, Maps.newHashMap());
        assertEquals(HttpServletResponse.SC_UNAUTHORIZED, result.getHttpStatus());
    }

    @Test
    public void testWellFormedHeader() throws Exception {
        Request request = mock(Request.class);
        Xdi xdi = mock(Xdi.class);
        AuthenticationToken token = new AuthenticationToken(1, "foo");
        when(xdi.resolveToken(anyString())).thenReturn(token);
        String signature = token.signature(AuthenticationTokenTest.SECRET_KEY);
        when(request.getHeader(GrantAuthenticationToken.X_AUTH_TOKEN)).thenReturn(signature);
        SwiftAuthenticator authorizer = new SwiftAuthenticator(() -> new StubHandler(), xdi);
        Resource result = authorizer.handle(request, Maps.newHashMap());
        assertEquals(HttpServletResponse.SC_OK, result.getHttpStatus());
    }

    class StubHandler implements RequestHandler {
        @Override
        public Resource handle(Request request, Map<String, String> routeParameters) throws Exception {
            return new TextResource("");
        }
    }
}
