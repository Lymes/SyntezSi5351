//
// UR5FFR VFO
// Copyright (c) Andrey Belokon, 2016 Odessa 
// https://github.com/andrey-belokon/SyntezSi5351
//

// all setting changed in config.h file !
#include "config.h"

#include <avr/eeprom.h> 
#include "utils.h"
#include "Encoder.h"
#include "Keypad_I2C.h"
#include "TinyRTC.h"
#include "pins.h"
#include "TRX.h"
#ifdef DISPLAY_LCD_1602
  #include "disp_1602.h"
#endif
#ifdef DISPLAY_TFT_ILI9341
  #include "disp_ILI9341.h"
#endif
#include "si5351a.h"
#include "i2c.h"

KeypadI2C keypad(0x26);
Encoder encoder(360);
#ifdef DISPLAY_LCD_1602
  Display_1602_I2C disp(0x27);
#endif
#ifdef DISPLAY_TFT_ILI9341
  Display_ILI9341_SPI disp;
#endif
TRX trx;

long EEMEM Si5351_correction_EEMEM = 0;
long Si5351_correction;
Si5351 vfo;

int EEMEM SMeterMap_EEMEM[15] = {70,140,210,280,350,420,490,560,630,700,770,840,910,940,980};
int SMeterMap[15];

InputPullUpPin inTX(4);
InputPullUpPin inTune(5);
InputAnalogPin inRIT(A0,5);
InputAnalogPin inSMeter(A1);
OutputBinPin outTX(6,false,HIGH);
OutputBinPin outQRP(7,false,HIGH);
OutputTonePin outTone(8,1000);

OutputPCF8574 outBandCtrl(0x25,0);
// распиновка I2C расширителя:
// двоичный дешифратор диапазона - пины 0-3
const int pnBand0 = 0;
const int pnBand1 = 1;
const int pnBand2 = 2;
const int pnBand3 = 3;
// 5й пин - ATT, 6й пин - Preamp
const int pnAtt = 5;
const int pnPre = 6;

void setup()
{
  i2c_init();
  eeprom_read_block(&Si5351_correction, &Si5351_correction_EEMEM, sizeof(Si5351_correction));
  eeprom_read_block(SMeterMap, SMeterMap_EEMEM, sizeof(SMeterMap));
#ifdef CAT_ENABLE
  Serial.begin(COM_BAUND_RATE);
#endif    
  vfo.setup(1,0,0);
  vfo.set_xtal_freq(SI5351_XTAL_FREQ+Si5351_correction);
  encoder.setup();
  keypad.setup();
  inTX.setup();
  inTune.setup();
  inRIT.setup();
  outTX.setup();
  outQRP.setup();
  outTone.setup();
  disp.setup();
  trx.SwitchToBand(1);
}

