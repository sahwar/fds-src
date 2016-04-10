package com.formationds.web.toolkit;

import com.formationds.commons.togglz.feature.flag.FdsFeatureToggles;
import com.google.common.collect.Multimap;

import org.apache.logging.log4j.Logger;
import org.apache.logging.log4j.ThreadContext;
import org.apache.logging.log4j.LogManager;
import org.eclipse.jetty.server.Request;
import org.eclipse.jetty.server.Response;
import org.json.JSONObject;

import javax.servlet.ServletException;
import javax.servlet.http.HttpServlet;
import javax.servlet.http.HttpServletRequest;
import javax.servlet.http.HttpServletResponse;

import java.io.IOException;
import java.util.*;
import java.util.concurrent.CompletableFuture;

/*
 * Copyright 2014 Formation Data Systems, Inc.
 */
public class Dispatcher extends HttpServlet {
    private static final long serialVersionUID = -7864317214814653580L;

    private static final Logger LOG = LogManager.getLogger(Dispatcher.class);

    private RouteFinder routeFinder;
    private String webDir;
    private List<AsyncRequestExecutor> asyncRequestExecutorList;
    private Boolean featureLoggingRequestWrapperEnabled = false;

    public Dispatcher(RouteFinder routeFinder, String webDir, List<AsyncRequestExecutor> executors) {
        this.routeFinder = routeFinder;
        this.webDir = webDir;
        this.asyncRequestExecutorList = executors;
        featureLoggingRequestWrapperEnabled = FdsFeatureToggles.WEB_LOGGING_REQUEST_WRAPPER.isActive();
    }

    @Override
    protected void doGet(HttpServletRequest req, HttpServletResponse resp) throws ServletException, IOException {
        service(req, resp);
    }

    @Override
    protected void doPost(HttpServletRequest req, HttpServletResponse resp) throws ServletException, IOException {
        service(req, resp);
    }

    @Override
    protected void doPut(HttpServletRequest req, HttpServletResponse resp) throws ServletException, IOException {
        service(req, resp);
    }

    @Override
    protected void doDelete(HttpServletRequest req, HttpServletResponse resp) throws ServletException, IOException {
        service(req, resp);
    }

