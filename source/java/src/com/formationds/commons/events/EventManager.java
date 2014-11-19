/*
 * Copyright (c) 2014, Formation Data Systems, Inc. All Rights Reserved.
 */

package com.formationds.commons.events;

import com.formationds.commons.crud.JDORepository;
import com.formationds.commons.model.Series;
import com.formationds.commons.model.entity.Event;
import com.formationds.commons.model.entity.SystemActivityEvent;
import com.formationds.commons.model.entity.UserActivityEvent;
import com.formationds.commons.model.entity.VolumeDatapoint;
import com.formationds.om.repository.SingletonRepositoryManager;
import com.formationds.om.repository.helper.FirebreakHelper;
import com.formationds.security.AuthenticatedRequestContext;
import com.formationds.security.AuthenticationToken;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.util.Arrays;
import java.util.List;
import java.util.Objects;

/**
 * The event manager is responsible for receiving events and storing them in the event repository.
 * <p/>
 * Initialization of the event manager EventNotificationHandler implementation is required if
 * persistence to anything other than a common log file is desired.  Given the singleton implementation,
 * an Object key is used to ensure that the event manager can only be modified by its owner.
 */
// TODO: prototype to get us to beta.  not sure if singleton makes sense here in the long run
public enum EventManager {

    /*
     * The singleton instance
     */
    INSTANCE;

    static final Logger logger = LoggerFactory.getLogger(EventManager.class);

    /**
     * Event notification handler interface.  Default implementation in EventManager simply
     * persists the event to a log file.  More sophisticated implementations may
     * queue events and persist them asynchronously.
     */
    public interface EventNotificationHandler {

        /**
         * Handle the event notification and persist it.
         * <p/>
         * Implementations may throw RuntimeExceptions on error.
         * @param e
         * @return true if successfully handled.  False otherwise.
         */
        public boolean handleEventNotification(Event e);
    }

    /**
     * Notify the EventManager of a new event with the specified descriptor and event arguments.
     *
     * @param d
     * @param eventArgs
     * @return true if successfully notifying of the event.
     */
    public static boolean notifyEvent(EventDescriptor d, Object... eventArgs) {
        return notifyEvent(d.type(), d.category(), d.severity(), d.defaultMessage(), d.key(), eventArgs);
    }

    /**
     *
     * @param t
     * @param c
     * @param s
     * @param key
     * @param eventArgs
     * @return
     */
    public static boolean notifyEvent(EventType t, EventCategory c, EventSeverity s, String dflt, String key, Object[] eventArgs) {
        Event e = newEvent(t, c, s, dflt, key, eventArgs);
        return (e != null ? INSTANCE.notifyEvent(e) : false);
    }

    /**
     * Create a new event of the specified type.
     *
     * @param t
     * @param c
     * @param s
     * @param key
     * @param eventArgs
     *
     * @return a new event
     */
    public static Event newEvent(EventType t, EventCategory c, EventSeverity s, String dflt, String key, Object[] eventArgs) {
        String[] eventArgStr = new String[eventArgs.length];
        int i =0;
        for (Object arg : eventArgs)
        {
            // TODO: convert to JSON or some other format?
            eventArgStr[i++] = arg.toString();
        }
        switch (t) {
            case SYSTEM_EVENT:
                return new SystemActivityEvent(c, s, dflt, key, eventArgs);
            case USER_ACTIVITY:
                AuthenticationToken token = AuthenticatedRequestContext.getToken();
                long userId = (token != null ? token.getUserId() : -1);
                return new UserActivityEvent(userId, c, s, dflt, key, eventArgs);
            default:
                // TODO: log warning or throw exception on unknown/unsupported type?  Fow now logging.
                // throw new IllegalArgumentException("Unsupported event type (" + t + ")");
                logger.warn("Unsupported event type %s", t);
                return null;
        }
    }

    // default logger notifier
    private EventNotificationHandler notifier = (e) -> {
        getLog().info(e.getMessageKey(), e.getMessageArgs());
        return true;
    };

    // This is necessary to workaround a limitation of making a static
    // reference to the LOG in the lambda expression initializing the default notifier above.
    private static Logger getLog() { return EventManager.logger; }

    // key used to guard against someone other than the owner changing the notifier.
    private Object key = null;

    /**
     * Initialize the event manager with the specified notification handler implementation.  The caller must
     * have a valid key in order to change the notification handler.  Assuming the key is private to the
     * object that owns it, this ensures that the notification handler configuration is managed from a
     * single location and prevents unintended modifications.
     *
     * @param key
     * @param n
     */
    synchronized public void initEventNotifier(Object key, EventNotificationHandler n) {
        if (this.key != null && !Objects.equals(this.key, key))
            throw new IllegalStateException("Key must match the existing key to change the notifier implementation.");

        notifier = n;
        this.key = key;
    }

    // TODO: this is probably not how we want to do this in the long run... trying to get something working for beta1
    public void initEventListeners() {
        SingletonRepositoryManager.instance()
                                  .getMetricsRepository()
                                  .addEntityPersistListener(new JDORepository.EntityPersistListener<VolumeDatapoint>() {
                                      EventDescriptor fbEvent = new EventDescriptor() {
                                          public EventType type() { return EventType.SYSTEM_EVENT; }
                                          public EventCategory category() { return EventCategory.FIREBREAK; }
                                          public EventSeverity severity() { return EventSeverity.WARNING; }
                                          public String defaultMessage() { return "Volume {0}; Series data {1}"; }
                                          public List<String> argNames() { return Arrays.asList("volumeId", "series"); }
                                          public String key() { return "VOLUME_FIREBREAK_EVENT"; }
                                      };
                                      @Override
                                      public void postPersist(VolumeDatapoint entity) {
                                          logger.trace( "postPersist handling of VolumeDatapoint {}", entity);
                                          try {
                                              List<VolumeDatapoint> vdp = Arrays.asList( new VolumeDatapoint[]{ entity } );
                                              List<Series> fb =
                                                      new FirebreakHelper().processFirebreak(vdp);

                                              if( !fb.isEmpty() ) {
                                                  logger.trace( "Gathering firebreak details" );

                                                  // only expect one here (the way this is currently designed...
                                                  for( final Series s : fb ) {
                                                      logger.trace( "Firebreak event for '{}' series '{}'", entity.getVolumeName(), s );

                                                      // TODO: add series data?
                                                      EventManager.notifyEvent(fbEvent, entity.getVolumeId(), s);
                                                  }
                                              } else {
                                                  logger.trace( "no firebreak data available" );
                                              }
                                          }
                                          catch (Throwable t) {
                                              logger.error("Failed to process datapoint postPersist event detection", t);
                                              // TODO: do we want to rethrow and cause the VolumeDatapoint commit to fail?
                                          }
                                      }
                                  });
    }

    /**
     *
     * @param e
     * @return true if the event is successfully handled.
     */
    public boolean notifyEvent(Event e) {
        try { return notifier.handleEventNotification(e); }
        catch (RuntimeException re) {
            logger.error(String.format( "Failed to persist event key=%s", key ), re);
            return false;
        }
    }

    /**
     * @return the list of events.
     */
    public List<Event> getEvents() {
        return SingletonRepositoryManager.instance().getEventRepository().findAll();
    }

}