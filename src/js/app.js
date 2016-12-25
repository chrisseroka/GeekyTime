var initDone = false;
var tempScale = 'F';
var tempCorrect = 0;
var btVibrate = 'On';
var dateFormat = 'mmdd';
var autoLocation = 'On';
var manLocation = '';
var refreshIntrvl = 30;
var owmAppId = '9b46f205cf161eb68ebcf12970587b88';
var faceVersion = '1.0'; //default version

function sendToWatchSuccess(e)
{
  console.log("Message #" + e.data.transactionId + " sent to watch successfully!");
}
function sendToWatchFail(e)
{
  console.log("Message #" + e.data.transactionId + " sending failed: " + e.error.message);
}

function getSmogData(callback){
		var result = {};
		var req = new XMLHttpRequest();
		req.open('GET', "http://powietrze.malopolska.pl/_powietrzeapi/api/dane?act=danemiasta&ci_id=01", true);
		req.onload = function(a,b,c)
		{
        if (this.readyState == 4 && this.status == 200) {
            var response = JSON.parse(this.responseText);
            var actual = response.dane.actual;
            var station = null;
            var i = 0;
            for(i = 0; i<actual.length; i++){
                if (actual[i].station_id == 3){ station = actual[i]; }
            }
            for(i =0; i <station.details.length; i++){
                result[station.details[i].o_wskaznik] = station.details[i].o_value;
            }
            callback(result);
        }

		};
		req.send(null);
}

function parseWeatherResponse() {
  if (this.readyState == 4) {
    if(this.status == 200) {
      console.log(this.responseText);
      var response = JSON.parse(this.responseText);
      var temperature = '--';
      var temperatureC = '--';
      var temperatureF = '--';
      var pm25 = '25';
      var pm10 = '10';
      var icon = '00';
      var location = 'Unknown';
      if (response && response.weather && response.weather.length > 0) {
        var weatherResult = response.weather[0];
        icon = weatherResult.icon;
      }
      if (response && response.main && response.main.temp !== null &&
          response.main.temp != 'undefined' && response.main.temp !== '' &&
          !isNaN(response.main.temp)) {
        temperatureC = response.main.temp - 273.15;
        console.log('temp C before correction=' + temperatureC);
        console.log('temp Correction=' + tempCorrect);
        temperatureC = Math.round(temperatureC + tempCorrect);
        console.log('temp C after correction=' + temperatureC);
        temperatureF = Math.round(temperatureC * 9 / 5 + 32);
        //assign temp based on settings
        if (tempScale == 'C')
        {
          temperature = temperatureC;
        }
        else
        {
          temperature = temperatureF;
        }
      }
      if (response && response.name ) {
        location = response.name;
      }

      getSmogData(function(smogData){
        pm25 = smogData["pm2.5"];
        pm10 = smogData["pm10"];
        
        console.log('Icon=' + icon);
        console.log('Temp=' + temperature);
        console.log('Temp C=' + temperatureC);
        console.log('Temp F=' + temperatureF);
        console.log('Location=' + location);
        console.log('PM25=' + pm25);
        console.log('PM10=' + pm10);

        var msgId = Pebble.sendAppMessage({
          "icon":icon,
          "temperature":temperature.toString(),
          //"location":location}, sendToWatchSuccess, sendToWatchFail);
          "location":location,
          "pm25":pm25,
          "pm10":pm10}, sendToWatchSuccess, sendToWatchFail);
        
        console.log("Sending message to watch ...");
      });

    }
    else
    {
      console.log("HTTP Error = " + this.status);
    }
  }
}

function fetchWeatherForCoords(latitude, longitude) {
  console.log("JS fetch weather from web for coords: " + latitude + "," +
    longitude);
  var req = new XMLHttpRequest();
  req.open('GET', "http://api.openweathermap.org/data/2.5/weather?" +
    "lat=" + latitude + "&lon=" + longitude + '&APPID=' + owmAppId, true);
  req.onload = parseWeatherResponse;
  req.send(null);
}