	@Override
    public void service(HttpServletRequest httpServletRequest, HttpServletResponse response) throws IOException, ServletException {
        long then = System.currentTimeMillis();

        Request request = ( featureLoggingRequestWrapperEnabled ?
                              RequestLog.newJettyRequestLogger( (Request) httpServletRequest ) :
                              (Request) httpServletRequest );

        @SuppressWarnings("unchecked")
		String requestLogEntryBase = (featureLoggingRequestWrapperEnabled ?
		                                 ((RequestLog.LoggingRequestWrapper<Request>)request).getRequestInfoHeader() :
		                                 String.format( "%s %s", request.getMethod(), request.getRequestURI() ) );

        @SuppressWarnings("unchecked")
        long requestId = ( featureLoggingRequestWrapperEnabled &&
                           request instanceof RequestLog.LoggingRequestWrapper<?> ) ?
                               ((RequestLog.LoggingRequestWrapper<Request>)request).getRequestId() :
                               RequestLog.nextRequestId();

        // Add Dispatcher and requestId to thread name for the duration of the request processing.
        // This has value as long as the request is all processed on the same thread.  Requests that
        // are passed along to other threads for asynchronous handling will not have this thread
        // name.  That is not a problem, you just don't get the same tracking ability.
        String threadBaseName = Thread.currentThread().getName();
        String dispatchContextName = String.format( "Dispatcher(%d:%d)",
                                                    Thread.currentThread().getId(),
                                                    requestId );

        ThreadContext.put( "reqid", Long.toString( requestId ) );
        ThreadContext.put( "uri", request.getRequestURI() );

        Thread.currentThread().setName( dispatchContextName );
        LOG.info("{} Starting request", requestLogEntryBase);
        Resource resource = new FourOhFour();
        try {
	        RequestHandler requestHandler;
	        Map<String, String> routeAttributes = new HashMap<>();

	        if (isStaticAsset(request) && webDir != null) {
	            requestHandler = new StaticFileHandler(webDir);
	        } else {

	            Optional<CompletableFuture<Void>> execution = tryExecuteAsyncApp(request, (Response) response);
	            if(execution.isPresent())
	                return;

	            Optional<Route> route = routeFinder.resolve(request);
	            if (!route.isPresent()) {
	                route = Optional.of(new Route(request, new HashMap<>(),
	                                              FourOhFour::new ));
	            }
	            requestHandler = route.get().getHandler().get();
	            request = route.get().getRequest();
	            routeAttributes = route.get().getAttributes();
	        }

	        try {
	            // TODO: for most of our request handlers, this is going to show as HttpErrorHandler
	            // that wraps the admin and auth handlers.  In order for this to be useful, I think we
	            // we need an interface in RequestHandler that returns an ID/Name that resolves to
	            // the wrapped handler name.  That turns out to be non-trivial and requires some
	            // gymnastics with the HttpAuthenticator's handler wrapped in a function.  The
	            // alternative is adding this in every single handler
	            //ThreadContext.put( "handler", requestHandler.getClass().getSimpleName() );
	            resource = requestHandler.handle(request, routeAttributes);
	        } catch (UsageException e) {
	            resource = new JsonResource(new JSONObject().put("message", e.getMessage()), HttpServletResponse.SC_BAD_REQUEST);
	        } catch (Throwable t) {
	            LOG.fatal(t.getMessage(), t);
	            resource = new ErrorPage(t.getMessage(), t);
	        } finally {
	            // see above todo comment: ThreadContext.remove( "handler" );
	        }

	        sendResponse( response, resource );

        } finally {
	        long elapsed = System.currentTimeMillis() - then;
	        LOG.info("{} Complete HTTP status: {}, {} ms", requestLogEntryBase, resource.getHttpStatus(), elapsed);
        	if ( featureLoggingRequestWrapperEnabled ) {
        	    RequestLog.requestComplete(request);
        	}
            Thread.currentThread().setName( threadBaseName );

            ThreadContext.remove( "reqid" );
            ThreadContext.remove( "uri" );
        }
    }

    /**
     * Utility method to send a resource to the specified response, closing the response
     * upon completion.
     *
     * @param response
     * @param resource
     * @throws IOException
     */
    public static void sendResponse( HttpServletResponse response, Resource resource ) throws IOException {
        Arrays.stream(resource.cookies()).forEach( response::addCookie );
        response.addHeader("Access-Control-Allow-Origin", "*");
        response.setContentType(resource.getContentType());
        response.setStatus(resource.getHttpStatus());
        response.setHeader("Server", "Formation Data Systems");
        Multimap<String, String> extraHeaders = resource.extraHeaders();
        for (String headerName : extraHeaders.keySet()) {
            for (String value : extraHeaders.get(headerName)) {
                response.addHeader(headerName, value);
            }
        }

        ClosingInterceptor outputStream = new ClosingInterceptor(response.getOutputStream());
        resource.render(outputStream);
        outputStream.flush();
        outputStream.doCloseForReal();
    }

    private Optional<CompletableFuture<Void>> tryExecuteAsyncApp(Request request, Response response) {
        if(asyncRequestExecutorList != null) {
            for (AsyncRequestExecutor executor : asyncRequestExecutorList) {
                Optional<CompletableFuture<Void>> cf = executor.tryExecuteRequest(request, response);
                if (cf.isPresent())
                    return cf;
            }
        }

        return Optional.empty();
    }

    private static boolean isStaticAsset(HttpServletRequest request) {
        String path = request.getRequestURI()
                .replaceAll("^" + request.getServletPath() + "/", "")
                .replaceAll("^/", "");

        return path.matches(".*\\..+");
    }
}
