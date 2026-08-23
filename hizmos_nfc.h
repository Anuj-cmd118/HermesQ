#pragma once
// ═══════════════════════════════════════════════════════════════
//  hizmos_nfc.h  —  NFC Engine for HermesQ UI
//
//  Extracted from NFC Tool V14 (mate/sketch/sketch.ino) and
//  adapted to integrate seamlessly with the HermesQ UI shell
//  (io project).
//
//  Entry point: nfcAppRun()
//    Call this from hizmos_feature.h when the user selects an
//    NFC sub-menu item.  It blocks until the user presses BACK
//    all the way out to the HermesQ main menu.
//
//  Hardware: PN532 on SPI, SS = D10  (same as mate project).
//  Bridge:   uses hizBridgeCall() / hizBridgeOk() from
//            hizmos_bridge.h, which wrap Arduino_RouterBridge.
//
//  Python:   db_* RPC calls are served by nfc_main.py.
//
//  Dependencies (add to sketch.yaml libraries):
//    - dir: libs/pn532/PN532
//    - dir: libs/pn532/PN532_SPI
//    - dir: libs/pn532/NDEF
// ═══════════════════════════════════════════════════════════════

#include <SPI.h>
#include <PN532_SPI.h>
#include <PN532.h>
#include <NdefMessage.h>
#include "emulatetag.h"      // lowercase — Linux FS is case-sensitive

#include "hizmos_display.h"
#include "hizmos_input.h"
#include "hizmos_ui.h"
#include "hizmos_bridge.h"

// ─────────────────────────────────────────────────────────────
//  Hardware objects (defined once here; extern if needed elsewhere)
// ─────────────────────────────────────────────────────────────
#define NFC_SS  10

static PN532_SPI  _nfc_spi(SPI, NFC_SS);
static PN532      _nfc(_nfc_spi);
static EmulateTag _emuTag(_nfc_spi);
static bool       _nfcReady = false;

// ─────────────────────────────────────────────────────────────
//  Internal state — all static to avoid polluting global scope
// ─────────────────────────────────────────────────────────────

// ── App states ────────────────────────────────────────────────
#define NFC_ST_MENU        0
#define NFC_ST_CARD_INFO   1
#define NFC_ST_CLONE       2
#define NFC_ST_EMULATE     3
#define NFC_ST_SAVED       4
#define NFC_ST_SETTINGS    5
static int _nfcState = NFC_ST_MENU;

// ── Card-info sub-states ──────────────────────────────────────
#define NFC_RS_WAIT    0
#define NFC_RS_READING 1
#define NFC_RS_SUMMARY 2
#define NFC_RS_ADD_FAV 3
#define NFC_RS_PAGES   4
#define NFC_RS_SECT    5
#define NFC_RS_BLOCK   6
static int _ciSub = NFC_RS_WAIT;
static int _ciPage = 0;

// ── Clone sub-states ─────────────────────────────────────────
#define NFC_CS_SRC_WAIT  0
#define NFC_CS_READING   1
#define NFC_CS_CONFIRM   2
#define NFC_CS_WARN      3
#define NFC_CS_DST_WAIT  4
#define NFC_CS_WRITING   5
#define NFC_CS_VERIFY    6
#define NFC_CS_RESULT    7
static int _clSub = NFC_CS_SRC_WAIT;

// ── Emulate sub-states ───────────────────────────────────────
#define NFC_EMU_LIST    0
#define NFC_EMU_RUNNING 1
static int _emuSub = NFC_EMU_LIST;
#define NFC_EMU_URI  0
#define NFC_EMU_TEXT 1
static int  _emuMode = NFC_EMU_URI;
static char _emuContent[128] = "https://github.com";

// ── Main menu ─────────────────────────────────────────────────
#define NFC_MENU_CNT 5
static const char* _nfcMenuItems[NFC_MENU_CNT] = {
  "Card Info", "Clone", "Emulate", "Saved Cards", "Settings"
};
static int _nfcMenuCur = 0, _nfcMenuScroll = 0;

