/*
 * Copyright (c) 2015 Formation Data Systems. All rights Reserved.
 */

package com.formationds.om.webkit.rest.v08.volumes;

import com.formationds.apis.FDSP_ModifyVolType;
import com.formationds.apis.VolumeDescriptor;
import com.formationds.client.v08.converters.ExternalModelConverter;
import com.formationds.client.v08.model.Size;
import com.formationds.client.v08.model.SizeUnit;
import com.formationds.client.v08.model.SnapshotPolicy;
import com.formationds.client.v08.model.Volume;
import com.formationds.client.v08.model.VolumeSettingsBlock;
import com.formationds.client.v08.model.VolumeSettingsISCSI;
import com.formationds.client.v08.model.VolumeSettingsNfs;
import com.formationds.commons.model.helper.ObjectModelHelper;
import com.formationds.om.helper.SingletonConfigAPI;
import com.formationds.protocol.ApiException;
import com.formationds.protocol.ErrorCode;
import com.formationds.protocol.NfsOption;
import com.formationds.protocol.svc.types.FDSP_VolumeDescType;
import com.formationds.security.AuthenticationToken;
import com.formationds.security.Authorizer;
import com.formationds.util.thrift.ConfigurationApi;
import com.formationds.web.toolkit.RequestHandler;
import com.formationds.web.toolkit.Resource;
import com.formationds.web.toolkit.TextResource;
import com.google.common.base.CharMatcher;
import org.apache.thrift.TException;
import org.eclipse.jetty.server.Request;
import org.apache.logging.log4j.Logger;
import org.apache.logging.log4j.LogManager;

import java.io.InputStream;
import java.util.Collections;
import java.util.List;
import java.util.Map;

public class CreateVolume implements RequestHandler
{

    private static final Logger logger = LogManager.getLogger( CreateVolume.class );

    private final Authorizer authorizer;
    private ConfigurationApi configApi;
    private final AuthenticationToken token;

    public CreateVolume( final Authorizer authorizer, final AuthenticationToken token )
    {

        this.authorizer = authorizer;
        this.token = token;
    }

    @Override
    public Resource handle( Request request, Map<String, String> routeParameters )
            throws Exception
    {

        Volume newVolume;

        logger.debug( "Creating a new volume." );

        try (final InputStream is = request.getInputStream( ) )
        {
            final String jsonBody = readBody( is );

            logger.trace( "CREATE VOLUME JSON BODY::" + jsonBody );

            newVolume = ObjectModelHelper.toObject( jsonBody, Volume.class );

            logger.trace( "AFTER MODEL::" + ObjectModelHelper.toJSON( newVolume ) );
        }
        catch ( Exception e )
        {
            logger.error( "Unable to convert the body to a valid Volume object.", e );
            throw new ApiException( "Invalid input parameters", ErrorCode.BAD_REQUEST );
        }

        if( newVolume == null )
        {
            throw new ApiException( "Badly formatted volume", ErrorCode.BAD_REQUEST );
        }

        if( CharMatcher.WHITESPACE.matchesAnyOf( newVolume.getName() ) )
        {
            throw new ApiException( "Badly formatted volume, name cannot contain spaces",
                                    ErrorCode.BAD_REQUEST );
        }

        validateVolumeSize( newVolume );

        VolumeDescriptor internalVolume =
                ExternalModelConverter.convertToInternalVolumeDescriptor( newVolume );

        dumpVolume( internalVolume );

        final String domainName = "";

        // creating the volume
        try
        {

            getConfigApi( ).createVolume( domainName,
                                          internalVolume.getName( ),
                                          internalVolume.getPolicy( ),
                                          getAuthorizer( ).tenantId( getToken( ) ) );
        }
        catch ( ApiException e )
        {

            if ( e.getErrorCode( )
                    .equals( ErrorCode.RESOURCE_ALREADY_EXISTS ) )
            {

                throw new ApiException( "The specified volume name ( " + internalVolume.getName() + " ) already exists.",
                                        ErrorCode.RESOURCE_ALREADY_EXISTS );
            }

            logger.error( "CREATE::FAILED::" + e.getMessage( ), e );

            // allow dispatcher to handle
            throw e;
        }
        catch ( TException | SecurityException se )
        {
            logger.error( "CREATE::FAILED::" + se.getMessage( ), se );

            // allow dispatcher to handle
            throw se;
        }

        long volumeId = getConfigApi( ).getVolumeId( newVolume.getName( ) );
        newVolume.setId( volumeId );

        // setting the QOS for the volume
        try
        {
            setQosForVolume( newVolume, true );
        }
        catch ( TException thriftException )
        {
            logger.error( "CREATE::FAILED::" + thriftException.getMessage( ), thriftException );
            throw thriftException;
        }

        // new that we've finished all that - create and attach the snapshot policies to this volume
        try
        {
            createSnapshotPolicies( newVolume );
        }
        catch ( TException thriftException )
        {
            logger.error( "CREATE::FAILED::" + thriftException.getMessage( ), thriftException );
            throw thriftException;
        }

        VolumeDescriptor vd = getConfigApi( ).statVolume( domainName, internalVolume.getName( ) );

        List<Volume> volumes =
                ExternalModelConverter.convertToExternalVolumes( Collections.singletonList( vd ) );
        Volume myVolume = null;

        for ( Volume volume : volumes )
        {
            if ( volume.getId( )
                    .equals( volumeId ) )
            {
                myVolume = volume;
                break;
            }
        }

        String volumeString = ObjectModelHelper.toJSON( myVolume );

        return new TextResource( volumeString );
    }

