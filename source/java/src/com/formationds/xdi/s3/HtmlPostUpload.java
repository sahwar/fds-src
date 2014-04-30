package com.formationds.xdi.s3;

import com.formationds.web.toolkit.RequestHandler;
import com.formationds.web.toolkit.Resource;
import com.formationds.web.toolkit.StaticFileHandler;
import com.formationds.web.toolkit.TextResource;
import com.formationds.xdi.Xdi;
import com.google.common.collect.Maps;
import org.apache.commons.io.IOUtils;
import org.eclipse.jetty.server.Request;

import javax.servlet.MultipartConfigElement;
import javax.servlet.http.Part;
import java.util.Map;

/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

public class HtmlPostUpload implements RequestHandler {
    private Xdi xdi;

    public HtmlPostUpload(Xdi xdi) {
        this.xdi = xdi;
    }

    @Override
    public Resource handle(Request request, Map<String, String> routeParameters) throws Exception {
        String bucketName = requiredString(routeParameters, "bucket");

        if (request.getContentType() != null && request.getContentType().startsWith("multipart/form-data")) {
            request.setAttribute(Request.__MULTIPART_CONFIG_ELEMENT, MULTI_PART_CONFIG);
        }

        Map<String, String> metadata = Maps.newHashMap();

        Part filePart = request.getPart("file");
        String fileName = getFileName(filePart);

        Part contentTypePart = request.getPart("Content-Type");

        if (contentTypePart != null) {
            metadata.put("Content-Type", IOUtils.toString(contentTypePart.getInputStream()));
        } else {
            metadata.put("Content-Type", StaticFileHandler.getMimeType(fileName));
        }

        //xdi.writeStream(Main.FDS_S3, bucketName, fileName, filePart.getInputStream(), metadata);
        return new TextResource("");
    }

    private String getFileName(Part part) {
        String contentDisp = part.getHeader("content-disposition");
        System.out.println("content-disposition header= "+contentDisp);
        String[] tokens = contentDisp.split(";");
        for (String token : tokens) {
            if (token.trim().startsWith("filename")) {
                return token.substring(token.indexOf("=") + 2, token.length()-1);
            }
        }
        return "";
    }

    private static final MultipartConfigElement MULTI_PART_CONFIG = new MultipartConfigElement(System.getProperty("java.io.tmpdir"));

}