// ── Keys ─────────────────────────────────────────────────────
static uint8_t _kFactory[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
static uint8_t _kNDEF[6]    = {0xD3,0xF7,0xD3,0xF7,0xD3,0xF7};
static uint8_t _kMAD[6]     = {0xA0,0xA1,0xA2,0xA3,0xA4,0xA5};
static uint8_t* _keys[3]    = {_kNDEF, _kMAD, _kFactory};

// ── Card struct ──────────────────────────────────────────────
struct NfcCard {
  uint8_t  uid[10];
  uint8_t  uidLen;
  uint16_t atqa;
  uint8_t  sak;
  char     typeStr[24];
  uint8_t  blocks[64][16];
  bool     blockOK[64];
  int      blockCount;
  bool     ndefPresent;
  char     ndefType[8];
  char     ndefContent[128];
  char     uidStr[24];
};

static NfcCard _card;
static NfcCard _cloneSrc;
static bool    _hasCloneSrc = false;

// ── DB list ──────────────────────────────────────────────────
#define NFC_DB_MAX    32
#define NFC_DB_PAGE    3
static char        _dbBuf[NFC_DB_MAX][20];
static const char* _dbItems[NFC_DB_MAX];
static int         _dbCur = 0, _dbScroll = 0, _dbCnt = 0;
static int         _dbIds[NFC_DB_MAX];
static bool        _dbLoaded = false;

// ── Settings ─────────────────────────────────────────────────
static bool _autoSave    = true;
static bool _verifyClone = true;
#define NFC_SET_CNT 4
static int  _setCur = 0, _setScroll = 0;

// ── Misc ─────────────────────────────────────────────────────
static int  _browseBlock  = 4;
static int  _ndefScroll   = 0;
static uint8_t _uid0[10], _uid1[10];
static uint8_t _ulen0, _ulen1;

// ── NDEF file buffer for emulation ───────────────────────────
static uint8_t _ndefBuf[128];
static uint8_t _ndefBufLen = 0;

// ── Emulate history ──────────────────────────────────────────
#define NFC_EMU_NEW_CNT 2
static const char* _emuNewLabels[NFC_EMU_NEW_CNT] = {"> New URI", "> New Text"};
#define NFC_EMU_HIST_MAX 30
static char        _emuHistBuf[NFC_EMU_HIST_MAX][20];
static int         _emuHistIds[NFC_EMU_HIST_MAX];
static int         _emuHistCnt = 0;
static const char* _emuAllItems[NFC_EMU_HIST_MAX + NFC_EMU_NEW_CNT];
static int         _emuListCur = 0, _emuListScroll = 0, _emuListCnt = 0;
static bool        _emuListLoaded = false;

// ── URI prefix table ─────────────────────────────────────────
static const char* _uriPfx[] = {
  "","http://www.","https://www.","http://","https://",
  "tel:","mailto:","ftp://anonymous:anonymous@","ftp://ftp.",
  "ftps://","sftp://","smb://","nfs://","ftp://","dav://",
  "news:","telnet://","imap:","rtsp://","urn:","pop:",
  "sip:","sips:","tftp:","btspp://","btl2cap://","btgoep://",
  "tcpobex://","irdaobex://","file://","urn:epc:id:",
  "urn:epc:tag:","urn:epc:pat:","urn:epc:raw:","urn:epc:","urn:nfc:"
};
#define NFC_URI_PFX_CNT 36

// ═══════════════════════════════════════════════════════════════
//  DISPLAY HELPERS  (wrap u8g2 directly; ui shell owns the object)
// ═══════════════════════════════════════════════════════════════

static void _nfcTitle(const char* t) {
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 10, t);
  u8g2.drawHLine(0, 12, 128);
}

static void _nfcStatus(const char* t,
                        const char* l1 = "",
                        const char* l2 = "",
                        const char* l3 = "") {
  u8g2.clearBuffer();
  _nfcTitle(t);
  u8g2.setFont(u8g2_font_6x10_tr);
  if (l1[0]) u8g2.drawStr(0, 26, l1);
  if (l2[0]) u8g2.drawStr(0, 40, l2);
  if (l3[0]) u8g2.drawStr(0, 54, l3);
  u8g2.sendBuffer();
}

static void _nfcList(const char* title, const char** items, int cnt,
                     int cur, int scroll) {
  u8g2.clearBuffer();
  _nfcTitle(title);
  u8g2.setFont(u8g2_font_6x10_tr);
  for (int i = 0; i < 3 && (scroll + i) < cnt; i++) {
    bool sel = (scroll + i == cur);
    int  y   = 26 + i * 13;
    if (sel) {
      u8g2.drawRBox(0, y - 10, 124, 12, 2);
      u8g2.setDrawColor(0);
      u8g2.drawStr(4, y, items[scroll + i]);
      u8g2.setDrawColor(1);
    } else {
      u8g2.drawStr(4, y, items[scroll + i]);
    }
  }
  u8g2.setFont(u8g2_font_5x7_tr);
  if (scroll > 0)         u8g2.drawStr(120, 22, "^");
  if (scroll + 3 < cnt)   u8g2.drawStr(120, 63, "v");
  char sc[8]; sprintf(sc, "%d/%d", cur + 1, cnt);
  u8g2.drawStr(86, 63, sc);
  u8g2.sendBuffer();
}

static void _nfcDrawMenu() {
  _nfcList("NFC", (const char**)_nfcMenuItems, NFC_MENU_CNT,
           _nfcMenuCur, _nfcMenuScroll);
}

static void _nfcScrollText(const char* title, const char* text, int sl) {
  u8g2.clearBuffer();
  _nfcTitle(title);
  u8g2.setFont(u8g2_font_5x7_tr);
  int len   = strlen(text);
  int total = (len + 20) / 21;
  int y     = 24;
  for (int ln = sl; ln < sl + 4 && ln < total; ln++) {
    char buf[22]; int start = ln * 21;
    int  chars = min(21, len - start);
    strncpy(buf, text + start, chars); buf[chars] = '\0';
    u8g2.drawStr(0, y, buf);
    y += 11;
  }
  if (sl > 0)             u8g2.drawStr(120, 22, "^");
  if (sl + 4 < total)     u8g2.drawStr(120, 63, "v");
  char pg[12]; sprintf(pg, "L%d/%d", sl + 1, total);
  u8g2.drawStr(80, 63, pg);
  u8g2.sendBuffer();
}

static void _nfcShowBlock(uint8_t num, uint8_t* data) {
  char label[12]; sprintf(label, "Block %02d", num);
  char hex1[17]="", hex2[17]="", ascii[17]="";
  for (int i = 0; i < 8;  i++) { char b[3]; sprintf(b,"%02X",data[i]);   strcat(hex1,b); }
  for (int i = 8; i < 16; i++) { char b[3]; sprintf(b,"%02X",data[i]);   strcat(hex2,b); }
  for (int i = 0; i < 16; i++) ascii[i]=(data[i]>=32&&data[i]<127)?data[i]:'.';
  ascii[16]='\0';
  u8g2.clearBuffer(); _nfcTitle(label);
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(0,24,hex1); u8g2.drawStr(0,35,hex2); u8g2.drawStr(0,46,ascii);
  u8g2.drawStr(0,63,"Rot:scroll  BACK:exit");
  u8g2.sendBuffer();
}

