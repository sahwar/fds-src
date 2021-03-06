/*
 * Copyright (c) 2014, Formation Data Systems, Inc. All Rights Reserved.
 */

package com.formationds.commons.model;

import com.formationds.apis.MediaPolicy;
import com.formationds.commons.model.abs.Context;
import com.formationds.commons.model.util.ModelFieldValidator;

import java.util.HashMap;
import java.util.Map;
import java.util.Objects;

import static com.formationds.commons.model.util.ModelFieldValidator.KeyFields;
import static com.formationds.commons.model.util.ModelFieldValidator.outOfRange;

/**
 * @author ptinius
 */
public class Volume
  extends Context {
  private static final long serialVersionUID = 7961922641732546048L;

  private String domain;                // domain name
  private long tenantId;
  private String name;
  private Long limit;                   // maximum IOPS
  private Long sla;                     // minimum IOPS -- service level agreement

  // TODO: change to a long to be consistent with the Thrift definition in VolumeDescriptor
  private String id;                    // volume Id
  private Integer priority;
  private Integer max_object_size;
  private Connector data_connector;
  private Usage current_usage;
  private MediaPolicy mediaPolicy;
  private Long commit_log_retention;

  private static final Map<String, ModelFieldValidator> VALIDATORS =
    new HashMap<>();

  static {
    VALIDATORS.put( "priority",
                    new ModelFieldValidator( KeyFields.PRIORITY,
                                             PRIORITY_MIN,
                                             PRIORITY_MAX ) );
    VALIDATORS.put( "sla",
                    new ModelFieldValidator( KeyFields.SLA,
                                             SLA_MIN,
                                             SLA_MAX ) );
    VALIDATORS.put( "limit",
                    new ModelFieldValidator( KeyFields.LIMIT,
                                             SLA_MIN,
                                             SLA_MAX ) );
    VALIDATORS.put( "max_object_size",
                    new ModelFieldValidator( KeyFields.MAX_OBJECT_SIZE,
                                             OBJECT_SIZE_MIN,
                                             OBJECT_SIZE_MAX ) );
  }

  /**
   * default constructor
   */
  public Volume() {
    super();
  }

    public Volume( long tenantId, String id, String domain, String name ) {
        this.id = id;
        this.name = name;
        this.tenantId = tenantId;
        this.domain = domain;
    }

    public Volume( String domain, long tenantId, String name, Long limit, Long sla, String id, Integer priority,
                   Connector data_connector, Usage current_usage, MediaPolicy mediaPolicy,
                   Long commit_log_retention ) {
        this.domain = domain;
        this.tenantId = tenantId;
        this.name = name;
        this.limit = limit;
        this.sla = sla;
        this.id = id;
        this.priority = priority;
        this.data_connector = data_connector;
        this.current_usage = current_usage;
        this.mediaPolicy = mediaPolicy;
        this.commit_log_retention = commit_log_retention;
    }

    @Override
  public boolean equals(Object o) {
    if( this == o ) {

      return true;
    }

    if( ( o == null ) || ( getClass() != o.getClass() ) ) {

      return false;
    }

    Volume volume = ( Volume ) o;

    return Objects.equals( id, volume.id ) &&
        Objects.equals( name, volume.name );

  }

  public long getTenantId( ) {
    return tenantId;
  }

  public void setTenantId( final long tenantId ) {
    this.tenantId = tenantId;
  }

  @Override
  public int hashCode() {
    return Objects.hash(name, id);
  }

  /**
   * @return Returns a {@link String} representing the volume name
   */
  public String getName() {
    return name;
  }

  /**
   * @param name the {@link String} representing the volume name
   */
  public void setName( final String name ) {
    this.name = name;
  }

  /**
   * @return Returns a {@code int} representing the priority associated with
   * this volume
   */
  public int getPriority() {
    return priority;
  }

  /**
   * @param priority a {@code int} representing the priority associated with
   *                 this volume
   */
  public void setPriority( final int priority ) {
    if( !VALIDATORS.get( "priority" )
                   .isValid( priority ) ) {
      throw new IllegalArgumentException(
        outOfRange(
          VALIDATORS.get( "priority" ), ( long ) priority ) );
    }
    this.priority = priority;
  }

  /**
   * @return Returns a {@code int} representing the maximum number of bytes
   * in the volume's underlying objects.
   */
  public int getMaxObjectSize() {
    if (max_object_size == null) {
	    return 0;
    }
    return max_object_size;
  }

  /**
   * @param max_object_size a {@code int} representing the maximum number of
   * bytes in the volume's underlying objects.
   */
  public void setMaxObjectSize( final int max_object_size ) {
    if( !VALIDATORS.get( "max_object_size" )
                   .isValid( max_object_size ) ) {
      throw new IllegalArgumentException(
        outOfRange(
          VALIDATORS.get( "max_object_size" ), ( long ) max_object_size ) );
    }
    this.max_object_size = max_object_size;
  }


  /**
   * @return Returns a {@link String} representing the universally unique
   * identifier
   */
  public String getId() {
    return id;
  }

  /**
   * @param id a {@link String} representing the universally unique identifier
   */
  public void setId( final String id ) {
    this.id = id;
  }

  /**
   * @return Returns a {@code long} representing the minimum service level
   * agreement for IOPS
   */
  public long getSla() {
    return sla;
  }

  /**
   * @param sla a {@code long} representing the minimum service level agreement
   *            for IOPS
   */
  public void setSla( final long sla ) {
    if( !VALIDATORS.get( "sla" )
                   .isValid( sla ) ) {
      throw new IllegalArgumentException(
        outOfRange(
          VALIDATORS.get( "sla" ), sla ) );
    }

    this.sla = sla;
  }

  /**
   * @return Returns a {@code long} representing the maximum service level
   * agreement for IOPS
   */
  public long getLimit() {
    return limit;
  }

  /**
   * @param limit a {@code long} representing the maximum service level
   *              agreement for IOPS
   */
  public void setLimit( final long limit ) {
    if( !VALIDATORS.get( "limit" )
                   .isValid( limit ) ) {
      throw new IllegalArgumentException(
        outOfRange(
          VALIDATORS.get( "limit" ), limit ) );
    }

    this.limit = limit;
  }
  
  /**
   * 
   * @return return a {@link MediaPolicy} that dictates where/how data should be stored
   */
  public MediaPolicy getMediaPolicy(){
	  return mediaPolicy;
  }
  
  public void setMediaPolicy( MediaPolicy aPolicy ){
	  mediaPolicy = aPolicy;
  }

   /**	  
   * Get the number of ? that the volume journal will be retained.
   */
  public long getCommit_log_retention(){
	  return commit_log_retention;
  }
  
  /**
   * @param retention a {@code long} representing the number of ? that the volume 
   * commit journal will be retained.
   */
  public void setCommit_log_retention( Long retention ) {
	  this.commit_log_retention = retention;
  }

  /**
   * @param data_connector a {@link Connector} representing type of data
   *                       connector
   */
  public void setData_connector( Connector data_connector ) {
    this.data_connector = data_connector;
  }

  /**
   * @return Returns a {@link Connector} representing the type of data
   * connector
   */
  public Connector getData_connector() {
    return data_connector;
  }

  /**
   * @return Returns the {@link Usage}
   */
  public Usage getCurrent_usage() {
    return current_usage;
  }

  /**
   * @param current_usage the {@link com.formationds.commons.model.Usage}
   */
  public void setCurrent_usage( final Usage current_usage ) {
    this.current_usage = current_usage;
  }

  /**
   * @return Returns a {@link T} representing the context
   */
  @SuppressWarnings( "unchecked" )
  @Override
  public <T> T getContext() {
    return ( T ) this.name;
  }

  @Override
  public String toString() {
    final StringBuilder sb = new StringBuilder("Volume{");
    sb.append("id='").append(id).append('\'');
    sb.append(", name='").append(name).append('\'');
    sb.append(", limit=").append(limit);
    sb.append(", sla=").append(sla);
    sb.append(", priority=").append(priority);
    sb.append(", max_object_size=").append(max_object_size);
    sb.append(", data_connector=").append(data_connector);
    sb.append(", current_usage=").append(current_usage);
    sb.append('}');
    return sb.toString();
  }
}
