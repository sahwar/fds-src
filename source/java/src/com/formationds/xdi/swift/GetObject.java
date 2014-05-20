package com.formationds.xdi.swift;
/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

import com.formationds.apis.BlobDescriptor;
import com.formationds.web.toolkit.RequestHandler;
import com.formationds.web.toolkit.Resource;
import com.formationds.web.toolkit.StaticFileHandler;
import com.formationds.web.toolkit.StreamResource;
import com.formationds.xdi.Xdi;
import com.google.common.collect.LinkedListMultimap;
import com.google.common.collect.Multimap;
import org.apache.commons.codec.binary.Hex;
import org.eclipse.jetty.server.Request;
import org.joda.time.DateTime;

import java.io.InputStream;
import java.util.ArrayList;
import java.util.Map;
import java.util.Optional;

public class GetObject implements RequestHandler {
    private Xdi xdi;

    public GetObject(Xdi xdi) {
        this.xdi = xdi;
    }

    private class Range
    {
        public Optional<Long> rangeMin;
        public Optional<Long> rangeMax;
    }

    @Override
    public Resource handle(Request request, Map<String, String> routeParameters) throws Exception {
        String domain = requiredString(routeParameters, "account");
        String volume = requiredString(routeParameters, "container");
        String object = requiredString(routeParameters, "object");

        ArrayList<Range> ranges = parseRanges(request.getHeader("Range"));
        BlobDescriptor stat = xdi.statBlob(domain, volume, object);

        InputStream objStream = null;
        if(ranges.size() == 0) {
            objStream = xdi.readStream(domain, volume, object);
        } else if(ranges.size() == 1) {
            objStream = readStreamForRange(xdi, domain, volume, object, ranges.get(0), stat.byteCount);
        } else {
            // FIXME: support multiple ranges
            throw new Exception("multiple ranges are not supported");
        }

        // FIXME: add Accept-Ranges header
        // FIXME: add actual value for Last-Modified header

        Map<String, String> metadata = stat.getMetadata();
        String contentType = metadata.getOrDefault("Content-Type", StaticFileHandler.getMimeType(object));
        String lastModified = metadata.getOrDefault("Last-Modified", SwiftUtility.formatRfc1123Date(DateTime.now()));

        return SwiftUtility.swiftResource(new StreamResource(objStream, contentType))
                .withHeader("ETag", Hex.encodeHexString(stat.getDigest()))
                .withHeader("Last-Modified", lastModified);
    }

    private InputStream readStreamForRange(Xdi xdi, String domain, String volume, String object, Range range, long blobLength) throws Exception {
        // NB: this case is inconsistently specified in the openstack API docs
        if(range.rangeMin.isPresent() && range.rangeMax.isPresent())
            return xdi.readStream(domain, volume, object, range.rangeMin.get(), 1 + range.rangeMax.get() - range.rangeMin.get());

        if(range.rangeMin.isPresent())
            return xdi.readStream(domain, volume, object, range.rangeMin.get(), blobLength - range.rangeMin.get());

        if(range.rangeMax.isPresent())
            return xdi.readStream(domain, volume, object, blobLength - range.rangeMax.get(), range.rangeMax.get() );

        throw new Exception("Invalid range specified");
    }

    private ArrayList<Range> parseRanges(String rangeDefinition) {
        ArrayList<Range> ranges = new ArrayList<Range>();
        if(rangeDefinition == null)
            return ranges;

        rangeDefinition = rangeDefinition.replaceFirst("bytes=", "").trim();

        String[] rangeSpecs = rangeDefinition.split(",");
        for(String rangeSpec : rangeSpecs){
            Range range = new Range();

            String[] rangeElements = rangeSpec.split("-");

            if(!rangeElements[0].isEmpty())
                range.rangeMin = Optional.of(Long.parseLong(rangeElements[0]));
            if(!rangeElements[1].isEmpty())
                range.rangeMax = Optional.of(Long.parseLong(rangeElements[1]));

            ranges.add(range);
        }
        return ranges;
    }
}