static void _nfcShowSectorMap(NfcCard* c) {
  u8g2.clearBuffer(); _nfcTitle("Sector Map");
  u8g2.setFont(u8g2_font_5x7_tr);
  int row=0, col=0;
  for (int s = 0; s < 16; s++) {
    uint8_t fb = s*4;
    bool ok = c->blockOK[fb]||c->blockOK[fb+1]||c->blockOK[fb+2];
    char sc[5]; snprintf(sc,5,"S%02d",s);
    int x=col*26, y=24+row*14;
    if (ok) {
      u8g2.drawRBox(x,y-9,24,11,2);
      u8g2.setDrawColor(0); u8g2.drawStr(x+2,y,sc); u8g2.setDrawColor(1);
    } else { u8g2.drawStr(x,y,sc); }
    col++; if(col>=5){col=0;row++;}
  }
  u8g2.drawStr(0,63,"Enc:browse  BACK:info");
  u8g2.sendBuffer();
}

static void _nfcShowCardSummary(NfcCard* c) {
  u8g2.clearBuffer(); _nfcTitle("Card Found");
  u8g2.setFont(u8g2_font_5x7_tr);
  char l[32];
  snprintf(l,32,"UID: %s",c->uidStr); u8g2.drawStr(0,24,l);
  u8g2.drawStr(0,33,c->typeStr);
  if (c->ndefPresent) {
    snprintf(l,32,"%s:",c->ndefType); u8g2.drawStr(0,42,l);
    char prev[22]; strncpy(prev,c->ndefContent,21); prev[21]='\0';
    u8g2.drawStr(0,51,prev);
  } else { u8g2.drawStr(0,42,"No NDEF"); }
  u8g2.drawStr(0,63,"Enc:details  BACK:menu");
  u8g2.sendBuffer();
}

#define NFC_INFO_PAGES 3
static void _nfcShowCardInfo(NfcCard* c, int page) {
  char pt[16]; sprintf(pt,"Info %d/%d",page+1,NFC_INFO_PAGES);
  u8g2.clearBuffer(); _nfcTitle(pt);
  u8g2.setFont(u8g2_font_5x7_tr);
  char l[32];
  if (page==0) {
    snprintf(l,32,"UID: %s",c->uidStr);               u8g2.drawStr(0,24,l);
    snprintf(l,32,"Len: %d bytes",c->uidLen);          u8g2.drawStr(0,33,l);
    snprintf(l,32,"ATQA: %04X",c->atqa);               u8g2.drawStr(0,42,l);
    snprintf(l,32,"SAK:  %02X",c->sak);                u8g2.drawStr(0,51,l);
  } else if (page==1) {
    u8g2.drawStr(0,24,c->typeStr);
    snprintf(l,32,"Blocks: %d",c->blockCount);         u8g2.drawStr(0,33,l);
    snprintf(l,32,"NDEF: %s",c->ndefPresent?"Yes":"No"); u8g2.drawStr(0,42,l);
    if (c->ndefPresent) {
      snprintf(l,32,"Type: %s",c->ndefType);           u8g2.drawStr(0,51,l);
    }
  } else {
    if (c->ndefPresent) {
      _nfcScrollText("NDEF Content", c->ndefContent, _ndefScroll); return;
    } else { u8g2.drawStr(0,33,"No NDEF data"); }
  }
  u8g2.drawStr(0,63,"Enc:next  BACK:back");
  u8g2.sendBuffer();
}

// ═══════════════════════════════════════════════════════════════
//  BRIDGE HELPERS
// ═══════════════════════════════════════════════════════════════

static String _nfcCall(const char* method, const String& arg = "") {
  String r = "";
  if (arg.length() == 0) Bridge.call(method).result(r);
  else                   Bridge.call(method, arg).result(r);
  return r;
}

static void _loadDBList(const char* method) {
  String result = _nfcCall(method);
  _dbCnt = 0; _dbCur = 0; _dbScroll = 0;
  int pos = 0;
  while (pos < (int)result.length() && _dbCnt < NFC_DB_MAX) {
    int sep  = result.indexOf(':', pos);
    int pipe = result.indexOf('|', pos);
    if (sep == -1) break;
    _dbIds[_dbCnt] = result.substring(pos, sep).toInt();
    int ne = (pipe == -1) ? result.length() : pipe;
    result.substring(sep+1, ne).toCharArray(_dbBuf[_dbCnt], 20);
    _dbItems[_dbCnt] = _dbBuf[_dbCnt];
    _dbCnt++; pos = (pipe == -1) ? result.length() : pipe + 1;
  }
}

// ═══════════════════════════════════════════════════════════════
//  NFC UTILITY
// ═══════════════════════════════════════════════════════════════

static bool _cardPresent(uint8_t* uid, uint8_t* uidLen) {
  return _nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, uidLen, 300);
}

static bool _smartAuth(uint8_t* uid, uint8_t uidLen, uint8_t block, uint8_t* wk) {
  for (int k = 0; k < 3; k++) {
    if (_nfc.mifareclassic_AuthenticateBlock(uid, uidLen, block, 0, _keys[k])) {
      if (wk) memcpy(wk, _keys[k], 6);
      return true;
    }
    uint8_t t[10]; uint8_t tl;
    _nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, t, &tl, 200);
    delay(40);
  }
  return false;
}

static bool _authBlock0Factory(uint8_t* uid, uint8_t uidLen) {
  if (_nfc.mifareclassic_AuthenticateBlock(uid, uidLen, 0, 0, _kFactory)) return true;
  uint8_t t[10]; uint8_t tl;
  _nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, t, &tl, 200);
  delay(40);
  return false;
}

static void _detectType(uint8_t uidLen, uint8_t sak, char* out) {
  if      (uidLen==4 && (sak&0x08) && !(sak&0x20)) strcpy(out,"MIFARE Classic 1K");
  else if (uidLen==4 && sak==0x38)                  strcpy(out,"MIFARE Classic 4K");
  else if (uidLen==7 && sak==0x00)                  strcpy(out,"MIFARE Ultralight");
  else if (uidLen==4 && sak==0x20)                  strcpy(out,"MIFARE DESFire");
  else if (uidLen==7)                               strcpy(out,"NFC Type2/Phone");
  else                                              strcpy(out,"Unknown");
}

