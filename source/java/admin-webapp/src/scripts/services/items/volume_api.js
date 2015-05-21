angular.module( 'volume-management' ).factory( '$volume_api', [ '$http_fds', '$rootScope', '$interval', function( $http_fds, $rootScope, $interval ){

    var api = {};

    var pollerId = -1;
    var errCount = 0;

    var startPoller = function(){
        pollerId = $interval( getVolumes, 30000 );
    };
    
    var getVolumes = function( callback ){

        if ( pollerId === -1 ){
            startPoller();
        }
        
        return $http_fds.get( '/api/config/volumes',
            function( data ){
                errCount = 0;
                api.volumes = data;

                if ( angular.isDefined( callback ) && angular.isFunction( callback ) ){
                    callback( api.volumes );
                }
            },
            function( error ){
                errCount++;
            
                if ( errCount >= 3 ){
                    $interval.cancel( pollerId );
                }
            });
    };

    $rootScope.$on( 'fds::authentication_logout', function(){
        
        $interval.cancel( pollerId );
        api.volume = [];
    });

    $rootScope.$on( 'fds::authentication_success', function(){
        
        getVolumes().then( startPoller );
    });

    api.save = function( volume, callback, failure ){

        // save a new one
        if ( !angular.isDefined( volume.id ) ){
            return $http_fds.post( '/api/config/volumes', volume,
                function( response ){

                    getVolumes();

                    if ( angular.isDefined( callback ) ){
                        callback( response );
                    }
                },
                failure );
        }
        // update an existing one
        else {
            return $http_fds.put( '/api/config/volumes/' + volume.id, volume, function( volume ){
                
                if ( angular.isFunction( callback ) ){
                    callback( volume );
                }
                
                getVolumes();
            });
        }
    };
    
    api.clone = function( volume, callback, failure ){
        
        // the actual volume we try to create should now have an ID yet - but the ID here will be 
        // the original one
        var id = volume.id;
        volume.id = '';
        
        // convert timelineTime from MS to seconds and adding 1 so that it makes sure to get the right snapshot
        var timelineTime = Math.round( volume.timelineTime / 1000 ) + 1;
        volume.timelineTime = timelineTime;
        
        return $http_fds.post( '/api/config/volumes/clone/' + id + '/' + volume.name + '/' + volume.timelineTime, volume,
            function( response ){

                getVolumes();

                if ( angular.isDefined( callback ) ){
                    callback( response );
                }
            },
            failure );
    };
    
    api.cloneSnapshot = function( volume, callback, failure ){
        return $http_fds.post( '/api/config/snapshot/clone/' + volume.id + '/' + volume.name, volume,
            function( response ){

                getVolumes();

                if ( angular.isDefined( callback ) ){
                    callback( response );
                }
            },
            failure );
    };

    api.delete = function( volume, callback, failure ){
        return $http_fds.delete( '/api/config/volumes/' + volume.name, 
            function( result ){
                getVolumes();
            
                if ( angular.isFunction( callback ) ){
                    callback();
                }
            },
            failure );
    };
    
    api.createSnapshot = function( volumeId, newName, callback, failure ){
        
        return $http_fds.post( '/api/config/volumes/' + volumeId + '/snapshot', {volumeId: volumeId, name: newName, retention: 0}, callback, failure );
    };
    
    api.deleteSnapshot = function( volumeId, snapshotId, callback, failure ){
        $http_fds.delete( '/api/config/snapshot/' + volumeId + '/' + snapshotId, callback, failure );
    };

    api.getSnapshots = function( volumeId, callback, failure ){

        return $http_fds.get( '/api/config/volumes/' + volumeId + '/snapshots', callback, failure );
    };

    api.getSnapshotPoliciesForVolume = function( volumeId, callback, failure ){

//        return [{
//            policyName: 'Fake_name',
//            recurrenceRule: {
//                freq: 'YEARLY',
//                count: 5
//            },
//            retentionTime: 3885906383
//        }];

        return $http_fds.get( '/api/config/volumes/' + volumeId + '/snapshot/policies', callback, failure );
    };

    api.refresh = function( callback ){
        getVolumes( callback );
    };
    
    api.getQosPolicyPresets = function( callback, failure ){
        return $http_fds.get( '/api/config/volumes/presets/qos', callback, failure );
    };
    
    api.getSnapshotPolicyPresets = function( callback, failure ) {
        
        return $http_fds.get( '/api/config/volumes/presets/timeline', callback, failure );
    };

    api.volumes = [];

    return api;

}]);
