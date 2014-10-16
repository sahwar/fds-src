angular.module( 'volumes' ).controller( 'volumeCreateController', ['$scope', '$volume_api', '$snapshot_service', '$modal_data_service', function( $scope, $volume_api, $snapshot_service, $modal_data_service ){

    var creationCallback = function( volume, newVolume ){

            // for each time deliniation used we need to create a policy and attach
            // it using the volume id in the name so we can identify it easily
            for ( var i = 0; angular.isDefined( volume.snapshotPolicies ) && i < volume.snapshotPolicies.length; i++ ){
                volume.snapshotPolicies[i].name =
                    newVolume.id + '_' + volume.snapshotPolicies[i].name;

                $snapshot_service.createSnapshotPolicy( volume.snapshotPolicies[i], function( policy, code ){
                    // attach the policy to the volume
                    $snapshot_service.attachPolicyToVolume( policy, newVolume.id, function(){} );
                });
            }
    };
    
    var createVolume = function( volume ){
        /**
        * Because this is a shim the API does not yet have business
        * logic to combine the attachments so we need to do this in many calls
        * TODO:  Replace with server side logic
        **/
        $volume_api.save( volume, function( newVolume ){ creationCallback( volume, newVolume ); }, genericFailure );

        $scope.cancel();
    };
    
    var cloneVolume = function( volume ){
        volume.id = $scope.volumeVars.clone.id;
        
        if ( !angular.isDefined( $scope.volumeVars.clone.sla ) ){
            $volume_api.cloneSnapshot( volume, function( newVolume ){ creationCallback( volume, newVolume ); }, genericFailure );
        }
        else {
            $volume_api.clone( volume, function( newVolume ){ creationCallback( volume, newVolume ); }, genericFailure );
        }
        
        $scope.cancel();
    };
    
    var editVolume = function( volume ){
        
        volume.id = $scope.volumeVars.selectedVolume.id;
        
          // helper to create a real policy from the UI parts
        var createPolicy = function( makePolicy ){
             var policy = {
                id: makePolicy.id,
                name: makePolicy.name.toUpperCase(),
                recurrenceRule: {
                    FREQ: makePolicy.displayName.toUpperCase()
                },
//                retention: $snapshot_service.convertTimePeriodToSeconds( makePolicy.value, makePolicy.units.name.toUpperCase() )
                retention: makePolicy.retention
            };
            
            return policy;
        };

        var newPolicies = $modal_data_service.getData().snapshotPolicies;
        var oldPolicies = $scope.volumeVars.selectedVolume.snapshotPolicies;
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
            
            $snapshot_service.detachPolicy( deleteList[d], $scope.volumeVars.selectedVolume.id, function( result ){
                $snapshot_service.deleteSnapshotPolicy( id, function(){} );
            });
        }
        
        // creating / editing the new selections
        for ( var i = 0; i < newPolicies.length; i++ ){

            var sPolicy = newPolicies[i];
            
            // if it has an ID then it's already exists
            if ( angular.isDefined( sPolicy.id ) ){
                
                $snapshot_service.editSnapshotPolicy( createPolicy( sPolicy ), function(){} );
            }
            else {
                
                // if it's in use, create it.
                if ( sPolicy.use === true ){
                    $snapshot_service.createSnapshotPolicy( createPolicy( sPolicy ), function( policy ){
                        $snapshot_service.attachPolicyToVolume( policy, $scope.volumeVars.selectedVolume.id, function(){} );
                    } );
                }
            }
        }
        
        $volume_api.save( volume, function( savedVolume ){
            },
            genericFailure );
        
        $scope.cancel();
    };
    
    $scope.save = function(){

        var volume = $modal_data_service.getData();
        
        if ( volume.sla === 'None' ){
            volume.sla = 0;
        }

        if ( volume.limit === 'None' ){
            volume.limit = 0;
        }
        
        if ( $scope.volumeVars.creating === true ){
            
            if ( $scope.volumeVars.toClone === 'clone' ){
                cloneVolume( volume );
            }
            else{
                createVolume( volume );
            }
        }
        else{
            editVolume( volume );
        }
    };

    $scope.cancel = function(){
        $scope.volumeVars.editing = false;
        $scope.volumeVars.creating = false;
        $scope.volumeVars.toClone = false;
        $scope.volumeVars.back();
    };

    $scope.$watch( 'volumeVars.creating', function( newVal ){
        if ( newVal === true ){
            $modal_data_service.start();
        }
    });
    
    $scope.$watch( 'volumeVars.editing', function( newVal ){
        if ( newVal === true ){
            $modal_data_service.start();
        }
    });

}]);
