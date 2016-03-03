/*
 * Copyright (c) 2014-2016, Formation Data Systems, Inc. All Rights Reserved.
 */
package com.formationds.om.repository.helper;

import com.formationds.client.v08.model.Size;
import com.formationds.client.v08.model.SizeUnit;
import com.formationds.client.v08.model.Volume;
import com.formationds.commons.calculation.Calculation;
import com.formationds.commons.model.Datapoint;
import com.formationds.commons.model.DateRange;
import com.formationds.commons.model.Series;
import com.formationds.commons.model.Statistics;
import com.formationds.commons.model.abs.Calculated;
import com.formationds.commons.model.abs.Context;
import com.formationds.commons.model.abs.Metadata;
import com.formationds.commons.model.builder.DatapointBuilder;
import com.formationds.commons.model.calculated.capacity.CapacityConsumed;
import com.formationds.commons.model.calculated.capacity.CapacityDeDupRatio;
import com.formationds.commons.model.calculated.capacity.CapacityFull;
import com.formationds.commons.model.calculated.capacity.CapacityToFull;
import com.formationds.commons.model.calculated.capacity.TotalCapacity;
import com.formationds.commons.model.calculated.performance.AverageIOPs;
import com.formationds.commons.model.calculated.performance.IOPsConsumed;
import com.formationds.commons.model.calculated.performance.PercentageConsumed;
import com.formationds.commons.model.entity.Event;
import com.formationds.commons.model.entity.IVolumeDatapoint;
import com.formationds.commons.model.entity.VolumeDatapoint;
import com.formationds.commons.model.type.Metrics;
import com.formationds.commons.model.type.StatOperation;
import com.formationds.commons.togglz.feature.flag.FdsFeatureToggles;
import com.formationds.commons.util.DateTimeUtil;
import com.formationds.commons.util.Memoizer;
import com.formationds.commons.util.Tuples;
import com.formationds.commons.util.Tuples.Pair;
import com.formationds.om.helper.SingletonConfigAPI;
import com.formationds.om.redis.RedisSingleton;
import com.formationds.om.repository.EventRepository;
import com.formationds.om.repository.MetricRepository;
import com.formationds.om.repository.SingletonRepositoryManager;
import com.formationds.om.repository.query.MetricQueryCriteria;
import com.formationds.om.repository.query.QueryCriteria;
import com.formationds.om.repository.query.builder.MetricQueryCriteriaBuilder;
import com.formationds.security.AuthenticationToken;
import com.formationds.security.Authorizer;

import org.apache.commons.math3.stat.regression.SimpleRegression;
import org.apache.thrift.TException;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.time.Duration;
import java.time.LocalDateTime;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.OptionalLong;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.Function;
import java.util.stream.Collectors;
import java.util.stream.DoubleStream;

/**
 * @author ptinius
 */
public class QueryHelper {
    private static final Logger logger =
        LoggerFactory.getLogger( QueryHelper.class );

    /**
     * A shared instance, used only if the USE_SHARED_QUERY_HELPER feature toggle is enabled.
     */
    private static final QueryHelper SHARED = new QueryHelper();

    /**
     * If the {@link #USE_SHARED_QUERY_HELPER} configuration/toggle is set, the use
     * a shared instance that may perform query caching.  Otherwise, use a new instance that
     * will not cache values.
     *
     * @return a QueryHelper instance
     */
    public static QueryHelper instance() {
        if (FdsFeatureToggles.USE_SHARED_QUERY_HELPER.isActive()) {
            return SHARED;
        } else {
            return new QueryHelper();
        }
    }

    /**
     * default constructor
     */
    protected QueryHelper() {
    }