static void _uidToStr(uint8_t* uid, uint8_t len, char* out) {
  out[0]='\0';
  for (uint8_t i=0;i<len;i++){
    char b[4]; sprintf(b,"%02X",uid[i]); strcat(out,b);
    if(i<len-1) strcat(out,":");
  }
}

static void _parseUIDStr(const char* s, uint8_t* uid, uint8_t* len) {
  *len=0; char tmp[24]; strncpy(tmp,s,23); tmp[23]='\0';
  char* tok=strtok(tmp,":");
  while(tok&&*len<7){uid[(*len)++]=(uint8_t)strtol(tok,nullptr,16);tok=strtok(nullptr,":");}
}

// ── NDEF Parser ──────────────────────────────────────────────
static void _parseNDEF(NfcCard* c) {
  strcpy(c->ndefType,"NONE"); c->ndefContent[0]='\0'; c->ndefPresent=false;
  static uint8_t stream[64*16]; int slen=0;
  for (uint8_t b=4;b<64;b++){
    if(!c->blockOK[b]||(b%4==3)) continue;
    memcpy(stream+slen,c->blocks[b],16); slen+=16;
  }
  int pos=0;
  while(pos<slen){
    uint8_t tlv=stream[pos++];
    if(tlv==0x00) continue;
    if(tlv==0xFE) return;
    if(tlv==0x03){
      if(pos>=slen) return;
      int nlen=stream[pos++];
      if(nlen==0xFF){ if(pos+1>=slen) return; nlen=((int)stream[pos]<<8)|stream[pos+1]; pos+=2; }
      if(pos+nlen>slen) return;
      uint8_t* rec=stream+pos;
      if(nlen<3) return;
      uint8_t flags=rec[0], tnf=flags&0x07;
      bool sr=flags&0x10, il=flags&0x08;
      uint8_t typeLen=rec[1];
      int plBytes=sr?1:4; if(nlen<2+plBytes) return;
      uint32_t pl;
      if(sr) pl=rec[2];
      else pl=((uint32_t)rec[2]<<24)|((uint32_t)rec[3]<<16)|((uint32_t)rec[4]<<8)|(uint32_t)rec[5];
      int hdr=2+plBytes; if(il) hdr++;
      uint8_t* typeBuf=rec+hdr; uint8_t* payload=typeBuf+typeLen;
      if(tnf==0x01&&typeLen==1&&typeBuf[0]=='U'&&pl>=1){
        c->ndefPresent=true; strcpy(c->ndefType,"URI");
        uint8_t pi=payload[0];
        const char* pfx=(pi<NFC_URI_PFX_CNT)?_uriPfx[pi]:"";
        int tl=(int)pl-1; if(tl>120) tl=120;
        snprintf(c->ndefContent,sizeof(c->ndefContent),"%s%.*s",pfx,tl,(char*)(payload+1));
        return;
      }
      if(tnf==0x01&&typeLen==1&&typeBuf[0]=='T'&&pl>=3){
        c->ndefPresent=true; strcpy(c->ndefType,"TEXT");
        uint8_t sb=payload[0]; uint8_t ll=sb&0x3F;
        int ts=1+ll, tl=(int)pl-ts;
        if(tl<0) tl=0; if(tl>127) tl=127;
        memcpy(c->ndefContent,payload+ts,tl); c->ndefContent[tl]='\0';
        return;
      }
      return;
    } else {
      if(pos>=slen) return;
      int skip=stream[pos++];
      if(skip==0xFF&&pos+1<slen){skip=((int)stream[pos]<<8)|stream[pos+1];pos+=2;}
      pos+=skip;
    }
  }
}

// ── Card Reading ─────────────────────────────────────────────
static bool _readSector0(NfcCard* c) {
  for (int k=0;k<3;k++){
    if(_nfc.mifareclassic_AuthenticateBlock(c->uid,c->uidLen,0,0,_keys[k])){
      for(uint8_t b=0;b<4;b++)
        if(_nfc.mifareclassic_ReadDataBlock(b,c->blocks[b])) c->blockOK[b]=true;
      return true;
    }
    uint8_t t[10]; uint8_t tl;
    _nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A,t,&tl,200); delay(40);
  }
  return false;
}

static int _readFullCard(NfcCard* c) {
  memset(c->blockOK,0,sizeof(c->blockOK)); c->blockCount=0;
  _readSector0(c);
  for(uint8_t s=1;s<16;s++){
    uint8_t fb=s*4; uint8_t wk[6];
    if(!_smartAuth(c->uid,c->uidLen,fb,wk)) continue;
    for(uint8_t off=0;off<3;off++){
      uint8_t b=fb+off;
      if(_nfc.mifareclassic_ReadDataBlock(b,c->blocks[b])){
        c->blockOK[b]=true; c->blockCount++;
      }
    }
  }
  _parseNDEF(c);
  return c->blockCount;
}

// ── Save to DB ───────────────────────────────────────────────
static void _saveCardToDB(NfcCard* c, const char* name) {
  String blocks="";
  for(int b=0;b<64;b++){
    if(!c->blockOK[b]) continue;
    if(blocks.length()) blocks+="|";
    char e[6]; sprintf(e,"%d:",b); blocks+=e;
    for(int i=0;i<16;i++){char h[3];sprintf(h,"%02X",c->blocks[b][i]);blocks+=h;}
  }
  char atqa[6]; sprintf(atqa,"%04X",c->atqa);
  char sak[4];  sprintf(sak,"%02X",c->sak);
  String r="";
  Bridge.call("nfc_save",
    String(c->uidStr),String(c->typeStr),
    String(atqa),String(sak),
    String(c->ndefType),
    String(c->ndefPresent?c->ndefContent:""),
    String(name),blocks
  ).result(r);
}

