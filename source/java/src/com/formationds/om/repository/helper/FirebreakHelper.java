/*
 * Copyright (c) 2014, Formation Data Systems, Inc. All Rights Reserved.
 */

package com.formationds.om.repository.helper;

import com.formationds.apis.VolumeStatus;
import com.formationds.commons.calculation.Calculation;
import com.formationds.commons.events.FirebreakType;
import com.formationds.commons.model.Datapoint;
import com.formationds.commons.model.Series;
import com.formationds.commons.model.builder.SeriesBuilder;
import com.formationds.commons.model.builder.VolumeBuilder;
import com.formationds.commons.model.entity.VolumeDatapoint;
import com.formationds.commons.model.exception.UnsupportedMetricException;
import com.formationds.commons.model.type.Metrics;
import com.formationds.commons.util.ExceptionHelper;
import com.formationds.om.helper.SingletonConfigAPI;
import com.formationds.om.repository.SingletonRepositoryManager;
import org.apache.thrift.TException;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.ArrayList;
import java.util.EnumMap;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.stream.Collectors;

/**
 * @author ptinius
 */
public class FirebreakHelper {
    private static final Logger logger =
        LoggerFactory.getLogger( FirebreakHelper.class );

    private static final String INDEX_OUT_BOUNDS =
        "index is out-of-bounds %d, must between %d and %d.";

    // zero is not a value last occurred time
    public static final Double NEVER = 0.0;

    /**
     * default constructor
     */
    public FirebreakHelper() {

    }

    /**
     * @param queryResults the {@link List} representing the {@link VolumeDatapoint}
     *
     * @return Returns the @[link List} of {@link Series} representing firebreaks
     *
     * @throws org.apache.thrift.TException any unhandled thrift error
     */
    public List<Series> processFirebreak( final List<VolumeDatapoint> queryResults )
        throws TException {
        Map<String, VolumeDatapointPair> firebreakPointsVDP = findFirebreak( queryResults );
        Map<String, Datapoint> firebreakPoints = new HashMap<>();
        firebreakPointsVDP.entrySet().stream().forEach(kv -> firebreakPoints.put(kv.getKey(), kv.getValue().getDatapoint()));

        /*
         * TODO may not be a problem, but I need to think about.
         *
         * Currently, the firebreak series will not include any volumes
         * that have not had at least one existing entity record in the
         * object store.
         */

        final List<Series> series = new ArrayList<>();
        if( !firebreakPoints.isEmpty() ) {
            logger.trace( "Gathering firebreak details" );

            final Set<String> keys = firebreakPoints.keySet();

            for( final String key : keys ) {
                logger.trace( "Gathering firebreak for '{}'", key );
                final String volumeName =
                    SingletonConfigAPI.instance()
                                      .api()
                                      .getVolumeName( Long.valueOf( key ) );

                // only provide stats for existing volumes
                if( volumeName != null && volumeName.length() > 0 ) {
                    final Series s =
                        new SeriesBuilder().withContext(
                            new VolumeBuilder().withId( key )
                                               .withName( volumeName )
                                               .build() )
                                           .withDatapoint( firebreakPoints.get( key ) )
                                           .build();
                    /*
                     * "Firebreak" is not a valid series type, based on the
                     * Metrics enum type. But since the UI provides all of its
                     * own json parsing, i.e. it doesn't use object mode
                     * used by OM.
                     */
                    s.setType( "Firebreak" );
                    series.add( s );
                }
            }
        } else {
            logger.trace( "no firebreak data available" );
        }

        return series;
    }
    
    /**
     * This method will get a list of all firebreak events from the datapoint list organized by volume ID
     * @param datapoints
     * @return
     * @throws TException
     */
    public Map<String, List<VolumeDatapointPair>> findAllFirebreaksByVolume( final List<VolumeDatapoint> datapoints ) throws TException {
    
    	final Map<String, List<VolumeDatapointPair>> results = new HashMap<String, List<VolumeDatapointPair>>();
    	
    	final List<VolumeDatapointPair> paired = extractPairs( datapoints );
    	
    	paired.stream().forEach( pair -> {
    	
    		String key = pair.getShortTermSigma().getVolumeId();
    		
    		if ( !results.containsKey( key ) ){
    			
    			List<VolumeDatapointPair> listOfEvents = new ArrayList<VolumeDatapointPair>();
    			results.put( key, listOfEvents );
    		}
    		
    		// if it is a firebreak, add it to the list
    		if ( isFirebreak( pair ) ){
    			results.get( key ).add( pair );
    		}
    	});
    	
    	return results;
    }

