#pragma once

// CYD touch UI: LVGL 8 interface for the 240x320 CYD + LR1121 companion.
// Drop-in replacement for ui-new (same UITask class name / interface).

#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/SensorManager.h>
#include <helpers/MultiSerialInterface.h>
#include <Arduino.h>

#include "../AbstractUITask.h"
#include "../NodePrefs.h"

#ifndef CYD_MAX_MSGS
  #define CYD_MAX_MSGS 48
#endif
#ifndef CYD_MAX_CONVS
  #define CYD_MAX_CONVS 24
#endif
#define CYD_MSG_LEN 160
#define CYD_CONSOLE_LEN 1536

enum class CydScreen : uint8_t { Home, Chats, Conversation, Compose, Contacts, Settings, EditName, ContactDetail, Login, Status, Console, CliInput, PathEdit, PathInput };

struct CydMsg {
  uint32_t ts;           // epoch (our clock)
  uint32_t ack;          // expected ACK crc for outgoing DMs
  uint8_t  conv;         // index into _convs
  uint8_t  out;          // 1 = sent by us
  uint8_t  status;       // 0 none, 1 sending, 2 delivered, 3 failed
  uint8_t  echoes;       // times we heard a repeater re-send it
  uint32_t tag;          // echo-tracking id for outgoing messages
  char     sender[24];   // channel messages: who said it
  char     text[CYD_MSG_LEN];
};

struct CydConv {
  uint8_t  used;
  uint8_t  is_channel;
  uint8_t  chan_idx;
  uint8_t  pub_prefix[6];
  uint8_t  type;          // ADV_TYPE_* for contacts
  uint16_t unread;
  uint32_t last_ts;
  char     name[32];
  char     last[56];
};

struct CydPrefs {
  uint8_t  magic = 0xC7;
  uint8_t  brightness = 160;
  uint8_t  screen_off_idx = 1;   // 15s, 30s, 1m, 5m, never
  int8_t   tz_hours = 0;         // UTC offset
  uint8_t  msg_led = 1;          // blink the RGB LED for unread messages
};

class UITask : public AbstractUITask {
  DisplayDriver* _display = nullptr;
  SensorManager* _sensors = nullptr;
  NodePrefs* _node_prefs = nullptr;

  CydPrefs _prefs;
  CydMsg*  _msgs = nullptr;        // heap: CYD_MAX_MSGS entries
  uint16_t _msg_head = 0, _msg_count = 0;
  uint32_t _tag_ctr = 0;
  CydConv  _convs[CYD_MAX_CONVS];

  CydScreen _screen = CydScreen::Home;
  int       _open_conv = -1;
  int       _contact_filter = 0;   // 0 all, 1 people, 2 repeaters/rooms
  bool      _dirty = true;
  bool      _screen_on = true;
  bool      _swallow_touch = false;
  unsigned long _last_touch = 0, _next_tick = 0, _toast_until = 0;
  uint32_t  _last_minute = 0;

  // contact detail + ping
  uint8_t   _detail_pub[32];
  bool      _delete_armed = false;
  uint32_t  _ping_tag = 0;
  unsigned long _ping_sent = 0, _ping_deadline = 0;
  char      _ping_result[64] = "";
  // route editor
  uint8_t   _pe_path[8][4];   // hop hashes (up to 4 bytes each)
  uint8_t   _pe_n = 0, _pe_hsz = 1;

  // remote admin (repeaters / room servers)
  uint8_t   _admin_pub[32];           // full key of the node we're logged in to (this session)
  bool      _admin_ok = false;
  char      _admin_pw[24] = "";
  CydScreen _after_login = CydScreen::Status;
  bool      _login_pending = false;
  unsigned long _login_deadline = 0;
  char      _login_msg[48] = "";
  uint8_t   _status[64];
  uint8_t   _status_len = 0;
  bool      _status_pending = false;
  unsigned long _status_deadline = 0, _status_at = 0;
  char*     _console = nullptr;        // heap, CYD_CONSOLE_LEN
  bool      _cli_pending = false;
  unsigned long _cli_deadline = 0;
  bool      isAdminFor(const uint8_t* pub) const { return _admin_ok && memcmp(_admin_pub, pub, 6) == 0; }
  void      consoleAppend(const char* prefix, const char* text);
  // saved admin passwords (flash, /meshsyd_pw), one per node
  struct SavedPw { uint8_t pub[6]; char pw[24]; };
  SavedPw   _pws[8];
  void      loadPasswords();
  void      savePasswords();
  const char* savedPassword(const uint8_t* pub) const;
  void      rememberPassword(const uint8_t* pub, const char* pw);
public:
  void      forgetPassword();
  bool      hasSavedPassword(const uint8_t* pub) const { return savedPassword(pub) != nullptr; }
private:

