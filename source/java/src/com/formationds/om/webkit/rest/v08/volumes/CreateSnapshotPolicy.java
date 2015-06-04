/*
 * Copyright (c) 2015, Formation Data Systems, Inc. All Rights Reserved.
 */
package com.formationds.om.webkit.rest.v08.volumes;

import java.io.InputStreamReader;
import java.util.List;
import java.util.Map;

import org.apache.thrift.TException;
import org.eclipse.jetty.server.Request;

import com.formationds.apis.ErrorCode;
import com.formationds.client.v08.converters.ExternalModelConverter;
import com.formationds.client.v08.model.SnapshotPolicy;
import com.formationds.client.v08.model.Volume;
import com.formationds.client.v08.model.SnapshotPolicy.SnapshotPolicyType;
import com.formationds.commons.model.helper.ObjectModelHelper;
import com.formationds.om.helper.SingletonConfigAPI;
import com.formationds.apis.ApiException;
import com.formationds.security.AuthenticationToken;
import com.formationds.security.Authorizer;
import com.formationds.util.thrift.ConfigurationApi;
import com.formationds.web.toolkit.RequestHandler;
import com.formationds.web.toolkit.Resource;
import com.formationds.web.toolkit.TextResource;

public class CreateSnapshotPolicy implements RequestHandler{
	
	private static final String VOLUME_ARG = "volume_id";
	
	private ConfigurationApi configApi;
	private Authorizer authorizer;
	private AuthenticationToken token;
	
	public CreateSnapshotPolicy( Authorizer authorizer, AuthenticationToken token ){
		this.authorizer = authorizer;
		this.token = token;
	}

	@Override
	public Resource handle(Request request, Map<String, String> routeParameters)
			throws Exception {
		
		long volumeId = requiredLong( routeParameters, VOLUME_ARG );
		
		final InputStreamReader reader = new InputStreamReader( request.getInputStream() );
		SnapshotPolicy policy = ObjectModelHelper.toObject( reader, SnapshotPolicy.class );
		
		long policyId = createSnapshotPolicy( volumeId, policy );
		
		policy = getSnapshotPolicy( volumeId, policyId );
		
		String jsonString = ObjectModelHelper.toJSON( policy );
		
		return new TextResource( jsonString );
	}
	
	public long createSnapshotPolicy( long volumeId, SnapshotPolicy policy ) throws Exception{
		
		
		if ( policy.getType().equals( SnapshotPolicyType.SYSTEM_TIMELINE ) ){
			policy.setName( volumeId + "_" + SnapshotPolicyType.SYSTEM_TIMELINE + "_" + policy.getRecurrenceRule().getFrequency() );
		}
		else {
			Volume volume = (new GetVolume( getAuthorizer(), getToken() )).getVolume( volumeId );
			
			SnapshotPolicy highestId = 
				volume.getDataProtectionPolicy().getSnapshotPolicies().stream().max( (p1, p2) -> {
					return p1.getId().compareTo( p2.getId() );
				}).get();
			
			policy.setName( volumeId + "_" + (highestId.getId() + 1) ); 	
		}
		
		long policyId = getConfigApi().createSnapshotPolicy( policy.getName(),
				 policy.getRecurrenceRule().toString(),
				 policy.getRetentionTime().getSeconds(), 
				 0 );
		
		attachSnapshotPolicy( volumeId, policyId );
		
		return policyId;
	}
	
	private void attachSnapshotPolicy( long volumeId, long policyId ) throws ApiException, TException{
		
		getConfigApi().attachSnapshotPolicy( volumeId, policyId );
	}
	
	public SnapshotPolicy getSnapshotPolicy( long volumeId, long policyId ) throws ApiException, TException{
		
		List<com.formationds.apis.SnapshotPolicy> internalPolicies = getConfigApi().listSnapshotPoliciesForVolume( volumeId );
		
		for ( com.formationds.apis.SnapshotPolicy internalPolicy : internalPolicies ){
			
			if ( internalPolicy.getId() == policyId ){
				SnapshotPolicy externalPolicy = ExternalModelConverter.convertToExternalSnapshotPolicy( internalPolicy );
				return externalPolicy;
			}
		}
		
		throw new ApiException( "Could not find the newly created snapshot policy.", ErrorCode.MISSING_RESOURCE );
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