    /**
     * Find firebreak related stats for display in the UI.  This is similar to {@link #findFirebreakEvents},
     * except that it does not distinguish between performance and capacity events.
     *
     * @param datapoints the {@link VolumeDatapoint} representing the volume datapoints
     *
     * @return Returns {@link java.util.Map} of volume id to
     *  {@link com.formationds.om.repository.helper.FirebreakHelper.VolumeDatapointPair}
     */
    protected Map<String, VolumeDatapointPair> findFirebreak(final List<VolumeDatapoint> datapoints) throws TException {
        final Map<String, VolumeDatapointPair> results = new HashMap<>();
        final List<VolumeDatapointPair> paired = extractPairs(datapoints);

        paired.stream().forEach( pair -> {

            final String key = pair.getShortTermSigma().getVolumeId();
            final String volumeName = pair.getShortTermSigma()
                                          .getVolumeName();

            if (!results.containsKey(key)) {
                final Datapoint datapoint = new Datapoint();
                datapoint.setY(NEVER);    // firebreak last occurrence

                final Optional<VolumeStatus> status =
                    SingletonRepositoryManager.instance()
                                              .getMetricsRepository()
                                              .getLatestVolumeStatus( volumeName );
                if( status.isPresent() ) {
                    // use the usage, OBJECT volumes have no fixed capacity
                    datapoint.setX((double)status.get().getCurrentUsageInBytes());
                }

                pair.setDatapoint(datapoint);
                results.put(key, pair);
            }

            if (isFirebreak(pair)) {
                /*
                 * if there is already a firebreak for the volume in the
                 * results use it instead of the pair and set the timestamp
                 * using the current pair.
                 */
                results.get(key).getDatapoint().setY((double)pair.getShortTermSigma().getTimestamp());
            }
        } );

        return results;
    }

    /**
     * Given the list of volume datapoints, determine if any of them represent a
     * firebreak event.  The results contain at most 2 events per volume, one for CAPACITY,
     * and one for PERFORMANCE.
     *
     * @param datapoints the {@link VolumeDatapoint} representing the volume datapoints
     *
     * @return Returns {@link java.util.Map} of volume id to an enum map of the FirebreakType to
     *  {@link com.formationds.om.repository.helper.FirebreakHelper.VolumeDatapointPair}
     */
    // NOTE: this is very similar to findFirebreak except that it distinguishes results by
    // volume id AND FirebreakType.  It also caches volume status outside of the loop
    public Map<String, EnumMap<FirebreakType, VolumeDatapointPair>> findFirebreakEvents(final List<VolumeDatapoint> datapoints)
        throws TException {
        final Map<String, EnumMap<FirebreakType,VolumeDatapointPair>> results = new HashMap<>();
        final List<VolumeDatapointPair> paired = extractPairs(datapoints);

        // TODO: use volume id once available
        // cache volume status for each known volume for updating the datapoint
        final Map<String, VolumeStatus> vols = loadCurrentVolumeStatus();

        paired.stream().forEach( pair -> {
            final String volId = pair.getShortTermSigma().getVolumeId();
            final FirebreakType type = pair.getFirebreakType();
            final String volumeName = pair.getShortTermSigma()
                                          .getVolumeName();

            if (isFirebreak(pair)) {

                final Datapoint datapoint = new Datapoint();
                datapoint.setY((double)pair.getShortTermSigma().getTimestamp());    // firebreak last occurrence
                
                // initializing the usage to 0 in case the status is not defined
                datapoint.setX( 0.0D );
                
                // TODO: use volid once available
                final VolumeStatus status = vols.get(volumeName);
                if (status != null) {

                    /*
                     * this is include for both capacity and performance so the
                     * GUI know the size to draw the firebreak box that represents
                     * this volume.
                     */
                    datapoint.setX((double)status.getCurrentUsageInBytes());
                }
                pair.setDatapoint(datapoint);

                EnumMap<FirebreakType,VolumeDatapointPair> pt = results.get(volId);
                if (pt == null) {
                    pt = new EnumMap<>(FirebreakType.class);
                    results.put(volId, pt);
                }
                pt.put(type, pair);
            }
        } );

        return results;
    }