    /**
     * Handle setting the QOS for the volume in question
     *
     * @param externalVolume
     * @throws ApiException
     * @throws TException
     */
    public void setQosForVolume( Volume externalVolume )
            throws ApiException, TException
    {
        setQosForVolume( externalVolume, false );
    }

    public void setQosForVolume( Volume externalVolume,
                                 final boolean isCreate )
                                         throws ApiException, TException
    {

        validateQOSSettings( externalVolume, isCreate );

        if ( externalVolume.getId( ) != null && externalVolume.getId( ) > 0 )
        {

            try
            {
                Thread.sleep( 200 );
            } catch ( InterruptedException e )
            {
                logger.warn( "Failed to wait for volume to become propagated.",
                             e );
            }

            FDSP_VolumeDescType volumeDescType =
                    ExternalModelConverter.convertToInternalVolumeDescType( externalVolume );

            getConfigApi( ).ModifyVol(
                                      new FDSP_ModifyVolType( externalVolume.getName( ), externalVolume.getId( ),
                                                              volumeDescType ) );
        } else
        {
            String message = "Could not verify volume to set QOS parameters.";


            throw new ApiException( message, ErrorCode.SERVICE_NOT_READY );
        }
    }

    /**
     * Create and attach all the snapshot policies to the newly created volume
     *
     * @param externalVolume
     * @throws Exception
     */
    public void createSnapshotPolicies( Volume externalVolume )
            throws Exception
    {

        CreateSnapshotPolicy createEndpoint = new CreateSnapshotPolicy( getAuthorizer( ),
                                                                        getToken( ) );

        for ( SnapshotPolicy policy : externalVolume.getDataProtectionPolicy( )
                .getSnapshotPolicies( ) )
        {
            createEndpoint.createSnapshotPolicy( externalVolume.getId( ), policy );
        }
    }

    private static final long TWO_TO_32DN = ( long ) Math.pow( 2, 32 );
    private static final String VOL_SIZE_ERR =
        "The specified volume size of %s%s is too large, based on the requested volume attributes of [ block size: %s%s volume size: %s%s ].";

    /**
     * @param volume the {@link Volume} representing the external model object
     * @throws ApiException if the Volume size is not valid
     */
    public void validateVolumeSize( final Volume volume )
        throws ApiException
    {
        // ensure that we initialize the to our default values.
        long blockSize = Size.kb( 128 ).getValue( SizeUnit.B ).longValue();
        // this should always be passed in, but just in case.
        long requestedVolumeSize = Size.gb( 10 ).getValue( SizeUnit.B ).longValue();
        switch( volume.getSettings().getVolumeType() )
        {
            /*
             * this validation only applies to block based volume types
             *
             * iSCSI is currently the only production supported block volume. The System test still
             * use BLOCK, but don't provide the block size, so no need to check it.
             */
            case ISCSI:
                final VolumeSettingsISCSI settingsISCSI = ( VolumeSettingsISCSI ) volume.getSettings();
                if( settingsISCSI.getBlockSize() != null )
                {
                    /*
                     * FS-6036 Unable to create multiple volumes without refreshing the browser
                     *
                     * Adding a defensive check to ensure we use the default if block size is
                     * zero ( 0 ).
                     */
                    if( !settingsISCSI.getBlockSize().isZero() )
                    {
                        blockSize = settingsISCSI.getBlockSize( )
                                                 .getValue( SizeUnit.B )
                                                 .longValue( );
                    }
                }

                if( settingsISCSI.getCapacity() != null )
                {
                    requestedVolumeSize = settingsISCSI.getCapacity( )
                                                       .getValue( SizeUnit.B )
                                                       .longValue( );
                }
                break;
            default:
                return;
        }

        final long maxCalculated = blockSize * TWO_TO_32DN;
        if( requestedVolumeSize > maxCalculated )
        {
            throw new ApiException( String.format( VOL_SIZE_ERR,
                                                   Size.of( requestedVolumeSize, SizeUnit.B ).getValue( SizeUnit.GB ),
                                                   SizeUnit.GB.name(),
                                                   Size.of( blockSize, SizeUnit.B ).getValue( SizeUnit.KB ),
                                                   SizeUnit.KB.name(),
                                                   Size.of( requestedVolumeSize, SizeUnit.B ).getValue( SizeUnit.GB ),
                                                   SizeUnit.GB.name() ),
                                    ErrorCode.BAD_REQUEST );
        }
    }

