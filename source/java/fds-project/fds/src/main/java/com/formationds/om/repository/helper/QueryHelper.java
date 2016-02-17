/*
 * Copyright (c) 2014, Formation Data Systems, Inc. All Rights Reserved.
 */

package com.formationds.om.repository.helper;

import com.formationds.client.v08.model.Size;
import com.formationds.client.v08.model.SizeUnit;
import com.formationds.client.v08.model.Volume;
import com.formationds.client.v08.model.stats.Calculated;
import com.formationds.client.v08.model.stats.Series;
import com.formationds.client.v08.model.stats.Statistics;
import com.formationds.client.v08.model.stats.Datapoint;
import com.formationds.commons.calculation.Calculation;
import com.formationds.commons.model.DateRange;
import com.formationds.commons.model.abs.Context;
import com.formationds.commons.model.abs.Metadata;
import com.formationds.commons.model.calculated.capacity.CapacityConsumed;
import com.formationds.commons.model.calculated.capacity.CapacityDeDupRatio;
import com.formationds.commons.model.calculated.capacity.CapacityFull;
import com.formationds.commons.model.entity.Event;
import com.formationds.commons.model.entity.IVolumeDatapoint;
import com.formationds.commons.model.entity.VolumeDatapoint;
import com.formationds.commons.model.type.Metrics;
import com.formationds.commons.model.type.StatOperation;
import com.formationds.commons.togglz.feature.flag.FdsFeatureToggles;
import com.formationds.commons.util.DateTimeUtil;
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

import java.time.Instant;
import java.time.LocalDateTime;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.OptionalLong;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.stream.Collectors;
import java.util.stream.DoubleStream;

/**
 * @author ptinius
 */
public class QueryHelper {
    private static final Logger logger =
        LoggerFactory.getLogger( QueryHelper.class );

    private final MetricRepository repo;

    /**
     * default constructor
     */
    public QueryHelper() {
        this.repo =
            SingletonRepositoryManager.instance()
                                      .getMetricsRepository();
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
                
                final Calculated iopsConsumed = new Calculated( Calculated.IOPS_CONSUMED, 0.0 );
                calculatedList.add( iopsConsumed );

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
	                
	                Calculated totalCapacity = new Calculated( Calculated.TOTAL_CAPACITY, systemCapacityInBytes );
	                calculatedList.add( totalCapacity );

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
                
                Calculated consumedBytes = new Calculated( Calculated.CONSUMED_BYTES, bytesConsumed );
                calculatedList.add( consumedBytes );

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
                                final Datapoint dp = new Datapoint();
                                dp.setX( (double )p.getTimestamp() );
                                dp.setY( p.getValue() );
                                
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

            	Datapoint earlyDatapoint = new Datapoint();
            	earlyDatapoint.setX( (double )justBefore.getTimestamp() );
            	earlyDatapoint.setY( justBefore.getValue() );
            	
            	s.setDatapoint( earlyDatapoint );

	        	// at start time
            	Datapoint startTime = new Datapoint();
            	startTime.setX( (double)theStart.getTimestamp() );
            	startTime.setY( theStart.getValue() );
	        	s.setDatapoint( startTime );
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

                            }

                            Volume volume = new Volume( Long.parseLong(volumeId), vd.getName() );

