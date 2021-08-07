# weather-at-my-command
Simple Weather station based on esp8266 - Node mcu modul
The code is hevily inspired by [ESP8266 IoT Electronics Starter Kit](https://www.tindie.com/products/squix78/esp8266-iot-electronics-starter-kit/#shipping) and related codebases [esp8266-weather-station](https://github.com/thingpulse/esp8266-weather-station) and [esp8266-dht-thingspeak-logger](https://github.com/squix78/esp8266-dht-thingspeak-logger).

## Current features
Uploading measured data to ThinkSpeak and visualization of the current values and time series.
OLED display shows a sequence of measured values, downloaded weather predictions, current weather and current time 
Local measurement of temperature, humidity and light conditions

## Changelog
* Working both in wifi connected mode and in disconnected mode. Disconnected mode displays only measured values.
* Added one frame to display local measurements: mesured values, downloaded values (weather forecast from openweather) 
* GY302 (BH1750) for detected light conditions.
* DHT11 for temperature and humidity.

* Secrets separated to a file represented by a template

## Planned features
 * BMP280 for measurring air pressure  
 
 ## Installation

 ## Documentation
 See original documentation [here](|https://blog.squix.org/wp-content/uploads/2018/09/esp8266weatherstationgettingstartedguide-20180911.pdf)
