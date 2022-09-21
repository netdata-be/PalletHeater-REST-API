#include <constants.h>


bool isConnected()
{
    return (WiFi.status() == WL_CONNECTED);
}

void getStoveState() // Get detailed stove state
{
    stoveState = stove.read(ram, stoveStateAddr);

    switch (stoveState)
    {
    case OFF:
        StoveStateStr = "Off";
        StoveIsOn = false;
        break;
    case STARTING:
        StoveStateStr = "Starting";
        StoveIsOn = true;
        break;
    case PALLET_LOADING:
        StoveStateStr = "Pellet loading"; // Phase I
        StoveIsOn = true;
        break;
    case IGNITION:
        StoveStateStr = "Ignition"; // Phase II
        StoveIsOn = true;
        break;
    case WORKING:
        StoveStateStr = "Working";
        StoveIsOn = true;
        break;
    case BRAZIER_CLEANING:
        StoveStateStr = "Brazier cleaning";
        break;
    case FINAL_CLEANING:
        StoveStateStr = "Final cleaning"; // Shut Down
        StoveIsOn = false;
        break;
    case STANDBY:
        StoveStateStr = "Standby";
        StoveIsOn = false;
        break;
    case ALARM:
        StoveStateStr = "Alarm State";
        break;
    case IGNITION_FAILURE:
        StoveStateStr = "Ignition failure";
        StoveIsOn = false;
        break;
    case ALARM_STATE:
        StoveStateStr = "Alarm";
        StoveIsOn = false;
        break;
    case UNKNOWN:
        StoveStateStr = "Unkown [RS232 Serial Error]";
        StoveIsOn = false;
        break;
    }
}

String getErrorCode(int error)
{
    String errorCode;
    switch (error)
    {
    case 0:
        errorCode = "---";
        break;
    case 1:
        errorCode = "E8";
        break;
    case 2:
        errorCode = "E4";
        break;
    case 4:
        errorCode = "E9";
        break;
    case 8:
        errorCode = "E7";
        break;
    case 16:
        errorCode = "E3";
        break;
    case 32:
        errorCode = "E1";
        break;
    case 64:
        errorCode = "E2";
        break;
    case 128:
        errorCode = "E6";
        break;
    case 129:
        errorCode = "E12";
        break;
    case 130:
        errorCode = "E11";
        break;
    case 132:
        errorCode = "E19";
        break;
    case 136:
        errorCode = "E13";
        break;
    case 255:
        errorCode = "PowerError";
        break;
    }
    return errorCode;
}

String getErrorDesc(int error)
{
    String errorCode;
    switch (error)
    {
    case 0:
        errorCode = "No Error";
        break;
    case 1:
        errorCode = "Fume sensor error";
        break;
    case 2:
        errorCode = "Fume temperature to high";
        break;
    case 4:
        errorCode = "Ignition temperature not reached within limit";
        break;
    case 8:
        errorCode = "Fume temperatuer to low";
        break;
    case 16:
        errorCode = "Temperature inside stove to high";
        break;
    case 32:
        errorCode = "Pallet loading door is open for to long or air pressure inside stove is wrong";
        break;
    case 64:
        errorCode = "Pressure sensor error during ignition";
        break;
    case 128:
        errorCode = "Auger error - To much pallets are dispatched";
        break;
    case 129:
        errorCode = "Fume fan error";
        break;
    case 130:
        errorCode = "Unknown error - Not described in manual";
        break;
    case 132:
        errorCode = "External contact N.PEL / Pallet is active";
        break;
    case 136:
        errorCode = "Unknown error - Not described in manual";
        break;
    case 255:
        errorCode = "Display shows power error";
        break;
    }
    return errorCode;
}