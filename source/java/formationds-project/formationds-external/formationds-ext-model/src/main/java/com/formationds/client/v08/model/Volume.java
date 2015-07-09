/**
 * Copyright (c) 2015 Formation Data Systems.  All rights reserved.
 */

package com.formationds.client.v08.model;

import java.time.Duration;
import java.time.Instant;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;

/**
 * The volume represents a data container in the system.  Each volume is uniquely identified by
 * it's domain, tenant, and volume name.  Additionally, each volume may be associated with at most
 * one Application.  The volume has various policies that control how it is accessed and used in
 * the system.
 */
public class Volume extends AbstractResource<Long> {

    public static class Builder {

        private final String         volumeName;
        private Tenant               tenant;
        private Optional<Long>       id = Optional.empty();
        private String               application;
        private VolumeSettings       settings;
        private MediaPolicy          mediaPolicy;
        private DataProtectionPolicy dataProtectionPolicy;
        private VolumeAccessPolicy   accessPolicy;
        private QosPolicy            qosPolicy;
        private VolumeStatus         status;
        private final Map<String, String> tags = new HashMap<>();
        private Optional<Instant> creationTime = Optional.empty();

        /**
         * Create a new Volume with settings from an existing volume.  You must provide a
         * name (unique to the tenant) and the id will be generated by the system once saved.
         *
         * @param from the existing volume
         */
        public Builder(Volume from, String name) {
            this(name);
            tenant = from.getTenant();
            application = from.getApplication();
            settings = from.getSettings().newSettingsFrom();
            mediaPolicy = from.getMediaPolicy();
            dataProtectionPolicy = from.getDataProtectionPolicy().newPolicyFrom();
            accessPolicy = from.getAccessPolicy();
            qosPolicy = from.getQosPolicy();
            tags.putAll(from.getTags());
        }

        public Builder( String volumeName ) {
            this.volumeName = volumeName;
        }

        public Builder( Tenant tenant, String volumeName ) {
            this(volumeName);
            this.tenant = tenant;
        }

        public Builder id(Number id) { this.id = (id != null ? Optional.of( id.longValue() ) : Optional.empty()); return this; }
        public Builder creationTime(Instant t) { this.creationTime = Optional.of( t ); return this; }
        public Builder tenant(Tenant t) { this.tenant = t; return this; }
        public Builder addTag(String k, String v) { this.tags.put(k, v); return this; }
        public Builder addTags(Map<String,String> tags) { this.tags.putAll(tags); return this; }
        public Builder application(String app) { this.application = app; return this; }
        public Builder status(VolumeStatus status) { this.status = status; return this; }
        public Builder settings(VolumeSettings value) { settings = value; return this; }
        public Builder mediaPolicy(MediaPolicy m) { this.mediaPolicy = m; return this; }
        public Builder accessPolicy(VolumeAccessPolicy ap) { this.accessPolicy = ap; return this; }
        public Builder qosPolicy(QosPolicy qp) { this.qosPolicy = qp; return this; }

        public Builder dataProtectionPolicy(DataProtectionPolicy dpp) { this.dataProtectionPolicy = dpp; return this; }
        public Builder dataProtectionPolicy(DataProtectionPolicyPreset preset) {
            return dataProtectionPolicy( DataProtectionPolicy.fromPreset( preset ) );
        }
        public Builder dataProtectionPolicy(long commitLogRetention,
                                            TimeUnit unit,
                                            SnapshotPolicy... snapshotPolicies) {
            List<SnapshotPolicy> sp = Arrays.asList( snapshotPolicies );
            return dataProtectionPolicy( new DataProtectionPolicy( Duration.of( commitLogRetention, unit), sp) );
        }

        public Volume create() {
            return new Volume(id.orElse( null ),
                              volumeName,
                              tenant,
                              application,
                              status,
                              settings,
                              mediaPolicy,
                              dataProtectionPolicy,
                              accessPolicy,
                              qosPolicy,
                              creationTime.orElse( null ),
                              tags );
        }
    }

    private Tenant               tenant;
    private String               application;
    private VolumeSettings       settings;
    private MediaPolicy          mediaPolicy;
    private DataProtectionPolicy dataProtectionPolicy;
    private VolumeAccessPolicy   accessPolicy;
    private QosPolicy            qosPolicy;
    private Instant				 created = Instant.now();
    private VolumeStatus         status;
    private Map<String, String>  tags;

