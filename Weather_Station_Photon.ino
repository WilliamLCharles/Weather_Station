// This #include statement was automatically added by the Particle IDE.
#include <SparkFun_Photon_Weather_Shield_Library.h>
/*



  This code is based on the Wimp Weather Station sketch by Nathan Seidle
  https://github.com/sparkfun/Wimp_Weather_Station,
  as well as Sparkfun's Joel Bartlett.
  https://github.com/sparkfun/Photon_Weather_Shield


      HTU21D ------------- Photon
      (-) ------------------- GND
      (+) ------------------- 3.3V (VCC)
       CL ------------------- D1/SCL
       DA ------------------- D0/SDA
    MPL3115A2 ------------- Photon
      GND ------------------- GND
      VCC ------------------- 3.3V (VCC)
      SCL ------------------ D1/SCL
      SDA ------------------ D0/SDA
    Soil Moisture Sensor ----- Photon
        GND ------------------- GND
        VCC ------------------- D5
        SIG ------------------- A1
    DS18B20 Temp Sensor ------ Photon
        VCC (Red) ------------- 3.3V (VCC)
        GND (Black) ----------- GND
        SIG (White) ----------- D4
*/


WiFi.selectAntenna(ANT_EXTERNAL);


int WDIR = A0;
int RAIN = D2;
int WSPEED = D3;
int VH400 = A1;



//Global Variables
long lastSecond; //The millis counter to see when a second rolls by
byte seconds; //When it hits 60, increase the current minute
byte seconds_2m; //Keeps track of the "wind speed/dir avg" over last 2 minutes array of data
byte minutes; //Keeps track of where we are in various arrays of data
byte minutes_10m; //Keeps track of where we are in wind gust/dir over last 10 minutes array of data





//We need to keep track of the following variables:
//Wind speed/dir each update (no storage)
//Wind gust/dir over the day (no storage)
//Wind speed/dir, avg over 2 minutes (store 1 per second)
//Wind gust/dir over last 10 minutes (store 1 per minute)
//Rain over the past hour (store 1 per minute)
//Total rain over date (store one per day)

byte windspdavg[120]; //120 bytes to keep track of 2 minute average
int winddiravg[120]; //120 ints to keep track of 2 minute average
double windgust_10m[10]; //10 floats to keep track of 10 minute max
int windgustdirection_10m[10]; //10 ints to keep track of 10 minute max
volatile float rainHour[60]; //60 floating numbers to keep track of 60 minutes of rain

//These are all the weather values that wunderground expects:
int winddir = 0; // [0-360 instantaneous wind direction]
double windspeedmph = 0; // [mph instantaneous wind speed]
float windgustmph = 0; // [mph current wind gust, using software specific time period]
int windgustdir = 0; // [0-360 using software specific time period]
float windspdmph_avg2m = 0; // [mph 2 minute average wind speed mph]
int winddir_avg2m = 0; // [0-360 2 minute average wind direction]
float windgustmph_10m = 0; // [mph past 10 minutes wind gust mph ]
int windgustdir_10m = 0; // [0-360 past 10 minutes wind gust direction]
double rainin = 0; // [rain inches over the past hour)] -- the accumulated rainfall in the past 60 min
long lastWindCheck = 0;
volatile float dailyrainin = 0; // [rain inches so far today in local time]

double humidity = 0;
double tempf = 0;
double InTempC = 0;//original temperature in C from DS18B20
double soiltempf = 0;//converted temperature in F from DS18B20
double pascals = 0;
double altf = 0;
double baroTemp = 0;
double soilMoisture = 0;

int count = 0;//This triggers a post and print on the first time through the loop

// volatiles are subject to modification by IRQs
volatile long lastWindIRQ = 0;
volatile byte windClicks = 0;
volatile unsigned long raintime, rainlast, raininterval, rain;

//Create Instance of HTU21D or SI7021 temp and humidity sensor and MPL3115A2 barrometric sensor
Weather sensor;

//Interrupt routines (these are called by the hardware interrupts, not by the main code)
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void rainIRQ()
// Count rain gauge bucket tips as they occur
// Activated by the magnet and reed switch in the rain gauge, attached to input D2
{
  raintime = millis(); // grab current time
  raininterval = raintime - rainlast; // calculate interval between this and last event

    if (raininterval > 10) // ignore switch-bounce glitches less than 10mS after initial edge
  {
    dailyrainin += 0.011; //Each dump is 0.011" of water
    rainHour[minutes] += 0.011; //Increase this minute's amount of rain

    rainlast = raintime; // set up for next event
  }
}

void wspeedIRQ()
// Activated by the magnet in the anemometer (2 ticks per rotation), attached to input D3
{
  if (millis() - lastWindIRQ > 10) // Ignore switch-bounce glitches less than 10ms (142MPH max reading) after the reed switch closes
  {
    lastWindIRQ = millis(); //Grab the current time
    windClicks++; //There is 1.492MPH for each click per second.
  }
}

