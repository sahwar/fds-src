package com.formationds.web.toolkit;

import org.eclipse.jetty.server.Request;
import org.eclipse.jetty.util.MultiMap;
import org.junit.Test;

import static org.junit.Assert.assertEquals;

/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
public class RouteFinderTest {
    @Test
    public void testWithWildcards() throws Exception {
        RouteFinder routeFinder = new RouteFinder();
        routeFinder.route(HttpMethod.GET, "/foo/:bar", () -> new Foo());
        routeFinder.route(HttpMethod.GET, "/foo/:bar/hello", () -> new Bar());
        routeFinder.route(HttpMethod.GET, "/foo/:bar/hello/:panda", () -> new Foo());

        Route route = resolve(HttpMethod.GET, routeFinder, "/foo/andy");
        assertEquals(200, route.getHandler().get().handle(route.getRequest()).getHttpStatus());

        route = resolve(HttpMethod.GET, routeFinder, "/foo/andy/hello");
        assertEquals(200, route.getHandler().get().handle(route.getRequest()).getHttpStatus());

        route = resolve(HttpMethod.GET, routeFinder, "/foo/andy/hello/somethingelse");
        assertEquals(200, route.getHandler().get().handle(route.getRequest()).getHttpStatus());
    }

    @Test
    public void testResolve() throws Exception {
        RouteFinder routeFinder = new RouteFinder();
        routeFinder.route(HttpMethod.GET, "/hello/foo", () -> new Foo());
        routeFinder.route(HttpMethod.POST, "/hello/bar", () -> new Bar());

        Route route = resolve(HttpMethod.GET, routeFinder, "/hello/foo");
        assertEquals(200, route.getHandler().get().handle(route.getRequest()).getHttpStatus());

        route = resolve(HttpMethod.GET, routeFinder, "/hello/foo/");
        assertEquals(200, route.getHandler().get().handle(route.getRequest()).getHttpStatus());

        route = resolve(HttpMethod.POST, routeFinder, "/hello/bar");
        assertEquals(200, route.getHandler().get().handle(route.getRequest()).getHttpStatus());

        route = resolve(HttpMethod.POST, routeFinder, "/hello/foo");
        assertEquals(404, route.getHandler().get().handle(route.getRequest()).getHttpStatus());

        route = resolve(HttpMethod.POST, routeFinder, "poop");
        assertEquals(404, route.getHandler().get().handle(route.getRequest()).getHttpStatus());
    }

    @Test
    public void testExpandArgs() throws Exception {
        RouteFinder routeFinder = new RouteFinder();
        routeFinder.route(HttpMethod.GET, "/a/:b/c/:d", () -> new Foo());

        Route route = resolve(HttpMethod.GET, routeFinder, "/a/b/c/d");
        assertEquals(200, route.getHandler().get().handle(route.getRequest()).getHttpStatus());
        assertEquals("b", route.getRequest().getParameter("b"));
        assertEquals("d", route.getRequest().getParameter("d"));
    }

    private Route resolve(HttpMethod httpMethod, RouteFinder routeFinder, String q) {
        Request request = new MockRequest(httpMethod.toString(), q, new MultiMap<>());
        return routeFinder.resolve(request);
    }

    class MockRequest extends Request {
        private String httpMethod;
        private String requestUri;
        private MultiMap<String> parameters;

        MockRequest(String httpMethod, String requestUri, MultiMap<String> parameters) {
            this.httpMethod = httpMethod;
            this.requestUri = requestUri;
            this.parameters = parameters;
        }

        @Override
        public String getMethod() {
            return httpMethod;
        }

        @Override
        public String getRequestURI() {
            return requestUri;
        }

        @Override
        public void setParameters(MultiMap<String> parameters) {
            this.parameters = parameters;
        }

        @Override
        public String getParameter(String name) {
            return (String) parameters.getValue(name, 0);
        }
    }
    class Foo extends TextResource implements RequestHandler {
        public Foo() {
            super("foo");
        }

        @Override
        public Resource handle(Request request) throws Exception {
            return new TextResource("foo");
        }
    }

    class Bar extends TextResource implements RequestHandler {
        public Bar() {
            super("foo");
        }

        @Override
        public Resource handle(Request request) throws Exception {
            return new TextResource("foo");
        }
    }
}