    /**
     * @param datapoints the {@link List} of {@link VolumeDatapoint}
     *
     * @return Return {@link Map} representing volumes as the keys and value
     * representing a {@link List} of {@link VolumeDatapoint}
     */
    protected static <V extends IVolumeDatapoint> Map<String, List<V>> byVolumeNameTimestamp( final List<? extends IVolumeDatapoint> datapoints ) {
        final Map<String, List<V>> mapped = new HashMap<>();

        final Comparator<IVolumeDatapoint> VolumeDatapointComparator =
            Comparator.comparing( IVolumeDatapoint::getVolumeName )
                      .thenComparing( IVolumeDatapoint::getTimestamp );

        datapoints.stream()
                  .sorted( VolumeDatapointComparator )
                  .forEach( ( v ) -> {
                      if ( !mapped.containsKey( v.getVolumeName() ) ) {
                          mapped.put( v.getVolumeName(), new ArrayList<>() );
                      }

                      mapped.get( v.getVolumeName() )
                            .add( (V)v );
                  } );

        return mapped;
    }

    /**
     * @param query the {@link com.formationds.om.repository.query.MetricQueryCriteria} representing the query
     *
     * @return Returns the {@link Statistics} representing the result of {@code
     * query}
     */
    @SuppressWarnings("unchecked")
    public Statistics execute( final MetricQueryCriteria query, final Authorizer authorizer,
                               final AuthenticationToken token )
        throws TException {
        final Statistics stats = new Statistics();
        if ( query != null ) {
            final List<Series> series = new ArrayList<>();
            final List<Calculated> calculatedList = new ArrayList<>();

            MetricRepository repo = SingletonRepositoryManager.instance().getMetricsRepository();

            query.setContexts( validateContextList( query, authorizer, token ) );

            final List<IVolumeDatapoint> queryResults = (List<IVolumeDatapoint>) repo.query( query );

            final Map<String, List<IVolumeDatapoint>> originated =
                byVolumeNameTimestamp( queryResults );

            if ( isPerformanceQuery( query.getSeriesType() ) ) {

                series.addAll(
                    SeriesHelper.getRollupSeries( queryResults,
                                                        query.getRange(),
                                                        query.getSeriesType(),
                                                        StatOperation.SUM ) );
                final IOPsConsumed ioPsConsumed = new IOPsConsumed();
                ioPsConsumed.setDailyAverage( 0.0 );
                calculatedList.add( ioPsConsumed );

            } else if( isCapacityQuery( query.getSeriesType() ) ) {

                series.addAll(
                    SeriesHelper.getRollupSeries( queryResults,
                                                        query.getRange(),
                                                        query.getSeriesType(),
                                                        StatOperation.MAX_X) );

                calculatedList.add( deDupRatio() );

                // let's get the physical bytes consumed.
                Series physicalBytes = series.stream()
                                             .filter( ( s ) -> Metrics.UBYTES.matches( s.getType( ) ) )
                                             .findFirst()
                                             .orElse( null );

                // only the FDS admin is allowed to get data about the capacity limit
                // of the system
                if ( authorizer.userFor( token ).isIsFdsAdmin() ) {

	                // TODO finish implementing -- once the platform has total system capacity
                	Long capacity = SingletonConfigAPI.instance().api().getDiskCapacityTotal();
                    logger.trace( "Disk Capacity Total::{}", capacity );
                    final Size systemCapacity;
                    if( FdsFeatureToggles.NEW_SUPERBLOCK.isActive( ) )
                    {
                        // normalize to bytes
                        systemCapacity =
                            Size.of( Size.gb( capacity ).getValue( SizeUnit.B ),
                                     SizeUnit.B );
                    }
                    else
                    {
                        // normalize to bytes
                        systemCapacity =
                            Size.of( Size.mb( capacity ).getValue( SizeUnit.B ),
                                     SizeUnit.B );
                    }

                    final Double systemCapacityInBytes = systemCapacity.getValue( SizeUnit.B ).doubleValue();
	                TotalCapacity totalCap = new TotalCapacity();
	                totalCap.setTotalCapacity( systemCapacityInBytes );
	                calculatedList.add( totalCap );

	            	if ( physicalBytes != null ){
	            		calculatedList.add( secondsToFullThirtyDays( query.getContexts(),
                                                                     Size.of( systemCapacityInBytes,
                                                                              SizeUnit.B ) ) );

	            	}
	            	else {
	            		logger.info( "There were no physical bytes reported for the system.  Cannot calculate time to full.");
	            	}
                }

                Double bytesConsumed = RedisSingleton.INSTANCE
                                                     .api()
                                                     .getDomainUsedCapacity()
                                                     .getValue( )
                                                     .doubleValue();
                final CapacityConsumed consumed = new CapacityConsumed();
                consumed.setTotal( bytesConsumed );
                calculatedList.add( consumed );

            } else if ( isPerformanceBreakdownQuery( query.getSeriesType() ) ) {

            	series.addAll(
            		SeriesHelper.getRollupSeries( queryResults,
                                                        query.getRange(),
                                                        query.getSeriesType(),
                                                        StatOperation.RATE) );

            	calculatedList.add( getAverageIOPs( series ) );
            	calculatedList.addAll( getTieringPercentage( series ) );

            } else {

                // individual stats

                query.getSeriesType()
                     .stream()
                     .forEach( ( m ) ->
                         series.addAll( otherQueries( originated,
                        		 					  query.getRange(),
                                                      m ) ) );
            }

            if( !series.isEmpty() ) {

                series.forEach( ( s ) -> {

                	// if the datapoints set is null, don't try to sort it.  Leave it alone
                	if ( s.getDatapoints() != null && !s.getDatapoints().isEmpty() ) {
                		new DatapointHelper().sortByX( s.getDatapoints() );
                	}
                });
                stats.setSeries( series );
            }

            if( !calculatedList.isEmpty() ) {
                stats.setCalculated( calculatedList );
            }

        }

        return stats;
    }

