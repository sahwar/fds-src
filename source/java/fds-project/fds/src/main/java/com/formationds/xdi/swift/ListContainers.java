package com.formationds.xdi.swift;
/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

import com.formationds.apis.VolumeDescriptor;
import com.formationds.apis.VolumeStatus;
import com.formationds.security.AuthenticationToken;
import com.formationds.util.JsonArrayCollector;
import com.formationds.web.W3cXmlResource;
import com.formationds.web.toolkit.JsonResource;
import com.formationds.web.toolkit.Resource;
import com.formationds.web.toolkit.TextResource;
import com.formationds.xdi.AuthenticatedXdi;
import com.google.common.base.Joiner;
import org.eclipse.jetty.server.Request;
import org.json.JSONArray;
import org.json.JSONObject;

import javax.xml.parsers.DocumentBuilder;
import javax.xml.parsers.DocumentBuilderFactory;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;

public class ListContainers implements SwiftRequestHandler {
    private AuthenticatedXdi xdi;
    private AuthenticationToken token;

    public ListContainers(AuthenticatedXdi xdi, AuthenticationToken token) {
        this.xdi = xdi;
        this.token = token;
    }

    @Override
    public Resource handle(Request request, Map<String, String> routeParameters) throws Exception {
        String accountName = requiredString( routeParameters, "account" );

        ResponseFormat format = obtainFormat( request );
        // TODO: get xdi call for a subset of this info
        // TODO: implement limit, marker, end_marker, format, prefix, delimiter query string variables
        List<VolumeDescriptor> volumes = xdi.listVolumes(token, accountName);

        volumes = new SkipUntil<VolumeDescriptor>(request.getParameter("marker"), v -> v.getName())
                .apply(volumes);

        Resource result;
        switch (format) {
            case xml:
                result = xmlView(volumes, accountName);
                break;

            case json:
                result = jsonView(volumes, accountName);
                break;

            default:
                result = plainView(volumes);
                break;
        }

        // TODO: Implement X-Account-Object-Count, X-Account-Bytes-Used, X-Account-Meta-*
        //
        return SwiftUtility.swiftResource(result);
    }

    private Resource plainView(List<VolumeDescriptor> volumes) {
        List<String> volumeNames = volumes.stream().map(v -> v.getName()).collect( Collectors.toList() );
        String joined = Joiner.on( "\n" ).join( volumeNames );
        return new TextResource(joined);
    }

    protected Resource jsonView(List<VolumeDescriptor> volumes, String accountName) {
        JSONArray array = volumes.stream()
                .map( v -> {
                    VolumeStatus status = null;
                    try {
                        status = xdi.statVolume( token, accountName, v.getName() ).get();
                    } catch ( Exception e ) {
                        throw new RuntimeException( e );
                    }
                    return new JSONObject()
                               .put( "name", v.getName() )
                               .put( "count", status.getBlobCount() )
                               .put( "bytes", status.getCurrentUsageInBytes() );
                } )
                .collect( new JsonArrayCollector() );
        return new JsonResource(array);
    }

    protected Resource xmlView(List<VolumeDescriptor> volumes, String accountName) {

        try {
            DocumentBuilder builder = DocumentBuilderFactory.newInstance().newDocumentBuilder();
            org.w3c.dom.Document document = builder.newDocument();

            org.w3c.dom.Element root = document.createElement( "account" );
            root.setAttribute( "name", accountName );

            volumes.stream()
                   .forEach( v -> {
                       VolumeStatus status = null;
                       try {
                           status = xdi.statVolume( token, accountName, v.getName() ).get();
                       } catch ( Exception e ) {
                           throw new RuntimeException( e );
                       }
                       org.w3c.dom.Element object = document.createElement( "container" );
                       root.appendChild( object );

                       org.w3c.dom.Element name = document.createElement( "name" );
                       name.setTextContent( v.getName() );
                       object.appendChild( name );

                       org.w3c.dom.Element count = document.createElement( "count" );
                       count.setTextContent( Long.toString( status.getBlobCount() ) );
                       object.appendChild( count );

                       org.w3c.dom.Element bytes = document.createElement( "bytes" );
                       bytes.setTextContent( Long.toString( status.getCurrentUsageInBytes() ) );
                       object.appendChild( bytes );
                   } );

            document.appendChild( root );

            return new W3cXmlResource( document );
        } catch (Exception e) {
            // documentBuilderFactory failed
            throw new IllegalStateException( "Failed to initialize document builder", e );
        }

    }
}