    private static final String CREATED_MSG =
            " Volume was created successfully, just no QOS policy was set." +
                    " QOS policy can be added, to the volume, by editing the volume" +
                    " ( %d:%s ).";

    /**
     * @param volume the {@link Volume} representing the external model object
     * @throws ApiException if the QOS settings are not valid
     */
    public void validateQOSSettings( final Volume volume )
            throws ApiException
    {
        validateQOSSettings( volume, false );
    }

    public void validateQOSSettings( final Volume volume,
                                     final boolean isCreate )
                                             throws ApiException
    {

        if ( volume == null )
        {
            throw new ApiException( "The specified volume is null",
                                    ErrorCode.BAD_REQUEST );
        }

        if ( volume.getQosPolicy( ) == null )
        {

            String message = "The specified volume QOS policy is null.";
            if ( isCreate )
            {
                message += String.format( CREATED_MSG, volume.getId( ), volume.getName( ) );
            }

            throw new ApiException( message,
                                    ErrorCode.BAD_REQUEST );
        }

        logger.trace(
                     "Validate QOS ( {}:{} ) -- MIN(assured): {} MAX(throttled): {}",
                     volume.getId( ),
                     volume.getName( ),
                     volume.getQosPolicy( )
                     .getIopsMin( ),
                     volume.getQosPolicy( )
                     .getIopsMax( ) );

        switch( volume.getSettings().getVolumeType() )
        {
            case BLOCK:
                break;
            case OBJECT:
                break;
            case SYSTEM:
                break;
            case ISCSI:
                final VolumeSettingsISCSI volumeSettingsISCSI =
                ( VolumeSettingsISCSI ) volume.getSettings();
                logger.trace( "Validate ( {}:{} ) -- iSCSI(target={})",
                              volume.getId( ),
                              volume.getName( ),
                              volumeSettingsISCSI.getTarget() );
                break;
            case NFS:
                final VolumeSettingsNfs volumeSettingsNfs =
                ( VolumeSettingsNfs ) volume.getSettings();
                logger.trace( "Validate ( {}:{} ) -- NFS(client={}, options={})",
                              volume.getId( ),
                              volume.getName( ),
                              volumeSettingsNfs.getClients(),
                              volumeSettingsNfs.getOptions() );
                break;
        }

        if ( !( ( volume.getQosPolicy( )
                .getIopsMax( ) == 0 ) ||
                ( volume.getQosPolicy( )
                        .getIopsMin( ) <=
                        volume.getQosPolicy( )
                        .getIopsMax( ) ) ) )
        {
            String message =
                    "QOS value out-of-range ( assured must be less than or equal to throttled ).";

            if ( isCreate )
            {
                message += String.format( CREATED_MSG, volume.getId( ), volume.getName( ) );
            }

            logger.error( message );
            throw new ApiException( message, ErrorCode.BAD_REQUEST );
        }
    }

    private Authorizer getAuthorizer( )
    {
        return this.authorizer;
    }

    private AuthenticationToken getToken( )
    {
        return this.token;
    }

    private ConfigurationApi getConfigApi( )
    {

        if ( configApi == null )
        {
            configApi = SingletonConfigAPI.instance( )
                    .api( );
        }

        return configApi;
    }

    private void dumpVolume( final VolumeDescriptor volume )
    {
        final StringBuilder sb = new StringBuilder( );

        sb.append( " name: " ).append( volume.getName() )
        .append( " created: " ).append( volume.getDateCreated() )
        .append( " state: " ).append( volume.getState() )
        .append( " tenantId: " ).append( volume.getTenantId() )
        .append( " volumeId: " ).append( volume.getVolId() );

        final com.formationds.apis.VolumeSettings volumeSettings = volume.getPolicy();
        switch( volumeSettings.getVolumeType() )
        {
            case OBJECT:
                break;
            case BLOCK:
                break;
            case ISCSI:
                break;
            case NFS:
                final NfsOption options = volumeSettings.getNfsOptions( );
                if( options != null )
                {
                    sb.append( " client " )
                    .append( options.getClient( ) )
                    .append( " options " )
                    .append( options.getOptions( ) );
                }
                break;
        }

        logger.trace( "INTERNAL VOLUME:: {}", sb.toString() );
    }
}