    /**
     *
     * @param query the {@link com.formationds.om.repository.query.MetricQueryCriteria} representing the query
     * @return the events matching the query criteria
     * @throws TException
     */
    public List<? extends Event> executeEventQuery( final QueryCriteria query )
            throws TException {
        EventRepository er = SingletonRepositoryManager.instance().getEventRepository();

        return er.query(query);
    }

    /**
     * @param metrics the [@link List} of {@link Metrics}
     *
     * @return Returns {@code true} if all {@link Metrics} within the
     *         {@link List} are of performance type. Otherwise {@code false}
     */
    protected boolean isPerformanceQuery( final List<Metrics> metrics ) {

    	if ( metrics.size() != Metrics.PERFORMANCE.size() ){
    		return false;
    	}

        for( final Metrics m : metrics ) {
            if( !Metrics.PERFORMANCE.contains( m ) ) {
                return false;
            }
        }

        return true;
    }

    /**
     * @param metrics the [@link List} of {@link Metrics}
     *
     * @return Returns {@code true} if all {@link Metrics} within the
     *         {@link List} are of capacity type. Otherwise {@code false}
     */
    protected boolean isCapacityQuery( final List<Metrics> metrics ) {

    	if ( metrics.size() != Metrics.CAPACITY.size() ){
    		return false;
    	}

        for( final Metrics m : metrics ) {
            if( !Metrics.CAPACITY.contains( m ) ) {
                return false;
            }
        }

        return true;
    }

    /**
     * determine if the {@link List} of {@link Metrics} matches the performance breakdown definition
     *
     * @param metrics
     *
     * @return returns true if all {@link Metrics} are included in both sets
     */
    protected boolean isPerformanceBreakdownQuery( final List<Metrics> metrics ) {

    	if ( metrics.size() != Metrics.PERFORMANCE_BREAKDOWN.size() ){
    		return false;
    	}

    	for ( final Metrics m : metrics ) {
    		if ( !Metrics.PERFORMANCE_BREAKDOWN.contains( m ) ){
    			return false;
    		}
    	}

    	return true;
    }

