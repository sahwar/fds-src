require( './mockAuth' );
require( './mockSnapshot' );
require( './mockVolume' );
require( './mockTenant' );
require( './mockNode' );
require( './mockStats' );
require( './mockActivities' );

addModule = function( moduleName, module ){

    if ( browser.params.mockServer === true ){
        browser.addMockModule( moduleName, module );
    }
};

addModule( 'user-management', mockAuth );
addModule( 'volume-management', mockVolume );
addModule( 'qos', mockSnapshot );
addModule( 'tenant-management', mockTenant );
addModule( 'node-management', mockNode );
addModule( 'statistics', mockStats );

clean = function(){
    if ( browser.localStorage ){
        browser.localStorage.clear();
    }
};

login = function(){
    browser.get( '#/' );

    var uEl = element( by.model( 'username' ) );
    uEl = uEl.element( by.tagName( 'input' ) );
    var pEl = element( by.model( 'password' ) );
    pEl = pEl.element( by.tagName( 'input' ) );
    var button = element( by.id( 'login.submit' ) );

    uEl.clear();
    pEl.clear();
    uEl.sendKeys( 'admin' );
    pEl.sendKeys( 'admin' );

    button.click();
    browser.sleep( 200 );
};

logout = function(){

    var logoutMenu = element( by.id( 'main.usermenu' ) );
    var logoutEl = element( by.id( 'main.usermenu' ) ).all( by.tagName( 'li' ) ).last();

    logoutMenu.click();
    logoutEl.click();
    browser.sleep( 200 );
};

goto = function( tab ){

    var tab = element( by.id( 'main.' + tab ) );
    tab.click();
    browser.sleep( 200 );
};

clickOk = function(){
    element( by.css( '.fds-modal-ok' ) ).click();
};

clickCancel = function(){
    element( by.css( '.fds-modal-cancel' ) ).click();
};

createTenant = function( name ){
    
    goto( 'tenants' );
    var createLink = element( by.css( '.create-tenant-link' ) );
    createLink.click();

    var nameEl = element( by.css( '.tenant-name-input' )).element( by.tagName( 'input' ));
    nameEl.sendKeys( name );

    element( by.css( '.create-tenant-button' ) ).click();
    browser.sleep( 200 );
};

setTimelineTimes = function( pageEl, timeline ){
    
    var timelinePanel = pageEl.element( by.css( '.protection-policy' ));
    timelinePanel.all( by.css( '.button-bar-button' )).get( 3 ).click();
    
    var sliderWidget = pageEl.element( by.css( '.waterfall-slider' ));

    for ( var i = 0; i < timeline.length; i++ ){

        var settings = timeline[i];

        // need to hover to show the edit button
        browser.actions().mouseMove( sliderWidget.all( by.css( '.display-only' )).get( settings.slider )).perform();

        sliderWidget.all( by.css( '.icon-admin' )).get( settings.slider ).click();

        var dropdown = sliderWidget.all( by.css( '.dropdown' )).get( settings.slider );
        dropdown.element( by.tagName( 'button' )).click();
        dropdown.all( by.tagName( 'li' )).get( settings.unit ).click();

        // 4 = Forever and no number is allowed for forever
        if ( settings.unit !== 4 ){
            var spinner = sliderWidget.all( by.css( '.spinner-parent' )).get( settings.slider );
            var spinnerInput = spinner.element( by.tagName( 'input' ));
            spinnerInput.clear();
            spinnerInput.sendKeys( '\u0008' + '\u0008' + '\u0008' + settings.value );
        }

        sliderWidget.all( by.css( '.set-value-button' )).get( settings.slider ).click();

        browser.sleep( 500 );
    }
};

setTimelineStartTimes = function( pageEl, timelineStartTimes ){
    var timelinePanel = pageEl.element( by.css( '.protection-policy' ));
    timelinePanel.all( by.css( '.button-bar-button' )).get( 3 ).click();
    
    pageEl.element( by.css( '.create-volume-button-panel' )).click();
        
    var hourChoice = timelinePanel.element( by.css( '.hour-choice' ));
    hourChoice.click();
    hourChoice.all( by.tagName( 'li' )).get( timelineStartTimes.hours ).click();
    
    var dayChoice = timelinePanel.element( by.css( '.day-choice' ));
    dayChoice.click();
    dayChoice.all( by.tagName( 'li' )).get( timelineStartTimes.days ).click();
    
    var monthChoice = timelinePanel.element( by.css( '.month-choice' ));
    monthChoice.click();
    monthChoice.all( by.tagName( 'li' )).get( timelineStartTimes.months ).click();
    
    var yearChoice = timelinePanel.element( by.css( '.year-choice' ));
    yearChoice.click();
    yearChoice.all( by.tagName( 'li' )).get( timelineStartTimes.years ).click();
};