// ── NDEF file builder for emulation ─────────────────────────
static bool _buildNdefFile() {
  NdefMessage msg = NdefMessage();
  if(_emuMode==NFC_EMU_URI) msg.addUriRecord(_emuContent);
  else                       msg.addTextRecord(_emuContent,"en");
  int sz=msg.getEncodedSize();
  if(sz<=0||sz>(int)sizeof(_ndefBuf)) return false;
  msg.encode(_ndefBuf); _ndefBufLen=(uint8_t)sz;
  return true;
}

// ── Emulate history list ─────────────────────────────────────
static void _loadEmuHistory() {
  String r=_nfcCall("nfc_list_history");
  _emuHistCnt=0; int pos=0;
  while(pos<(int)r.length()&&_emuHistCnt<NFC_EMU_HIST_MAX){
    int sep=r.indexOf(':',pos), pipe=r.indexOf('|',pos);
    if(sep==-1) break;
    _emuHistIds[_emuHistCnt]=r.substring(pos,sep).toInt();
    int ne=(pipe==-1)?r.length():pipe;
    r.substring(sep+1,ne).toCharArray(_emuHistBuf[_emuHistCnt],20);
    _emuHistCnt++; pos=(pipe==-1)?r.length():pipe+1;
  }
  for(int i=0;i<NFC_EMU_NEW_CNT;i++)  _emuAllItems[i]=_emuNewLabels[i];
  for(int i=0;i<_emuHistCnt;i++)      _emuAllItems[NFC_EMU_NEW_CNT+i]=_emuHistBuf[i];
  _emuListCnt=NFC_EMU_NEW_CNT+_emuHistCnt;
}

// ═══════════════════════════════════════════════════════════════
//  INIT  (call once from sketch setup after SPI.begin())
// ═══════════════════════════════════════════════════════════════
static void nfcInit() {
  if (_nfcReady) return;
  pinMode(NFC_SS, OUTPUT);
  digitalWrite(NFC_SS, HIGH);
  _nfc.begin();
  _nfc.SAMConfig();
  uint32_t ver = _nfc.getFirmwareVersion();
  _nfcReady = (ver != 0);
}

// ═══════════════════════════════════════════════════════════════
//  INPUT HELPERS  (use io's inputXxx() API)
// ═══════════════════════════════════════════════════════════════
// Scroll a list, clamp to [0, cnt-1], return updated cursor
static int _nfcScroll(int cur, int cnt, int NFC_PAGE=3) {
  int8_t s = inputConsumeScrollFast(); uiTouchActivity();
  if (!s || cnt == 0) return cur;
  cur = constrain((int)cur + (int)s, 0, cnt - 1);
  return cur;
}

// ═══════════════════════════════════════════════════════════════
//  SUB-SCREENS
// ═══════════════════════════════════════════════════════════════

// ── Card Info screen ─────────────────────────────────────────
static void _nfcRunCardInfo() {
  switch (_ciSub) {

    case NFC_RS_WAIT:
      if (inputBackFired()) {
        inputSettle(150); _nfcState = NFC_ST_MENU; _nfcDrawMenu(); return;
      }
      if (_cardPresent(_uid0, &_ulen0)) {
        memcpy(_card.uid, _uid0, _ulen0); _card.uidLen = _ulen0;
        _card.atqa = 0; _card.sak = 0;
        _detectType(_ulen0, 0, _card.typeStr);
        _uidToStr(_uid0, _ulen0, _card.uidStr);
        _nfcStatus("CARD INFO", "Reading...", "Keep still!");
        _ciSub = NFC_RS_READING;
      }
      break;

    case NFC_RS_READING:
      _readFullCard(&_card);
      if (_autoSave) _saveCardToDB(&_card, _card.uidStr);
      _nfcShowCardSummary(&_card);
      _ciSub = NFC_RS_SUMMARY;
      break;

    case NFC_RS_SUMMARY:
      if (inputBackFired()) {
        inputSettle(150); _nfcState = NFC_ST_MENU; _ciSub = NFC_RS_WAIT; _nfcDrawMenu(); return;
      }
      if (inputSelectOrLearnFired()) {
        inputSettle(150);
        u8g2.clearBuffer(); _nfcTitle("Card Options");
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.drawStr(0, 26, "Add to Favourites?");
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawStr(0, 42, "Enc:Yes  BACK:Skip");
        u8g2.sendBuffer();
        _ciSub = NFC_RS_ADD_FAV;
      }
      break;

    case NFC_RS_ADD_FAV: {
      if (inputSelectOrLearnFired()) {
        inputSettle(150);
        String lastId = _nfcCall("nfc_last_id");
        if (lastId.length() > 0) _nfcCall("nfc_add_fav", lastId);
        _nfcStatus("Saved!", "Added to", "Favourites");
        delay(1200);
        _ciPage = 0; _ndefScroll = 0;
        _nfcShowCardInfo(&_card, 0); _ciSub = NFC_RS_PAGES;
      }
      if (inputBackFired()) {
        inputSettle(150); _ciPage = 0; _ndefScroll = 0;
        _nfcShowCardInfo(&_card, 0); _ciSub = NFC_RS_PAGES;
      }
      break;
    }

    case NFC_RS_PAGES: {
      // Handle scroll for NDEF text on page 2
      if (_ciPage == 2 && _card.ndefPresent) {
        int8_t s = inputConsumeScrollFast(); uiTouchActivity();
        if (s) {
          int total = (strlen(_card.ndefContent) + 20) / 21;
          _ndefScroll = constrain(_ndefScroll + s, 0, max(0, total - 4));
          _nfcShowCardInfo(&_card, 2);
        }
      }
      if (inputBackFired()) {
        inputSettle(150); _nfcShowCardSummary(&_card); _ciSub = NFC_RS_SUMMARY; return;
      }
      if (inputSelectOrLearnFired()) {
        inputSettle(150);
        _ciPage = (_ciPage + 1) % NFC_INFO_PAGES; _ndefScroll = 0;
        if (_ciPage == 0) { _nfcShowSectorMap(&_card); _ciSub = NFC_RS_SECT; }
        else _nfcShowCardInfo(&_card, _ciPage);
      }
      break;
    }

    case NFC_RS_SECT:
      if (inputBackFired()) {
        inputSettle(150); _nfcShowCardInfo(&_card,0); _ciPage=0; _ciSub=NFC_RS_PAGES; return;
      }
      if (inputSelectOrLearnFired()) {
        inputSettle(150);
        _browseBlock = 4;
        for(; _browseBlock<64; _browseBlock++)
          if(_card.blockOK[_browseBlock]&&_browseBlock%4!=3) break;
        _nfcShowBlock(_browseBlock, _card.blocks[_browseBlock]);
        _ciSub = NFC_RS_BLOCK;
      }
      break;

    case NFC_RS_BLOCK: {
      int8_t s = inputConsumeScrollFast(); uiTouchActivity();
      if (s) {
        int nb = _browseBlock;
        if(s>0){for(int b=nb+1;b<64;b++) if(_card.blockOK[b]&&b%4!=3){nb=b;break;}}
        else   {for(int b=nb-1;b>=4;b--) if(_card.blockOK[b]&&b%4!=3){nb=b;break;}}
        if(nb!=_browseBlock){_browseBlock=nb;_nfcShowBlock(_browseBlock,_card.blocks[_browseBlock]);}
      }
      if (inputBackFired()) {
        inputSettle(150); _nfcShowSectorMap(&_card); _ciSub=NFC_RS_SECT; return;
      }
      break;
    }
  }
}

