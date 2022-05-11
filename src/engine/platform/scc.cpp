/**
 * Furnace Tracker - multi-system chiptune tracker
 * Copyright (C) 2021-2022 tildearrow and contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "scc.h"
#include "../engine.h"
#include <math.h>

#define CHIP_DIVIDER 32

#define rWrite(a,v) {if (!skipRegisterWrites) {scc->scc_w(true,a,v); regPool[a]=v; if (dumpWrites) addWrite(a,v); }}

const char* regCheatSheetSCC[]={
  "Ch1_Wave", "00",
  "Ch2_Wave", "20",
  "Ch3_Wave", "40",
  "Ch4_5_Wave", "60",
  "Ch1_FreqL", "80",
  "Ch1_FreqH", "81",
  "Ch2_FreqL", "82",
  "Ch2_FreqH", "83",
  "Ch3_FreqL", "84",
  "Ch3_FreqH", "85",
  "Ch4_FreqL", "86",
  "Ch4_FreqH", "87",
  "Ch5_FreqL", "88",
  "Ch5_FreqH", "89",
  "Ch1_Vol", "8a",
  "Ch2_Vol", "8b",
  "Ch3_Vol", "8c",
  "Ch4_Vol", "8d",
  "Ch5_Vol", "8e",
  "Output", "8f",
  "Test", "e0",
  NULL
};

const char* regCheatSheetSCCPlus[]={
  "Ch1_Wave", "00",
  "Ch2_Wave", "20",
  "Ch3_Wave", "40",
  "Ch4_Wave", "60",
  "Ch5_Wave", "80",
  "Ch1_FreqL", "a0",
  "Ch1_FreqH", "a1",
  "Ch2_FreqL", "a2",
  "Ch2_FreqH", "a3",
  "Ch3_FreqL", "a4",
  "Ch3_FreqH", "a5",
  "Ch4_FreqL", "a6",
  "Ch4_FreqH", "a7",
  "Ch5_FreqL", "a8",
  "Ch5_FreqH", "a9",
  "Ch1_Vol", "aa",
  "Ch2_Vol", "ab",
  "Ch3_Vol", "ac",
  "Ch4_Vol", "ad",
  "Ch5_Vol", "ae",
  "Output", "af",
  "Test", "c0",
  NULL
};

const char** DivPlatformSCC::getRegisterSheet() {
  return isPlus ? regCheatSheetSCCPlus : regCheatSheetSCC;
}

const char* DivPlatformSCC::getEffectName(unsigned char effect) {
  switch (effect) {
    case 0x10:
      return "10xx: Change waveform";
      break;
  }
  return NULL;
}

void DivPlatformSCC::acquire(short* bufL, short* bufR, size_t start, size_t len) {
  for (size_t h=start; h<start+len; h++) {
    for (int i=0; i<16; i++) {
      scc->tick();
    }
    short out=(short)scc->out()<<5;
    bufL[h]=bufR[h]=out;

    for (int i=0; i<5; i++) {
      oscBuf[i]->data[oscBuf[i]->needle++]=scc->chan_out(i)<<7;
    }
  }
}

void DivPlatformSCC::updateWave(int ch) {
  int dstCh=(!isPlus && ch>=4)?3:ch;
  for (int i=0; i<32; i++) {
    rWrite(dstCh*32+i,(unsigned char)chan[ch].ws.output[i]-128);
  }
}

void DivPlatformSCC::tick(bool sysTick) {
  for (int i=0; i<5; i++) {
    chan[i].std.next();
    if (chan[i].std.vol.had) {
      chan[i].outVol=((chan[i].vol&15)*MIN(15,chan[i].std.vol.val))/15;
      rWrite(regBase+10+i,chan[i].outVol);
    }
    if (chan[i].std.arp.had) {
      if (!chan[i].inPorta) {
        if (chan[i].std.arp.mode) {
          chan[i].baseFreq=NOTE_PERIODIC(chan[i].std.arp.val);
        } else {
          chan[i].baseFreq=NOTE_PERIODIC(chan[i].note+chan[i].std.arp.val);
        }
      }
      chan[i].freqChanged=true;
    } else {
      if (chan[i].std.arp.mode && chan[i].std.arp.finished) {
        chan[i].baseFreq=NOTE_PERIODIC(chan[i].note);
        chan[i].freqChanged=true;
      }
    }
    if (chan[i].std.wave.had) {
      if (chan[i].wave!=chan[i].std.wave.val || chan[i].ws.activeChanged()) {
        chan[i].wave=chan[i].std.wave.val;
        chan[i].ws.changeWave1(chan[i].wave);
      }
    }
    if (chan[i].std.pitch.had) {
      if (chan[i].std.pitch.mode) {
        chan[i].pitch2+=chan[i].std.pitch.val;
        CLAMP_VAR(chan[i].pitch2,-2048,2048);
      } else {
        chan[i].pitch2=chan[i].std.pitch.val;
      }
      chan[i].freqChanged=true;
    }
    if (chan[i].active) {
      if (chan[i].ws.tick()) {
        updateWave(i);
      }
    }
    if (chan[i].freqChanged) {
      chan[i].freq=parent->calcFreq(chan[i].baseFreq,chan[i].pitch,true,0,chan[i].pitch2,chipClock,CHIP_DIVIDER)-1;
      if (chan[i].freq<0) chan[i].freq=0;
      if (chan[i].freq>4095) chan[i].freq=4095;
      rWrite(regBase+0+i*2,chan[i].freq&0xff);
      rWrite(regBase+1+i*2,chan[i].freq>>8);
      chan[i].freqChanged=false;
    }
  }
}

int DivPlatformSCC::dispatch(DivCommand c) {
  switch (c.cmd) {
    case DIV_CMD_NOTE_ON: {
      DivInstrument* ins=parent->getIns(chan[c.chan].ins,DIV_INS_SCC);
      if (c.value!=DIV_NOTE_NULL) {
        chan[c.chan].baseFreq=NOTE_PERIODIC(c.value);
        chan[c.chan].freqChanged=true;
        chan[c.chan].note=c.value;
      }
      chan[c.chan].active=true;
      chan[c.chan].macroInit(ins);
      if (!isMuted[c.chan]) {
        rWrite(regBase+15,regPool[regBase+15]|(1<<c.chan));
      }
      if (chan[c.chan].wave<0) {
        chan[c.chan].wave=0;
        chan[c.chan].ws.changeWave1(chan[c.chan].wave);
      }
      chan[c.chan].ws.init(ins,32,255,chan[c.chan].insChanged);
      chan[c.chan].insChanged=false;
      break;
    }
    case DIV_CMD_NOTE_OFF:
      chan[c.chan].active=false;
      chan[c.chan].macroInit(NULL);
      rWrite(regBase+15,regPool[regBase+15]&~(1<<c.chan));
      break;
    case DIV_CMD_NOTE_OFF_ENV:
    case DIV_CMD_ENV_RELEASE:
      chan[c.chan].std.release();
      break;
    case DIV_CMD_INSTRUMENT:
      if (chan[c.chan].ins!=c.value || c.value2==1) {
        chan[c.chan].ins=c.value;
      }
      break;
    case DIV_CMD_VOLUME:
      if (chan[c.chan].vol!=c.value) {
        chan[c.chan].vol=c.value;
        if (!chan[c.chan].std.vol.has) {
          chan[c.chan].outVol=c.value;
          rWrite(regBase+10+c.chan,c.value);
        }
      }
      break;
    case DIV_CMD_GET_VOLUME:
      if (chan[c.chan].std.vol.has) {
        return chan[c.chan].vol;
      }
      return chan[c.chan].outVol;
      break;
    case DIV_CMD_PITCH:
      chan[c.chan].pitch=c.value;
      chan[c.chan].freqChanged=true;
      break;
    case DIV_CMD_WAVE:
      chan[c.chan].wave=c.value;
      chan[c.chan].ws.changeWave1(chan[c.chan].wave);
      break;
    case DIV_CMD_NOTE_PORTA: {
      int destFreq=NOTE_PERIODIC(c.value2);
      bool return2=false;
      if (destFreq>chan[c.chan].baseFreq) {
        chan[c.chan].baseFreq+=c.value;
        if (chan[c.chan].baseFreq>=destFreq) {
          chan[c.chan].baseFreq=destFreq;
          return2=true;
        }
      } else {
        chan[c.chan].baseFreq-=c.value;
        if (chan[c.chan].baseFreq<=destFreq) {
          chan[c.chan].baseFreq=destFreq;
          return2=true;
        }
      }
      chan[c.chan].freqChanged=true;
      if (return2) {
        chan[c.chan].inPorta=false;
        return 2;
      }
      break;
    }
    case DIV_CMD_LEGATO:
      chan[c.chan].baseFreq=NOTE_PERIODIC(c.value+((chan[c.chan].std.arp.will && !chan[c.chan].std.arp.mode)?(chan[c.chan].std.arp.val):(0)));
      chan[c.chan].freqChanged=true;
      chan[c.chan].note=c.value;
      break;
    case DIV_CMD_PRE_PORTA:
      if (chan[c.chan].active && c.value2) {
        if (parent->song.resetMacroOnPorta) chan[c.chan].macroInit(parent->getIns(chan[c.chan].ins,DIV_INS_SCC));
      }
      chan[c.chan].inPorta=c.value;
      break;
    case DIV_CMD_GET_VOLMAX:
      return 15;
      break;
    case DIV_ALWAYS_SET_VOLUME:
      return 1;
      break;
    default:
      break;
  }
  return 1;
}

void DivPlatformSCC::muteChannel(int ch, bool mute) {
  isMuted[ch]=mute;
  if (mute) {
    rWrite(regBase+15,regPool[regBase+15]&~(1<<ch));
  } else if (chan[ch].active) {
    rWrite(regBase+15,regPool[regBase+15]|(1<<ch));
  }
}

void DivPlatformSCC::forceIns() {
  for (int i=0; i<5; i++) {
    chan[i].insChanged=true;
    chan[i].freqChanged=true;
    updateWave(i);
  }
}

void* DivPlatformSCC::getChanState(int ch) {
  return &chan[ch];
}

DivDispatchOscBuffer* DivPlatformSCC::getOscBuffer(int ch) {
  return oscBuf[ch];
}

unsigned char* DivPlatformSCC::getRegisterPool() {
  return (unsigned char*)regPool;
}

int DivPlatformSCC::getRegisterPoolSize() {
  return 225;
}

void DivPlatformSCC::reset() {
  memset(regPool,0,225);
  scc->reset();
  for (int i=0; i<5; i++) {
    chan[i]=DivPlatformSCC::Channel();
    chan[i].std.setEngine(parent);
    chan[i].ws.setEngine(parent);
    chan[i].ws.init(NULL,32,255,false);
    chan[i].vol=15;
    chan[i].outVol=15;
    rWrite(regBase+10+i,15);
  }
  if (dumpWrites) {
    addWrite(0xffffffff,0);
  }
}

bool DivPlatformSCC::isStereo() {
  return false;
}

void DivPlatformSCC::notifyWaveChange(int wave) {
  for (int i=0; i<5; i++) {
    if (chan[i].wave==wave) {
      chan[i].ws.changeWave1(chan[i].wave);
      updateWave(i);
    }
  }
}

void DivPlatformSCC::notifyInsDeletion(void* ins) {
  for (int i=0; i<5; i++) {
    chan[i].std.notifyInsDeletion((DivInstrument*)ins);
  }
}

void DivPlatformSCC::poke(unsigned int addr, unsigned short val) {
  rWrite(addr,val);
}

void DivPlatformSCC::poke(std::vector<DivRegWrite>& wlist) {
  for (DivRegWrite& i: wlist) rWrite(i.addr,i.val);
}

void DivPlatformSCC::setChipModel(bool isplus) {
  isPlus=isplus;
}

int DivPlatformSCC::init(DivEngine* p, int channels, int sugRate, unsigned int flags) {
  parent=p;
  dumpWrites=false;
  skipRegisterWrites=false;
  writeOscBuf=0;
  for (int i=0; i<5; i++) {
    isMuted[i]=false;
    oscBuf[i]=new DivDispatchOscBuffer;
  }
  chipClock=COLOR_NTSC;
  rate=chipClock/16;
  for (int i=0; i<5; i++) {
    oscBuf[i]->rate=rate;
  }
  if (isPlus) {
    scc=new k052539_scc_core;
    regBase=0xa0;
  } else {
    scc=new k051649_scc_core;
    regBase=0x80;
  }
  reset();
  return 5;
}

void DivPlatformSCC::quit() {
  for (int i=0; i<5; i++) {
    delete oscBuf[i];
  }
  if (scc!=NULL) {
    delete scc;
  }
}

DivPlatformSCC::~DivPlatformSCC() {
}