    /**
     * @param organized the {@link Map} of volume containing a {@link List}
     *                  of {@link VolumeDatapoint}
     * @param metrics   the {@link Metrics} representing the none firebreak
     *                  metric
     *
     * @return Returns q {@link List} of {@link Series}
     */
    protected List<Series> otherQueries(
        final Map<String, List<IVolumeDatapoint>> organized,
        final DateRange dateRange,
        final Metrics metrics ) {
        final List<Series> series = new ArrayList<>();

        organized.forEach( ( key, volumeDatapoints ) -> {
            final Series s = new Series();

            // if a bogus metric was sent in, don't attempt to add it to the set
            if ( metrics == null ){
            	return;
            }

            s.setType( metrics.name() );
            volumeDatapoints.stream()
                            .distinct()
                            .filter( ( p ) -> metrics.matches( p.getKey() ) )
                            .forEach( ( p ) -> {
                                final Datapoint dp =
                                    new DatapointBuilder().withX( (double)p.getTimestamp() )
                                                          .withY( p.getValue() )
                                                          .build();
                                s.setDatapoint( dp );
                                s.setContext( new Volume( Long.parseLong( p.getVolumeId() ), p.getVolumeName() ) );
                            } );

            // get earliest data point
            OptionalLong oLong = volumeDatapoints.stream()
            		.filter( ( p ) -> metrics.matches( p.getKey() ) )
            		.mapToLong( IVolumeDatapoint::getTimestamp )
            		.min();

            if ( oLong.isPresent() && oLong.getAsLong() > dateRange.getStart() && volumeDatapoints.size() > 0 ){

            	String volumeId = volumeDatapoints.get( 0 ).getVolumeId();
            	String volumeName = volumeDatapoints.get( 0 ).getVolumeName();
            	String metricKey = volumeDatapoints.get( 0 ).getKey();

	            // a point just earlier than the first real point. ... let's do one second
            	VolumeDatapoint justBefore = new VolumeDatapoint( oLong.getAsLong() - 1, volumeId, volumeName, metricKey, 0.0 );
            	VolumeDatapoint theStart = new VolumeDatapoint( dateRange.getStart(), volumeId, volumeName, metricKey, 0.0 );

            	s.setDatapoint( new DatapointBuilder().withX( (double)justBefore.getTimestamp() )
            										  .withY( justBefore.getValue() )
            										  .build());

	        	// at start time
	        	s.setDatapoint( new DatapointBuilder().withX( (double)theStart.getTimestamp() )
	        										  .withY( theStart.getValue() )
	        										  .build() );
            }

            series.add( s );
        } );

        return series;
    }

    /**
     * This will look at the {@link Context} of the query and make sure the user has access
     * to them before allowing them to query for its information.  If there are no contexts,
     * it will fill them in with the {@link Volume}s the user can access.
     *
     * ** THIS ASSUMES CONTEXT IS A VOLUME **
     * If this assumption becomes false at some point then this method will need to change
     * in order to take that into consideration
     *
     * @param query
     * @param token
     */
    protected List<Volume> validateContextList( final MetricQueryCriteria query, final Authorizer authorizer, final AuthenticationToken token ){

    	List<Volume> contexts = query.getContexts();

    	com.formationds.util.thrift.ConfigurationApi api = SingletonConfigAPI.instance().api();

    	// fill it with all the volumes they have access to
    	if ( contexts.isEmpty() ){

    		try {

	    		contexts = api.listVolumes("")
	    			.stream()
                        .filter(vd -> authorizer.ownsVolume(token, vd.getName()))

/*
 * HACK
 * .filter( v -> isSystemVolume() != true )
 *
 * THIS IS ALSO IN ListVolumes.java
 */
                        .filter( v-> !v.getName().startsWith( "SYSTEM_" )  )
                        .map(vd -> {

                            String volumeId = "";

                            try {
                                volumeId = String.valueOf(api.getVolumeId(vd.getName()));
                            } catch (TException ignored ) {
                                return null;
                            }

                            Volume volume = new Volume( Long.parseLong(volumeId), vd.getName() );

                            return volume;
                        })
                        .filter( (v) -> v != null )
	    			    .collect( Collectors.toList() );

    		} catch ( Exception e ){
    			logger.error( "Could not gather the volumes this user has access to.", e) ;
    		}
    	}
    	// validate the request matches the authorization
    	else {

    		contexts = contexts.stream().filter( c -> {
                boolean hasAccess = authorizer.ownsVolume(token, ((Volume) c).getName());

                if ( !hasAccess ){
    				// TODO: Add an audit event here because someone may be trying an attack
    				logger.warn( "User does not have access to query for volume: " + ((Volume)c).getName() +
    					".  It will be removed from the query context." );
    			}

    			return hasAccess;
    		})
    		.collect( Collectors.toList() );

    	}

    	return contexts;
    }

    /**
     * @return Returns a {@link List} of {@link Metadata}
     */
    protected List<Metadata> metadata() {
        return new ArrayList<>();
    }

