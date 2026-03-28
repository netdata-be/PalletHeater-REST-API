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
        StoveIsOn = true;
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

struct ErrorEntry {
    int id;
    const char* code;
    const char* desc;
};

static const ErrorEntry errorTable[] = {
    {   0, "---",        "No Error" },
    {   1, "E8",         "Fume sensor error" },
    {   2, "E4",         "Fume temperature to high" },
    {   4, "E9",         "Ignition temperature not reached within limit" },
    {   8, "E7",         "Fume temperatuer to low" },
    {  16, "E3",         "Temperature inside stove to high" },
    {  32, "E1",         "Pallet loading door is open for to long or air pressure inside stove is wrong" },
    {  64, "E2",         "Pressure sensor error during ignition" },
    { 128, "E6",         "Auger error - To much pallets are dispatched" },
    { 129, "E12",        "Fume fan error" },
    { 130, "E11",        "Unknown error - Not described in manual" },
    { 132, "E19",        "External contact N.PEL / Pallet is active" },
    { 136, "E13",        "Unknown error - Not described in manual" },
    { 255, "PowerError", "Display shows power error" },
};
static const int errorTableSize = sizeof(errorTable) / sizeof(errorTable[0]);

String getErrorCode(int error)
{
    for (int i = 0; i < errorTableSize; i++) {
        if (errorTable[i].id == error) return errorTable[i].code;
    }
    return "---";
}

String getErrorDesc(int error)
{
    for (int i = 0; i < errorTableSize; i++) {
        if (errorTable[i].id == error) return errorTable[i].desc;
    }
    return "Unknown error";
}