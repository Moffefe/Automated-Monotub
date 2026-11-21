#include <MainController.h>



MainController mainController;



void setup()
{
  #ifdef DEBUG_ENABLED
  Serial.begin(115200);
  #endif

  mainController.begin();
}

void loop()
{
  delay(1);
}
