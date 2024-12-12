#include <ESP.h>
#include <ESP8266WiFi.h>
#include <Wire.h>
#include <SPI.h>
#include <DHT.h>
#include "sensirion_common.h"
#include "sgp30.h"
#include "Seeed_BME280.h"
#include "keys.h"
#include "SHT31.h"
// #include "sgp30_custom_lib.c"
#include <InfluxDbClient.h>

// Topic setup
#define TOPIC_TEMPERATURE "temperature"
#define TOPIC_HUMIDITY "humidity"
#define TOPIC_PRESSURE "pressure"
#define TOPIC_ALTITUDE "altitude"
#define TOPIC_VOC "voc"
#define TOPIC_CO2 "co2eq"

#define DEBUG 0

// Influx settings
// Some settings in keys.h
// InfluxDB 2 organization name or id (Use: InfluxDB UI -> Settings -> Profile -> <name under tile> )
#define INFLUXDB_ORG "aqm"
// InfluxDB 2 bucket name (Use: InfluxDB UI -> Load Data -> Buckets)
#define INFLUXDB_BUCKET "aqm"
#define BATCH_SIZE 10 //number of measurements that will be recorded each loop, to ensure batch is sent every loop
Point influxDataPoint("air_quality");

// DHT setup
#define DHTPIN 13       // what pin we're connected to
#define DHTTYPE DHT22   // DHT 22  (AM2302)
DHT dht(DHTPIN, DHTTYPE);

// SHT31 setup
SHT31 sht31 = SHT31();

// BME280 setup
BME280 bme280;

// General params
#define SAMPLE_FREQ 60 //sample frequency in seconds

//Setup Communication
WiFiClient espClient;
InfluxDBClient influxClient(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN); // see https://github.com/tobiasschuerg/InfluxDB-Client-for-Arduino/blob/master/examples/BasicWrite/BasicWrite.ino


void setup()
{

  // Start the serial port for debugging
  Serial.begin(115200);  // Init serial link for debugging
  Serial.println("Init sensors!");

  //Power on the Grove Sensors on Wio Link Board
  #if defined(ESP8266)
    pinMode(15,OUTPUT);
    digitalWrite(15,1);
    Serial.println("Set wio sensors power!");
    delay(500);
  #endif

  //Start sensors
  dht.begin();
  sht31.begin();
  
  //Init SGP30 module
  setupSGP30Sensor();

  // Start BME280 sensor
  if(!bme280.init()){
    Serial.println("BME280 device error!");
  }

  Serial.println("Sensors setup finished");

  // Connect to the wifi
  setupWiFi(WIFI_SSID, WIFI_PASSWORD);

  // Setup influx
  // influxDataPoint.addTag("device", "WIO LINK");
  if (influxClient.validateConnection()) {
    Serial.print("Connected to InfluxDB: ");
    Serial.println(influxClient.getServerUrl());
  } else {
    Serial.print("InfluxDB connection failed: ");
    Serial.println(influxClient.getLastErrorMessage());
  }
  influxClient.setWriteOptions(WriteOptions().batchSize(BATCH_SIZE));
}

void setupSGP30Sensor() {
  s16 err;
  u16 scaled_ethanol_signal, scaled_h2_signal;
  /*  Init module,Reset all baseline,The initialization takes up to around 15 seconds, during which
      all APIs measuring IAQ(Indoor air quality ) output will not change.Default value is 400(ppm) for co2,0(ppb) for tvoc*/
  while (sgp_probe() != STATUS_OK) {
      Serial.println("SGP failed");
      while (1);
  }
  /*Read H2 and Ethanol signal in the way of blocking*/
  err = sgp_measure_signals_blocking_read(&scaled_ethanol_signal,
                                          &scaled_h2_signal);
  if (err == STATUS_OK) {
      Serial.println("get ram signal!");
  } else {
      Serial.println("error reading signals");
  }
  err = sgp_iaq_init();
}

void setupWiFi(const char* ssid, const char* password) {
  if (WiFi.status() != WL_CONNECTED) {
//    Serial.print("Connecting to ");
//    Serial.println(ssid);
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED)
    {
      delay(500);
//      Serial.print(".");
    }
  }
  if (DEBUG){
    Serial.println("WiFi connected");
  }
}

void influxDataPublish(char *measurement_type, char *sensor, float measurement) {
  influxDataPoint.clearFields();
  influxDataPoint.clearTags();
  influxDataPoint.addField(measurement_type, measurement);
  influxDataPoint.addTag("sensor", sensor);
  Serial.println("Writing following data point:");
  Serial.println(influxClient.pointToLineProtocol(influxDataPoint));
  //Serial.printf("Line length: %s\n", String(influxClient.pointToLineProtocol(influxDataPoint).length())); //test the typical line length
  
  //batch writing is enabled, batch size:th write will return write result
  if(!influxClient.writePoint(influxDataPoint)) {
    Serial.print("InfluxDB write failed: ");
    Serial.println(influxClient.getLastErrorMessage());
  }
}

void readDHTSensor()
{
  // sampling data
  float temperature = dht.readTemperature(false);
  float humidity = dht.readHumidity();


  // Check if any reads failed and exit early (to try again).
  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("Got bad readings from DHT sensor, skipping read this time...");
    return;
  }
  influxDataPublish(TOPIC_TEMPERATURE, "DHT22", temperature);
  influxDataPublish(TOPIC_HUMIDITY, "DHT22", humidity);
}

void readSGP30Sensor()
{
  /* start a tVOC and CO2-eq measurement and to readout the values */
  s16 err=0;
  u16 tvoc_ppb, co2_eq_ppm;
  err = sgp_measure_iaq_blocking_read(&tvoc_ppb, &co2_eq_ppm);
  if (err != STATUS_OK) {
    Serial.println("error reading tvoc_ppb & co2_eq_ppm values\n");
  }

  influxDataPublish(TOPIC_VOC, "SGP30", tvoc_ppb);
  influxDataPublish(TOPIC_CO2, "SGP30", co2_eq_ppm);
}

void readSHT31Sensor()
{
  float temp = sht31.getTemperature();
  float hum = sht31.getHumidity();

  influxDataPublish(TOPIC_TEMPERATURE, "SHT31", temp);
  influxDataPublish(TOPIC_HUMIDITY, "SHT31", hum);
}

void readMQ9Sensor()
{
  //Not yet implemented
  return;
}

void readBME280Sensor()
{
  float pressure = bme280.getPressure();
  float temperature = bme280.getTemperature();
  float altitude = bme280.calcAltitude(pressure);
  float humidity = bme280.getHumidity();

  influxDataPublish(TOPIC_PRESSURE, "BME280", pressure);
  influxDataPublish(TOPIC_TEMPERATURE, "BME280", temperature);
  influxDataPublish(TOPIC_ALTITUDE, "BME280", altitude);
  influxDataPublish(TOPIC_HUMIDITY, "BME280", humidity);
}

void loop()
{
  readDHTSensor();
  readSGP30Sensor();
  readSHT31Sensor();
  readBME280Sensor();
  readMQ9Sensor(); // not yet implemented
  
  // ESP.deepSleep(SAMPLE_FREQ * 1e6);
  delay(SAMPLE_FREQ * 1e3);
}