function fetchWeatherForStaticLocation(locationString) {
  console.log("JS fetch weather from web for static location: " +
      locationString);
  var req = new XMLHttpRequest();
  req.open('GET', "http://api.openweathermap.org/data/2.5/weather?" +
    "q=" + locationString + '&APPID=' + owmAppId, true);
  req.onload = parseWeatherResponse;
  req.send(null);
}

function locationSuccess(pos) {
  var coordinates = pos.coords;
  fetchWeatherForCoords(coordinates.latitude, coordinates.longitude);
}

function locationError(err) {
  console.warn('location error (' + err.code + '): ' + err.message);
  var errCode = 'UN';
  switch (err.code)
  {
    case err.TIMEOUT:
      errCode = 'TO';
      break;
    case err.POSITION_UNAVAILABLE:
      errCode = 'GE';
      break;
    case err.PERMISSION_DENIED:
      errCode = 'PD';
      break;
  }
  var msgId = Pebble.sendAppMessage({
    "icon":"00",
    "temperature":"--",
    "location":"LocErr: " + errCode
  }, sendToWatchSuccess, sendToWatchFail);
  console.log("Sending message #" + msgId + " to watch ...");
}

function initConfigOptions()
{
  var appinfo = require('./generated/appinfo');
  if (typeof appinfo === 'undefined' || typeof appinfo.appInfo === 'undefined' || appinfo.appInfo === null)
  {
    console.log("AppInfo undefined!!! Will use default version: " + faceVersion);    
  }
  else
  {
    console.log("WatchFace version: " + appinfo.appInfo.versionLabel);
    faceVersion = appinfo.appInfo.versionLabel;
  }
  var tempScaleLS = localStorage.getItem('tempScale');
  if (tempScaleLS !== null && tempScaleLS != 'undefined' && tempScaleLS.length == 1)
  {
    tempScale = tempScaleLS;
    console.log("Assigned tempScale from storage=" + tempScaleLS);
  }
  var tempCorrectLS = localStorage.getItem('tempCorrect');
  if (tempCorrectLS !== null && tempCorrectLS != 'undefined' && !isNaN(tempCorrectLS))
  {
    tempCorrect = parseFloat(tempCorrectLS);
    console.log("Assigned tempCorrect from storage=" + tempCorrectLS);
  }
  else
  {
    tempCorrect = 0;
    console.log("Assigned default tempCorrect=0");
  }
  var btVibrateLS = localStorage.getItem('btVibrate');
  if (btVibrateLS !== null && btVibrateLS != 'undefined' && btVibrateLS.length > 0)
  {
    btVibrate = btVibrateLS;
    console.log("Assigned btVibrate from storage=" + btVibrateLS);
  }
  
  var dateFormatLS = localStorage.getItem('dateFormat');
  if (dateFormatLS !== null && dateFormatLS != 'undefined' && dateFormatLS.length > 0)
  {
    dateFormat = dateFormatLS;
    console.log("Assigned dateFormat from storage=" + dateFormatLS);
  }

  var autoLocationLS = localStorage.getItem('autoLocation');
  if(autoLocationLS !== null && autoLocationLS != 'undefined' &&
     autoLocationLS.trim().length > 0)
  {
    autoLocation = autoLocationLS;
    console.log("Assigned autoLocation from storage=" + autoLocationLS);
  }
  var manLocationLS = localStorage.getItem('manLocation');
  if(manLocationLS !== null && manLocationLS != 'undefined' &&
     manLocationLS.trim().length > 0)
  {
    manLocation = manLocationLS;
    console.log("Assigned manLocation from storage=" + manLocationLS);
  }
  
  var refreshIntrvlLS = localStorage.getItem('refreshIntrvl');
  if (refreshIntrvlLS !== null && refreshIntrvlLS != 'undefined' && refreshIntrvlLS.length > 0)
  {
    refreshIntrvl = refreshIntrvlLS;
    console.log("Assigned refreshIntrvl from storage=" + refreshIntrvlLS);
  }
  
  sendWatchConfigToWatch();
}