// ── Clone screen ─────────────────────────────────────────────
static void _nfcRunClone() {
  switch (_clSub) {

    case NFC_CS_SRC_WAIT:
      if (inputBackFired()) {
        inputSettle(150); _nfcState=NFC_ST_MENU; _clSub=NFC_CS_SRC_WAIT; _nfcDrawMenu(); return;
      }
      if (_cardPresent(_uid1, &_ulen1)) {
        if(_ulen1!=4){_nfcStatus("CLONE","Need MIFARE card");delay(2000);return;}
        memcpy(_cloneSrc.uid,_uid1,_ulen1); _cloneSrc.uidLen=_ulen1;
        _uidToStr(_uid1,_ulen1,_cloneSrc.uidStr);
        _detectType(_ulen1,0,_cloneSrc.typeStr);
        _nfcStatus("CLONE 1/2","Reading source...","Keep still!");
        _clSub=NFC_CS_READING;
      }
      break;

    case NFC_CS_READING: {
      _readFullCard(&_cloneSrc); _hasCloneSrc=true;
      char msg[24]; sprintf(msg,"%d blocks read",_cloneSrc.blockCount);
      _nfcStatus("CLONE 1/2","Source stored!",msg,"Enc:continue");
      _clSub=NFC_CS_CONFIRM; break;
    }

    case NFC_CS_CONFIRM:
      if (inputBackFired()) {
        inputSettle(150); _nfcState=NFC_ST_MENU; _nfcDrawMenu(); return;
      }
      if (inputSelectOrLearnFired()) {
        inputSettle(150);
        u8g2.clearBuffer(); _nfcTitle("CLONE 2/2");
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawStr(0,24,"Magic Card Required");
        u8g2.drawStr(0,35,"UID-changeable card");
        u8g2.drawStr(0,46,"only. Standard cards");
        u8g2.drawStr(0,57,"will be rejected.");
        u8g2.setFont(u8g2_font_5x7_tr);
        u8g2.drawStr(0,63,"Enc:OK  BACK:cancel");
        u8g2.sendBuffer(); _clSub=NFC_CS_WARN;
      }
      break;

    case NFC_CS_WARN:
      if (inputBackFired()) {
        inputSettle(150); _nfcState=NFC_ST_MENU; _clSub=NFC_CS_SRC_WAIT; _nfcDrawMenu(); return;
      }
      if (inputSelectOrLearnFired()) {
        inputSettle(150); _nfcStatus("CLONE 2/2","Tap MAGIC card","BACK to cancel"); _clSub=NFC_CS_DST_WAIT;
      }
      break;

    case NFC_CS_DST_WAIT:
      if (inputBackFired()) {
        inputSettle(150); _nfcState=NFC_ST_MENU; _clSub=NFC_CS_SRC_WAIT; _nfcDrawMenu(); return;
      }
      if (_cardPresent(_uid1, &_ulen1)) {
        if(_ulen1!=4){_nfcStatus("CLONE","Need MIFARE card");delay(2000);return;}
        _nfcStatus("CLONE 2/2","Writing...","Keep still!"); _clSub=NFC_CS_WRITING;
      }
      break;

    case NFC_CS_WRITING: {
      bool b0ok=false;
      if(_cloneSrc.blockOK[0]){
        if(_authBlock0Factory(_uid1,_ulen1))
          if(_nfc.mifareclassic_WriteDataBlock(0,_cloneSrc.blocks[0])) b0ok=true;
      } else { b0ok=true; }
      if(!b0ok){
        _nfcStatus("CLONE FAILED","Not A Magic Card","Block 0 refused","BACK to exit");
        _clSub=NFC_CS_RESULT; break;
      }
      int ok=0,fail=0;
      for(uint8_t s=1;s<16;s++){
        uint8_t fb=s*4; uint8_t wk[6];
        if(!_smartAuth(_uid1,_ulen1,fb,wk)){fail+=3;continue;}
        for(uint8_t off=0;off<3;off++){
          uint8_t b=fb+off;
          if(!_cloneSrc.blockOK[b]) continue;
          if(_nfc.mifareclassic_WriteDataBlock(b,_cloneSrc.blocks[b])) ok++; else fail++;
        }
      }
      if(_verifyClone){ _nfcStatus("CLONE","Verifying..."); _clSub=NFC_CS_VERIFY; }
      else { char r[24]; sprintf(r,"OK:%d Fail:%d",ok,fail); _nfcStatus("CLONE DONE",r,"Press any"); _clSub=NFC_CS_RESULT; }
      break;
    }

    case NFC_CS_VERIFY: {
      int mm=0;
      for(uint8_t b=4;b<64;b++){
        if(b%4==3||!_cloneSrc.blockOK[b]) continue;
        if(b%4==0) _smartAuth(_uid1,_ulen1,b,nullptr);
        uint8_t rb[16];
        if(_nfc.mifareclassic_ReadDataBlock(b,rb))
          if(memcmp(rb,_cloneSrc.blocks[b],16)!=0) mm++;
      }
      char r[32]; if(mm==0) strcpy(r,"Verified OK!"); else sprintf(r,"%d blocks differ",mm);
      _nfcStatus("CLONE DONE",r,"Press any"); _clSub=NFC_CS_RESULT; break;
    }

    case NFC_CS_RESULT:
      if (inputBackFired() || inputSelectOrLearnFired()) {
        inputSettle(150); _clSub=NFC_CS_SRC_WAIT; _hasCloneSrc=false;
        _nfcState=NFC_ST_MENU; _nfcDrawMenu();
      }
      break;
  }
}

