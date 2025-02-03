#include "locker.h"

void app_main() 
{
    xTaskCreate(&locker_task,"locker_task",40000,NULL,6,NULL);
}