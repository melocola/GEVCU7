#include "OBCharger.h"

using namespace std;

OBChargerController::OBChargerController() : ChargeController()
{
    commonName = "On Board Battery Charger";
    shortName = "OBCHGR";
}

void OBChargerController::earlyInit()
{
    prefsHandler = new PrefHandler(OB_CHARGER);
}

void OBChargerController::setup()
{
    //prefsHandler->setEnabledStatus(true);
    tickHandler.detach(this);

    loadConfiguration();
    ChargeController::setup(); // run the parent class version of this function

    OBChargerConfiguration *config = (OBChargerConfiguration *)getConfiguration();

    ConfigEntry entry;
    //        cfgName                 helpText                               variable ref        Type                   Min Max Precision Funct
    entry = {"OBC-CANBUS", "Set which CAN bus to connect to (0-2)", &config->canbusNum, CFG_ENTRY_VAR_TYPE::BYTE, 0, 2, 0, nullptr};
    cfgEntries.push_back(entry);

    setAttachedCANBus(config->canbusNum);

    //watch for the charger status message
    attachedCANBus->attach(this, 0x319, 0x7f8, false);
    attachedCANBus->attach(this, 0x349, 0x7f8, false);

    tickHandler.attach(this, CFG_TICK_INTERVAL_OB);
    crashHandler.addBreadcrumb(ENCODE_BREAD("OBC") + 0);
}

void OBChargerController::handleCanFrame(const CAN_message_t &frame)
{
    uint8_t status = 0;
    Logger::debug("OB msg: %X   %X   %X   %X   %X   %X   %X   %X  %X", frame.id, frame.buf[0],
                  frame.buf[1],frame.buf[2],frame.buf[3],frame.buf[4],
                  frame.buf[5],frame.buf[6],frame.buf[7]);
    
    switch (frame.id)
    if (frame.id == 0x319 || frame.id == 0x349)
    {
        outputVoltage = (frame.buf[0] << 8) + (frame.buf[1]) / 10.0f;
        outputCurrent = (frame.buf[2] << 8) + (frame.buf[3]) / 10.0f;
        
        status = frame.buf[4];
        if (status & 1) Logger::error("Hardware failure of charger");
        if (status & 2) Logger::error("Charger over temperature!");
        if (status & 4) Logger::error("Input voltage out of spec!");
        if (status & 8) Logger::error("Charger can't detect proper battery voltage");
        if (status & 16) Logger::error("Comm timeout. Failed!");

        Logger::debug("Charger    V: %f  A: %f   Status: %u", outputVoltage, outputCurrent, status);

    }
}

void OBChargerController::handleTick()
{
    ChargeController::handleTick();
    
    sendSleepModeCmd();
    sendPowerCmd();
}

void OBChargerController::sendPowerCmd()
{
    OBChargerConfiguration *config = (OBChargerConfiguration *)getConfiguration();

    CAN_message_t output;
    output.len = 8;
    output.id = 0x305;
    output.flags.extended = 1;

    uint16_t vOutput = config->targetUpperVoltage * 275;
    uint16_t cOutput = config->targetCurrentLimit * 16;

    output.buf[0] = (vOutput >> 8);
    output.buf[1] = (vOutput & 0xFF);
    output.buf[2] = (cOutput >> 8);
    output.buf[3] = (cOutput & 0xFF);
    output.buf[4] = 0; // 0 = start charging
    output.buf[5] = 0; //unused
    output.buf[6] = 0; //unused
    output.buf[7] = 0; //unused

    attachedCANBus->sendFrame(output);
    Logger::debug("OB Charger cmd: %X %X %X %X %X %X %X %X %X ",output.id, output.buf[0],
                  output.buf[1],output.buf[2],output.buf[3],output.buf[4],output.buf[5],output.buf[6],output.buf[7]);
    crashHandler.addBreadcrumb(ENCODE_BREAD("OBC") + 1);
}