void UpdateFreq() 
{

#ifdef MODE_DC
  vfo.set_freq(
    CLK0_MULT*(trx.state.VFO[trx.GetVFOIndex()] + (trx.RIT && !trx.TX ? trx.RIT_Value : 0)),
    0,
    0
  );
#endif

#ifdef MODE_DC_QUADRATURE
  vfo.set_freq_quadrature(
    trx.state.VFO[trx.GetVFOIndex()] + (trx.RIT && !trx.TX ? trx.RIT_Value : 0),
    0
  );
#endif

#ifdef MODE_SINGLE_IF
  #if defined(IFreq_USB) && defined(IFreq_LSB)
    vfo.set_freq( // инверсия боковой - гетеродин сверху
      CLK0_MULT*(trx.state.VFO[trx.GetVFOIndex()] + (trx.state.sideband == LSB ? IFreq_USB : IFreq_LSB) + (trx.RIT && !trx.TX ? trx.RIT_Value : 0)),
      CLK1_MULT*(trx.state.sideband == LSB ? IFreq_USB : IFreq_LSB),
      0
    );
  #elif defined(IFreq_USB)
    long f = trx.state.VFO[trx.GetVFOIndex()] + (trx.RIT && !trx.TX ? trx.RIT_Value : 0);
    if (trx.state.sideband == LSB) {
      f+=IFreq_USB;
    } else {
      f = abs(IFreq_USB-f);
    }
    vfo.set_freq(CLK0_MULT*f,CLK1_MULT*IFreq_USB,0);
  #elif defined(IFreq_LSB)
    long f = trx.state.VFO[trx.GetVFOIndex()] + (trx.RIT && !trx.TX ? trx.RIT_Value : 0);
    if (trx.state.sideband == USB) {
      f+=IFreq_LSB;
    } else {
      f = abs(IFreq_LSB-f);
    }
    vfo.set_freq(CLK0_MULT*f,CLK1_MULT*IFreq_LSB,0);
  #else
    #error You must define IFreq_LSB/IFreq_USB
  #endif 
#endif

#ifdef MODE_SINGLE_IF_RXTX
  #if defined(IFreq_USB) && defined(IFreq_LSB)
    long f = CLK1_MULT*(trx.state.sideband == LSB ? IFreq_USB : IFreq_LSB);
    vfo.set_freq( // инверсия боковой - гетеродин сверху
      CLK0_MULT*(trx.state.VFO[trx.GetVFOIndex()] + (trx.state.sideband == LSB ? IFreq_USB : IFreq_LSB) + (trx.RIT && !trx.TX ? trx.RIT_Value : 0)),
      trx.TX ? 0 : f,
      trx.TX ? f : 0
    );
  #elif defined(IFreq_USB)
    long f = trx.state.VFO[trx.GetVFOIndex()] + (trx.RIT && !trx.TX ? trx.RIT_Value : 0);
    if (trx.state.sideband == LSB) {
      f+=IFreq_USB;
    } else {
      f = abs(IFreq_USB-f);
    }
    vfo.set_freq(
      CLK0_MULT*f,
      trx.TX ? 0 : CLK1_MULT*IFreq_USB,
      trx.TX ? CLK1_MULT*IFreq_USB : 0
    );
  #elif defined(IFreq_LSB)
    long f = trx.state.VFO[trx.GetVFOIndex()] + (trx.RIT && !trx.TX ? trx.RIT_Value : 0);
    if (trx.state.sideband == USB) {
      f+=IFreq_LSB;
    } else {
      f = abs(IFreq_LSB-f);
    }
    vfo.set_freq(
      CLK0_MULT*f,
      trx.TX ? 0 : CLK1_MULT*IFreq_LSB,
      trx.TX ? CLK1_MULT*IFreq_LSB : 0
    );
  #else
    #error You must define IFreq_LSB/IFreq_USB
  #endif 
#endif

#ifdef MODE_SINGLE_IF_SWAP
  #if defined(IFreq_USB) && defined(IFreq_LSB)
    long vfo = CLK0_MULT*(trx.state.VFO[trx.GetVFOIndex()] + (trx.state.sideband == LSB ? IFreq_USB : IFreq_LSB) + (trx.RIT && !trx.TX ? trx.RIT_Value : 0));
    long f = CLK1_MULT*(trx.state.sideband == LSB ? IFreq_USB : IFreq_LSB);
    vfo.set_freq( // инверсия боковой - гетеродин сверху
      trx.TX ? f : vfo,
      trx.TX ? vfo : f,
      0
    );
  #elif defined(IFreq_USB)
    long f = trx.state.VFO[trx.GetVFOIndex()] + (trx.RIT && !trx.TX ? trx.RIT_Value : 0);
    if (trx.state.sideband == LSB) {
      f+=IFreq_USB;
    } else {
      f = abs(IFreq_USB-f);
    }
    vfo.set_freq(
      trx.TX ? CLK1_MULT*IFreq_USB : CLK0_MULT*f,
      trx.TX ? CLK0_MULT*f : CLK1_MULT*IFreq_USB,
      0
    );
  #elif defined(IFreq_LSB)
    long f = trx.state.VFO[trx.GetVFOIndex()] + (trx.RIT && !trx.TX ? trx.RIT_Value : 0);
    if (trx.state.sideband == USB) {
      f+=IFreq_LSB;
    } else {
      f = abs(IFreq_LSB-f);
    }
    vfo.set_freq(
      trx.TX ? CLK1_MULT*IFreq_LSB : CLK0_MULT*f,
      trx.TX ? CLK0_MULT*f : CLK1_MULT*IFreq_LSB,
      0
    );
  #else
    #error You must define IFreq_LSB/IFreq_USB
  #endif 
#endif

#ifdef MODE_DOUBLE_IF
  long IFreq = (trx.state.sideband == USB ? IFreq_USB : IFreq_LSB);
  vfo.set_freq(
    CLK0_MULT*(trx.state.VFO[trx.GetVFOIndex()] + IFreqEx + (trx.RIT && !trx.TX ? trx.RIT_Value : 0)),
    CLK1_MULT*(IFreqEx + IFreq),
    CLK2_MULT*(IFreq)
  );
#endif

#ifdef MODE_DOUBLE_IF_LSB
  vfo.set_freq(
    CLK0_MULT*(trx.state.VFO[trx.GetVFOIndex()] + IFreqEx + (trx.RIT && !trx.TX ? trx.RIT_Value : 0)),
    CLK1_MULT*(IFreqEx + (trx.state.sideband == LSB ? IFreq_LSB : -IFreq_LSB)),
    CLK2_MULT*(IFreq_LSB)
  );
#endif

#ifdef MODE_DOUBLE_IF_USB
  vfo.set_freq(
    CLK0_MULT*(trx.state.VFO[trx.GetVFOIndex()] + IFreqEx + (trx.RIT && !trx.TX ? trx.RIT_Value : 0)),
    CLK1_MULT*(IFreqEx + (trx.state.sideband == USB ? IFreq_USB : -IFreq_USB)),
    CLK2_MULT*(IFreq_USB)
  );
#endif

#ifdef MODE_DOUBLE_IF_SWAP23
  long IFreq = (trx.state.sideband == USB ? IFreq_USB : IFreq_LSB); 
  long f1 = CLK0_MULT*(trx.state.VFO[trx.GetVFOIndex()] + IFreqEx + (trx.RIT && !trx.TX ? trx.RIT_Value : 0));
  long f2 = CLK1_MULT*(IFreqEx + IFreq);
  long f3 = CLK2_MULT*(IFreq);
  vfo.set_freq(
    f1,
    trx.TX ? f3 : f2,
    trx.TX ? f2 : f3
  );
#endif

#ifdef MODE_DOUBLE_IF_LSB_SWAP23
  long f1 = CLK0_MULT*(trx.state.VFO[trx.GetVFOIndex()] + IFreqEx + (trx.RIT && !trx.TX ? trx.RIT_Value : 0));
  long f2 = CLK1_MULT*(IFreq + (trx.state.sideband == LSB ? IFreq_LSB : -IFreq_LSB));
  long f3 = CLK2_MULT*(IFreq_LSB);
  vfo.set_freq(
    f1,
    trx.TX ? f3 : f2,
    trx.TX ? f2 : f3
  );
#endif

#ifdef MODE_DOUBLE_IF_USB_SWAP23
  long f1 = CLK0_MULT*(trx.state.VFO[trx.GetVFOIndex()] + IFreqEx + (trx.RIT && !trx.TX ? trx.RIT_Value : 0));
  long f2 = CLK1_MULT*(IFreq + (trx.state.sideband == USB ? IFreq_USB : -IFreq_USB));
  long f3 = CLK2_MULT*(IFreq_USB);
  vfo.set_freq(
    f1,
    trx.TX ? f3 : f2,
    trx.TX ? f2 : f3
  );
#endif
}

