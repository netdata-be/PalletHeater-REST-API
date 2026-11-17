#include <constants.h>


bool isConnected()
{
    return (WiFi.status() == WL_CONNECTED);
}

void getStoveState() // Get detailed stove state
{
    stoveState = stove.read(ram, stoveStateAddr);
    snprintf(StoveStateStr, sizeof(StoveStateStr), "%s", pc::stoveStateName(stoveState));
    StoveIsOn = pc::stoveStateIsOn(stoveState);
}

// Error table: lib/pelletcore (pc::errorTable), unit tested
String getErrorCode(int error) { return pc::errorCode(error); }
String getErrorDesc(int error) { return pc::errorDesc(error); }