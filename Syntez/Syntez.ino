//
// Синтезатор UR5FFR
// Copyright (c) Andrey Belokon, 2016 Odessa 
// https://github.com/andrey-belokon/SyntezSi5351
//

// раскоментарить тип используемого дисплея (только один!)
//#define DISPLAY_LCD_1602
#define DISPLAY_TFT_ILI9341

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

#define CAT_ENABLE
#define COM_BAUND_RATE  9600      // скорость обмена COM-порта
#define RIT_MAX_VALUE   1200      // максимальная расстройка
#define MENU_DELAY      1000      // задержка вызова меню в сек

// мапинг сканкодов на команды
const uint8_t KeyMap[4][4] = {
  cmdBandUp, cmdBandDown, cmdAttPre, cmdVFOSel,
  cmdVFOEQ,  cmdUSBLSB,   cmdLock,   cmdSplit,
  cmdRIT,    cmdHam,      cmdZero,   cmdQRP,
  cmdNone,   cmdNone,     cmdNone,   cmdNone
};

KeypadI2C keypad(0x26);
Encoder encoder(360);
#ifdef DISPLAY_LCD_1602
  Display_1602_I2C disp(0x27);
#endif
#ifdef DISPLAY_TFT_ILI9341
  Display_ILI9341_SPI disp;
#endif
TRX trx;

#define SI5351_XTAL_FREQ         270000000  // 0.1Hz resolution (10x mutiplier)
#define SI5351_CALIBRATION_FREQ  30000000   // частота на которой проводится калибровка
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

// необходимо раскоментировать требуемую моду (только одну!)

//*****************************************************************************************//

// режим прямого преобразования. частота формируется на 1ом выводе. установить
// CLK0_MULT в значение 1/2/4 в зависимости от коэффициента деления частоты гетеродина
// второй и третий гетеродины отключены
//#define MODE_DC

// режим прямого преобразования с формированием квадратурн
// частота формируется на выводах CLK0,CLK1 со сдвигом фаз 90град
// CLK2 отключен. Минимальная частота настройки 2MHz (по даташиту 4MHz) может зависеть от экземпляра Si5351
//#define MODE_DC_QUADRATURE

//*****************************************************************************************//

// одна промежуточная частота. требуемая боковая формируется на счет переключения
// первого гетеродина с инверсией боковой либо без инверсии. второй гетеродин формируется на выходе CLK1
// тип КФ зависит от параметров IFreq_LSB/IFreq_USB. если фильтр симметричный (определены две частоты IFreq)
// то частота первого гетеродина всегда сверху (меньше пораженок) а боковая выбирается изменением частоты второго гетеродина
#define MODE_SINGLE_IF

// аналогично MODE_SINGLE_IF но второй гетеродин генерируется на CLK1 при RX и
// на CLK2 в режиме TX
//#define MODE_SINGLE_IF_RXTX

// аналогично MODE_SINGLE_IF_VFOSB но в режиме передачи гетеродины комутируются,
// тоесть первый формируется на CLK1, а второй - на CLK0
// для трактов где необходимо переключение гетеродинов при смене RX/TX
//#define MODE_SINGLE_IF_SWAP

//*****************************************************************************************//

// две промежуточные частоты. гетеродины формируются 1й - CLK0, 2й - CLK1, 3й - CLK2
// первый гетеродин всегда "сверху". выбор боковой полосы производится сменой частоты
// второго гетеродина для режимов MODE_DOUBLE_IF_USB/LSB, или сменой частоты третьего гетеродина MODE_DOUBLE_IF
// в режиме MODE_DOUBLE_IF второй гетеродин выше первой ПЧ
//#define MODE_DOUBLE_IF
//#define MODE_DOUBLE_IF_USB
//#define MODE_DOUBLE_IF_LSB

// режим аналогичен MODE_DOUBLE_IF но в режиме передачи 2й и 3й гетеродины комутируются,
// тоесть второй формируется на CLK2, а третий - на CLK1
// для трактов с двумя промежуточными частотами где необходимо переключение
// гетеродинов при смене RX/TX
//#define MODE_DOUBLE_IF_SWAP23
//#define MODE_DOUBLE_IF_USB_SWAP23
//#define MODE_DOUBLE_IF_LSB_SWAP23

// множители частоты на выходах. в случае необходимости получения на выводе 2/4 кратной
// частоты установить в соответствующее значение
const long CLK0_MULT = 1;
const long CLK1_MULT = 1;
const long CLK2_MULT = 1;

