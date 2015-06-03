/*
 * Copyright (c) 2015 Formation Data Systems. All rights Reserved.
 */

package com.formationds.om.webkit.rest.v08.volumes;

import com.formationds.apis.FDSP_ModifyVolType;
import com.formationds.apis.VolumeDescriptor;
import com.formationds.client.v08.converters.ExternalModelConverter;
import com.formationds.client.v08.model.SnapshotPolicy;
import com.formationds.client.v08.model.Volume;
import com.formationds.commons.model.helper.ObjectModelHelper;
import com.formationds.om.helper.SingletonConfigAPI;
import com.formationds.protocol.ApiException;
import com.formationds.protocol.ErrorCode;
import com.formationds.protocol.FDSP_VolumeDescType;
import com.formationds.security.AuthenticationToken;
import com.formationds.security.Authorizer;
import com.formationds.util.thrift.ConfigurationApi;
import com.formationds.web.toolkit.JsonResource;
import com.formationds.web.toolkit.RequestHandler;
import com.formationds.web.toolkit.Resource;
import com.formationds.web.toolkit.TextResource;

import org.apache.thrift.TException;
import org.eclipse.jetty.server.Request;
import org.json.JSONObject;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.InputStreamReader;
import java.io.Reader;
import java.util.List;
import java.util.Map;

import javax.servlet.http.HttpServletResponse;

public class CreateVolume implements RequestHandler {

	private static final Logger logger = LoggerFactory
			.getLogger(CreateVolume.class);

	private final Authorizer authorizer;
	private ConfigurationApi configApi;
	private final AuthenticationToken token;

	public CreateVolume(
			final Authorizer authorizer,
			final AuthenticationToken token) {
		
		this.authorizer = authorizer;
		this.token = token;
	}

	@Override
	public Resource handle(Request request, Map<String, String> routeParameters)
			throws Exception {

		Volume newVolume = null;
		
		try {
		
			final Reader bodyReader = new InputStreamReader( request.getInputStream() );
			
			newVolume = ObjectModelHelper.toObject( bodyReader, Volume.class );
		}
		catch( Exception e ){
			logger.error( "Unable to convet the body to a valid Volume object." );
			return new JsonResource(
					new JSONObject().put( "message", "Invalid input parameters" ), HttpServletResponse.SC_BAD_REQUEST );
		}
		
		VolumeDescriptor internalVolume = ExternalModelConverter.convertToInternalVolumeDescriptor( newVolume );
		
		final String domainName = "";
		
		// creating the volume
		try {
			
			getConfigApi().createVolume( domainName, 
										 internalVolume.getName(), 
										 internalVolume.getPolicy(), 
										 getAuthorizer().tenantId( getToken() ));
	    } catch( ApiException e ) {
	
	        if ( e.getErrorCode().equals(ErrorCode.RESOURCE_ALREADY_EXISTS)) {

	            return new JsonResource( new JSONObject().put( "message", "A volume with this name already exists." ), 
	            		                 HttpServletResponse.SC_INTERNAL_SERVER_ERROR );
	        }
	
	        logger.error( "CREATE::FAILED::" + e.getMessage(), e );
	
	        // allow dispatcher to handle
	        throw e;
	    } catch ( TException | SecurityException se ) {
	        logger.error( "CREATE::FAILED::" + se.getMessage(), se );
	
	        // allow dispatcher to handle
	        throw se;
	    }
		
		long volumeId = getConfigApi().getVolumeId( newVolume.getName() );
		
		// setting the QOS for the volume
		try {
			setQosForVolume( volumeId, newVolume );
		}
		catch( ApiException apiException ){
			logger.error( "CREATE::FAILED::" + apiException.getMessage(), apiException );
			throw apiException;
		}
		catch( TException thriftException ){
			logger.error( "CREATE::FAILED::" + thriftException.getMessage(), thriftException );
			throw thriftException;
		}
		
		// new that we've finished all that - create and attach the snapshot policies to this volume
		try {
			createSnapshotPolicies( volumeId, newVolume );
		}
		catch( TException thriftException ){
			logger.error( "CREATE::FAILED::" + thriftException.getMessage(), thriftException );
			throw thriftException;
		}
		
		List<Volume> volumes = (new ListVolumes( getAuthorizer(), getToken() )).listVolumes();
		Volume myVolume = null;
		
		for ( Volume volume : volumes ){
			if ( volume.getId().equals( volumeId ) ){
				myVolume = volume;
				break;
			}
		}
		
		String volumeString = ObjectModelHelper.toJSON( myVolume );
		
		return new TextResource( volumeString );
	}
	
	/**
	 * Handle setting the QOS for the volume in question
	 * @param externalVolume
	 * @throws ApiException
	 * @throws TException
	 */
	private void setQosForVolume( long volumeId, Volume externalVolume ) throws ApiException, TException{
		
	    if( volumeId > 0 ) {

	    	try {
				Thread.sleep( 200 );
				} catch (InterruptedException e) {
					// TODO Auto-generated catch block
					e.printStackTrace();
				}
	          
	    	FDSP_VolumeDescType volumeDescType = ExternalModelConverter.convertToInternalVolumeDescType( externalVolume );
	          
	    	getConfigApi().ModifyVol( new FDSP_ModifyVolType( externalVolume.getName(), externalVolume.getId(), volumeDescType ) );    
	    }
	    else {
	    	throw new ApiException( "Could not verify volume to set QOS parameters.", ErrorCode.SERVICE_NOT_READY );
	    }
	}
	
	/**
	 * Create and attach all the snapshot policies to the newly created volume
	 * @param externalVolume
	 * @throws TException 
	 */
	private void createSnapshotPolicies( long volumeId, Volume externalVolume ) throws TException{
		
		for ( SnapshotPolicy policy : externalVolume.getDataProtectionPolicy().getSnapshotPolicies() ){
			
			long policyId = getConfigApi().createSnapshotPolicy( volumeId + "_TIMELINE_" + policy.getRecurrenceRule().getFrequency(),
																 policy.getRecurrenceRule().toString(),
																 policy.getRetentionTime().getSeconds(), 
																 0 );
			getConfigApi().attachSnapshotPolicy( volumeId, policyId );
			
		}
	}
	
	private Authorizer getAuthorizer(){
		return this.authorizer;
	}
	
	private AuthenticationToken getToken(){
		return this.token;
	}
	
	private ConfigurationApi getConfigApi(){
		
		if ( configApi == null ){
			configApi = SingletonConfigAPI.instance().api();
		}
		
		return configApi;
	}
}