                            return volume;
                        })
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

    protected MetricRepository getRepo(){
    	return this.repo;
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
    protected Calculated getAverageIOPs( List<Series> series ){

    	// sum each series (which is already a series of averages)
    	// divide by input # to get the average of averages
    	// now add the averages together for the total average
    	Double rawAvg = series.stream().flatMapToDouble( s -> {
    		return DoubleStream.of( s.getDatapoints().stream()
    				.flatMapToDouble( dp -> DoubleStream.of( dp.getY() ) ).sum() / s.getDatapoints().size() );
    	}).sum();
    	
    	Calculated averageIops = new Calculated( Calculated.AVERAGE_IOPS, rawAvg );
    	return averageIops;
    }

    /**
     * This will look at all the gets and calculate the percentage of each
     *
     * @param series
     * @return
     */
    protected List<Calculated> getTieringPercentage( List<Series> series ){

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

    	Calculated ssd = new Calculated( Calculated.PERCENTAGE, (double)ssdPerc );
    	Calculated hdd = new Calculated( Calculated.PERCENTAGE, (double)hddPerc );
    	
    	List<Calculated> percentages = new ArrayList<>( );
    	percentages.add( ssd );
    	percentages.add( hdd );

    	return percentages;
    }

    /**
     * @return Returns {@link CapacityDeDupRatio}
     */
    protected Calculated deDupRatio() {
        final Double lbytes =
            SingletonRepositoryManager.instance()
                                      .getMetricsRepository()
                                      .sumLogicalBytes();

        final Double pbytes =
            SingletonRepositoryManager.instance()
                                      .getMetricsRepository()
                                      .sumPhysicalBytes();
        
        Double d = Calculation.ratio( lbytes, pbytes );
        d = (d < 1.0 ? 1.0 : d );
        
        Calculated dedupRatio = new Calculated( Calculated.RATIO, d );
        
        return dedupRatio;
    }


    /**
     * @param consumed       the {@link CapacityConsumed} representing bytes
     *                       used
     * @param systemCapacity the {@link Double} representing system capacity in
     *                       bytes
     *
     * @return Returns {@link CapacityFull}
     */
    public Calculated percentageFull( final CapacityConsumed consumed,
                                        final Double systemCapacity ) {
    	
    	final int percentageFull = (int) Calculation.percentage( consumed.getTotal(), systemCapacity );
    	
        Calculated full = new Calculated( Calculated.PERCENTAGE, (double)percentageFull );
        return full;
    }

    private Calculated capacityToFull = null;
    private Instant lastUpdated;
    private static final long SecondsIn24Hours = TimeUnit.HOURS.toSeconds( 24 );
    
    public Calculated secondsToFullThirtyDays( final List<Volume> volumes,
                                                   final Size systemCapacity )
    {
        if( ( capacityToFull == null ) || lastUpdated.isAfter( Instant.now( )
                                                                      .plusSeconds(
                                                                          SecondsIn24Hours ) ) )
        {
            MetricQueryCriteriaBuilder queryBuilder =
                new MetricQueryCriteriaBuilder( QueryCriteria.QueryType.SYSHEALTH_CAPACITY );

            // TODO: for capacity time-to-full we need enough history to calculate the regression
            // This was previously querying from 0 for all possible datapoints.  I think reducing to
            // the last 30 days is sufficient, but will need to validate that.
            DateRange range = DateRange.last( 30L, com.formationds.client.v08.model.TimeUnit.DAYS );
            MetricQueryCriteria query = queryBuilder.withContexts( volumes )
                                                    .withSeriesType( Metrics.UBYTES )
                                                    .withRange( range )
                                                    .build( );
            query.setColumns( new ArrayList<>( ) );

            final MetricRepository metricsRepository = SingletonRepositoryManager.instance( )
                                                                                 .getMetricsRepository( );

            @SuppressWarnings( "unchecked" ) final List<IVolumeDatapoint> queryResults =
                ( List<IVolumeDatapoint> ) metricsRepository.query( query );

            List<Series> series = SeriesHelper.getRollupSeries( queryResults, query.getRange( ),
                                                                query.getSeriesType( ),
                                                                StatOperation.SUM );

            capacityToFull = toFull( series.get( 0 ), systemCapacity.getValue( SizeUnit.B ).doubleValue() );
            lastUpdated = Instant.now();
        }

        return capacityToFull;
    }

    /**
     * @return Returns {@link CapacityFull}
     */
    public Calculated toFull( final Series pSeries,  final Double systemCapacity ) {
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

        final Calculated to = new Calculated( Calculated.TO_FULL, secondsToFull );

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
