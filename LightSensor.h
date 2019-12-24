#define ALS_PIN A0
#define ALS_AVG_SIZE 50

int _avg[ALS_AVG_SIZE];

void updateAvgLux(int als)
{
  static int i = 0;

  _avg[i++] = als;
  if (i == ALS_AVG_SIZE) i = 0;
}

int getAvgLux()
{
  int sum = 0;

  for (int i = 0; i < ALS_AVG_SIZE; i++)
    sum += _avg[i];

  if (sum < 0.0) sum = 0;

  return sum / ALS_AVG_SIZE;
}

void handleAmbientLightSensor()
{
  static uint64_t pt = 0;

  uint64_t ct = millis64() / 10; // Read ADC every 10ms because of Wifi Bug (https://github.com/krzychb/EspScopeA0/tree/master/Bravo)

  if (ct == pt)
    return ;

  pt = ct;

  yield();
  //updateAvgLux(lightMeter.readLightLevel());
  yield();
}