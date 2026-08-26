/*
 * Independent relay controller for ATtiny1626 / megaTinyCore.
 *
 * ATtiny1626 resources deliberately used here:
 * - hardware watchdog
 * - brown-out/power-on reset support
 * - 256-byte EEPROM for persistent relay state
 * - second USART remains available for future diagnostics
 * - 12-bit ADC/PGA remains available for future low-voltage health sensing
 *
 * Pin plan:
 *   PA1..PA5 : five physical switches, active LOW
 *   PA6,PA7,PB4,PB5,PC0 : five relay-driver outputs
 *   PB2 : USART0 TX -> ESP32 RX
 *   PB3 : USART0 RX <- ESP32 TX
 *   PA0 : reserved for UPDI/RESET
 *
 * This firmware controls only the low-voltage relay-driver side.
 */

#include <Arduino.h>
#include <EEPROM.h>
#include <avr/wdt.h>

static constexpr uint8_t RELAY_COUNT=5;
static constexpr uint8_t RELAY_ACTIVE_LEVEL=HIGH;
static constexpr uint8_t SWITCH_ACTIVE_LEVEL=LOW;
static constexpr uint32_t UART_BAUD=115200;
static constexpr uint32_t DEBOUNCE_MS=25;
static constexpr uint32_t STATE_REPORT_MS=2000;
static constexpr uint32_t EEPROM_COMMIT_DELAY_MS=150;

static const uint8_t relayPins[RELAY_COUNT]={
  PIN_PA6,PIN_PA7,PIN_PB4,PIN_PB5,PIN_PC0
};
static const uint8_t switchPins[RELAY_COUNT]={
  PIN_PA1,PIN_PA2,PIN_PA3,PIN_PA4,PIN_PA5
};

static constexpr uint8_t SOF=0xA5;
static constexpr uint8_t CMD_SET=0x10;
static constexpr uint8_t CMD_GET=0x11;
static constexpr uint8_t CMD_PING=0x12;
static constexpr uint8_t CMD_STATE=0x20;
static constexpr uint8_t CMD_SWITCH=0x21;
static constexpr uint8_t CMD_HELLO=0x30;
static constexpr uint8_t FRAME_LEN=7;

struct Frame { uint8_t sof,cmd,index,value,seq,crcLo,crcHi; };

static bool relayState[RELAY_COUNT]={0,0,0,0,0};
static bool stableSwitch[RELAY_COUNT]={0,0,0,0,0};
static bool rawSwitch[RELAY_COUNT]={0,0,0,0,0};
static uint32_t switchChangedAt[RELAY_COUNT]={0,0,0,0,0};
static uint8_t txSeq=1;
static uint32_t lastReport=0;
static bool eepromDirty=false;
static uint32_t eepromDirtyAt=0;
static uint8_t eepromSeq=0;

struct EepromRecord { uint8_t magic,seq,state,crc; };
static constexpr uint8_t EEPROM_MAGIC=0xD3;
static constexpr uint8_t EEPROM_SLOT0=0;
static constexpr uint8_t EEPROM_SLOT1=4;

static uint16_t crc16(const uint8_t *d,size_t n){
  uint16_t c=0xFFFF;
  for(size_t i=0;i<n;i++){ c^=d[i]; for(uint8_t b=0;b<8;b++)
    c=(c&1U)?(uint16_t)((c>>1)^0xA001U):(uint16_t)(c>>1); }
  return c;
}
static uint8_t crc8(const uint8_t *d,size_t n){
  uint8_t c=0xA7;
  for(size_t i=0;i<n;i++){ c^=d[i]; for(uint8_t b=0;b<8;b++)
    c=(c&0x80U)?(uint8_t)((c<<1)^0x1DU):(uint8_t)(c<<1); }
  return c;
}
static bool validRecord(const EepromRecord&r){
  return r.magic==EEPROM_MAGIC && crc8((const uint8_t*)&r,3)==r.crc;
}
static bool seqNewer(uint8_t a,uint8_t b){
  return a!=b && (uint8_t)(a-b)<128U;
}