void OBChargerController::sendWakeUpCmd()
{
    OBChargerConfiguration *config = (OBChargerConfiguration *)getConfiguration();

    CAN_message_t output;
    output.len = 8;
    output.id = 0x171;
    output.flags.extended = 1;

    output.buf[0] = 0; //unused
    output.buf[1] = 0; //unused
    output.buf[2] = 0; //unused
    output.buf[3] = 0; //unused
    output.buf[4] = 0; //unused
    output.buf[5] = 0; //unused
    output.buf[6] = 0; //unused
    output.buf[7] = 0; //unused

    attachedCANBus->sendFrame(output);
    Logger::debug("OB Charger cmd: %X %X %X %X %X %X %X %X %X ",output.id, output.buf[0],
                  output.buf[1],output.buf[2],output.buf[3],output.buf[4],output.buf[5],output.buf[6],output.buf[7]);
    crashHandler.addBreadcrumb(ENCODE_BREAD("OBC") + 1);
}

void OBChargerController::sendSleepModeCmd()
{
    OBChargerConfiguration *config = (OBChargerConfiguration *)getConfiguration();

    CAN_message_t output;
    output.len = 8;
    output.id = 0x261;
    output.flags.extended = 1;

    output.buf[0] = 0x80; //unused
    output.buf[1] = 0; //unused
    output.buf[2] = 0; //unused
    output.buf[3] = 0; //unused
    output.buf[4] = 0; //unused
    output.buf[5] = 0; //unused
    output.buf[6] = 0; //unused
    output.buf[7] = 0; //unused

    attachedCANBus->sendFrame(output);
    Logger::debug("OB Charger cmd: %X %X %X %X %X %X %X %X %X ",output.id, output.buf[0],
                  output.buf[1],output.buf[2],output.buf[3],output.buf[4],output.buf[5],output.buf[6],output.buf[7]);
    crashHandler.addBreadcrumb(ENCODE_BREAD("OBC") + 1);
}

void OBChargerController::sendHVOutputCmd()
{
    OBChargerConfiguration *config = (OBChargerConfiguration *)getConfiguration();

    CAN_message_t output;
    output.len = 8;
    output.id = 0x185;
    output.flags.extended = 1;

    uint16_t vOutput = config->targetUpperVoltage * 10;
    uint16_t cOutput = config->targetCurrentLimit * 10;

    output.buf[0] = 0; //unused
    output.buf[1] = 0; //unused
    output.buf[2] = 0; //unused
    output.buf[3] = 0; //unused
    output.buf[4] = 0x08;
    output.buf[5] = 0; //unused
    output.buf[6] = 0; //unused
    output.buf[7] = 0; //unused

    attachedCANBus->sendFrame(output);
    Logger::debug("OB Charger cmd: %X %X %X %X %X %X %X %X %X ",output.id, output.buf[0],
                  output.buf[1],output.buf[2],output.buf[3],output.buf[4],output.buf[5],output.buf[6],output.buf[7]);
    crashHandler.addBreadcrumb(ENCODE_BREAD("OBC") + 1);
}

DeviceId OBChargerController::getId() {
    return (OB_CHARGER);
}

uint32_t OBChargerController::getTickInterval()
{
    return CFG_TICK_INTERVAL_OB;
}

void OBChargerController::loadConfiguration() {
    OBChargerConfiguration *config = (OBChargerConfiguration *)getConfiguration();

    if (!config) {
        config = new OBChargerConfiguration();
        setConfiguration(config);
    }

    ChargeController::loadConfiguration(); // call parent

    prefsHandler->read("CanbusNum", &config->canbusNum, 1);
}

void OBChargerController::saveConfiguration() {
    OBChargerConfiguration *config = (OBChargerConfiguration *)getConfiguration();

    if (!config) {
        config = new OBChargerConfiguration();
        setConfiguration(config);
    }
    prefsHandler->write("CanbusNum", config->canbusNum);
    ChargeController::saveConfiguration();
}

OBChargerController OB;


int main()
{
    // vector<string> msg {"Hello", "C++", "World", "from", "VS Code", "and the C++ extension!"};

    // for (const string& word : msg)
    // {
    //     cout << word << " ";
    // }
    // cout << endl;
}