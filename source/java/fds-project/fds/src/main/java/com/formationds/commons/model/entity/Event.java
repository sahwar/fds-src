/**
 * Copyright (c) 2015 Formation Data Systems. All rights reserved.
 */
package com.formationds.commons.model.entity;

import com.formationds.commons.events.EventCategory;
import com.formationds.commons.events.EventSeverity;
import com.formationds.commons.events.EventState;
import com.formationds.commons.events.EventType;
import com.formationds.commons.model.abs.ModelBase;
import com.google.gson.annotations.SerializedName;

import javax.xml.bind.annotation.XmlRootElement;
import java.text.MessageFormat;
import java.time.Instant;

/**
 * Represents a persisted event.  Events are stored with a generated id, a timestamp, type, category, severity and
 * a state.  The event details include a message key and arguments.  The key and arguments are intended to match
 * a resource bundle key to enable localization of messages either by the server before sending to clients, or at
 * the client side.  All fields except for the state and the modified timestamp are <i>effectively immutable</i>
 * via protected setters that allow JPA system access.
 * <p/>
 * The valid state transitions are SOFT -> HARD -> RECOVERED or SOFT -> RECOVERED;  Any modification in state updates
 * the modifiedTimestamp.
 * <p/>
 * The Event should be overridden to provide access to event type specific information.  For example, a user activity
 * or audit event will include a user id or name.
 */
@XmlRootElement
abstract public class Event extends ModelBase {

    private EventType type;
    @SerializedName("category")
    private EventCategory category;
    private EventSeverity severity;

    // TODO: do we need to represent Success/Failure events?  For example in configuration activity?
    // TODO: should a change in event state be treated as a new event?  If so, we will need a mechanism to
    // track the original event?
    private EventState state;

    private Long initialTimestamp;
    private Long modifiedTimestamp;

    /**
     * ResourceBundle message lookup key for the event.  Enables event message localization.
     */
    private String messageKey;

    /**
     * ResourceBundle message arguments for the event.
     */
    private Object[] messageArgs;

    /**
     * The default message format specification
     */
    private String messageFormat;

    /**
     * The default message formatted based on the default message format and message arguments.
     */
    private String defaultMessage;

    /**
     * Default constructor for JPA support
     */
    protected Event() { }

    /**
     * Create a new event.  The event state will be initialized to SOFT and the timestamp set to now.
     *
     * @param type the event type
     * @param category the event category
     * @param severity the event severity
     * @param defaultMessageFmt the default (english) message format.
     * @param messageKey resource bundle message lookup key
     * @param messageArgs resource bundle message arguments
     */
    public Event( EventType type, EventCategory category, EventSeverity severity, String defaultMessageFmt,
                  String messageKey, Object... messageArgs ) {
        this( Instant.now().toEpochMilli(), type, category, severity, defaultMessageFmt, messageKey, messageArgs );
    }

    /**
     * Create a new event.  The event state will be initialized to SOFT and the timestamp set to now.
     *
     * @param ts the event timestamp, overriding the default
     * @param type the event type
     * @param category the event category
     * @param severity the event severity
     * @param defaultMessageFmt the default (english) message format.
     * @param messageKey resource bundle message lookup key
     * @param messageArgs resource bundle message arguments
     */
    protected Event( Long ts, EventType type, EventCategory category, EventSeverity severity, String defaultMessageFmt,
                     String messageKey, Object... messageArgs ) {
        this.type = type;
        this.category = category;
        this.severity = severity;
        this.initialTimestamp = ts;
        this.modifiedTimestamp = this.initialTimestamp;
        this.messageFormat = defaultMessageFmt;
        this.messageKey = messageKey;
        this.messageArgs = (messageArgs != null ? messageArgs.clone() : new Object[0]);
        state = EventState.SOFT;

        this.defaultMessage = MessageFormat.format( messageFormat, messageArgs );
    }

    /**
     * @return the default (english) message to be used if localization fails.
     */
    public String getDefaultMessage() { return defaultMessage; }

    /**
     *
     * @param d
     */
    protected void setDefaultMessage( String d ) { defaultMessage = d; }