    /**
     *
     * @param series
     * @return Returns the average IOPs for the collection of series passed in
     */
    protected AverageIOPs getAverageIOPs( List<Series> series ){

    	// sum each series (which is already a series of averages)
    	// divide by input # to get the average of averages
    	// now add the averages together for the total average
    	Double rawAvg = series.stream().flatMapToDouble( s -> {
    		return DoubleStream.of( s.getDatapoints().stream()
    				.flatMapToDouble( dp -> DoubleStream.of( dp.getY() ) ).sum() / s.getDatapoints().size() );
    	}).sum();

    	final AverageIOPs avgIops = new AverageIOPs();
    	avgIops.setAverage( rawAvg );

    	return avgIops;
    }

    /**
     * This will look at all the gets and calculate the percentage of each
     *
     * @param series
     * @return
     */
    protected List<PercentageConsumed> getTieringPercentage( List<Series> series ){

//    	Series gets = series.stream().filter( s -> s.getType().equals( Metrics.HDD_GETS.name() ) )
//        	.findFirst().get();
//    	Double getsHdd = gets.getDatapoints().stream().mapToDouble( Datapoint::getY ).sum();

    	Double getsHdd = RedisSingleton.INSTANCE
                                       .api()
                                       .getDomainUsedCapacity()
                                       .getValue( SizeUnit.B )
                                       .doubleValue();

    	Series getsssd = series.stream().filter( s -> s.getType().equals( Metrics.SSD_GETS.name() ) )
        		.findFirst().get();

    	Double getsSsd = getsssd.getDatapoints().stream().mapToDouble( Datapoint::getY ).sum();

    	Double sum = getsHdd + getsSsd;

    	long ssdPerc = 0L;
    	long hddPerc = 0L;

    	if ( sum > 0 ){
    		ssdPerc = Math.round( (getsSsd / sum) * 100.0 );
    		hddPerc = Math.round( (getsHdd / sum) * 100.0 );
    	}

        logger.trace( "Tiering Percentage::HDD Capacity: {} ({}%)::SDD Capacity: {} ({}%)::Sum: {}",
                      getsHdd, hddPerc, getsSsd, ssdPerc, sum );

    	PercentageConsumed ssd = new PercentageConsumed();
    	ssd.setPercentage( (double)ssdPerc );

    	PercentageConsumed hdd = new PercentageConsumed();
    	hdd.setPercentage( (double)hddPerc );

    	List<PercentageConsumed> percentages = new ArrayList<>( );
    	percentages.add( ssd );
    	percentages.add( hdd );

    	return percentages;
    }

    /**
     * @return Returns {@link CapacityDeDupRatio}
     */
    protected CapacityDeDupRatio deDupRatio() {
        final Double lbytes =
            SingletonRepositoryManager.instance()
                                      .getMetricsRepository()
                                      .sumLogicalBytes();

        final Double ubytes =
            SingletonRepositoryManager.instance()
                                      .getMetricsRepository()
                                      .sumUsedBytes();

        final CapacityDeDupRatio dedup = new CapacityDeDupRatio();
        final Double d = Calculation.ratio( lbytes, ubytes );

        logger.trace( "DE-DUP: LBYTES:{} UBYTES:{} Ratio:{} ( {} )",
                      Size.of( lbytes, SizeUnit.B ).getValue( SizeUnit.MB ),
                      Size.of( ubytes, SizeUnit.B ).getValue( SizeUnit.MB ),
                      d, ( ( d < 0 ) ? 1.0 : d ) );

        dedup.setRatio( ( ( d < 0 ) ? 1.0 : d ) );
        return dedup;
    }

    /**
     * @param consumed       the {@link CapacityConsumed} representing bytes
     *                       used
     * @param systemCapacity the {@link Double} representing system capacity in
     *                       bytes
     *
     * @return Returns {@link CapacityFull}
     */
    public CapacityFull percentageFull( final CapacityConsumed consumed,
                                        final Double systemCapacity ) {
        final CapacityFull full = new CapacityFull();
        full.setPercentage( ( int ) Calculation.percentage( consumed.getTotal(),
                                                            systemCapacity ) );
        return full;
    }