    /**
     * Load the current volume status into a map indexed by volume name
     *
     * @return a map containing the current volume status for each known volume
     * @throws TException
     */
    // TODO: replace volume name with id once available.
    private Map<String, VolumeStatus> loadCurrentVolumeStatus() throws TException {

        final Map<String,VolumeStatus> vols = new HashMap<>();
        SingletonConfigAPI.instance().api().listVolumes("").forEach((vd) -> {

            final Optional<VolumeStatus> optionalStatus =
                SingletonRepositoryManager.instance()
                                          .getMetricsRepository()
                                          .getLatestVolumeStatus(
                                              vd.getName() );
            if( optionalStatus.isPresent() ) {
                vols.put( vd.getName(),
                          optionalStatus.get() );
            }
        });

        return vols;
    }

    /**
     * Extract VolumeDatapoint Pairs that represent Firebreak events.
     *
     * @param datapoints all datapoints to filter
     *
     * @return a list of firebreak VolumeDatapoint pairs
     */
    public List<VolumeDatapointPair> extractPairs(List<VolumeDatapoint> datapoints) {
        // TODO: would it make more sense to return a Map of either Timestamp to Map of Volume -> VolumeDatapointPair or
        // a List/Set (ordered by timestamp) of Map Volume -> VolumeDatapointPair?
        final List<VolumeDatapointPair> paired = new ArrayList<>( );
        final List<VolumeDatapoint> firebreakDP = extractFirebreakDatapoints(datapoints);

        Map<Long, Map<String, List<VolumeDatapoint>>> orderedFBDPs;
        orderedFBDPs = firebreakDP.stream()
                                  .collect(Collectors.groupingBy(VolumeDatapoint::getTimestamp,
                                                                 Collectors.groupingBy(VolumeDatapoint::getVolumeName)));

        // TODO: we may be able to parallelize this with a Spliterator by splitting first on timestamp and second on volume.
        // (similar to the StreamHelper.consecutiveStream used previously)
        orderedFBDPs.forEach((ts, voldps) -> {
            // at this point we know:
            // 1) we only have firebreak stats and there are up to four values per volume
            // 2) they are for a specific timestamp
            // We don't know for certain if all are present.
            voldps.forEach((vname, vdps) -> {
                Optional<VolumeDatapoint> stc_sigma = findMetric(Metrics.STC_SIGMA, vdps);
                Optional<VolumeDatapoint> ltc_sigma = findMetric(Metrics.LTC_SIGMA, vdps);
                Optional<VolumeDatapoint> stp_sigma = findMetric(Metrics.STP_SIGMA, vdps);
                Optional<VolumeDatapoint> ltp_sigma = findMetric(Metrics.LTP_SIGMA, vdps);
                if (stc_sigma.isPresent() && ltc_sigma.isPresent())
                    paired.add(new VolumeDatapointPair(stc_sigma.get(), ltc_sigma.get()));
                if (stp_sigma.isPresent() && ltp_sigma.isPresent())
                    paired.add(new VolumeDatapointPair(stp_sigma.get(), ltp_sigma.get()));
            });
        });

        return paired;
    }

    /**
     * @param m the metric to find in the list of datapoints
     * @param dps a list of datapoints for a specific timestamp and volume.
     *
     * @return the optional first datapoint matching the metric if present in the specified list.
     */
    private Optional<VolumeDatapoint> findMetric(Metrics m, List<VolumeDatapoint> dps) {
        return dps.stream().filter((v) -> Metrics.byMetadataKey(v.getKey()).equals(m)).findFirst();
    }

    /**
     * Given a list of datapoints, extract all datapoints that are related to Firebreak.
     *
     * @param datapoints all datapoints to filter.
     *
     * @return the list of datapoints that are firebreak datapoints.
     */
    public List<VolumeDatapoint> extractFirebreakDatapoints(List<VolumeDatapoint> datapoints) {
        final List<VolumeDatapoint> firebreakDP = new ArrayList<>();

        /*
         * filter just the firebreak volume datapoints
         */
        datapoints.stream()
                  .filter(this::isFirebreakType)
                  .forEach(firebreakDP::add);
        return firebreakDP;
    }

    /**
     * @param p the datapoint pair
     * @return true if the datapoint pair represents a firebreak
     */
    protected boolean isFirebreak(VolumeDatapointPair p) {
        return isFirebreak(p.getShortTermSigma(), p.getLongTermSigma());
    }

