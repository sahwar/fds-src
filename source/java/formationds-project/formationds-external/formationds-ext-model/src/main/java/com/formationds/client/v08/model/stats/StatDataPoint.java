package com.formationds.client.v08.model.stats;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

import org.json.JSONArray;
import org.json.JSONObject;

public class StatDataPoint {

	public static final String REPORT_TIME = "reportTime";
	public static final String METRIC_NAME = "metricName";
	public static final String METRIC_VALUE = "metricValue";
	public static final String COLLECTION_PERIOD = "collectionPeriod";
	public static final String COLLECTION_TIME_UNIT = "collectionTimeUnit";
	public static final String CONTEXT_ID = "contextId";
	public static final String CONTEXT_TYPE_STR = "contextType";
	public static final String NUMBER_OF_SAMPLES = "numberOfSamples";
	public static final String MAXIMUM_VALUE = "maximumValue";
	public static final String MINIMUM_VALUE = "minimumValue";
	public static final String RELATED_CONTEXTS = "relatedContexts";
	
	public enum TIME_UNITS{
		MILLISECONDS,
		SECONDS,
		MINUTES,
		HOURS,
		WEEKS,
		DAYS,
		YEARS
	};
	
	/**
	 * This is the time the statistic was reported
	 */
	private Long reportTime = 0L;
	
	/**
	 * This is the name of the metric.  This will act as a key for most searches and is
	 * based mostly upon the enumeration of stat types found in the definition:
	 * com.formationds.commons.model.type.Metrics
	 */
	private String metricName = "UNKNOWN";
	
	/**
	 * The actual metric value.  This should be a raw value that is not
	 * an average or other computation
	 */
	private Double metricValue = 0.0;
	
	/**
	 * In the case that the value is calculated, this allows for a min value not to be lost.
	 */
	private Double minimumValue = 0.0;
	
	/**
	 * In the case that the value is calculated, this allows for a max value not to be lost.
	 */
	private Double maximumValue = 0.0;
	
	/**
	 * The period of time it took to collect the value.  In other words,
	 * if the value is a cumulative value, this would be the amount of time
	 * over which the summation was done.
	 */
	private Long collectionPeriod = 0L;
	
	/**
	 * This is an integer instead of an enum to promote the cross language 
	 * concerns.  Descriptions of what each integer means in terms of time period can be found in
	 * this file as static final members.
	 */
	private TIME_UNITS collectionTimePeriod = TIME_UNITS.MILLISECONDS;
	
	/**
	 * This represents how many samples were included in the statistical
	 * period for which this is reporting.  In the case that the value
	 * is a sum, this number would give us a better idea of averages that
	 * are not by nature time based, but sample based.
	 */
	private Integer numberOfSamples = 1;
	
	/**
	 * The internal FDS ID for the item that the stat is about.  
	 */
	private Long contextId = -1L;
	
	/**
	 * A list of related entities - this is optional
	 */
	private List<ContextDef> relatedContexts;
	
	/**
	 * The type (or system noun) that describes what the stat is about
	 * 0 = Volume
	 * 1 = Node
	 * 2 = Service
	 */
	private ContextType contextType;
	
	public StatDataPoint(){}
	
	public StatDataPoint( Long reportTime, 
						  String metricName,
						  Double metricValue,
						  Long collectionPeriod,
						  TIME_UNITS collectionTimePeriod,
						  Integer numberOfSamples,
						  Long contextId,
						  ContextType contextType ){
		
		this.reportTime = reportTime;
		this.metricName = metricName;
		this.metricValue = metricValue;
		this.collectionPeriod = collectionPeriod;
		this.numberOfSamples = numberOfSamples;
		this.contextId = contextId;
		this.contextType = contextType;
		this.collectionTimePeriod = collectionTimePeriod;
	}

	public Long getReportTime() {
		return reportTime;
	}

	public void setReportTime( Long reportTime ) {
		this.reportTime = reportTime;
	}

	public String getMetricName() {
		return metricName;
	}

	public void setMetricName( String metricName ) {
		this.metricName = metricName;
	}

	public Double getMetricValue() {
		return metricValue;
	}

	public void setMetricValue( Double metricValue ) {
		this.metricValue = metricValue;
	}
	
