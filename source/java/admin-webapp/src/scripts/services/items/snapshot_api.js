angular.module( 'qos' ).factory( '$snapshot_service', ['$http_fds', function( $http_fds ){

    var service = {};
    
    service.convertTimePeriodToSeconds = function( value, timeUnit ){

        var mult;

        switch( timeUnit ){
    //        case 'HOURLY':
    //            mult = 60*60;
    //            break;
            case 'DAYS':
                mult = 24*60*60;
                break;
            case 'WEEKS':
                mult = 7*24*60*60;
                break;
            case 'MONTHS':
                mult = 31*24*60*60;
                break;
            case 'YEARS':
                mult = 366*24*60*60;
                break;
            default:
                mult = 60*60;
        }

        return (value*mult);
    };

    service.convertSecondsToTimePeriod = function( seconds ){

        var rtn = { value: 0, units: 0 };

        if ( seconds % 366*24*60*60 === 0 ){
            rtn.value = seconds / (366*24*60*60);
            rtn.units = 4;
        }
        else if ( seconds % 31*24*60*60 === 0 ){
            rtn.value = seconds / (31*24*60*60);
            rtn.units = 3;
        }
        else if ( seconds % 7*24*60*60 === 0 ){
            rtn.value = seconds / (7*24*60*60);
            rtn.units = 2;
        }
        else if ( seconds % (24*60*60) === 0 ){
            rtn.value = seconds / (24*60*60);
            rtn.units = 1;
        }
        else {
            rtn.value = seconds / 3600;
            rtn.units = 0;
        }

        return rtn;
    };

    service.createSnapshotPolicy = function( policy, callback, failure ){

        policy.timelineTime = 0;
        
        return $http_fds.post( '/api/config/snapshot/policies', policy, callback, failure );
    };

    service.deleteSnapshotPolicy = function( policyId, callback, failure ){

        return $http_fds.delete( '/api/config/snapshot/policies/' + policyId, callback, failure );
    };
    
    service.editSnapshotPolicy = function( policy, callback, failure ){
        
        return $http_fds.put( '/api/config/snapshot/policies', policy, callback, failure );
    };

    service.attachPolicyToVolume = function( policy, volumeId, callback, failure ){

        return $http_fds.put( '/api/config/snapshot/policies/' + policy.id + '/attach/' + volumeId, {}, callback, failure );
    };

    service.detachPolicy = function( policy, volumeId, callback, failure ){

        return $http_fds.put( '/api/config/snapshot/policies/' + policy.id + '/detach/' + volumeId, {}, callback, failure );
    };

    service.cloneSnapshotToNewVolume = function( snapshot, volumeName, callback, failure ){

        return $http_fds.post( '/api/snapshot/clone/' + snapshot.id + '/' + escape( volumeName ), snapshot, callback, failure );
    };
    
    service.saveSnapshotPolicies = function( volumeId, oldPolicies, newPolicies, callback, failure ){
        
        var deleteList = [];
        
        // figuring out what to delete, then deleting them
        
        for ( var o = 0; o < oldPolicies.length; o++ ){
            
            var found = false;
            
            for ( var n = 0; n < newPolicies.length; n++ ){
                
                if ( oldPolicies[o].id === newPolicies[n].id ){
                    found = true;
                    break;
                }
            }// new policies
            
            // not in the new list... delete it
            if ( found === false ){
                deleteList.push( oldPolicies[o] );
            }
            
        }// old policies
                                
        for( var d = 0; d < deleteList.length; d++ ){
            
            var id = deleteList[d].id;
            
            service.detachPolicy( deleteList[d], volumeId, function( result ){
                service.deleteSnapshotPolicy( id, function(){} );
            });
        }
        
        // creating / editing the new selections
        for ( var i = 0; i < newPolicies.length; i++ ){

            var sPolicy = newPolicies[i];
            
            // if it has an ID then it's already exists
            if ( angular.isDefined( sPolicy.id ) ){
                
                // earlier the frequency was set to the policy name,
                // but for edit that means it has a long too... get rid of it
                // and reset the frequency to the displayname property - which matches the enum.
                sPolicy.recurrenceRule.FREQ = sPolicy.displayName.toUpperCase();
                service.editSnapshotPolicy( sPolicy, function(){} );
            }
            else {
                
                // if it's in use, create it.
                if ( sPolicy.use === true ){
                    
                    sPolicy.name = sPolicy.name + '_' + volumeId;
                    
                    service.createSnapshotPolicy( sPolicy, function( policy ){
                        service.attachPolicyToVolume( policy, volumeId, function(){} );
                    } );
                }
            }
        }
        
    };

    return service;
}]);