  void loadPrefs();
  // message LED (red + green only; blue is not used)
  uint8_t   _led_color = 0;          // bit0 red, bit1 green
  uint8_t   _led_steps = 0;          // remaining on/off half-steps
  unsigned long _led_next = 0, _led_remind = 0;
  void ledSet(uint8_t color);
  void ledFlash(uint8_t color, uint8_t count);
  void ledLoop(unsigned long now);
  void loadHistory();
  void saveHistory();
  unsigned long _save_due = 0;
  void historyChanged() { _save_due = millis() + 4000; }
  void savePrefs();
  void applyBrightness();
  void wake();
  unsigned long screenOffMillis() const;

  // data
  int  findOrAddChannelConv(int chan_idx, const char* name);
  int  findOrAddContactConv(const ContactInfo& c);
  void refreshChannels();
  void addMsg(int conv, bool out, const char* sender, const char* text, uint32_t ts, uint32_t ack, uint8_t status);
  int  totalUnread() const;

public:
  UITask(mesh::MainBoard* board, MultiSerialInterface* serial) : AbstractUITask(board, serial) {}
  void begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs);

  // navigation (public: used by LVGL event callbacks)
  void show(CydScreen s);
  void openConversation(int conv);
  void openContact(int contact_idx);
  void sendCompose(const char* text);
  void sendAdvert();
  void setContactFilter(int f) { _contact_filter = f; show(CydScreen::Contacts); }
  void toast(const char* text);
  void onTouchActivity();
  bool consumeWakeTouch();
  void endWakeTouch();

  CydPrefs& prefs() { return _prefs; }
  void prefsChanged() { savePrefs(); applyBrightness(); if (!_prefs.msg_led) { _led_steps = 0; ledSet(0); } }
  void setNodeName(const char* name);
  void toggleBluetooth();
  int  openConvIdx() const { return _open_conv; }

  // MyMesh::Listener
  void onMessageRecv(mesh::Packet *pkt, const ContactInfo &from, uint8_t txt_type, uint32_t sender_timestamp, const char* text) override;
  void onChannelMessageRecv(mesh::Packet *pkt, ChannelDetails& channel_details, const char* text) override;
  void onQueueSizeChanged(int msgcount) override { }
  void onDiscoveredContact(ContactInfo &contact, bool is_new, uint8_t path_len, const uint8_t* path) override;
  void onACKRecv(uint32_t ack_crc) override;
  void onEchoHeard(uint32_t ui_tag, uint8_t count) override;
  void onTraceRecv(mesh::Packet *pkt, uint32_t tag, uint32_t auth_code, uint8_t flags,
                   const uint8_t *path_snrs, const uint8_t *path_hashes, uint8_t path_len) override;

  // contact detail actions
  void contactMessage();
  void contactPing();
  void contactResetPath();
  void contactDelete();
  void pathEditOpen();
  void pathEditAdd(int contact_idx);
  void pathEditRemove(int hop);
  void pathEditSave(bool flood);
  void pathEditTyped(const char* text);
  void adminOpen(CydScreen target);
  void submitLogin(const char* password);
  void requestStatus();
  void sendCli(const char* cmd);
  void onLoginResult(const ContactInfo &contact, bool ok, uint8_t permissions) override;
  void onStatusResult(const ContactInfo &contact, const uint8_t *data, uint8_t len) override;

  // AbstractUITask
  void notify(UIEventType t = UIEventType::none) override { _dirty = true; }
  void loop() override;

  // screen builders (internal, public for callbacks' convenience)
  void buildHome();
  void buildChats();
  void buildConversation();
  void buildCompose();
  void buildContacts();
  void buildSettings();
  void buildEditName();
  void buildContactDetail();
  void buildLogin();
  void buildStatus();
  void buildConsole();
  void buildCliInput();
  void buildPathEdit();
  void buildPathInput();
  const CydConv& conv(int i) const { return _convs[i]; }
};
