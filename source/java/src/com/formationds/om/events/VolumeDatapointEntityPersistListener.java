/*
 * Copyright (c) 2014, Formation Data Systems, Inc. All Rights Reserved.
 */

package com.formationds.om.events;

import com.formationds.commons.crud.EntityPersistListener;
import com.formationds.commons.events.FirebreakType;
import com.formationds.commons.model.Datapoint;
import com.formationds.commons.model.Volume;
import com.formationds.commons.model.builder.VolumeBuilder;
import com.formationds.commons.model.entity.FirebreakEvent;
import com.formationds.commons.model.entity.IVolumeDatapoint;
import com.formationds.commons.model.entity.VolumeDatapoint;
import com.formationds.om.repository.JDOEventRepository;
import com.formationds.om.repository.MetricRepository;
import com.formationds.om.repository.SingletonRepositoryManager;
import com.formationds.om.repository.helper.FirebreakHelper;
import org.apache.thrift.TException;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.time.Duration;
import java.time.Instant;
import java.util.*;
import java.util.concurrent.ConcurrentHashMap;

/**
 * A postPersist listener on VolumeDatapoint persistence operations to intercept and detect firebreak events.
 */
public class VolumeDatapointEntityPersistListener implements EntityPersistListener<IVolumeDatapoint> {

    private static final Logger logger = LoggerFactory.getLogger(VolumeDatapointEntityPersistListener.class);

    private MetricRepository mr = SingletonRepositoryManager.instance()
                                                            .getMetricsRepository();

    class FBInfo {
        private final Volume        v;
        private final FirebreakType type;

        public FBInfo( Volume v, FirebreakType type ) {
            this.v = v;
            this.type = type;
        }

        @Override
        public boolean equals( Object o ) {
            if ( this == o ) return true;
            if ( o == null || getClass() != o.getClass() ) return false;

            FBInfo fbInfo = (FBInfo) o;
            if ( !Objects.equals( this.type, fbInfo.type ) ) return false;
            // TODO: should be based on a unique id... once backend supports it
            return Objects.equals( this.v.getName(), fbInfo.v.getName() );
        }

        @Override
        public int hashCode() {
            // TODO: should be based on a unique id... once backend supports it
            return Objects.hash( v.getName(), type );
        }
    }

    final Map<FBInfo, FirebreakEvent> activeFirebreaks = new ConcurrentHashMap<>();
    Map<Volume, Boolean> isVolumeloaded = new ConcurrentHashMap<>();

    // TODO: this occurs within the VolumeDatapoint persistence path and could potentially prevent saving metrics
    // if errors are not handled here.  There could also be some impact on performance of that operation, though it
    // is unlikely to be in the user data path and so impact should be minimal.
    @Override
    public void postPersist( Collection<IVolumeDatapoint> vdp) {
        logger.trace( "postPersist handling of {} Volume data points.", vdp.size());
        try {
            doPostPersist(vdp);
        }
        catch (Throwable t) {
            logger.error("Failed to process datapoint postPersist event detection", t);
            // TODO: do we want to rethrow and cause the VolumeDatapoint commit to fail?
        }
    }

    /**
     * Post-persist implementation.  Use the FirebreakHelper to detect any firebreak events occurring
     * in the given set of volume datapoints.  If there are events, determine if there is already an
     * active firebreak for the associated volume and if not, generate a FirebreakEvent.
     *
     * @param vdp list of volume datapoints
     * @throws TException
     */
    protected void doPostPersist(Collection<IVolumeDatapoint> vdp) throws TException {
        Map<String, EnumMap<FirebreakType,FirebreakHelper.VolumeDatapointPair>> fb =
            new FirebreakHelper().findFirebreakEvents( (vdp instanceof List ?
                                                        (List<IVolumeDatapoint>) vdp :
                                                        new ArrayList<IVolumeDatapoint>( vdp )) );

        // first iterate over each volume
        fb.forEach((vid, fbvdps) -> {
            // for each volume, there may be 0, 1, or 2 datapoint pairs representing events.
            fbvdps.forEach((fbtype, volDp) -> {
                if (volDp.getDatapoint().getY().equals(FirebreakHelper.NEVER))
                    return;

                Volume v = new VolumeBuilder().withId(vid)
                                              .withName(volDp.getShortTermSigma().getVolumeName())
                                              .build();

                Optional<FirebreakEvent> activeFbe = hasActiveFirebreak(v, fbtype);
                if (!activeFbe.isPresent()) {
                    logger.trace("Firebreak event for '{}({})' with datapoints '{}'", v.getId(), v.getName(), volDp);

                    Datapoint dp = volDp.getDatapoint();
                    FirebreakEvent fbe = new FirebreakEvent(v, fbtype,
                                                            Instant.ofEpochSecond(dp.getY().longValue()).toEpochMilli(),
                                                            dp.getX().longValue(),
                                                            (volDp.getShortTermSigma().getValue() != 0.0D ?
                                                             volDp.getShortTermSigma().getValue() :
                                                             volDp.getLongTermSigma().getValue()));
                    EventManager.INSTANCE.notifyEvent(fbe);
                    activeFirebreaks.put(new FBInfo(v, fbtype), fbe);
                } else {
                    logger.trace("Firebreak event skipped - active firebreak for volume {}({}): {}",
                                 v.getId(), v.getName(), activeFbe.get());
                }
            });
        });
    }

    /**
     * @param vol the volume
     * @return true if there is an active firebreak for the volume and metric
     */
    protected Optional<FirebreakEvent> hasActiveFirebreak(Volume vol, FirebreakType fbtype) {
        Boolean isLoaded = isVolumeloaded.get(vol);
        if (isLoaded == null || !isLoaded) {
            loadActiveFirebreaks(vol, fbtype);
        }

        if (fbtype == null)
            return Optional.empty();

        FBInfo fbi = new FBInfo(vol, fbtype);
        FirebreakEvent fbe = activeFirebreaks.get(fbi);
        if (fbe == null)
            return Optional.empty();

        Instant oneDayAgo = Instant.now().minus(Duration.ofDays(1));
        Instant fbts = Instant.ofEpochMilli(fbe.getInitialTimestamp());
        if (fbts.isBefore(oneDayAgo)) {
            activeFirebreaks.remove(fbi);
            isVolumeloaded.remove(vol);
            return Optional.empty();
        }

        return Optional.of(fbe);
    }

    protected void loadActiveFirebreaks(Volume vol, FirebreakType fbtype) {
        // TODO: it really shouldn't be necessary to create a new instance here, but without it i'm
        // seeing duplicate results.  Need to revisit.  Note it may have something to do with the
        // "in" clause used in EventRepository.
        JDOEventRepository er = new JDOEventRepository();
        try {
            FirebreakEvent fb = er.findLatestFirebreak(vol, fbtype);

            if (fb != null) {
                activeFirebreaks.put(new FBInfo(vol, fb.getFirebreakType()), fb);
                isVolumeloaded.put(vol, Boolean.TRUE);
            }
        } finally {
            er.close();
        }
    }
}
