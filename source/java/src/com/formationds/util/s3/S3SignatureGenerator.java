package com.formationds.util.s3;
/*
 * Copyright 2014 Formation Data Systems, Inc.
 */

import com.amazonaws.auth.AWSCredentials;
import com.amazonaws.http.HttpMethodName;
import org.apache.http.Header;
import org.apache.http.HttpRequest;
import org.eclipse.jetty.server.Request;

import javax.crypto.Mac;
import javax.crypto.spec.SecretKeySpec;
import java.net.URI;
import java.security.InvalidKeyException;
import java.security.NoSuchAlgorithmException;
import java.util.*;
import java.util.stream.Collectors;

public class S3SignatureGenerator {
    private static final String[] SUB_RESOURCES = new String[] { "acl", "delete", "lifecycle", "location", "logging",
                                                                "notification", "partNumber", "policy", "requestPayment",
                                                                "torrent", "uploadId", "uploads", "versionId",
                                                                "versioning", "versions", "website" };

    public static final String AUTHORIZATION_HEADER = "Authorization";

    public static String hash(Request request, AWSCredentials credentials) {
        return hash(request.getMethod(), request.getRequestURI(), getNormalizedJettyHeaders(request), request.getQueryString(), credentials);
    }

    public static String hash(HttpRequest request, AWSCredentials credentials) {
        Map<String, List<String>> headers = new HashMap<>();
        for(Header header : request.getAllHeaders()) {
            headers.computeIfAbsent(header.getName(), s -> new ArrayList<>()).add(header.getValue());
        }
        return hash(request.getRequestLine().getMethod(), request.getRequestLine().getUri(), headers, credentials);
    }

    private static Map<String, List<String>> getNormalizedJettyHeaders(Request request) {
        Map<String, List<String>> headers = new HashMap<>();
        Enumeration<String> headersEnum = request.getHeaderNames();
        while(headersEnum.hasMoreElements()) {
            String headerKey = headersEnum.nextElement();
            List<String> headerValue = new ArrayList<>();
            Enumeration<String> headerValuesEnum = request.getHeaders(headerKey);
            while(headerValuesEnum.hasMoreElements()) {
                String value = headerValuesEnum.nextElement();
                if(headerKey.equals("Content-Type")) {
                    // horrid hack because jetty actually messes with the content type header
                    value = value.toLowerCase();
                }
                headerValue.add(value);
            }
            headers.put(headerKey, headerValue);
        }
        return headers;
    }

    public static String hash(String method, String requestUrl, Map<String, List<String>> headers, AWSCredentials credentials) {
        URI uri = URI.create(requestUrl);
        return hash(method, uri.getPath(), headers, parseQueryString(uri.getQuery()), credentials);
    }

    public static String hash(String method, String path, Map<String, List<String>> headers, String queryString, AWSCredentials credentials) {
        return hash(method, path, headers, parseQueryString(queryString), credentials);
    }

    public static String hash(String method, String path, Map<String, List<String>> headers, Map<String, List<String>> queryParameters, AWSCredentials credentials) {
        String stringToSign = buildSignatureString(method, path, headers, queryParameters);
        String signature = HmacSHA1(stringToSign, credentials.getAWSSecretKey());
        return "AWS " + credentials.getAWSAccessKeyId() + ":" + signature;
    }

    private static String buildSignatureString(String method, String path, Map<String, List<String>> headers, Map<String, List<String>> queryParameters) {
        StringBuilder stringToSign = new StringBuilder();
        stringToSign.append(method.toUpperCase());
        stringToSign.append("\n");

        stringToSign.append(getSignatureHeader("Content-MD5", headers));
        stringToSign.append("\n");

        stringToSign.append(getSignatureHeader("Content-Type", headers));
        stringToSign.append("\n");

        stringToSign.append(getSignatureHeader("Date", headers));
        stringToSign.append("\n");

        String[] headerKeys = headers.keySet().toArray(new String[headers.size()]);
        Arrays.sort(headerKeys);
        for(String key : headerKeys) {
            String lckey = key.toLowerCase();
            List<String> headerValues = headers.get(key).stream().map(s -> s.replaceAll("\n", " ")).collect(Collectors.toList());
            String headerValue = String.join(",", headerValues);

            if(!lckey.startsWith("x-amz"))
                continue;
            stringToSign.append(lckey);
            stringToSign.append(":");
            stringToSign.append(headerValue);
            stringToSign.append("\n");
        }

        stringToSign.append(assembleCanonicalizedResource(path, queryParameters));
        return stringToSign.toString();
    }

    private static String HmacSHA1(String data, String key) {
        try {
            SecretKeySpec keySpec = new SecretKeySpec(key.getBytes(), "HmacSHA1");
            Mac mac = Mac.getInstance("HmacSHA1");
            mac.init(keySpec);
            byte[] rawHmac = mac.doFinal(data.getBytes());
            return Base64.getEncoder().encodeToString(rawHmac);
        } catch(NoSuchAlgorithmException | InvalidKeyException ex) {
            throw new RuntimeException();
        }
    }

    private static String assembleCanonicalizedResource(String path, Map<String, List<String>> queryParameters) {
        String canonicalizedResource = "";
        // TODO: doesn't support host syntax
        canonicalizedResource += path;
        List<String> queryParts = new ArrayList<>();
        for(String subResource : SUB_RESOURCES) {
            if(queryParameters.containsKey(subResource)) {
                if(queryParameters.get(subResource).size() != 1)
                    throw new IllegalArgumentException("queryParameters contains subresource with two values");
                String value = queryParameters.get(subResource).get(0);
                if(value == null || value.isEmpty())
                    value = null;

                if(value == null)
                    queryParts.add(subResource);
                else
                    queryParts.add(subResource + "=" + value);
            }
        }

        if(queryParts.size() > 0)
            canonicalizedResource += "?" + String.join("&", queryParts);
        return canonicalizedResource;
    }

    private static String getSignatureHeader(String key, Map<String, List<String>> headers) {
        List<String> lst = headers.getOrDefault(key, Collections.emptyList());
        if(lst.size() > 0)
            return lst.get(0);
        return "";
    }

    private static Map<String,List<String>> parseQueryString(String query) {
        Map<String, List<String>> params = new HashMap<>();
        if(query == null)
            return params;

        String[] segments = query.split("&");
        for(String segment : segments) {
            String parts[] = segment.split("=", 2);
            List<String> values = params.computeIfAbsent(parts[0], s -> new ArrayList<String>());
            if(parts.length == 1) {
                values.add(null);
            } else {
                values.add(parts[1]);
            }
        }
        return params;
    }
}