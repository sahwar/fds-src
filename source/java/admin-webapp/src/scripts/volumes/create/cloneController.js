angular.module( 'volumes' ).controller( 'cloneVolumeController', ['$scope', '$volume_api', function( $scope, $volume_api ){
    
    var init = function(){
        $scope.volumeVars.toClone = 'new';
        $scope.selectedItem = {name: 'None'};
        $scope.choosing = false;
    }
    
    init();
    
    $scope.cloneOptions = [];
    
    $scope.cloneColumns = [
        { name: 'Name', property: 'name' }
    ];
    
    $scope.getSnapshots = function( volume, callback ){
        $volume_api.getSnapshots( volume.id, callback );
    };
    
    $scope.cancel = function(){
        $scope.choosing = false;
    };
    
    $scope.choose = function(){
        $scope.choosing = false;
        $scope.volumeVars.clone = $scope.selectedItem;
    };
    
    $scope.$watch( 'selectedItem', function( newVal ){
    });
    
    var refresh = function( newValue ){
        
        if ( newValue === false ){
            return;
        }
        
        $scope.cloneOptions = $volume_api.volumes;
        init();
    };
    
    $scope.$watch( 'volumeVars.creating', refresh );
    $scope.$watch( 'volumeVars.editing', refresh );
    
}]);