// ── Emulate screen ───────────────────────────────────────────
static void _nfcRunEmulate() {
  switch (_emuSub) {

    case NFC_EMU_LIST:
      if (!_emuListLoaded) {
        _loadEmuHistory(); _emuListLoaded=true;
        _emuListCur=0; _emuListScroll=0;
        _nfcList("Emulate",(const char**)_emuAllItems,_emuListCnt,_emuListCur,_emuListScroll);
      }
      if (inputBackFired()) {
        inputSettle(150); _emuListLoaded=false; _nfcState=NFC_ST_MENU; _nfcDrawMenu(); return;
      }
      {
        int8_t s=inputConsumeScroll();
        if(s&&_emuListCnt){
          _emuListCur=constrain(_emuListCur+s,0,_emuListCnt-1);
          if(_emuListCur<_emuListScroll) _emuListScroll=_emuListCur;
          if(_emuListCur>=_emuListScroll+3) _emuListScroll=_emuListCur-2;
          _nfcList("Emulate",(const char**)_emuAllItems,_emuListCnt,_emuListCur,_emuListScroll);
        }
      }
      if (inputSelectOrLearnFired()) {
        inputSettle(150);
        if (_emuListCur < NFC_EMU_NEW_CNT) {
          // New URI or Text — use default content
          _emuMode = (_emuListCur == 0) ? NFC_EMU_URI : NFC_EMU_TEXT;
          if (_emuMode == NFC_EMU_URI)  strcpy(_emuContent, "https://github.com");
          else                           strcpy(_emuContent, "Hello from HermesQ");
        } else {
          // Load from history
          int histIdx = _emuListCur - NFC_EMU_NEW_CNT;
          String ndefStr = _nfcCall("nfc_get_ndef", String(_emuHistIds[histIdx]));
          int colon = ndefStr.indexOf(':');
          if (colon > 0) {
            String type = ndefStr.substring(0,colon);
            String cont = ndefStr.substring(colon+1);
            if(type=="URI")  { _emuMode=NFC_EMU_URI;  cont.toCharArray(_emuContent,sizeof(_emuContent)); }
            else if(type=="TEXT"){ _emuMode=NFC_EMU_TEXT; cont.toCharArray(_emuContent,sizeof(_emuContent)); }
            else { _nfcStatus("EMULATE","Unsupported","No URI/Text NDEF","BACK to return"); return; }
          } else {
            _nfcStatus("EMULATE","No NDEF","BACK to return"); return;
          }
        }
        if (!_buildNdefFile()) {
          _nfcStatus("EMULATE","NDEF build failed","BACK to return"); return;
        }
        _emuTag.setNdefFile(_ndefBuf, _ndefBufLen);
        _emuTag.init();
        _emuSub=NFC_EMU_RUNNING;
        _nfcStatus("EMULATING",_emuContent,"Hold near phone","BACK to stop");
      }
      break;

    case NFC_EMU_RUNNING:
      if (inputBackFired()) {
        inputSettle(150);
        _nfc.SAMConfig();  // restore reader mode
        _emuSub=NFC_EMU_LIST; _emuListLoaded=false;
        _nfcState=NFC_ST_MENU; _nfcDrawMenu(); return;
      }
      _emuTag.emulate(50);
      break;
  }
}

