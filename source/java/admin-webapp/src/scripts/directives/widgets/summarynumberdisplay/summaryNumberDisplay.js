angular.module( 'display-widgets' ).directive( 'summaryNumberDisplay', function(){
    
    return {
        restrict: 'E',
        replace: true,
        transclude: false,
        templateUrl: 'scripts/directives/widgets/summarynumberdisplay/summaryNumberDisplay.html',
        // data is an array of numbers and can hold up to 3
        // [{number: number, description: desc, suffix: suffix, iconClass: <optional>, iconColor: <optional>}, ... ]
        scope: { data: '=ngModel', autoChange: '@' }, 
        controller: function( $scope, $interval ){
            
            $scope.visibleIndex = -1;
            $scope.numberFontSize = 34;
            $scope.numberMinWidth = 0;
            $scope.decimalFontSize = 18;
            $scope.decimalMinWidth = 0;
            $scope.intervalId = -1;
            $scope.separator = '.';
            
            $scope.dotClicked = function( $index ){
                
                var numSize = measureText( $scope.data[$index].wholeNumber, $scope.numberFontSize );
                $scope.numberMinWidth = numSize.width + 'px';
                
                var dStr = '';
                
                if ( $scope.data[$index].decimals > 0 ){
                    dStr = $scope.data[$index].decimals + '.';
                }
                
                if ( angular.isDefined( $scope.data[ $index ].suffix ) ){
                    dStr += $scope.data[ $index].suffix;
                }
                
                var decSize = measureText( dStr, $scope.decimalFontSize );
                $scope.decimalMinWidth = decSize.width + 'px';
                
                $scope.visibleIndex = $index;
            };
            
            var calculateNumbers = function( item ){
                
                var pInt = parseFloat( item.number );
                
                if ( !isNaN( pInt ) && angular.isNumber( pInt ) ){
                
                    item.wholeNumber = Math.floor( pInt );
                    item.decimals = Math.round( (pInt - item.wholeNumber)*100 );
                }
                else {
                    item.wholeNumber = item.number;
                }
            };
            
            $scope.getIconColor = function(){
                
                if ( angular.isDefined( $scope.data[ $scope.visibleIndex ] ) && 
                    angular.isDefined( $scope.data[ $scope.visibleIndex ].iconColor ) ){
                    return $scope.data[ $scope.visibleIndex ].iconColor;
                }
                
                return 'black';
            };
            
            $scope.$watch( 'data', function( newVal ){
                
                if ( !angular.isDefined( $scope.data )){
                    return;
                }
                
                if ( !angular.isArray( $scope.data ) || $scope.data.length === 0 ){
                    
                    if ( angular.isDefined( $scope.data.number ) ){
                        var d = [];
                        d.push( $scope.data );
                        $scope.data = d;
                    }
                    
                    return;
                }
                
                for ( var i = 0; i < $scope.data.length; i++ ){
                    calculateNumbers( $scope.data[i] );
                }
                
                if ( $scope.visibleIndex === -1 ){
                    $scope.dotClicked( 0 );
                }
                
                if ( angular.isDefined( newVal.separator ) ){
                    $scope.separator = newVal.separator;
                }
            });
            
            if ( $scope.autoChange === true || $scope.autoChange === 'true' ){
                intervalId = $interval( function(){
                    
                    if ( !angular.isDefined( $scope.data ) ){
                        $interval.cancel( intervalId );
                        return;
                    }
                    
                    var i = $scope.visibleIndex;
                    i = (i+1) % $scope.data.length;
                    $scope.visibleIndex = i;
                },
                10000 );
            }
            
            $scope.$on( '$destroy', function(){
                if ( intervalId !== -1 ){
                    $interval.cancel( intervalId );
                }
            });
        }
    };
    
});