var Clay = require('@rebble/clay');
var clayConfig = require('./config.json');
var clay = new Clay(clayConfig);

var xhrRequest = function (url, type, cb) {
  var xhr = new XMLHttpRequest();
  xhr.onload = function () { cb(this.responseText); };
  xhr.onerror = function () { console.log('xhr error'); };
  xhr.open(type, url);
  xhr.send();
};

function locationSuccess(pos) {
  var url = 'https://api.open-meteo.com/v1/forecast?' +
      'latitude=' + pos.coords.latitude +
      '&longitude=' + pos.coords.longitude +
      '&current=temperature_2m,weather_code,is_day' +
      '&temperature_unit=celsius';

  xhrRequest(url, 'GET', function (body) {
    try {
      var json = JSON.parse(body);
      var temperature = Math.round(json.current.temperature_2m);
      var code = json.current.weather_code;
      var isDay = json.current.is_day;

      Pebble.sendAppMessage({
        TEMPERATURE: temperature,
        WEATHER_CODE: code,
        IS_DAY: isDay
      },
      function () { console.log('weather sent'); },
      function (e) { console.log('send failed: ' + JSON.stringify(e)); });
    } catch (e) {
      console.log('parse error: ' + e);
    }
  });
}

function locationError(err) {
  console.log('location error: ' + JSON.stringify(err));
}

function getWeather() {
  navigator.geolocation.getCurrentPosition(
    locationSuccess,
    locationError,
    { timeout: 15000, maximumAge: 60000 }
  );
}

Pebble.addEventListener('ready', function () {
  console.log('pkjs ready');
  getWeather();
});

Pebble.addEventListener('appmessage', function (e) {
  if (e.payload && e.payload.REQUEST_WEATHER) getWeather();
});