    // So here we are using Memoization to store the computed results.  As the system capacity
    // or volume list changes, this will automatically be recalculated (because the Pair tuple
    // representing the arguments to the function and stored as the memoized key will change)
    private final Function<Pair<List<Volume>, Size>,
                           CapacityToFull> ctfTask = Memoizer.memoize( Optional.of( Duration.ofHours( 12 ) ),
                                                                       (p) -> {
        List<Volume> volumes = p.getLeft();
        Size systemCapacity = p.getRight();

        return doCalculateTimeToFull( volumes, systemCapacity );

    } );

    /**
     * @param volumes
     * @param systemCapacity
     * @return
     */
    protected CapacityToFull doCalculateTimeToFull( List<Volume> volumes, Size systemCapacity ) {
        MetricQueryCriteriaBuilder queryBuilder =
                new MetricQueryCriteriaBuilder( QueryCriteria.QueryType.SYSHEALTH_CAPACITY );

        //REVIEWERS: IS UBYTES correct now or should this be PBYTES now????

        // For capacity time-to-full we need enough history to calculate the regression.  Using 30 days
        DateRange range = DateRange.last( 30L, com.formationds.client.v08.model.TimeUnit.DAYS );
        MetricQueryCriteria query = queryBuilder.withContexts( volumes )
                                                .withSeriesType( Metrics.UBYTES )
                                                .withRange( range )
                                                .build();

        final MetricRepository metricsRepository =
            SingletonRepositoryManager.instance( )
                                      .getMetricsRepository( );

        @SuppressWarnings("unchecked")
        final List<IVolumeDatapoint> queryResults =
            ( List<IVolumeDatapoint> ) metricsRepository.query( query );

        List<Series> series = SeriesHelper.getRollupSeries( queryResults,
                                                            query.getRange( ),
                                                            query.getSeriesType( ),
                                                            StatOperation.SUM );

        return toFull( series.get( 0 ), systemCapacity.getValue( SizeUnit.B )
                       .doubleValue() );
    }

    /**
     *
     * @param volumes
     * @param systemCapacity
     * @return the seconds to full calculation based on a regression over up to the last thirty days.
     */
    public CapacityToFull secondsToFullThirtyDays( final List<Volume> volumes,
                                                   final Size systemCapacity )
    {
        return ctfTask.apply( Tuples.tupleOf( volumes, systemCapacity ) );
    }

    /**
     * @return Returns {@link CapacityFull}
     */
    public CapacityToFull toFull( final Series pSeries,  final Double systemCapacity ) {
        /*
         * TODO finish implementation
         * Add a non-linear regression for potentially better matching
         *
         */
    	final SimpleRegression linearRegression = new SimpleRegression();

    	pSeries.getDatapoints()
               .stream()
               .forEach( ( point ) -> linearRegression.addData( point.getX( ), point.getY( ) ) );

    	Double secondsToFull = systemCapacity / linearRegression.getSlope();
        if( secondsToFull < 0.0 )
        {
            secondsToFull = Double.MAX_VALUE;
        }

        final CapacityToFull to = new CapacityToFull();
        to.setToFull( secondsToFull.longValue() );

        logger.trace( "To Full {} seconds; {} hours; {} days",
                      secondsToFull.longValue(),
                      TimeUnit.SECONDS.toHours( secondsToFull.longValue() ),
                      TimeUnit.SECONDS.toDays( secondsToFull.longValue() ) );

        return to;
    }

    /**
     * @return Returns {@link Integer} representing the number of firebreaks
     *         that have occurred in the last 24 hours.
     */
    protected Integer last24Hours( final List<Series> series ) {
        final AtomicInteger count = new AtomicInteger( 0 );
        final long twentyFourHoursAgo =
            DateTimeUtil.toUnixEpoch( LocalDateTime.now()
                                                   .minusHours( 24 ) );

        series.stream().forEach( ( s ) -> s.getDatapoints()
         .stream()
         .forEach( ( dp ) -> {
             if ( dp.getY() >= twentyFourHoursAgo ) {
                 count.getAndIncrement();
             }
         } ) );

        return count.intValue();
    }
}
