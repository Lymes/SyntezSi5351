#include "disp_1602.h"

byte chInverseT[8] = { 0b11111, 0b11111, 0b11111, 0b10001, 0b11011, 0b11011, 0b11111, 0b11111};
byte chInverseX[8] = { 0b11111, 0b11111, 0b11111, 0b10101, 0b11011, 0b10101, 0b11111, 0b11111};
byte chLock[8] = { 0b00000, 0b01110, 0b10001, 0b11111, 0b10101, 0b10001, 0b11111, 0b00000};
byte chCW[8] = { 0b01100, 0b10000, 0b10000, 0b01100, 0b00000, 0b10001, 0b10101, 0b01010};

byte SMetr11[8] = {0b00000,0b00000,0b00000,0b00000,0b00000,0b00000,0b11000,0b11000};
byte SMetr12[8] = {0b00000,0b00000,0b00000,0b00000,0b00011,0b00011,0b11011,0b11011};
byte SMetr21[8] = {0b00000,0b00000,0b00000,0b11000,0b11000,0b11000,0b11000,0b11000};
byte SMetr22[8] = {0b00000,0b00000,0b00011,0b11011,0b11011,0b11011,0b11011,0b11011};
byte SMetr31[8] = {0b11000,0b11000,0b11000,0b11000,0b11000,0b11000,0b11000,0b11000};

void Display_1602_I2C::setup() {
  lcd.init();
  lcd.backlight();// Включаем подсветку дисплея
  lcd.createChar(1, SMetr11);
  lcd.createChar(2, SMetr12);
  lcd.createChar(3, SMetr21);
  lcd.createChar(4, SMetr22);
  lcd.createChar(5, SMetr31); 
  lcd.createChar(6, chCW);
  lcd.createChar(7, chLock);
}

void Display_1602_I2C::Draw(TRX& trx) {
  char buf[2][17];
  int vfo_idx = trx.GetVFOIndex();
  bool freq_cw = trx.BandIndex >= 0 && Bands[trx.BandIndex].startSSB > 0 &&
    trx.state.VFO[vfo_idx] < Bands[trx.BandIndex].startSSB &&
    trx.state.VFO[vfo_idx] >= Bands[trx.BandIndex].start;
  bool wrong_sb = trx.BandIndex >= 0 && Bands[trx.BandIndex].sideband != trx.state.sideband;
  long f = (trx.state.VFO[vfo_idx]+50) / 100;

  memset(buf,' ',34);

  if (tx != trx.TX) {
    tx = trx.TX;
    // динамически переопределяем коды т.к. их всего 8 user defined
    if (tx) {
      lcd.createChar(6, chInverseT);
      lcd.createChar(7, chInverseX);
    } else {
      lcd.createChar(6, chCW);
      lcd.createChar(7, chLock);
    }
  }

  if (trx.TX) {
    buf[0][0] = (char)6;
    buf[0][1] = (char)7;
  } else {
    if (freq_cw)
      buf[0][0] = (char)6;
    if (trx.Lock)
      buf[0][1] = (char)7;
  }

  if (trx.QRP)
    buf[0][2] = (char)0b01011100;

  buf[0][13] = '0'+f%10; f/=10;
  buf[0][12] = '.';
  buf[0][11] = '0'+f%10; f/=10;
  buf[0][10] = '0'+f%10; f/=10;
  buf[0][9] = '0'+f%10; f/=10;
  buf[0][8] = '.';
  buf[0][7] = '0'+f%10; f/=10;
  if (f > 0) buf[0][6] = '0'+f;

  if (vfo_idx == 0) buf[0][15] = 'A';
  else buf[0][15] = 'B';

  switch (trx.state.AttPre) {
    case 1:
      buf[1][5] = 'A';
	  buf[1][6] = 'T';
      buf[1][7] = 'T';
      break;
    case 2:
      buf[1][5] = 'P';
      buf[1][6] = 'R';
      buf[1][7] = 'E';
      break;
  }

  if (!wrong_sb || ((millis() / 700) & 1)) {
    buf[1][10] = 'S';
    buf[1][11] = 'B';
    if (trx.state.sideband == LSB) buf[1][9]  = 'L';
    else buf[1][9]  = 'U';
  }

  if (trx.state.Split || trx.RIT) {
    if ((millis() / 700) & 1) {
      if (trx.state.Split) {
        buf[1][13] = 'S';
        buf[1][14] = 'P';
        buf[1][15] = 'L';
      }
    } else {
      if (trx.RIT) {
        buf[1][13] = 'R';
        buf[1][14] = 'I';
        buf[1][15] = 'T';
      }
    }
  }

  if (trx.BandIndex >= 0) {
    int mc = Bands[trx.BandIndex].mc;
    buf[1][3] = 'm';
    buf[1][2] = '0'+mc%10; mc/=10;
    buf[1][1] = '0'+mc%10; mc/=10;
    if (mc > 0) buf[1][0] = '0'+mc;
  }

  // S-meter
  if (trx.SMeter >= 9) {
    buf[0][3] = (char)2;
    buf[0][4] = (char)4;
    buf[0][5] = (char)5;
  } else if (trx.SMeter > 3) {
    switch (trx.SMeter) {
    	case 6:
    	  buf[0][3] = (char)2;
    	  break;
    	case 7:
    	  buf[0][3] = (char)2;
    	  buf[0][4] = (char)3;
    	  break;
    	case 8:
    	  buf[0][3] = (char)2;
    	  buf[0][4] = (char)4;
    	  break;
    	case 4:
    	case 5:
        buf[0][3] = (char)1;
        break;
    }
  }

  buf[0][16] = 0; // stop for .print
  buf[1][16] = 0; // stop for .print
  lcd.setCursor(0, 0);
  lcd.print(buf[0]);
  lcd.setCursor(0, 1);
  lcd.print(buf[1]);
}

void Display_1602_I2C::DrawMenu(const char* title, const char** items, byte selected, const char* help, byte fontsize)
{
  char buf[2][17];

  memset(buf,' ',34);
  strncpy(buf[0],title,16);
  sprintf(buf[1],">%s",items[selected]);
  // supress null writed by sprintf
  for (int i=2;i < 16;i++) {
    if (buf[1][i] == 0) buf[1][i]=' ';
  }
  buf[0][16] = 0; // stop for .print
  buf[1][16] = 0; // stop for .print
  lcd.setCursor(0, 0);
  lcd.print(buf[0]);
  lcd.setCursor(0, 1);
  lcd.print(buf[1]);
}

void Display_1602_I2C::DrawCalibration(const char* title, long value, bool hi_res, const char* help = NULL)
{
  char buf[2][17];

  memset(buf,' ',34);
  strncpy(buf[0],title,16);
  if (hi_res)
    buf[1][0] = '*';
  sprintf(buf[1]+2,"CORR: %ld",value);
  // supress null writed by sprintf
  for (byte i=2;i < 16;i++) {
    if (buf[1][i] == 0) buf[1][i]=' ';
  }

  buf[0][16] = 0; // stop for .print
  buf[1][16] = 0; // stop for .print
  lcd.setCursor(0, 0);
  lcd.print(buf[0]);
  lcd.setCursor(0, 1);
  lcd.print(buf[1]);
}