// следующие дефайны определяют какой у нас фильтр - нижняя либо верхняя боковая
// они задают частоту второго (или третьего) гетеродина
// если фильтр имеет симметричные скаты (например мостовой) либо высокое подавоение 
// по обеим скатам то раскоментарить и определить оба дефайна
//#define IFreq_USB   9827900L
#define IFreq_LSB   9831150L

// первая промежуточная частота для трактов с двойным преобразованием частоты
const long IFreqEx = 45000000;

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
    CLK1_MULT*(IFreq + (trx.state.sideband == LSB ? IFreq_LSB : -IFreq_LSB)),
    CLK2_MULT*(IFreq_LSB)
  );
#endif

#ifdef MODE_DOUBLE_IF_USB
  vfo.set_freq(
    CLK0_MULT*(trx.state.VFO[trx.GetVFOIndex()] + IFreqEx + (trx.RIT && !trx.TX ? trx.RIT_Value : 0)),
    CLK1_MULT*(IFreq + (trx.state.sideband == USB ? IFreq_USB : -IFreq_USB)),
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

#define CAT_BUF_SIZE  40
char CAT_buf[CAT_BUF_SIZE];
uint8_t CAT_buf_idx = 0;

void ExecCAT()
{
  int b;
  while ((b = Serial.read()) >= 0) {
    if (CAT_buf_idx >= CAT_BUF_SIZE) CAT_buf_idx = 0;
    CAT_buf[CAT_buf_idx++] = (uint8_t)b;
    if (b == ';') {
      // parse command
      if (CAT_buf[0] == 'I' && CAT_buf[1] == 'F') {
          ltoazp(CAT_buf+2,trx.state.VFO[trx.state.VFO_Index],11);
          memset(CAT_buf+13, ' ', 5);
          if (trx.RIT) {
            ltoazp(CAT_buf+18,trx.RIT_Value,5);
            CAT_buf[23] = '1';
          } else {
            memset(CAT_buf+18, '0', 6);
          }
          memset(CAT_buf+24, '0', 4);
          CAT_buf[28] = '0' + (trx.TX & 1);
          CAT_buf[29] = '1' + trx.state.sideband;
          CAT_buf[30] = '0' + trx.state.VFO_Index;
          CAT_buf[31] = '0';
          CAT_buf[32] = '0' + (trx.state.Split & 1);
          memset(CAT_buf+33, '0', 3);
          CAT_buf[36] = ' ';
          CAT_buf[37] = ';';
          CAT_buf[38] = 0;
          Serial.write(CAT_buf);
      } else if (CAT_buf[0] == 'F' && (CAT_buf[1] == 'A' || CAT_buf[1] == 'B')) {
        uint8_t i = CAT_buf[1]-'A';
        if (CAT_buf[2] == ';') {
          ltoazp(CAT_buf+2,trx.state.VFO[i],11);
          CAT_buf[13] = ';';
          CAT_buf[14] = 0;
          Serial.write(CAT_buf);
        } else {
          long freq = atoln(CAT_buf+2,11);
          if (trx.BandIndex < 0) {
            trx.state.VFO[i] = freq;
          } else {
            trx.ExecCommand(cmdHam);
            trx.state.VFO[i] = freq;
            trx.ExecCommand(cmdHam);
          }
        }
      } else if (CAT_buf[0] == 'M' && CAT_buf[1] == 'D') {
        if (CAT_buf[2] == ';') {
          CAT_buf[2] = '1' + trx.state.sideband;
          CAT_buf[3] = ';';
          CAT_buf[4] = 0;
          Serial.write(CAT_buf);
        } else if (CAT_buf[2] == '1' || CAT_buf[2] == '2') {
          uint8_t i = CAT_buf[2]-'1';
          if (i != trx.state.sideband) trx.ExecCommand(cmdUSBLSB);
        } 
      } else if (CAT_buf[0] == 'B' && CAT_buf[1] == 'D') {
        trx.ExecCommand(cmdBandDown);
      } else if (CAT_buf[0] == 'B' && CAT_buf[1] == 'U') {
        trx.ExecCommand(cmdBandUp);
      } else if (CAT_buf[0] == 'V' && CAT_buf[1] == 'V') {
        trx.ExecCommand(cmdVFOEQ);
      } else {
        Serial.write("?;");
      }
      CAT_buf_idx = 0;
    }
  }
}
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
    if (KeyMap[keycode & 0xF][keycode >> 4] == cmdLock && menu_tm >= 0 && millis()-menu_tm >= MENU_DELAY) {
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
    }
  } else {
    menu_tm = -1; 
  }
  if ((keycode=keypad.Read()) >= 0) {
    uint8_t cmd=KeyMap[keycode & 0xF][keycode >> 4];
    if (cmd == cmdLock) {
      // длительное нажатие MENU_KEY - вызов меню
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