// ── Saved Cards screen ───────────────────────────────────────
static void _nfcRunSaved() {
  if (!_dbLoaded) { _loadDBList("nfc_list_saved"); _dbLoaded=true; }
  if (inputBackFired()) {
    inputSettle(150); _dbLoaded=false; _nfcState=NFC_ST_MENU; _nfcDrawMenu(); return;
  }
  if (_dbCnt == 0) {
    _nfcStatus("Saved","No cards yet","BACK to menu"); return;
  }
  {
    int8_t s=inputConsumeScroll();
    if(s){
      _dbCur=constrain(_dbCur+s,0,_dbCnt-1);
      if(_dbCur<_dbScroll) _dbScroll=_dbCur;
      if(_dbCur>=_dbScroll+NFC_DB_PAGE) _dbScroll=_dbCur-(NFC_DB_PAGE-1);
    }
  }
  _nfcList("Saved Cards",(const char**)_dbItems,_dbCnt,_dbCur,_dbScroll);
  if (inputSelectOrLearnFired()) {
    inputSettle(150);
    String detail = _nfcCall("nfc_get", String(_dbIds[_dbCur]));
    _nfcScrollText("Card Detail", detail.c_str(), 0);
    // wait for back
    while (true) {
      inputPoll();
      if (inputBackFired()) { inputSettle(150); break; }
      delay(5);
    }
    _nfcList("Saved Cards",(const char**)_dbItems,_dbCnt,_dbCur,_dbScroll);
  }
}

// ── Settings screen ──────────────────────────────────────────
static void _nfcRunSettings() {
  if (inputBackFired()) {
    inputSettle(150); _nfcState=NFC_ST_MENU; _nfcDrawMenu(); return;
  }
  char s0[24]; snprintf(s0,24,"Auto Save: %s", _autoSave?"ON":"OFF");
  char s1[24]; snprintf(s1,24,"Verify Clone: %s", _verifyClone?"ON":"OFF");
  const char* items[NFC_SET_CNT]={s0,s1,"Clear History","Clear DB"};
  _nfcList("NFC Settings",(const char**)items,NFC_SET_CNT,_setCur,_setScroll);
  {
    int8_t s=inputConsumeScroll();
    if(s){
      _setCur=constrain(_setCur+s,0,NFC_SET_CNT-1);
      if(_setCur<_setScroll) _setScroll=_setCur;
      if(_setCur>=_setScroll+3) _setScroll=_setCur-2;
    }
  }
  if (inputSelectOrLearnFired()) {
    inputSettle(150);
    switch(_setCur){
      case 0: _autoSave=!_autoSave; break;
      case 1: _verifyClone=!_verifyClone; break;
      case 2: _nfcCall("nfc_clear_history"); _nfcStatus("Settings","History cleared"); delay(1500); break;
      case 3: _nfcCall("nfc_clear_db");      _nfcStatus("Settings","DB cleared");      delay(1500); break;
    }
  }
}

// ═══════════════════════════════════════════════════════════════
//  PUBLIC ENTRY POINTS
//  Call these from hizmos_feature.h / onNfcSelect()
// ═══════════════════════════════════════════════════════════════

// Open NFC app at the main NFC menu (Read / Clone / Emulate / Saved / Settings)
static void nfcAppRun() {
  nfcInit();
  if (!_nfcReady) {
    _nfcStatus("NFC Error","PN532 not found","Check SPI / D10","BACK to exit");
    while (true) {
      inputPoll();
      if (inputBackFired()) { inputSettle(150); return; }
      delay(5);
    }
  }
  _nfcState=NFC_ST_MENU; _nfcMenuCur=0; _nfcMenuScroll=0;
  _nfcDrawMenu(); inputSettle(200);

  while (true) {
    inputPoll();

    // Scroll main NFC menu
    {
      int8_t s=inputConsumeScroll();
      if(s&&_nfcState==NFC_ST_MENU){
        _nfcMenuCur=constrain(_nfcMenuCur+s,0,NFC_MENU_CNT-1);
        if(_nfcMenuCur<_nfcMenuScroll)       _nfcMenuScroll=_nfcMenuCur;
        if(_nfcMenuCur>=_nfcMenuScroll+3)    _nfcMenuScroll=_nfcMenuCur-2;
        _nfcDrawMenu();
      }
    }

    switch (_nfcState) {
      case NFC_ST_MENU:
        if (inputBackFired()) {
          inputSettle(200);
          _nfc.SAMConfig();  // restore reader before exit
          return;            // return to HermesQ main menu
        }
        if (inputSelectOrLearnFired()) {
          inputSettle(150);
          switch (_nfcMenuCur) {
            case 0: _nfcState=NFC_ST_CARD_INFO; _ciSub=NFC_RS_WAIT;
                    _nfcStatus("CARD INFO","Tap a card","BACK to cancel"); break;
            case 1: _nfcState=NFC_ST_CLONE; _clSub=NFC_CS_SRC_WAIT;
                    _nfcStatus("CLONE 1/2","Tap SOURCE card","BACK to cancel"); break;
            case 2: _nfcState=NFC_ST_EMULATE; _emuSub=NFC_EMU_LIST; _emuListLoaded=false; break;
            case 3: _nfcState=NFC_ST_SAVED; _dbLoaded=false; break;
            case 4: _nfcState=NFC_ST_SETTINGS; _setCur=0; _setScroll=0; break;
          }
        }
        break;

      case NFC_ST_CARD_INFO:  _nfcRunCardInfo();  break;
      case NFC_ST_CLONE:      _nfcRunClone();     break;
      case NFC_ST_EMULATE:    _nfcRunEmulate();   break;
      case NFC_ST_SAVED:      _nfcRunSaved();     break;
      case NFC_ST_SETTINGS:   _nfcRunSettings();  break;
    }

    delay(5);
  }
}

// Convenience: open NFC directly to a specific sub-screen
static void nfcAppRead()    { nfcInit(); _nfcState=NFC_ST_CARD_INFO; _ciSub=NFC_RS_WAIT;
                              nfcAppRun(); }
static void nfcAppWrite()   { nfcInit(); _nfcState=NFC_ST_EMULATE;  _emuSub=NFC_EMU_LIST;
                              _emuListLoaded=false; nfcAppRun(); }
static void nfcAppSaved()   { nfcInit(); _nfcState=NFC_ST_SAVED; _dbLoaded=false; nfcAppRun(); }