    /**
     *
     * @param name the volume name
     * @param tenant the tenant that the volume is assigned to. May be the "system" tenant
     * @param application the name of the application this volume is associated with
     * @param settings the volume settings
     * @param mediaPolicy the media policy
     * @param dataProtectionPolicy the data protection policy
     * @param accessPolicy the access policy
     * @param qosPolicy the quality of service policy
     * @param tags volume metadata tags.
     */
    public Volume( String name,
                   Tenant tenant,
                   String application,
                   VolumeSettings settings,
                   MediaPolicy mediaPolicy,
                   DataProtectionPolicy dataProtectionPolicy,
                   VolumeAccessPolicy accessPolicy,
                   QosPolicy qosPolicy,
                   Map<String, String> tags ) {
        super( name );
        this.tenant = tenant;
        this.tags = tags;
        this.application = application;
        this.settings = settings;
        this.mediaPolicy = mediaPolicy;
        this.dataProtectionPolicy = dataProtectionPolicy;
        this.accessPolicy = accessPolicy;
        this.qosPolicy = qosPolicy;
    }

    /**
     *
     * @param id the volume id.  May be null indicating that the volume is not yet saved or loaded
     * @param name the volume name
     * @param tenant the tenant that the volume is assigned to. May be the "system" tenant
     * @param application the name of the application this volume is associated with
     * @param status the current (last known) volume status
     * @param settings the volume settings
     * @param mediaPolicy the media policy
     * @param dataProtectionPolicy the data protection policy
     * @param accessPolicy the access policy
     * @param qosPolicy the quality of service policy
     * @param created the creation time
     * @param tags volume metadata tags.
     */
    public Volume( Long id,
                   String name,
                   Tenant tenant,
                   String application,
                   VolumeStatus status,
                   VolumeSettings settings,
                   MediaPolicy mediaPolicy,
                   DataProtectionPolicy dataProtectionPolicy,
                   VolumeAccessPolicy accessPolicy,
                   QosPolicy qosPolicy,
                   Instant created,
                   Map<String, String> tags ) {
        super( id, name );
        this.tenant = tenant;
        this.tags = tags;
        this.application = application;
        this.status = status;
        this.settings = settings;
        this.mediaPolicy = mediaPolicy;
        this.dataProtectionPolicy = dataProtectionPolicy;
        this.accessPolicy = accessPolicy;
        this.qosPolicy = qosPolicy;
        this.created = created;
    }

    /**
     * @param id the volume id
     * @param name the volume name
     */
    public Volume( Long id,
                   String name ){
    	this( id, name, null, null, null, null, null, null, null, null, null, null );
    }

    /**
     *
     * @return the tenant the volume is assigned to
     */
    public Tenant getTenant() {
        return tenant;
    }

    /**
     *
     * @param tenant the tenant the volume is assigned to
     */
    public void setTenant( Tenant tenant ) {
        this.tenant = tenant;
    }

    /**
     *
     * @return the map of volume metadata tags
     */
    public Map<String, String> getTags() {
        return tags;
    }

    /**
     *
     * @param tag the tag name
     * @param val the tag value
     */
    public void addTag( String tag, String val ) {
        this.tags.put( tag, val );
    }

    /**
     *
     * @param tag the tag to remove
     */
    public void removeTag( String tag ) {
        this.tags.remove( tag );
    }

    /**
     * @return the application this volume is associated with
     */
    public String getApplication() {
        return application;
    }

    /**
     * @param application the application name
     */
    public void setApplication( String application ) {
        this.application = application;
    }

    /**
     * @return the most-recent snapshot of the volume status
     */
    public VolumeStatus getStatus() {
        return status;
    }

    /**
     * @param status the current (most-recent) snapshot of the volume status
     */
    public void setStatus( VolumeStatus status ) {
        this.status = status;
    }

    /**
     *
     * @return the volume settings
     */
    public VolumeSettings getSettings() {
        return settings;
    }

    /**
     *
     * @param settings the settings for the volume
     */
    public void setSettings( VolumeSettings settings ) {
        this.settings = settings;
    }

    /**
     * @return the creation timestamp for the volume
     */
    public Instant getCreated(){ return created; }

    /**
     * @return the volume media policy
     */
    public MediaPolicy getMediaPolicy() { return mediaPolicy; }

    /**
     *
     * @return the data protection policy
     */
    public DataProtectionPolicy getDataProtectionPolicy() { return dataProtectionPolicy; }
    
    /**
     * 
     * @param policy the policy to switch to
     */
    public void setDataProtectionPolicy( DataProtectionPolicy policy ) {
    	this.dataProtectionPolicy = policy;
    }

    /**
     *
     * @return the quality of service policy
     */
    public QosPolicy getQosPolicy() { return qosPolicy; }

    /**
     * 
     * @param policy the policy to set the QOS policy to
     */
    public void setQosPolicy( QosPolicy policy ) {
    	this.qosPolicy = policy;
    }
    
    /**
     *
     * @return the volume access policy
     */
    public VolumeAccessPolicy getAccessPolicy() { return accessPolicy; }

    /**
     * 
     * @param policy the new access policy for this volume
     */
    public void setAccessPolicy( VolumeAccessPolicy policy ) {
    	this.accessPolicy = policy;
    }
}