    /**
     * @param s1 the short-term sigma
     * @param s2 the long-term sigma
     * @return true if the datapoint pair represents a firebreak
     */
    protected boolean isFirebreak(VolumeDatapoint s1, VolumeDatapoint s2) {
      return Calculation.isFirebreak(s1.getValue(),
                                     s2.getValue());
    }

    /**
     * @param vdp the volume datapoint
     * @return true if the volume datapoint represents a metric associated with a firebreak event
     */
    private boolean isFirebreakType( final VolumeDatapoint vdp ) {
        try {
            return Metrics.FIREBREAK.contains( Metrics.byMetadataKey( vdp.getKey() ) );
        } catch( UnsupportedMetricException e ) {
            logger.warn( e.getMessage() );
            logger.trace( ExceptionHelper.toString( e ) );
            return false;
        }
    }

    /**
     * Represents a pair of volume datapoints for Firebreak stats (Long/Short term Capacity/Performance).
     * Note that this also contains the Datapoint that is extracted and applied to a series to
     * return to the UI.  The datapoint is set separately during processing.
     */
    public static class VolumeDatapointPair {
        private final VolumeDatapoint shortTermSigma;
        private final VolumeDatapoint longTermSigma;

        private final FirebreakType firebreakType;

        private Datapoint datapoint;

        /**
         * Create the Volume datapoint pair with the initial sigma values.
         *
         * @param shortTerm the short-term sigma datapoint
         * @param longTerm the long-term sigma datapoint
         *
         * @throws java.lang.IllegalArgumentException if the datapoints are:
         *   <ul>
         *       <li>Not for the same volume</li>
         *       <li>Not a short-term and long-term capacity or performance metric</li>
         *       <li>Not the same firebreak data type</li>
         *   </ul>
         */
        VolumeDatapointPair(VolumeDatapoint shortTerm, VolumeDatapoint longTerm) {
            // TODO: use ID once provided by backend
            if (!shortTerm.getVolumeName().equals(longTerm.getVolumeName()))
                throw new IllegalArgumentException("Volume for short/long term datapoint pairs must match");

            Metrics sm = Metrics.byMetadataKey(shortTerm.getKey());
            if (!(Metrics.STC_SIGMA.equals(sm) || Metrics.STP_SIGMA.equals(sm)))
                throw new IllegalArgumentException("Short term sigma datapoint must be one of the valid short-term " +
                                                "capacity or performance metric types.  Sigma=" + sm);

            Metrics lm = Metrics.byMetadataKey(longTerm.getKey());
            if (!(Metrics.LTC_SIGMA.equals(lm) || Metrics.LTP_SIGMA.equals(lm)))
                throw new IllegalArgumentException("Long term sigma datapoint must be one of the valid long-term " +
                                                "capacity or performance metric types.  Sigma=" + sm);

            Optional<FirebreakType> smto = FirebreakType.metricFirebreakType(sm);
            Optional<FirebreakType> lmto = FirebreakType.metricFirebreakType(lm);
            if (!(smto.isPresent() && lmto.isPresent())) {
                throw new IllegalArgumentException("Both data points must translate to a valid Firebreak type.");
            }

            if (!smto.get().equals(lmto.get())) {
                throw new IllegalArgumentException("Both datapoints must be the same type of Firebreak metric, " +
                                                   "either CAPACITY or PERFORMANCE");
            }

            this.shortTermSigma = shortTerm;
            this.longTermSigma = longTerm;
            this.firebreakType = smto.get();
        }

        /**
         * Set the datapoint
         * @param dp the datapoint
         */
        protected void setDatapoint(Datapoint dp) { datapoint = dp; }

        /**
         * @return the datapoint associated with this VolumeDatapointPair
         */
        public Datapoint getDatapoint() { return datapoint; }

        /**
         * @return the short-term sigma
         */
        public VolumeDatapoint getShortTermSigma() { return shortTermSigma; }

        /**
         * @return the long-term sigma
         */
        public VolumeDatapoint getLongTermSigma() { return longTermSigma; }

        /**
         * @return the firebreak type represented by this pair
         */
        public FirebreakType getFirebreakType() { return firebreakType; }

        /**
         * @return Returns a {@link String} representing this object
         */
        @Override
        public String toString() {
            return String.format("ts=%s: %s/%s", getShortTermSigma().getTimestamp(), shortTermSigma, longTermSigma);
        }
    }
}