    /**
     * Update the event state to the specified state.
     * <p/>
     * If the new state is identical to the current state, it is ignored.  If the
     * current state is not SOFT and the new state is SOFT, an IllegalArgumentException is thrown.
     * Otherwise, the event state is updated
     *
     * @param newState
     *
     * @throws java.lang.IllegalArgumentException if the state is not a valid transition
     */
    public void updateEventState(EventState newState) {
        if (state.equals(newState))
            return;

        switch (state) {
            case SOFT:
                // any other state is valid
                break;

            case HARD:
                if (!EventState.RECOVERED.equals(newState))
                    throw new IllegalArgumentException("Invalid state transition: " + state + "->" + newState);
                break;

            case RECOVERED:
                throw new IllegalArgumentException("Invalid state transition: " + state + "->" + newState);
        }

        this.state = newState;
        this.modifiedTimestamp = Instant.now().toEpochMilli();
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (o == null || getClass() != o.getClass()) return false;

        Event event = (Event) o;

        if (category != event.category) return false;
        if (!initialTimestamp.equals(event.initialTimestamp)) return false;
        if (!messageKey.equals(event.messageKey)) return false;
        if (severity != event.severity) return false;
        if (type != event.type) return false;

        return true;
    }

    @Override
    public int hashCode() {
        int result = type.hashCode();
        result = 31 * result + category.hashCode();
        result = 31 * result + severity.hashCode();
        result = 31 * result + initialTimestamp.hashCode();
        result = 31 * result + messageKey.hashCode();
        return result;
    }

    /**
     *
     * @return the event type
     */
    public EventType getType() {
        return type;
    }

    /**
     *
     * Note that this is provided to satisfy JPA persistence requirements.
     * The event type should be treated as immutable
     *
     * @param type
     */
    protected void setType(EventType type) {
        this.type = type;
    }

    /**
     *
     * @return the event category
     */
    public EventCategory getCategory() {
        return category;
    }

    /**
     * Set the event category.  Note that this is provided to satisfy JPA persistence requirements.
     * The category should be treated as immutable
     *
     * @param category
     */
    protected void setCategory(EventCategory category) {
        this.category = category;
    }

    /**
     *
     * @return the event severity
     */
    public EventSeverity getSeverity() {
        return severity;
    }

    /**
     * Set the event severity.
     *
     * Note that this is provided to satisfy JPA persistence requirements.
     * The severity should be treated as immutable
     *
     * @param severity
     */
    protected void setSeverity(EventSeverity severity) {
        this.severity = severity;
    }

    /**
     *
     * @return the event state
     */
    public EventState getState() {
        return state;
    }

    /**
     * Set the event state.
     *
     * Note that this is provided to satisfy JPA persistence requirements.  The state should
     * only be set through the {@link #updateEventState(com.formationds.commons.events.EventState)}
     * API.
     *
     * @param state
     */
    protected void setState(EventState state) {
        this.state = state;
    }

    /**
     *
     * @return the initial timestamp for this event
     */
    public Long getInitialTimestamp() {
        return initialTimestamp;
    }

    /**
     * Set the initial timestamp
     *
     * Note that this is provided to satisfy JPA persistence requirements.
     * The initial timestamp should be treated as immutable
     *
     * @param initialTimestamp
     */
    protected void setInitialTimestamp(Long initialTimestamp) {
        this.initialTimestamp = initialTimestamp;
    }

    /**
     * @return the last modified timestamp
     */
    public Long getModifiedTimestamp() {
        return modifiedTimestamp;
    }

    /**
     * Set the last modified timestamp
     *
     * Note that this is provided to satisfy JPA persistence requirements.
     * The modified timestamp should only be modified internally through APIs that alter the event, such
     * as {@link #updateEventState(com.formationds.commons.events.EventState)}.
     *
     * @param modifiedTimestamp
     */
    protected void setModifiedTimestamp(Long modifiedTimestamp) {
        this.modifiedTimestamp = modifiedTimestamp;
    }

    /**
     *
     * @return the message localization key
     */
    public String getMessageKey() {
        return messageKey;
    }

    /**
     * Set the message localization key
     *
     * Note that this is provided to satisfy JPA persistence requirements.
     * The message key should be treated as immutable

     * @param key
     */
    protected void setMessageKey(String key) {
        this.messageKey = key;
    }

    /**
     *
     * @return the message localization format args
     */
    public Object[] getMessageArgs() {
        return messageArgs;
    }

    /**
     * Note that this is provided to satisfy JPA persistence requirements.
     * The message args should be treated as immutable.
     *
     * @param args
     */
    protected void setMessageArgs(Object... args) {
        this.messageArgs = (args != null ? args.clone() : new Object[0]);
    }
}