void UpdateBandCtrl() 
{
  // 0-nothing; 1-ATT; 2-Preamp
  switch (trx.state.AttPre) {
    case 0:
      outBandCtrl.Set(pnAtt,false);
      outBandCtrl.Set(pnPre,false);
      break;
    case 1:
      outBandCtrl.Set(pnAtt,true);
      outBandCtrl.Set(pnPre,false);
      break;
    case 2:
      outBandCtrl.Set(pnAtt,false);
      outBandCtrl.Set(pnPre,true);
      break;
  }
  outBandCtrl.Set(pnBand0, trx.BandIndex & 0x1);
  outBandCtrl.Set(pnBand1, trx.BandIndex & 0x2);
  outBandCtrl.Set(pnBand2, trx.BandIndex & 0x4);
  outBandCtrl.Set(pnBand3, trx.BandIndex & 0x8);
  outBandCtrl.Write();
}

#ifdef CAT_ENABLE
  #include "CAT.h"
#endif    

#include "menu.h"

void loop()
{
  static long menu_tm = -1;
  bool tune = inTune.Read();
  trx.TX = tune || inTX.Read();
  trx.ChangeFreq(encoder.GetDelta());
  int keycode;
  if ((keycode=keypad.GetLastCode()) >= 0) {
    if (menu_tm >= 0 && millis()-menu_tm >= MENU_DELAY) {
      if (KeyMap[keycode & 0xF][keycode >> 4] == cmdLock) {
        // отменяем команду
        trx.ExecCommand(cmdLock);
        // call to menu
        ShowMenu();
        // перерисовываем дисплей
        disp.clear();
        disp.reset();
        disp.Draw(trx);
        menu_tm = -1;
        return;
      } else if (KeyMap[keycode & 0xF][keycode >> 4] == cmdVFOSel) {
        trx.ExecCommand(cmdVFOSel);
        trx.ExecCommand(cmdVFOEQ);
        trx.ExecCommand(cmdVFOSel);
        menu_tm = -1;
        return;
      }
    }
  } else {
    menu_tm = -1; 
  }
  if ((keycode=keypad.Read()) >= 0) {
    uint8_t cmd=KeyMap[keycode & 0xF][keycode >> 4];
    if (cmd == cmdLock || cmd == cmdVFOSel) {
      // длительное нажатие MENU_KEY - вызов меню
      // длительное нажатие cmdVFOSel - A=B
      if (menu_tm < 0) {
        menu_tm = millis();
      }
    }
    trx.ExecCommand(cmd);
  }
  if (trx.RIT)
    trx.RIT_Value = (long)inRIT.ReadRaw()*2*RIT_MAX_VALUE/1024-RIT_MAX_VALUE;
  UpdateFreq();
  outQRP.Write(trx.QRP || tune);
  outTone.Write(tune);
  outTX.Write(trx.TX);
  UpdateBandCtrl();
  // read and convert smeter
  int val = inSMeter.Read();
  trx.SMeter =  0;
  for (byte i=14; i >= 0; i--) {
    if (val > SMeterMap[i]) {
      trx.SMeter =  i+1;
      break;
    }
  }
  // refresh display
  disp.Draw(trx);
#ifdef CAT_ENABLE
  // CAT
  if (Serial.available() > 0)
    ExecCAT();
#endif    
}
