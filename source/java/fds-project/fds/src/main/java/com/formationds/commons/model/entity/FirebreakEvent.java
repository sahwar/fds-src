/*
 * Copyright (c) 2014, Formation Data Systems, Inc. All Rights Reserved.
 */

package com.formationds.commons.model.entity;

import com.formationds.commons.events.EventCategory;
import com.formationds.commons.events.EventSeverity;
import com.formationds.commons.events.EventType;
import com.formationds.commons.events.FirebreakType;
import com.formationds.commons.model.Volume;

import javax.xml.bind.annotation.XmlRootElement;

/**
 * Represents an event with EventType#FIREBREAK_EVENT referencing the volume associated with the
 * firebreak occurrence and usage/sigma data relevant to the firebreak.
 */
@XmlRootElement
public class FirebreakEvent extends Event {

    public static final String DEFAULT_MESSAGE_FMT = "Volume {0}; Type data {1}; Current Usage (bytes)={2}; Sigma2={3}";
    public static final String MESSAGE_KEY = "FIREBREAK_EVENT";
    public static final String[] MESSAGE_ARG_NAMES = {"volumeName", "firebreakType", "currentUsageBytes", "sigma"};

    private Long volumeId;
    private String volumeName;

    private FirebreakType firebreakType;

    private Long currentUsageBytes;
    private Double sigma;

    /*
     * non-javadoc - this constructor is not intended for use except by the persistence layer
     */
    protected FirebreakEvent() {}

    /**
     *
     * @param vol the volume that the firebreak occurred in
     * @param t the firebreak type (capacity/performance)
     * @param ts the firebreak timestamp in milliseconds since the epoch
     * @param currentUsageInBytes the current volume usage in bytes
     * @param sigma the short term sigma value causing the firebreak event.
     */
    public FirebreakEvent(Volume vol, FirebreakType t,  Long ts, Long currentUsageInBytes, Double sigma) {
        super(ts, EventType.FIREBREAK_EVENT, EventCategory.FIREBREAK, EventSeverity.WARNING,
              DEFAULT_MESSAGE_FMT, MESSAGE_KEY, vol.getName(), t, currentUsageInBytes, sigma);
        this.volumeId = Long.valueOf( vol.getId() );
        this.volumeName = vol.getName();
        this.firebreakType = t;
        this.currentUsageBytes = currentUsageInBytes;
        this.sigma = sigma;
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof FirebreakEvent)) return false;
        if (!super.equals(o)) return false;

        FirebreakEvent that = (FirebreakEvent) o;

        if (!volumeId.equals(that.volumeId)) return false;
        if (!volumeName.equals(that.volumeName)) return false;
        if (firebreakType != that.firebreakType) return false;

        return true;
    }

    @Override
    public int hashCode() {
        int result = super.hashCode();
        result = 31 * result + volumeId.hashCode();
        result = 31 * result + volumeName.hashCode();
        result = 31 * result + firebreakType.hashCode();
        return result;
    }

    /**
     * @return the volume id
     */
    public Long getVolumeId() {
        return volumeId;
    }

    /*
     * non-javadoc - this method is not intended for use except by the persistence layer
     *
     * @param volId
     */
    protected void setVolumeId(Long volId) {
        this.volumeId = volId;
    }

    /**
     *
     * @return the volume name
     */
    public String getVolumeName() {
        return volumeName;
    }

    /*
     * non-javadoc - this method is not intended for use except by the persistence layer
     *
     * @param volName
     */
    protected void setVolumeName(String volName) {
        this.volumeName = volName;
    }

    /**
     *
     * @return the firebreak type
     */
    public FirebreakType getFirebreakType() {
        return firebreakType;
    }

    /*
     * non-javadoc - this method is not intended for use except by the persistence layer
     *
     * @param firebreakType
     */
    protected void setFirebreakType(FirebreakType firebreakType) {
        this.firebreakType = firebreakType;
    }

    /**
     *
     * @return
     */
    public Long getCurrentUsageBytes() {
        return currentUsageBytes;
    }

    /*
     * non-javadoc - this method is not intended for use except by the persistence layer
     */
    protected void setCurrentUsageBytes(Long currentUsageBytes) {
        this.currentUsageBytes = currentUsageBytes;
    }

    public Double getSigma() {
        return sigma;
    }

    /*
     * non-javadoc - this method is not intended for use except by the persistence layer
     */
    protected void setSigma(Double sigma) {
        this.sigma = sigma;
    }

    @Override
    public String toString() {
        final StringBuilder sb = new StringBuilder("FirebreakEvent{");
        sb.append("volumeId='").append(volumeId).append('\'')
          .append("ts=").append(getInitialTimestamp())
          .append(", volumeName='").append(volumeName).append('\'')
          .append(", firebreakType=").append(firebreakType)
          .append(", current usage (bytes)=").append(currentUsageBytes)
          .append(", sigma=").append(sigma)
          .append('}');
        return sb.toString();
    }
}