function applyAndStoreConfigOptions(inOptions)
{
  if (inOptions !== null && inOptions != 'undefined')
  {
    //these options are for the JS running on the phone
    if (inOptions.tempScale !== null && inOptions.tempScale.length == 1)
    {
      localStorage.setItem('tempScale', inOptions.tempScale);
      tempScale = inOptions.tempScale;
    }
    if (inOptions.tempCorrect !== null && !isNaN(inOptions.tempCorrect))
    {
      localStorage.setItem('tempCorrect', inOptions.tempCorrect);
      tempCorrect = parseFloat(inOptions.tempCorrect);
    }
    if (inOptions.autoLocation !== null && inOptions.autoLocation.trim().length > 0)
    {
      localStorage.setItem('autoLocation', inOptions.autoLocation);
      autoLocation = inOptions.autoLocation;
    }
    if (inOptions.manLocation !== null && inOptions.manLocation.trim().length > 0)
    {
      localStorage.setItem('manLocation', inOptions.manLocation);
      manLocation = inOptions.manLocation;
    }

    //this option is applicable to watch app only so store and send to watch
    if (inOptions.btVibrate !== null && inOptions.btVibrate.length > 0)
    {
      localStorage.setItem('btVibrate', inOptions.btVibrate);
      btVibrate = inOptions.btVibrate;
    }

    if (inOptions.dateFormat !== null && inOptions.dateFormat.length > 0)
    {
      localStorage.setItem('dateFormat', inOptions.dateFormat);
      dateFormat = inOptions.dateFormat;
    }
    
    if (inOptions.refreshIntrvl !== null && inOptions.refreshIntrvl.length > 0)
    {
      localStorage.setItem('refreshIntrvl', inOptions.refreshIntrvl);
      refreshIntrvl = inOptions.refreshIntrvl;
    }
    
    sendWatchConfigToWatch();
  }
}

//this function will send all the watch config options back to the watch (btVibrate, dateFormat and etc.)
function sendWatchConfigToWatch()
{
  console.log('Sending btVibrate=' + btVibrate + " to the watch");
  console.log('Sending dateFormat=' + dateFormat + " to the watch");
  console.log('Sending refreshIntrvl=' + refreshIntrvl + " to the watch");
  var msgId = Pebble.sendAppMessage(
                        {"btVibrate" : btVibrate,
                         "dateFormat" : dateFormat,
                         "refreshIntrvl" : parseInt(refreshIntrvl)}, 
                          sendToWatchSuccess, sendToWatchFail);
                         
  console.log("Sending config msg to watch ...");
}

var locationOptions = { "timeout": 30000, "maximumAge": 600000 };//30s, 10 minutes

function getAppropriateWeatherData()
{
  if ('On' === autoLocation)
  {
    navigator.geolocation.getCurrentPosition(locationSuccess, locationError, locationOptions);
  }
  else
  {
    fetchWeatherForStaticLocation(manLocation);
  }
}

Pebble.addEventListener("ready",
                        function(e) {
                          console.log("JS - ready called with param: " + e.ready);
                          if (!initDone)
                          {
                            console.log("JS - performing init tasks");
                            initConfigOptions();
                            initDone = true;
                            getAppropriateWeatherData();
                          }
                        });

Pebble.addEventListener("appmessage",
                        function(e) {
                          console.log("app message called");
                          getAppropriateWeatherData();
                        });

Pebble.addEventListener("webviewclosed",
                         function(e) {
                         console.log("webview closed");
                         var options = JSON.parse(decodeURIComponent(e.response));
                         console.log("Options = " + JSON.stringify(options));
                         applyAndStoreConfigOptions(options);
                         getAppropriateWeatherData();
                         });

Pebble.addEventListener("showConfiguration",
                         function() {
                         console.log("showing configuration");
                         initConfigOptions();
                         Pebble.openURL('http://pebbleappcfg.herokuapp.com/GeekyTime/geekyTimeCfg.html' 
                           + '?tempScale=' + tempScale 
                           + '&tempCorrect=' + tempCorrect 
                           + '&btVibrate=' + btVibrate 
                           + '&dateFormat=' + dateFormat 
                           + '&refreshIntrvl=' + refreshIntrvl 
                           + '&autoLocation=' + autoLocation 
                           + '&manLocation=' + manLocation 
                           + '&allowLocSelect=true' 
                           + '&faceVersion=' + faceVersion);
                         });