//---------------------------------------------------------------
void setup()
{
bool WiFi.antennaSelect(EXTERNAL);


    pinMode(D5, OUTPUT);
    digitalWrite(D5, HIGH);
    pinMode(WSPEED, INPUT_PULLUP); // input from wind meters windspeed sensor
    pinMode(RAIN, INPUT_PULLUP); // input from wind meters rain gauge sensor


    Serial.begin(9600);   // open serial over USB
    //Begin posting data immediately instead of waiting for a key press.

    //Initialize the I2C sensors and ping them
    sensor.begin();


    //sensor.setModeBarometer();//Set to Barometer Mode
    sensor.setModeAltimeter();//Set to altimeter Mode

    //These are additional MPL3115A2 functions the MUST be called for the sensor to work.
    sensor.setOversampleRate(7); // Set Oversample rate
    //Call with a rate from 0 to 7. See page 33 for table of ratios.
    //Sets the over sample rate. Datasheet calls for 128 but you can set it
    //from 1 to 128 samples. The higher the oversample rate the greater
    //the time between data samples.

    sensor.enableEventFlags(); //Necessary register calls to enble temp, baro ansd alt

    seconds = 0;
    lastSecond = millis();

    // attach external interrupt pins to IRQ functions
    attachInterrupt(RAIN, rainIRQ, FALLING);
    attachInterrupt(WSPEED, wspeedIRQ, FALLING);

    // turn on interrupts
    interrupts();

    getWeather();//get initial values out of the way
    delay(1000);
    getWeather();//print before entering the loop and waiting for count
    printInfo();

}
//---------------------------------------------------------------
void loop()
{
  //Keep track of which minute it is
  if(millis() - lastSecond >= 1000)
  {

    lastSecond += 1000;

    //Take a speed and direction reading every second for 2 minute average
    if(++seconds_2m > 119) seconds_2m = 0;

    //Calc the wind speed and direction every second for 120 second to get 2 minute average
    float currentSpeed = get_wind_speed();
    int currentDirection = get_wind_direction();
    windspdavg[seconds_2m] = (int)currentSpeed;
    winddiravg[seconds_2m] = currentDirection;
    //if(seconds_2m % 10 == 0) displayArrays(); //For testing

    //Check to see if this is a gust for the minute
    if(currentSpeed > windgust_10m[minutes_10m])
    {
      windgust_10m[minutes_10m] = currentSpeed;
      windgustdirection_10m[minutes_10m] = currentDirection;
    }

    //Check to see if this is a gust for the day
    if(currentSpeed > windgustmph)
    {
      windgustmph = currentSpeed;
      windgustdir = currentDirection;
    }

    if(++seconds > 59)
    {
      seconds = 0;

      if(++minutes > 59) minutes = 0;
      if(++minutes_10m > 9) minutes_10m = 0;

      rainHour[minutes] = 0; //Zero out this minute's rainfall amount
      windgust_10m[minutes_10m] = 0; //Zero out this minute's gust
    }


    //Rather than use a delay, keeping track of a counter allows the photon to
    // still take readings and do work in between printing out data.
    count++;
    //alter this number to change the amount of time between each reading
    if(count == 50)
    {
      //Get readings from all sensors
       getWeather();
       printInfo();

       count = 0;
    }
  }
}
//---------------------------------------------------------------
void printInfo()
{
  //This function prints the weather data out to the default Serial Port
      Serial.print("Wind_Dir:");
      switch (winddir)
      {
        case 0:
          Serial.print("North");
          break;
        case 1:
          Serial.print("NE");
          break;
        case 2:
          Serial.print("East");
          break;
        case 3:
          Serial.print("SE");
          break;
        case 4:
          Serial.print("South");
          break;
        case 5:
          Serial.print("SW");
          break;
        case 6:
          Serial.print("West");
          break;
        case 7:
          Serial.print("NW");
          break;
        default:
          Serial.print("No Wind");
          // if nothing else matches, do the
          // default (which is optional)
      }

      Serial.print(" Wind_Speed:");
      Serial.print(windspeedmph, 1);
      Serial.print("mph, ");
      Particle.variable("Windspeed", windspeedmph);


      Serial.print("Rain:");
      Serial.print(rainin, 2);
      Serial.print("in., ");
      Particle.variable("Rain", rainin);

      Serial.print("Temp:");
      Serial.print(tempf);
      Serial.print("F, ");
      Particle.variable("Temperature", tempf);

      Serial.print("Humidity:");
      Serial.print(humidity);
      Serial.print("%, ");
      Particle.variable("Humidity", humidity);

      Serial.print("Baro_Temp:");
      Serial.print(baroTemp);
      Serial.print("F, ");
      Particle.variable("Baro_Temp", baroTemp);

      Serial.print("Pressure:");
      Serial.print(pascals/100);
      Serial.print("hPa, ");

      Particle.variable("Pressure_hPa", pascals);

      Particle.variable("Alt:", altf);
      //The MPL3115A2 outputs the pressure in Pascals. However, most weather stations
      //report pressure in hectopascals or millibars. Divide by 100 to get a reading
      //more closely resembling what online weather reports may say in hPa or mb.
      //Another common unit for pressure is Inches of Mercury (in.Hg). To convert
      //from mb to in.Hg, use the following formula. P(inHg) = 0.0295300 * P(mb)
      //More info on conversion can be found here:
      //www.srh.noaa.gov/images/epz/wxcalc/pressureConversion.pdf

      //If in altitude mode, print with these lines
      //Serial.print("Altitude:");
      //Serial.print(altf);
      //Serial.println("ft.");

      Serial.print("Soil_Temp:");
      Serial.print(soiltempf);
      Serial.print("F, ");
      Particle.variable("Soil_Temp", soiltempf);


   // int moist = analogRead(VH400, 255);
      Serial.print("Soil_Mosit:");
      Serial.println(soilMoisture);
      Particle.variable("Soil_Moist", soilMoisture);
      Particle.variable("Moist", moist);
      //Mositure Content is expressed ;as an analog
      //value, which can range from 0 (completely dry) to the value of the
      //materials' porosity at saturation. The sensor tends to max out between
      //3000 and 3500.
}
//---------------------------------------------------------------

