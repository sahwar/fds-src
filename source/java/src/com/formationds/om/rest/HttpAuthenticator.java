package com.formationds.om.rest;
/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

import com.formationds.security.AuthenticationToken;
import com.formationds.security.Authenticator;
import com.formationds.web.toolkit.JsonResource;
import com.formationds.web.toolkit.RequestHandler;
import com.formationds.web.toolkit.Resource;
import org.apache.commons.lang.StringUtils;
import org.apache.log4j.Logger;
import org.eclipse.jetty.server.Request;
import org.json.JSONObject;

import javax.servlet.http.Cookie;
import javax.servlet.http.HttpServletResponse;
import java.util.Arrays;
import java.util.Map;
import java.util.Optional;
import java.util.function.Function;

public class HttpAuthenticator implements RequestHandler {
    private static final Logger LOG = Logger.getLogger(HttpAuthenticator.class);
    public static final String FDS_TOKEN = "token";
    private Function<AuthenticationToken, RequestHandler> f;
    private Authenticator authenticator;

    public HttpAuthenticator(Function<AuthenticationToken, RequestHandler> f, Authenticator authenticator) {
        this.f = f;
        this.authenticator = authenticator;
    }


    @Override
    public Resource handle(Request request, Map<String, String> routeParameters) throws Exception {

        if (authenticator.allowAll()) {
            return f.apply(AuthenticationToken.ANONYMOUS).handle(request, routeParameters);
        }

        Optional<AuthenticationToken> fromHeaders = parseHeaders(request);
        if (fromHeaders.isPresent()) {
            return f.apply(fromHeaders.get()).handle(request, routeParameters);
        }

        Cookie[] cookies = request.getCookies() == null ? new Cookie[0] : request.getCookies();
        Optional<Cookie> result = Arrays.stream(cookies)
                .filter(c -> FDS_TOKEN.equals(c.getName()))
                .findFirst();

        if (!result.isPresent()) {
            return new JsonResource(new JSONObject().put("message", "Invalid credentials"), HttpServletResponse.SC_UNAUTHORIZED);
        }

        try {
            AuthenticationToken token = authenticator.resolveToken(result.get().getValue());
            return f.apply(token).handle(request, routeParameters);
        } catch (Exception e) {
            LOG.error("Authentication error", e);
            return new JsonResource(new JSONObject().put("message", "Invalid credentials"), HttpServletResponse.SC_UNAUTHORIZED);
        }
    }

    private Optional<AuthenticationToken> parseHeaders(Request request) {
        String headerValue = request.getHeader("FDS-Auth");
        if (StringUtils.isBlank(headerValue)) {
            return Optional.empty();
        }

        try {
            return Optional.of(authenticator.resolveToken(headerValue));
        } catch (Exception e) {
            return Optional.empty();
        }
    }
}
