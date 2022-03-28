#ifndef OTA_H
#define OTA_H
#include <ArduinoOTA.h>


void enableOTA(){
    ArduinoOTA.setHostname("Pelletheater");
    ArduinoOTA.setPort(3232);
    ArduinoOTA.onStart([](){
        String type;
        type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
        Serial.println("Updating...");
    }).onEnd([](){
        Serial.println("\nEnd");
    }).onProgress([](unsigned int progress, unsigned int total){
        Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    }).onError([](ota_error_t error){
        Serial.printf("Error[%u]: ", error);
        switch(error){
            case OTA_AUTH_ERROR:
                Serial.println("Auth Failed");
                break;
            case OTA_BEGIN_ERROR:
                Serial.println("Begin Failed");
                break;
            case OTA_CONNECT_ERROR:
                Serial.println("Connect Failed");
                break;
            case OTA_RECEIVE_ERROR:
                Serial.println("Receive Failed");
                break;
            case OTA_END_ERROR:
                Serial.println("End Failed");
                break;
            default:
                Serial.println("¯\\_(ツ)_/¯ I... don't know.");
        }
    });

    ArduinoOTA.begin();
}

#endif