	public Double getMaximumValue(){
		return maximumValue;
	}
	
	public void setMaximumValue( Double value ){
		maximumValue = value;
	}
	
	public Double getMinimumValue(){
		return minimumValue;
	}
	
	public void setMinimumValue( Double value ){
		minimumValue = value;
	}

	public Long getCollectionPeriod() {
		return collectionPeriod;
	}

	public void setCollectionPeriod( Long collectionPeriod ) {
		this.collectionPeriod = collectionPeriod;
	}
	
	public Integer getNumberOfSamples(){
		return numberOfSamples;
	}
	
	public void setNumberOfSamples( Integer numberOfSamples ){
		this.numberOfSamples = numberOfSamples;
	}
	
	public TIME_UNITS getCollectionTimeUnit(){
		return collectionTimePeriod;
	}
	
	public void setCollectionTimeUnit( TIME_UNITS timeUnit ){
		this.collectionTimePeriod = timeUnit;
	}

	public Long getContextId() {
		return contextId;
	}

	public void setContextId( Long contextId ) {
		this.contextId = contextId;
	}

	public ContextType getContextType() {
		return contextType;
	}

	public void setContextType( ContextType contextType ) {
		this.contextType = contextType;
	}
	
	public List<ContextDef> getRelatedContexts(){
		
		if ( this.relatedContexts == null ){
			this.relatedContexts = new ArrayList<ContextDef>();
		}
		
		return this.relatedContexts;
	}
	
	/**
	 * Built in marshaling to json
	 * @return
	 */
	public String toJson(){
		
		JSONObject json = new JSONObject();
		
		json.put( REPORT_TIME, getReportTime() );
		json.put( METRIC_NAME, getMetricName() );
		json.put( METRIC_VALUE, getMetricValue() );
		json.put( COLLECTION_PERIOD, getCollectionPeriod() );
		json.put( CONTEXT_ID, getContextId() );
		json.put( CONTEXT_TYPE_STR, getContextType().name() );
		json.put( COLLECTION_TIME_UNIT, getCollectionTimeUnit().name() );
		json.put( NUMBER_OF_SAMPLES, getNumberOfSamples() );
		json.put( MINIMUM_VALUE, getMinimumValue() );
		json.put( MAXIMUM_VALUE, getMaximumValue() );
		
		JSONArray contextArray = new JSONArray();
		
		getRelatedContexts().stream().forEach( (cp) -> {
			
			contextArray.put( cp.toJsonObject() );
		});
		
		json.put( RELATED_CONTEXTS, contextArray );
		
		String rtn = json.toString();
		return rtn;
	}
	
	/**
	 * built in marshaling from json.
	 * @param jsonStr
	 * @return
	 */
	public static StatDataPoint fromJson( String jsonStr ){
		
		JSONObject json = new JSONObject( jsonStr );
		
		StatDataPoint datapoint = new StatDataPoint();
		
		datapoint.setReportTime( json.getLong( REPORT_TIME ) );
		datapoint.setCollectionPeriod( json.getLong( COLLECTION_PERIOD ) );
		datapoint.setContextId( json.getLong( CONTEXT_ID ) );
		datapoint.setContextType( ContextType.valueOf( json.getString( CONTEXT_TYPE_STR ) ) );
		datapoint.setMetricName( json.getString( METRIC_NAME ) );
		datapoint.setMetricValue( json.getDouble( METRIC_VALUE ) );
		datapoint.setCollectionTimeUnit( TIME_UNITS.valueOf( json.getString( COLLECTION_TIME_UNIT ) ) );
		datapoint.setNumberOfSamples( json.getInt( NUMBER_OF_SAMPLES ) );
		datapoint.setMaximumValue( json.getDouble( MAXIMUM_VALUE ) );
		datapoint.setMinimumValue( json.getDouble( MINIMUM_VALUE ) );
		
		try {
			JSONArray array = json.getJSONArray( RELATED_CONTEXTS );
			
			for ( int i = 0; i < array.length(); i++ ){
				datapoint.getRelatedContexts().add( ContextDef.buildFromJson( array.getJSONObject( i ).toString() ) );
			}
		}
		catch( Exception e ){
			
		}
		
		return datapoint;
	}
}