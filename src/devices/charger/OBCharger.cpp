#include "OBCharger.h"

// Certain commands need to be done every 100 miliseconds but we tick at a rate of 10
int tickCount = 0;

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
    attachedCANBus->attach(this, 0x318, 0x7f8, false);
    attachedCANBus->attach(this, 0x348, 0x7f8, false);

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
    if (frame.id == 0x318 || frame.id == 0x348)
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
    
    // sendWakeUpCmd();
    if (++tickCount % 10 == 0)
    {
        sendSleepModeCmd();
        sendHVOutputCmd();
        tickCount = 0;
    }
    sendHVOutputCmd();
}

void OBChargerController::sendPowerCmd()
{
    OBChargerConfiguration *config = (OBChargerConfiguration *)getConfiguration();

    CAN_message_t output;
    output.len = 8;
    output.id = 0x305;
    output.flags.extended = 1;

    uint16_t vOutput = config->targetUpperVoltage * 10;
    uint16_t cOutput = config->targetCurrentLimit * 10;

    output.buf[0] = (vOutput >> 8); //Voltage
    output.buf[1] = (vOutput & 0xFF); //Voltage
    output.buf[2] = (cOutput >> 8); //Alleged Current
    output.buf[3] = (cOutput & 0xFF); //Alleged Current
    output.buf[4] = 3; //Alleged maximum allowed power
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

    uint8_t sleep = 0;
    if(SLEEP_SETTING)
    {
        sleep = 0x80;
    }

    output.buf[0] = sleep; //unused
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

    uint8_t hv = 0;
    if(HV_SETTING)
    {
        hv = 8;
    }

    output.buf[0] = 0; //unused
    output.buf[1] = 0; //unused
    output.buf[2] = 0; //unused
    output.buf[3] = 0; //unused
    output.buf[4] = hv; 
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