//Read the wind direction sensor, return heading in degrees
int get_wind_direction()
{
  unsigned int adc;

  adc = analogRead(WDIR); // get the current reading from the sensor

  // The following table is ADC readings for the wind direction sensor output, sorted from low to high.
  // Each threshold is the midpoint between adjacent headings. The output is degrees for that ADC reading.
  // Note that these are not in compass degree order! See Weather Meters datasheet for more information.

  //Wind Vains may vary in the values they return. To get exact wind direction,
  //it is recomended that you AnalogRead the Wind Vain to make sure the values
  //your wind vain output fall within the values listed below.
  if(adc > 2270 && adc < 2290) return (0);//North
  if(adc > 3220 && adc < 3299) return (1);//NE
  if(adc > 3890 && adc < 3999) return (2);//East
  if(adc > 3780 && adc < 3850) return (3);//SE

  if(adc > 3570 && adc < 3650) return (4);//South
  if(adc > 2790 && adc < 2850) return (5);//SW
  if(adc > 1580 && adc < 1610) return (6);//West
  if(adc > 1930 && adc < 1950) return (7);//NW

  return (-1); // error, disconnected?
}
//---------------------------------------------------------------
//Returns the instataneous wind speed
float get_wind_speed()
{
  float deltaTime = millis() - lastWindCheck; //750ms

  deltaTime /= 1000.0; //Covert to seconds

  float windSpeed = (float)windClicks / deltaTime; //3 / 0.750s = 4

  windClicks = 0; //Reset and start watching for new wind
  lastWindCheck = millis();

  windSpeed *= 1.492; //4 * 1.492 = 5.968MPH

  /* Serial.println();
   Serial.print("Windspeed:");
   Serial.println(windSpeed);*/

  return(windSpeed);
}
//---------------------------------------------------------------
void getWeather()
{
    // Measure Relative Humidity from the HTU21D or Si7021
    humidity = sensor.getRH();

    // Measure Temperature from the HTU21D or Si7021
    tempf = sensor.getTempF();
    // Temperature is measured every time RH is requested.
    // It is faster, therefore, to read it from previous RH
    // measurement with getTemp() instead with readTemp()

    //Measure the Barometer temperature in F from the MPL3115A2
    baroTemp = sensor.readBaroTempF();

    //Measure Pressure from the MPL3115A2
    pascals = sensor.readPressure();

    //If in altitude mode, you can get a reading in feet  with this line:
    altf = sensor.readAltitudeFt();


    //Calc winddir
    winddir = get_wind_direction();

    //Calc windspeed
    windspeedmph = get_wind_speed();

    //Calc windgustmph
    //Calc windgustdir
    //Report the largest windgust today
    windgustmph = 0;
    windgustdir = 0;

    //Calc windspdmph_avg2m
    float temp = 0;
    for(int i = 0 ; i < 120 ; i++)
      temp += windspdavg[i];
    temp /= 120.0;
    windspdmph_avg2m = temp;

    //Calc winddir_avg2m
    temp = 0; //Can't use winddir_avg2m because it's an int
    for(int i = 0 ; i < 120 ; i++)
      temp += winddiravg[i];
    temp /= 120;
    winddir_avg2m = temp;

    //Calc windgustmph_10m
    //Calc windgustdir_10m
    //Find the largest windgust in the last 10 minutes
    windgustmph_10m = 0;
    windgustdir_10m = 0;
    //Step through the 10 minutes
    for(int i = 0; i < 10 ; i++)
    {
      if(windgust_10m[i] > windgustmph_10m)
      {
        windgustmph_10m = windgust_10m[i];
        windgustdir_10m = windgustdirection_10m[i];
      }
    }

    //Total rainfall for the day is calculated within the interrupt
    //Calculate amount of rainfall for the last 60 minutes
    rainin = 0;
    for(int i = 0 ; i < 60 ; i++)
      rainin += rainHour[i];
}