static void saveState(){
  uint8_t mask=0;
  for(uint8_t i=0;i<RELAY_COUNT;i++) if(relayState[i]) mask|=(uint8_t)(1U<<i);
  EepromRecord r{EEPROM_MAGIC,(uint8_t)(eepromSeq+1U),mask,0};
  r.crc=crc8((const uint8_t*)&r,3);
  uint8_t slot=(eepromSeq&1U)?EEPROM_SLOT1:EEPROM_SLOT0;
  EEPROM.update(slot+0,r.magic); EEPROM.update(slot+1,r.seq);
  EEPROM.update(slot+2,r.state); EEPROM.update(slot+3,r.crc);
  eepromSeq=r.seq; eepromDirty=false;
}
static void loadState(){
  EepromRecord a{},b{};
  for(uint8_t i=0;i<sizeof(EepromRecord);i++){
    ((uint8_t*)&a)[i]=EEPROM.read(EEPROM_SLOT0+i);
    ((uint8_t*)&b)[i]=EEPROM.read(EEPROM_SLOT1+i);
  }
  bool va=validRecord(a),vb=validRecord(b);
  const EepromRecord*best=nullptr;
  if(va&&vb) best=seqNewer(b.seq,a.seq)?&b:&a;
  else if(va) best=&a; else if(vb) best=&b;
  uint8_t mask=best?best->state:0;
  eepromSeq=best?best->seq:0;
  for(uint8_t i=0;i<RELAY_COUNT;i++) relayState[i]=(mask&(1U<<i))!=0;
}
static void writeRelay(uint8_t i,bool state){
  if(i>=RELAY_COUNT || relayState[i]==state) return;
  relayState[i]=state;
  digitalWrite(relayPins[i],state?RELAY_ACTIVE_LEVEL:
               (RELAY_ACTIVE_LEVEL==HIGH?LOW:HIGH));
  eepromDirty=true; eepromDirtyAt=millis();
}
static void sendFrame(uint8_t cmd,uint8_t index,uint8_t value){
  uint8_t f[FRAME_LEN]={SOF,cmd,index,value,txSeq++,0,0};
  uint16_t c=crc16(&f[1],4); f[5]=(uint8_t)c; f[6]=(uint8_t)(c>>8);
  Serial.write(f,sizeof(f));
}
static void sendAllStates(){
  for(uint8_t i=0;i<RELAY_COUNT;i++)
    sendFrame(CMD_STATE,i,relayState[i]?1:0);
}
static void processFrame(const Frame&f){
  if(f.cmd==CMD_SET && f.index<RELAY_COUNT){
    writeRelay(f.index,f.value!=0);
    sendFrame(CMD_STATE,f.index,relayState[f.index]?1:0);
  } else if(f.cmd==CMD_GET){
    sendAllStates();
  } else if(f.cmd==CMD_PING || f.cmd==CMD_HELLO){
    sendFrame(CMD_HELLO,0,1); sendAllStates();
  }
}
static void serviceUart(){
  static uint8_t buf[FRAME_LEN]; static uint8_t pos=0;
  while(Serial.available()){
    uint8_t b=(uint8_t)Serial.read();
    if(pos==0 && b!=SOF) continue;
    buf[pos++]=b;
    if(pos<FRAME_LEN) continue;
    pos=0;
    uint16_t got=(uint16_t)buf[5]|((uint16_t)buf[6]<<8);
    if(crc16(&buf[1],4)!=got) continue;
    Frame f; memcpy(&f,buf,sizeof(f)); processFrame(f);
  }
}
static void serviceSwitches(){
  uint32_t now=millis();
  for(uint8_t i=0;i<RELAY_COUNT;i++){
    bool raw=digitalRead(switchPins[i])==SWITCH_ACTIVE_LEVEL;
    if(raw!=rawSwitch[i]){rawSwitch[i]=raw;switchChangedAt[i]=now;}
    if(rawSwitch[i]!=stableSwitch[i] &&
       (uint32_t)(now-switchChangedAt[i])>=DEBOUNCE_MS){
      stableSwitch[i]=rawSwitch[i];
      writeRelay(i,stableSwitch[i]);
      sendFrame(CMD_SWITCH,i,relayState[i]?1:0);
    }
  }
}
static void initOutputsSafe(){
  for(uint8_t i=0;i<RELAY_COUNT;i++){
    digitalWrite(relayPins[i],RELAY_ACTIVE_LEVEL==HIGH?LOW:HIGH);
    pinMode(relayPins[i],OUTPUT);
  }
}
static void initInputs(){
  for(uint8_t i=0;i<RELAY_COUNT;i++){
    pinMode(switchPins[i],INPUT_PULLUP);
    rawSwitch[i]=stableSwitch[i]=
      digitalRead(switchPins[i])==SWITCH_ACTIVE_LEVEL;
  }
}
void setup(){
  initOutputsSafe(); initInputs(); loadState();
  for(uint8_t i=0;i<RELAY_COUNT;i++)
    digitalWrite(relayPins[i],relayState[i]?RELAY_ACTIVE_LEVEL:
                 (RELAY_ACTIVE_LEVEL==HIGH?LOW:HIGH));
  Serial.begin(UART_BAUD);
  wdt_enable(WDTO_2S);
  delay(10); sendFrame(CMD_HELLO,0,1); sendAllStates();
}
void loop(){
  wdt_reset(); serviceUart(); serviceSwitches();
  uint32_t now=millis();
  if((uint32_t)(now-lastReport)>=STATE_REPORT_MS){
    lastReport=now; sendAllStates();
  }
  if(eepromDirty &&
     (uint32_t)(now-eepromDirtyAt)>=EEPROM_COMMIT_DELAY_MS) saveState();
  delay(1);
}