// set the custom portion of qos
var setCustomQos = function( qos ){
    
    var createEl = $('.create-panel.volumes');
    
    // changing the QoS
    var editQosButton = createEl.all( by.css( '.qos-panel .button-bar-button' )).get( 3 );
    editQosButton.click();

    browser.sleep( 200 );

    browser.actions().mouseMove( createEl.element( by.css( '.volume-priority-slider' ) ).all( by.css( '.segment' )).get( qos.priority - 1 ) ).click().perform();

    browser.actions().mouseMove( createEl.element( by.css( '.volume-iops-slider' ) ).all( by.css( '.segment' )).get( qos.capacity/10 ) ).click().perform();

    var limitSegment = 0;

    switch( qos.limit ){
        case 100:
            limitSegment = 0;
            break;
        case 200:
            limitSegment = 1;
            break;
        case 300:
            limitSegment = 2;
            break;
        case 400:
            limitSegment = 3;
            break;
        case 500:
            limitSegment = 4;
            break;
        case 750:
            limitSegment = 5;
            break;
        case 1000:
            limitSegment = 6;
            break;
        case 2000:
            limitSegment = 7;
            break;
        case 3000: 
            limitSegment = 8;
            break;
        default:
            limitSegment = 9;
            break;
    }

    browser.actions().mouseMove( createEl.element( by.css( '.volume-limit-slider' ) ).all( by.css( '.segment' )).get( limitSegment ) ).click().perform();  
};

// timeline: [{ slider: #, value: #, unit: #index }... ] || preset: <index>
// qos: { preset: <index>, capacity:, limt:, priority: }
// media policy: SSD_ONLY, HYBRID_ONLY, HDD_ONLY
createVolume = function( name, data_type, qos, mediaPolicy, timeline, timelineStartTimes ){
    
    goto( 'volumes' );
    browser.sleep( 200 );
    
    var vContainer = element( by.css( '.volumecontainer' ));
    var createLink = vContainer.element( by.css( 'a.new_volume' ) );
    createLink.click();
    browser.sleep( 200 );
    
    var createEl = $('.create-panel.volumes');
    
    var nameText = createEl.element( by.css( '.volume-name-input') );
    nameText = nameText.element( by.tagName( 'input' ) );
    nameText.sendKeys( name );
    
    if ( data_type ){
            
//        var editDcButton = createEl.element( by.css( '.edit-data-connector-button' ) );
//        editDcButton.click();

        var typeMenu = createEl.element( by.css( '.data-connector-dropdown' ) );
        var blockEl = typeMenu.all( by.tagName( 'li' ) ).first();
        var objectEl = typeMenu.all( by.tagName( 'li' ) ).last();

        typeMenu.click();
        
        if ( data_type.type === 'block' ){
            blockEl.click();
            
            var sizeSpinner = createEl.element( by.css( '.data-connector-size-spinner' )).element( by.tagName( 'input' ));
            sizeSpinner.clear();
            sizeSpinner.sendKeys( data_type.attributes.size );
        }
        else {
            objectEl.click();
        }

    }// edit dc
    
    // let's set the qos stuff hooray!
    if ( qos ){
        
        if ( qos.preset !== undefined && !isNaN( parseInt( qos.preset ) ) ){
            createEl.all( by.css( '.qos-panel .button-bar-button' )).get( parseInt( qos.preset ) ).click();
        }
        else {
            setCustomQos( qos );
        }
    }

    if ( mediaPolicy ){
        
        var index = 0;
        
        switch( mediaPolicy ){
            case 'SSD_ONLY':
                index = 0;
                break;
            case 'HDD_ONLY':
                index = 2;
                break;
            case 'HYBRID_ONLY':
                index = 1;
                break;
            default: 
                index = 0;
                break;
        }
        
        createEl.all( by.css( '.tiering-policy-panel .button-bar-button' )).get( index ).click();
        
    }
    
    // set the timeline numbers
    if ( timeline ){
        
        if ( timeline.preset !== undefined && !isNaN( parseInt( timeline.preset ) ) ){
            createEl.all( by.css( '.protection-policy .button-bar-button' )).get( parseInt( timeline.preset ) ).click();
        }
        else {
            setTimelineTimes( createEl, timeline );
        }
    }
    
    if ( timelineStartTimes ){
        setTimelineStartTimes( createEl, timelineStartTimes );
    }
    
    element.all( by.buttonText( 'Create Volume' )).get( 0 ).click();
    browser.sleep( 300 );
};

deleteVolume = function( row ){
    element.all( by.css( '.volume-row' ) ).get( row ).click();

    browser.sleep( 500 );

    var viewEl = element.all( by.css( '.slide-window-stack-slide' ) ).get(2);
    viewEl.element( by.css( '.delete-volume' ) ).click();

    browser.sleep( 200 );

    //accept the warning
    element( by.css( '.fds-modal-ok' ) ).click();

    browser.sleep( 200 );

    element.all( by.css( 'volume-row' )).then( function( rows ){
        expect( rows.length ).toBe( 0 );
    });

    browser.sleep( 200 );
};
