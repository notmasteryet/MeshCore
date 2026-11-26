// MeshSyd: LVGL touch UI for the CYD + LR1121 MeshCore companion.
#include "UITask.h"
#include "target.h"
#include <lvgl.h>
#include <SPIFFS.h>
#include <time.h>
#include <helpers/AdvertDataHelpers.h>
#include <helpers/TxtDataHelpers.h>

#define MESHSYD_VERSION "0.1"

extern MyMesh the_mesh;

LV_FONT_DECLARE(inter_10)
LV_FONT_DECLARE(inter_12)
LV_FONT_DECLARE(inter_12b)
LV_FONT_DECLARE(inter_14)
LV_FONT_DECLARE(inter_16b)
LV_FONT_DECLARE(inter_17b)
LV_FONT_DECLARE(inter_46b)
LV_FONT_DECLARE(icons_14)
LV_FONT_DECLARE(icons_18)

// ---- palette (matches the approved mockup) ----
#define C_BG        0x0D1117
#define C_CARD      0x161B22
#define C_CARD2     0x1F2630
#define C_KEY       0x2A323D
#define C_BORDER    0x222A33
#define C_DIVIDER   0x1A2028
#define C_TXT       0xE6EDF3
#define C_TXT2      0x9DA7B3
#define C_TXT3      0x6E7681
#define C_ACCENT    0x4CC2FF
#define C_ACC_INK   0x04121C
#define C_ACC_SOFT  0x13344A
#define C_OUT       0x0F3A55
#define C_OUT_META  0x8AD8FF
#define C_AMBER     0xF5A524
#define C_AMBER_BG  0x2A2111
#define C_AMBER_TXT 0xF5C463
#define C_AMBER_INK 0x1A1204
#define C_GREEN     0x3FB950
#define C_RED       0xFF8A80

// ---- FontAwesome 5 glyphs (UTF-8) ----
#define I_HOME     "\xEF\x80\x95"
#define I_CHAT     "\xEF\x81\xB5"
#define I_USERS    "\xEF\x83\x80"
#define I_SLIDERS  "\xEF\x87\x9E"
#define I_BT       "\xEF\x8A\x94"
#define I_SIGNAL   "\xEF\x80\x92"
#define I_BOLT     "\xEF\x83\xA7"
#define I_TOWER    "\xEF\x94\x99"
#define I_LEFT     "\xEF\x81\x93"
#define I_RIGHT    "\xEF\x81\x94"
#define I_SEND     "\xEF\x87\x98"
#define I_PLUS     "\xEF\x81\xA7"
#define I_CLOSE    "\xEF\x80\x8D"
#define I_CHECK    "\xEF\x80\x8C"
#define I_BKSP     "\xEF\x95\x9A"
#define I_SHIFT    "\xEF\x81\xA2"
#define I_ENTER    "\xEF\x8E\xBE"
#define I_SUN      "\xEF\x86\x85"
#define I_USER     "\xEF\x80\x87"
#define I_CLOCK    "\xEF\x80\x97"
#define I_BELL     "\xEF\x83\xB3"
#define I_INFO     "\xEF\x81\x9A"
#define I_HASH     "\xEF\x8A\x92"
#define I_LOCK     "\xEF\x80\xA3"
#define I_SYNC     "\xEF\x8B\xB1"
static const char* I_BATT[] = { "\xEF\x89\x84", "\xEF\x89\x83", "\xEF\x89\x82", "\xEF\x89\x81", "\xEF\x89\x80" };

// Text fonts with the icon font as fallback, so labels can mix text and icons.
static lv_font_t F10, F12, F12B, F14, F16B;

static UITask* g_ui = nullptr;
static lv_obj_t* g_toast = nullptr;
static lv_obj_t* g_ta = nullptr;       // compose / edit textarea
static lv_obj_t* g_count = nullptr;    // compose char counter
static bool g_shift = false, g_nums = false, g_syms = false;

static void input_screen(const char* title, const char* initial, int max_len, bool password, int mode,
                         CydScreen back, const char* hint);

static const unsigned long SCREEN_OFF_MS[] = { 15000, 30000, 60000, 300000, 0 };
static const char* SCREEN_OFF_TXT[] = { "15 s", "30 s", "1 min", "5 min", "Never" };

// =====================================================================
// display + touch glue (LovyanGFX)
// =====================================================================
#define BUF_LINES 24
static lv_disp_draw_buf_t draw_buf;
static lv_color_t* buf1 = nullptr;   // heap (DMA-capable)
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;

static void flush_cb(lv_disp_drv_t* drv, const lv_area_t* a, lv_color_t* px) {
  uint32_t w = a->x2 - a->x1 + 1, h = a->y2 - a->y1 + 1;
  auto* gfx = display.gfx();
  gfx->startWrite();
  gfx->pushImage(a->x1, a->y1, w, h, (lgfx::swap565_t*)&px->full);
  gfx->endWrite();
  lv_disp_flush_ready(drv);
}

static uint32_t g_touch_reads = 0;
static void touch_cb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
  g_touch_reads++;
  uint16_t x, y;
  bool pressed = display.gfx()->getTouch(&x, &y) > 0;
  if (g_ui) {
    if (pressed) {
      g_ui->onTouchActivity();
      if (g_ui->consumeWakeTouch()) pressed = false;   // the press that woke the screen
    } else {
      g_ui->endWakeTouch();                            // any release ends it
    }
  }
  data->state = pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
  if (pressed) { data->point.x = x; data->point.y = y; }
#ifdef MESH_DEBUG
  static bool was = false;
  static unsigned long t0 = 0;
  bool raw = display.gfx()->getTouch(&x, &y) > 0;
  if (raw != was) {
    if (raw) { t0 = millis(); Serial.printf("MeshSyd: touch down %u,%u%s\n", x, y, pressed ? "" : " (swallowed)"); }
    else Serial.printf("MeshSyd: touch up after %lums\n", millis() - t0);
    was = raw;
  }
#endif
}

// =====================================================================
// small builders
// =====================================================================
static lv_obj_t* box(lv_obj_t* p, lv_coord_t w, lv_coord_t h, uint32_t bg = 0, lv_coord_t radius = 0, bool opaque = true) {
  lv_obj_t* o = lv_obj_create(p);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, w, h);
  if (opaque) {
    lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  }
  lv_obj_set_style_radius(o, radius, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

static lv_obj_t* row(lv_obj_t* p, lv_coord_t h, lv_coord_t gap = 6) {
  lv_obj_t* o = box(p, lv_pct(100), h, 0, 0, false);
  lv_obj_set_flex_flow(o, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(o, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(o, gap, 0);
  return o;
}

static lv_obj_t* col(lv_obj_t* p, lv_coord_t w, lv_coord_t h, lv_coord_t gap = 0) {
  lv_obj_t* o = box(p, w, h, 0, 0, false);
  lv_obj_set_flex_flow(o, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(o, gap, 0);
  return o;
}

static lv_obj_t* label(lv_obj_t* p, const char* txt, const lv_font_t* f, uint32_t color) {
  lv_obj_t* l = lv_label_create(p);
  lv_obj_remove_style_all(l);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  return l;
}

static lv_obj_t* spacer(lv_obj_t* p) {
  lv_obj_t* s = box(p, 1, 1, 0, 0, false);
  lv_obj_set_flex_grow(s, 1);
  return s;
}

static void style_scrollbar(lv_obj_t* o) {
  lv_obj_set_style_width(o, 3, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_color(o, lv_color_hex(C_KEY), LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_SCROLLBAR);
  lv_obj_set_style_radius(o, 2, LV_PART_SCROLLBAR);
  lv_obj_set_style_pad_right(o, 1, LV_PART_SCROLLBAR);
}

// round avatar with an initial
static lv_obj_t* avatar(lv_obj_t* p, lv_coord_t size, const char* name, bool square, bool channel) {
  static const uint32_t BG[] = { 0x3A2A12, 0x13344A, 0x2C2340, 0x1B3324, 0x3A1E1E };
  static const uint32_t FG[] = { 0xF5C463, 0x4CC2FF, 0xB392F0, 0x3FB950, 0xFF8A80 };
  uint32_t h = 0;
  for (const char* c = name; *c; c++) h = h * 31 + (uint8_t)*c;
  int k = channel ? 1 : (int)(h % 5);
  lv_obj_t* a = box(p, size, size, BG[k], square ? 7 : size / 2);
  char ini[8] = "#";
  if (!channel) {
    // first UTF-8 character of the name, uppercased if ASCII
    const char* s = name;
    while (*s == ' ' || *s == '#') s++;
    int n = 1;
    if ((uint8_t)*s >= 0xC0) n = ((uint8_t)*s >= 0xE0) ? 3 : 2;
    if (!*s) { ini[0] = '?'; ini[1] = 0; }
    else { memcpy(ini, s, n); ini[n] = 0; if (n == 1) ini[0] = toupper(ini[0]); }
  }
  lv_obj_t* l = label(a, ini, &F12B, FG[k]);
  lv_obj_center(l);
  lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
  return a;
}

static lv_obj_t* badge(lv_obj_t* p, int n) {
  char b[8]; snprintf(b, sizeof(b), "%d", n > 99 ? 99 : n);
  lv_obj_t* o = box(p, LV_SIZE_CONTENT, 15, C_AMBER, 8);
  lv_obj_set_style_min_width(o, 15, 0);
  lv_obj_set_style_pad_hor(o, 4, 0);
  lv_obj_t* l = label(o, b, &F10, C_AMBER_INK);
  lv_obj_set_style_text_font(l, &inter_10, 0);
  lv_obj_center(l);
  return o;
}

static lv_obj_t* screen_root() {
  lv_obj_t* scr = lv_scr_act();
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_t* root = col(scr, 240, 320);
  return root;
}

// ---- time helpers ----
static uint32_t now_epoch() { return rtc_clock.getCurrentTime(); }
// After a reboot without the phone, ESP32RTCClock restarts at RTC_TIME_MIN (1 Mar 2026). Treat anything
// within a week of that seed as "not set": logins stamped with it are dropped by repeaters as replays.
#define CLOCK_SEED 1772323200UL
static bool clock_valid() { return now_epoch() > CLOCK_SEED + 7 * 86400UL; }

static void local_tm(uint32_t epoch, struct tm* out) {
  time_t t = (time_t)epoch + (time_t)g_ui->prefs().tz_hours * 3600;
  gmtime_r(&t, out);
}

static void fmt_hm(uint32_t epoch, char* buf, size_t n) {
  if (epoch < 1700000000UL) { snprintf(buf, n, "--:--"); return; }
  struct tm tm; local_tm(epoch, &tm);
  snprintf(buf, n, "%02d:%02d", tm.tm_hour, tm.tm_min);
}

// short "when" for list rows: today -> HH:MM, this week -> weekday, else d/m
static void fmt_when(uint32_t epoch, char* buf, size_t n) {
  static const char* WD[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
  if (epoch < 1700000000UL || !clock_valid()) { buf[0] = 0; return; }
  uint32_t now = now_epoch();
  struct tm a, b; local_tm(epoch, &a); local_tm(now, &b);
  if (a.tm_yday == b.tm_yday && a.tm_year == b.tm_year) fmt_hm(epoch, buf, n);
  else if (now - epoch < 6 * 86400UL) snprintf(buf, n, "%s", WD[a.tm_wday]);
  else snprintf(buf, n, "%d/%d", a.tm_mday, a.tm_mon + 1);
}

static void fmt_ago(uint32_t epoch, char* buf, size_t n) {
  if (epoch < 1700000000UL || !clock_valid()) { buf[0] = 0; return; }
  uint32_t d = now_epoch() > epoch ? now_epoch() - epoch : 0;
  if (d < 60) snprintf(buf, n, "now");
  else if (d < 3600) snprintf(buf, n, "%lum", (unsigned long)(d / 60));
  else if (d < 86400) snprintf(buf, n, "%luh", (unsigned long)(d / 3600));
  else snprintf(buf, n, "%lud", (unsigned long)(d / 86400));
}


// Route text like the phone app: "direct", "via yHR1", "via A › B", or "flood".
// out_path_len packs the hop count (low 6 bits) and the hash size per hop ((len >> 6) + 1).
static void route_text(const ContactInfo& ci, char* buf, size_t n) {
  if (ci.out_path_len == OUT_PATH_UNKNOWN) { snprintf(buf, n, "flood"); return; }
  uint8_t hops = ci.out_path_len & 63, hsz = (ci.out_path_len >> 6) + 1;
  if (hops == 0) { snprintf(buf, n, "direct"); return; }
  size_t len = snprintf(buf, n, "via ");
  for (int i = 0; i < hops && len < n - 1; i++) {
    const uint8_t* h = &ci.out_path[i * hsz];
    char hop[24];
    ContactInfo* r = the_mesh.lookupContactByPubKey(h, hsz);
    if (r && r->name[0]) snprintf(hop, sizeof(hop), "%s", r->name);
    else if (hsz == 1) snprintf(hop, sizeof(hop), "%02X", h[0]);
    else snprintf(hop, sizeof(hop), "%02X%02X", h[0], h[1]);
    len += snprintf(buf + len, n - len, "%s%s", i ? " \xE2\x80\xBA " : "", hop);
  }
}

static int batt_percent(uint16_t mv) {
  static const uint16_t V[] = { 3300, 3600, 3700, 3750, 3800, 3850, 3950, 4050, 4150 };
  static const uint8_t  P[] = { 0,    5,    15,   25,   40,   55,   70,   85,   100 };
  if (mv <= V[0]) return 0;
  for (int i = 1; i < 9; i++)
    if (mv <= V[i]) return P[i - 1] + (P[i] - P[i - 1]) * (mv - V[i - 1]) / (V[i] - V[i - 1]);
  return 100;
}

// =====================================================================
// shared chrome: tab bar, headers, toast
// =====================================================================
static void nav_cb(lv_event_t* e) {
  g_ui->show((CydScreen)(intptr_t)lv_event_get_user_data(e));
}

static void tab_bar(lv_obj_t* root, CydScreen active, int unread) {
  lv_obj_t* bar = box(root, 240, 46, C_CARD);
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_width(bar, 1, 0);
  lv_obj_set_style_border_color(bar, lv_color_hex(C_BORDER), 0);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
  struct T { CydScreen s; const char* icon; const char* name; } tabs[] = {
    { CydScreen::Home, I_HOME, "Home" }, { CydScreen::Chats, I_CHAT, "Chats" },
    { CydScreen::Contacts, I_USERS, "Contacts" }, { CydScreen::Settings, I_SLIDERS, "Settings" },
  };
  for (auto& t : tabs) {
    bool on = t.s == active;
    uint32_t c = on ? C_ACCENT : C_TXT2;
    lv_obj_t* b = col(bar, 60, 46, 2);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, nav_cb, LV_EVENT_CLICKED, (void*)(intptr_t)t.s);
    label(b, t.icon, &icons_18, c);
    lv_obj_t* l = label(b, t.name, &F10, c);
    lv_obj_set_style_text_font(l, &inter_10, 0);
    if (t.s == CydScreen::Chats && unread > 0) {
      lv_obj_t* bd = badge(b, unread);
      lv_obj_add_flag(bd, LV_OBJ_FLAG_IGNORE_LAYOUT);
      lv_obj_align(bd, LV_ALIGN_TOP_MID, 12, 4);
    }
  }
}

static lv_obj_t* title_bar(lv_obj_t* root, const char* title) {
  lv_obj_t* r = row(root, 32);
  lv_obj_set_style_pad_hor(r, 10, 0);
  label(r, title, &F16B, C_TXT);
  spacer(r);
  return r;
}

static lv_obj_t* icon_btn(lv_obj_t* p, const char* icon, lv_coord_t w, lv_coord_t h, uint32_t bg, uint32_t fg,
                          lv_event_cb_t cb, void* ud, lv_coord_t radius = 8) {
  lv_obj_t* b = box(p, w, h, bg, radius, bg != 0);
  lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(b, lv_color_hex(bg ? (bg == C_ACCENT ? 0x8AD8FF : C_KEY) : C_CARD2), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_t* l = label(b, icon, &icons_14, fg);
  lv_obj_center(l);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
  return b;
}

static void pressable(lv_obj_t* o, uint32_t pressed_bg) {
  lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(o, lv_color_hex(pressed_bg), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_STATE_PRESSED);
}

void UITask::toast(const char* text) {
  if (g_toast) { lv_obj_del(g_toast); g_toast = nullptr; }
  g_toast = box(lv_layer_top(), LV_SIZE_CONTENT, 30, C_KEY, 15);
  lv_obj_set_style_pad_hor(g_toast, 14, 0);
  lv_obj_t* l = label(g_toast, text, &F12B, C_TXT);
  lv_obj_center(l);
  lv_obj_align(g_toast, LV_ALIGN_BOTTOM_MID, 0, -56);
  _toast_until = millis() + 1800;
}

// =====================================================================
// prefs
// =====================================================================
void UITask::loadPrefs() {
  File f = SPIFFS.open("/meshsyd", "r");
  if (f) {
    CydPrefs p;
    if (f.read((uint8_t*)&p, sizeof(p)) == sizeof(p) && p.magic == 0xC7) _prefs = p;
    f.close();
  }
  if (_prefs.brightness < 10) _prefs.brightness = 10;
  if (_prefs.screen_off_idx > 4) _prefs.screen_off_idx = 1;
}

void UITask::savePrefs() {
  File f = SPIFFS.open("/meshsyd", "w");
  if (f) { f.write((const uint8_t*)&_prefs, sizeof(_prefs)); f.close(); }
}

// Chat history: conversations + message ring, written a few seconds after changes.
struct HistHdr { uint32_t magic; uint16_t msg_size, conv_size, max_msgs, max_convs, head, count; };
#define HIST_MAGIC 0x4D534831   // "MSH1"

void UITask::loadHistory() {
  File f = SPIFFS.open("/meshsyd_hist", "r");
  if (!f) return;
  HistHdr h;
  bool ok = f.read((uint8_t*)&h, sizeof(h)) == sizeof(h) && h.magic == HIST_MAGIC &&
            h.msg_size == sizeof(CydMsg) && h.conv_size == sizeof(CydConv) &&
            h.max_msgs == CYD_MAX_MSGS && h.max_convs == CYD_MAX_CONVS &&
            f.read((uint8_t*)_convs, sizeof(_convs)) == sizeof(_convs) &&
            f.read((uint8_t*)_msgs, sizeof(CydMsg) * CYD_MAX_MSGS) == sizeof(CydMsg) * CYD_MAX_MSGS;
  f.close();
  if (!ok) { memset(_convs, 0, sizeof(_convs)); memset(_msgs, 0, sizeof(CydMsg) * CYD_MAX_MSGS); return; }
  _msg_head = h.head % CYD_MAX_MSGS;
  _msg_count = h.count > CYD_MAX_MSGS ? CYD_MAX_MSGS : h.count;
  for (int i = 0; i < CYD_MAX_MSGS; i++) {           // pending states can't complete after a reboot
    if (_msgs[i].status == 1) _msgs[i].status = 0;
    _msgs[i].tag = 0;
  }
}

void UITask::saveHistory() {
  _save_due = 0;
  File f = SPIFFS.open("/meshsyd_hist", "w");
  if (!f) return;
  HistHdr h = { HIST_MAGIC, sizeof(CydMsg), sizeof(CydConv), CYD_MAX_MSGS, CYD_MAX_CONVS, _msg_head, _msg_count };
  f.write((const uint8_t*)&h, sizeof(h));
  f.write((const uint8_t*)_convs, sizeof(_convs));
  f.write((const uint8_t*)_msgs, sizeof(CydMsg) * CYD_MAX_MSGS);
  f.close();
}

void UITask::applyBrightness() {
  if (_screen_on) display.gfx()->setBrightness(_prefs.brightness);
}

unsigned long UITask::screenOffMillis() const { return SCREEN_OFF_MS[_prefs.screen_off_idx]; }

void UITask::wake() {
  _last_touch = millis();
  if (!_screen_on) {
    _screen_on = true;
    display.gfx()->setBrightness(_prefs.brightness);
    _dirty = true;
  }
}

void UITask::onTouchActivity() {
  if (!_screen_on) { wake(); _swallow_touch = true; }
  _last_touch = millis();
}

bool UITask::consumeWakeTouch() { return _swallow_touch; }
void UITask::endWakeTouch() { _swallow_touch = false; }

// =====================================================================
// data
// =====================================================================
int UITask::findOrAddChannelConv(int chan_idx, const char* name) {
  int free_i = -1;
  for (int i = 0; i < CYD_MAX_CONVS; i++) {
    if (_convs[i].used && _convs[i].is_channel && _convs[i].chan_idx == chan_idx) {
      if (name && name[0]) strncpy(_convs[i].name, name, sizeof(_convs[i].name) - 1);
      return i;
    }
    if (!_convs[i].used && free_i < 0) free_i = i;
  }
  if (free_i < 0) return -1;
  CydConv& c = _convs[free_i];
  memset(&c, 0, sizeof(c));
  c.used = 1; c.is_channel = 1; c.chan_idx = chan_idx;
  strncpy(c.name, (name && name[0]) ? name : "Channel", sizeof(c.name) - 1);
  return free_i;
}

int UITask::findOrAddContactConv(const ContactInfo& ci) {
  int free_i = -1, oldest = -1;
  for (int i = 0; i < CYD_MAX_CONVS; i++) {
    if (_convs[i].used && !_convs[i].is_channel && memcmp(_convs[i].pub_prefix, ci.id.pub_key, 6) == 0) {
      strncpy(_convs[i].name, ci.name, sizeof(_convs[i].name) - 1);
      return i;
    }
    if (!_convs[i].used && free_i < 0) free_i = i;
    if (_convs[i].used && !_convs[i].is_channel && (oldest < 0 || _convs[i].last_ts < _convs[oldest].last_ts)) oldest = i;
  }
  int k = free_i >= 0 ? free_i : oldest;
  if (k < 0) return -1;
  CydConv& c = _convs[k];
  memset(&c, 0, sizeof(c));
  c.used = 1; c.is_channel = 0; c.type = ci.type;
  memcpy(c.pub_prefix, ci.id.pub_key, 6);
  strncpy(c.name, ci.name, sizeof(c.name) - 1);
  return k;
}

void UITask::refreshChannels() {
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    ChannelDetails cd;
    if (the_mesh.getChannel(i, cd) && cd.name[0]) findOrAddChannelConv(i, cd.name);
  }
}

void UITask::addMsg(int conv, bool out, const char* sender, const char* text, uint32_t ts, uint32_t ack, uint8_t status) {
  if (conv < 0) return;
  CydMsg& m = _msgs[_msg_head];
  _msg_head = (_msg_head + 1) % CYD_MAX_MSGS;
  if (_msg_count < CYD_MAX_MSGS) _msg_count++;
  memset(&m, 0, sizeof(m));
  m.ts = ts; m.ack = ack; m.conv = conv; m.out = out; m.status = status;
  strncpy(m.sender, sender ? sender : "", sizeof(m.sender) - 1);
  strncpy(m.text, text, sizeof(m.text) - 1);

  CydConv& c = _convs[conv];
  c.last_ts = ts;
  if (out) snprintf(c.last, sizeof(c.last), "You: %s", text);
  else if (c.is_channel && sender && sender[0]) snprintf(c.last, sizeof(c.last), "%s: %s", sender, text);
  else strncpy(c.last, text, sizeof(c.last) - 1);
  bool viewing = _screen_on && (_screen == CydScreen::Conversation || _screen == CydScreen::Compose) && _open_conv == conv;
  if (!out && !viewing) c.unread++;
  _dirty = true;
  historyChanged();
}

int UITask::totalUnread() const {
  int n = 0;
  for (int i = 0; i < CYD_MAX_CONVS; i++) if (_convs[i].used) n += _convs[i].unread;
  return n;
}

// ---- mesh listener ----
void UITask::onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t path_len, const uint8_t* path) {
  // Clock not set yet (no phone since reboot): adopt the advert timestamp of a node whose clock looks sane.
  uint32_t ts = contact.last_advert_timestamp;
  if (!clock_valid() && ts > CLOCK_SEED + 7 * 86400UL && ts < 2208988800UL /* 2040 */) {
    rtc_clock.setCurrentTime(ts);
#ifdef MESH_DEBUG
    Serial.printf("MeshSyd: clock set from %s advert: %lu\n", contact.name, (unsigned long)ts);
#endif
    toast("Clock set from the mesh");
  }
  _dirty = true;
}


void UITask::onMessageRecv(mesh::Packet* pkt, const ContactInfo& from, uint8_t txt_type, uint32_t sender_timestamp, const char* text) {
  if (txt_type == TXT_TYPE_CLI_DATA) {
    if (memcmp(from.id.pub_key, _admin_pub, 6) == 0) { _cli_pending = false; consoleAppend("", text); }
    return;
  }
  if (txt_type != TXT_TYPE_PLAIN && txt_type != TXT_TYPE_SIGNED_PLAIN) return;
  int k = findOrAddContactConv(from);
  addMsg(k, false, from.name, text, clock_valid() ? now_epoch() : sender_timestamp, 0, 0);
  wake();
}

void UITask::onChannelMessageRecv(mesh::Packet* pkt, ChannelDetails& cd, const char* text) {
  int idx = -1;
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    ChannelDetails x;
    if (the_mesh.getChannel(i, x) && memcmp(x.channel.hash, cd.channel.hash, sizeof(x.channel.hash)) == 0) { idx = i; break; }
  }
  int k = findOrAddChannelConv(idx < 0 ? 0 : idx, cd.name);
  // channel text arrives as "Sender: message"
  char sender[24] = "";
  const char* body = text;
  const char* sep = strstr(text, ": ");
  if (sep && sep - text < (int)sizeof(sender)) {
    memcpy(sender, text, sep - text); sender[sep - text] = 0;
    body = sep + 2;
  }
  addMsg(k, false, sender, body, now_epoch(), 0, 0);
  wake();
}

void UITask::onEchoHeard(uint32_t ui_tag, uint8_t count) {
  for (int i = 0; i < _msg_count; i++)
    if (_msgs[i].out && _msgs[i].tag == ui_tag) { _msgs[i].echoes = count; _dirty = true; historyChanged(); break; }
}

void UITask::onACKRecv(uint32_t ack_crc) {
  for (int i = 0; i < _msg_count; i++) {
    CydMsg& m = _msgs[i];
    if (m.out && m.status == 1 && m.ack == ack_crc) { m.status = 2; _dirty = true; historyChanged(); }
  }
}

// ---- actions ----
void UITask::sendAdvert() {
  toast(the_mesh.advert() ? "Advert sent" : "Advert failed");
}

void UITask::openConversation(int k) {
  if (k < 0) return;
  _open_conv = k;
  if (_convs[k].unread) { _convs[k].unread = 0; historyChanged(); }
  show(CydScreen::Conversation);
}

void UITask::openContact(int idx) {
  ContactInfo ci;
  if (!the_mesh.getContactByIdx(idx, ci)) return;
  memcpy(_detail_pub, ci.id.pub_key, 32);
  _delete_armed = false;
  _ping_result[0] = 0; _ping_tag = 0;
  show(CydScreen::ContactDetail);
}

void UITask::contactMessage() {
  ContactInfo* ci = the_mesh.lookupContactByPubKey(_detail_pub, 32);
  if (ci) openConversation(findOrAddContactConv(*ci));
}

void UITask::contactResetPath() {
  toast(the_mesh.uiResetPath(_detail_pub) ? "Path reset \xE2\x80\x94 next message floods" : "Contact not found");
  _dirty = true;
}

void UITask::contactDelete() {
  if (!_delete_armed) { _delete_armed = true; show(CydScreen::ContactDetail); return; }
  // drop any conversation with them too
  for (int i = 0; i < CYD_MAX_CONVS; i++)
    if (_convs[i].used && !_convs[i].is_channel && memcmp(_convs[i].pub_prefix, _detail_pub, 6) == 0) {
      _convs[i].used = 0;
      if (_open_conv == i) _open_conv = -1;
      historyChanged();
    }
  bool ok = the_mesh.uiRemoveContact(_detail_pub);
  toast(ok ? "Contact deleted" : "Delete failed");
  show(CydScreen::Contacts);
}

// ---- route editor ----
void UITask::pathEditOpen() {
  ContactInfo* ci = the_mesh.lookupContactByPubKey(_detail_pub, 32);
  if (!ci) return;
  _pe_n = 0; _pe_hsz = 1;
  if (ci->out_path_len != OUT_PATH_UNKNOWN) {
    _pe_hsz = (ci->out_path_len >> 6) + 1;
    uint8_t hops = ci->out_path_len & 63;
    for (int i = 0; i < hops && i < 8; i++) { memcpy(_pe_path[i], &ci->out_path[i * _pe_hsz], _pe_hsz); _pe_n++; }
  }
  show(CydScreen::PathEdit);
}

void UITask::pathEditAdd(int idx) {
  ContactInfo r;
  if (_pe_n >= 8) { toast("Max 8 hops"); return; }
  if (!the_mesh.getContactByIdx(idx, r)) return;
  memcpy(_pe_path[_pe_n++], r.id.pub_key, _pe_hsz);
  show(CydScreen::PathEdit);
}

void UITask::pathEditRemove(int hop) {
  if (hop < 0 || hop >= _pe_n) return;
  memmove(_pe_path[hop], _pe_path[hop + 1], (size_t)(_pe_n - hop - 1) * sizeof(_pe_path[0]));
  _pe_n--;
  show(CydScreen::PathEdit);
}

// Typed route: hex hop IDs separated by spaces, commas or ">", e.g. "a3 5f 1c" or "a35f,1c2d".
void UITask::pathEditTyped(const char* text) {
  uint8_t hops[8][4]; int n = 0, hsz = 0;
  const char* p = text;
  while (*p) {
    while (*p && !isxdigit((uint8_t)*p)) p++;
    if (!*p) break;
    const char* st = p;
    while (isxdigit((uint8_t)*p)) p++;
    int len = p - st;
    if (len % 2 || len < 2 || len > 8) { toast("Each ID needs 2, 4, 6 or 8 hex digits"); show(CydScreen::PathInput); return; }
    if (hsz && len / 2 != hsz) { toast("All IDs must be the same length"); show(CydScreen::PathInput); return; }
    if (n >= 8) { toast("Max 8 hops"); show(CydScreen::PathInput); return; }
    hsz = len / 2;
    for (int i = 0; i < hsz; i++) { char b[3] = { st[i * 2], st[i * 2 + 1], 0 }; hops[n][i] = (uint8_t)strtoul(b, nullptr, 16); }
    n++;
  }
  if (hsz == 3) { toast("Use 1, 2 or 4-byte IDs"); show(CydScreen::PathInput); return; }
  _pe_n = n; _pe_hsz = hsz ? hsz : 1;
  for (int i = 0; i < n; i++) memcpy(_pe_path[i], hops[i], _pe_hsz);
  show(CydScreen::PathEdit);
}

void UITask::buildPathInput() {
  char cur[64] = ""; size_t len = 0;
  for (int i = 0; i < _pe_n && len < sizeof(cur) - 10; i++) {
    if (i) cur[len++] = ' ';
    for (int b = 0; b < _pe_hsz; b++) len += snprintf(cur + len, sizeof(cur) - len, "%02x", _pe_path[i][b]);
  }
  cur[len] = 0;
  input_screen("Route IDs", cur, 60, false, 5, CydScreen::PathEdit,
               "Hex IDs in order, separated by spaces: e.g. a3 5f 1c. Empty = direct.");
}

void UITask::pathEditSave(bool flood) {
  bool ok;
  if (flood) ok = the_mesh.uiResetPath(_detail_pub);
  else {
    uint8_t flat[32];
    for (int i = 0; i < _pe_n; i++) memcpy(&flat[i * _pe_hsz], _pe_path[i], _pe_hsz);
    ok = the_mesh.uiSetPath(_detail_pub, flat, _pe_n, _pe_hsz);
  }
  toast(!ok ? "Could not save route" : flood ? "Route cleared \xE2\x80\x94 flood" : _pe_n ? "Route saved" : "Route saved: direct");
  show(CydScreen::ContactDetail);
}

// ---- remote admin ----
void UITask::loadPasswords() {
  memset(_pws, 0, sizeof(_pws));
  File f = SPIFFS.open("/meshsyd_pw", "r");
  if (f) { f.read((uint8_t*)_pws, sizeof(_pws)); f.close(); }
}

void UITask::savePasswords() {
  File f = SPIFFS.open("/meshsyd_pw", "w");
  if (f) { f.write((const uint8_t*)_pws, sizeof(_pws)); f.close(); }
}

const char* UITask::savedPassword(const uint8_t* pub) const {
  for (auto& e : _pws) if (e.pw[0] && memcmp(e.pub, pub, 6) == 0) return e.pw;
  // an empty saved password (guest) is stored as a single 0x01 marker
  for (auto& e : _pws) if (e.pw[0] == 0 && e.pw[1] == 1 && memcmp(e.pub, pub, 6) == 0) return "";
  return nullptr;
}

void UITask::rememberPassword(const uint8_t* pub, const char* pw) {
  SavedPw* slot = nullptr;
  for (auto& e : _pws) if ((e.pw[0] || e.pw[1]) && memcmp(e.pub, pub, 6) == 0) slot = &e;
  if (!slot) for (auto& e : _pws) if (!e.pw[0] && !e.pw[1]) { slot = &e; break; }
  if (!slot) { memmove(&_pws[0], &_pws[1], sizeof(SavedPw) * 7); slot = &_pws[7]; }   // drop the oldest
  memset(slot, 0, sizeof(*slot));
  memcpy(slot->pub, pub, 6);
  if (pw[0]) strncpy(slot->pw, pw, sizeof(slot->pw) - 1); else slot->pw[1] = 1;
  savePasswords();
}

void UITask::forgetPassword() {
  for (auto& e : _pws) if (memcmp(e.pub, _detail_pub, 6) == 0) memset(&e, 0, sizeof(e));
  savePasswords();
  if (memcmp(_admin_pub, _detail_pub, 6) == 0) { _admin_ok = false; _status_len = 0; }
  _admin_pw[0] = 0;
  toast("Password forgotten");
  show(CydScreen::ContactDetail);
}

void UITask::adminOpen(CydScreen target) {
  if (isAdminFor(_detail_pub)) {
    if (target == CydScreen::Status && !_status_len) requestStatus();
    show(target);
    return;
  }
  _after_login = target;
  _login_msg[0] = 0;
  const char* pw = savedPassword(_detail_pub);
  if (pw) { submitLogin(pw); return; }   // saved: log in without asking
  _admin_pw[0] = 0;
  show(CydScreen::Login);
}

void UITask::submitLogin(const char* password) {
  if (!clock_valid()) {
    // repeaters reject logins stamped earlier than the last one they accepted
    snprintf(_login_msg, sizeof(_login_msg), "Clock not set. Connect the phone app or wait for an advert.");
    strncpy(_admin_pw, password, sizeof(_admin_pw) - 1);
    show(CydScreen::Login);
    return;
  }
  strncpy(_admin_pw, password, sizeof(_admin_pw) - 1);
  uint32_t timeout = 0;
  int r = the_mesh.uiSendLogin(_detail_pub, password, timeout);
  if (r == MSG_SEND_FAILED) { toast("Login failed to send"); show(CydScreen::ContactDetail); return; }
  _login_pending = true;
  _login_deadline = millis() + (timeout ? timeout : 10000) + 3000;
  _admin_ok = false;
  memcpy(_admin_pub, _detail_pub, 32);   // MyMesh lookups compare the full 32-byte key
  _status_len = 0;
  toast("Logging in\xE2\x80\xA6");
  show(CydScreen::ContactDetail);
}

void UITask::onLoginResult(const ContactInfo& contact, bool ok, uint8_t permissions) {
  if (!_login_pending || memcmp(contact.id.pub_key, _admin_pub, 6) != 0) return;
  _login_pending = false;
  _admin_ok = ok;
  if (!ok) {
    for (auto& e : _pws) if (memcmp(e.pub, _admin_pub, 6) == 0) memset(&e, 0, sizeof(e));
    savePasswords();
    _admin_pw[0] = 0;
    snprintf(_login_msg, sizeof(_login_msg), "Wrong password, try again.");
    show(CydScreen::Login);
    return;
  }
  rememberPassword(_admin_pub, _admin_pw);
  toast((permissions & 0x03) == 0x03 || (permissions & 0x01) ? "Logged in (admin)" : "Logged in");
  if (_after_login == CydScreen::Status) requestStatus();
  show(_after_login);
}

void UITask::requestStatus() {
  uint32_t timeout = 0;
  if (the_mesh.uiSendStatusReq(_detail_pub, timeout) == MSG_SEND_FAILED) { toast("Status request failed"); return; }
  _status_pending = true;
  _status_deadline = millis() + (timeout ? timeout : 10000) + 3000;
  _dirty = true;
}

void UITask::onStatusResult(const ContactInfo& contact, const uint8_t* data, uint8_t len) {
  if (memcmp(contact.id.pub_key, _detail_pub, 6) != 0) return;
  _status_pending = false;
  _status_len = len > sizeof(_status) ? sizeof(_status) : len;
  memcpy(_status, data, _status_len);
  _status_at = millis();
  _dirty = true;
}

void UITask::consoleAppend(const char* prefix, const char* text) {
  if (!_console) { _console = (char*)calloc(CYD_CONSOLE_LEN, 1); if (!_console) return; }
  char line[200];
  snprintf(line, sizeof(line), "%s%s\n", prefix, text);
  size_t cur = strlen(_console), add = strlen(line);
  if (add >= CYD_CONSOLE_LEN) return;
  if (cur + add >= CYD_CONSOLE_LEN) {   // drop oldest lines
    size_t drop = cur + add - CYD_CONSOLE_LEN + 1;
    char* nl = strchr(_console + drop, '\n');
    size_t cut = nl ? (nl - _console) + 1 : cur;
    memmove(_console, _console + cut, cur - cut + 1);
    cur -= cut;
  }
  memcpy(_console + cur, line, add + 1);
  _dirty = true;
}

void UITask::sendCli(const char* cmd) {
  uint32_t timeout = 0;
  consoleAppend("> ", cmd);
  if (the_mesh.uiSendCli(_admin_pub, cmd, timeout) == MSG_SEND_FAILED) consoleAppend("", "(send failed)");
  else { _cli_pending = true; _cli_deadline = millis() + (timeout ? timeout : 10000) + 4000; }
  show(CydScreen::Console);
}

// Ping = trace round trip along the contact's stored route. Only repeaters (and room servers)
// forward traces, so for a person the trace turns around at the last repeater before them.
void UITask::contactPing() {
  ContactInfo* ci = the_mesh.lookupContactByPubKey(_detail_pub, 32);
  if (!ci) return;
  bool fwd = ci->type == ADV_TYPE_REPEATER || ci->type == ADV_TYPE_ROOM;
  uint8_t hops = ci->out_path_len == OUT_PATH_UNKNOWN ? 0 : (ci->out_path_len & 63);
  uint8_t hsz = ci->out_path_len == OUT_PATH_UNKNOWN ? 1 : (ci->out_path_len >> 6) + 1;
  uint8_t path[MAX_PATH_SIZE];
  int n = 0;
  for (int i = 0; i < hops; i++) { memcpy(&path[n], &ci->out_path[i * hsz], hsz); n += hsz; }
  if (fwd) { memcpy(&path[n], ci->id.pub_key, hsz); n += hsz; }
  else if (hops == 0) {
    snprintf(_ping_result, sizeof(_ping_result), "Ping needs a repeater on the route");
    show(CydScreen::ContactDetail); return;
  }   // person: the last repeater (already in path) is the turn-around point
  for (int i = (fwd ? hops : hops - 1) - 1; i >= 0; i--) { memcpy(&path[n], &ci->out_path[i * hsz], hsz); n += hsz; }
  uint8_t flags = hsz == 1 ? 0 : hsz == 2 ? 1 : hsz == 4 ? 2 : 3;
  uint32_t timeout = 0;
  _ping_tag = esp_random();
  if (!the_mesh.uiSendTrace(path, n, flags, _ping_tag, timeout)) {
    snprintf(_ping_result, sizeof(_ping_result), "Ping failed to send");
  } else {
    _ping_sent = millis();
    _ping_deadline = _ping_sent + (timeout ? timeout : 8000) + 2000;
    snprintf(_ping_result, sizeof(_ping_result), "Pinging\xE2\x80\xA6");
  }
  show(CydScreen::ContactDetail);
}

void UITask::onTraceRecv(mesh::Packet* pkt, uint32_t tag, uint32_t auth_code, uint8_t flags,
                         const uint8_t* path_snrs, const uint8_t* path_hashes, uint8_t path_len) {
  if (!_ping_tag || tag != _ping_tag) return;
  _ping_tag = 0;
  uint8_t hops = path_len >> (flags & 0x03);
  unsigned long rtt = millis() - _ping_sent;
  int len = snprintf(_ping_result, sizeof(_ping_result), "%lu.%lu s", rtt / 1000, (rtt % 1000) / 100);
  // SNR at each hop (dB), plus ours for the final leg
  for (int i = 0; i < hops && len < (int)sizeof(_ping_result) - 8; i++)
    len += snprintf(_ping_result + len, sizeof(_ping_result) - len, "%s%d", i ? " / " : " \xC2\xB7 SNR ", (int8_t)path_snrs[i] / 4);
  if (len < (int)sizeof(_ping_result) - 8)
    snprintf(_ping_result + len, sizeof(_ping_result) - len, "%s%d dB", hops ? " / " : " \xC2\xB7 SNR ", (int)pkt->getSNR());
  _dirty = true;
}

void UITask::sendCompose(const char* text) {
  if (_open_conv < 0 || !text || !text[0]) return;
  CydConv& c = _convs[_open_conv];
  uint32_t ts = rtc_clock.getCurrentTimeUnique();
  uint32_t tag = ++_tag_ctr;
  the_mesh.uiTrackNextTx(tag, c.is_channel ? PAYLOAD_TYPE_GRP_TXT : PAYLOAD_TYPE_TXT_MSG);
  if (c.is_channel) {
    ChannelDetails cd;
    if (!the_mesh.getChannel(c.chan_idx, cd)) { toast("Channel not found"); return; }
    bool ok = the_mesh.sendGroupMessage(ts, cd.channel, the_mesh.getNodeName(), text, strlen(text));
#ifdef MESH_DEBUG
    Serial.printf("MeshSyd: channel send '%s' idx %d ts %lu -> %s\n", cd.name, c.chan_idx, (unsigned long)ts, ok ? "queued" : "FAILED");
#endif
    addMsg(_open_conv, true, nullptr, text, now_epoch(), 0, ok ? 2 : 3);
  } else {
    ContactInfo* ci = the_mesh.lookupContactByPubKey(c.pub_prefix, 6);
    if (!ci) { toast("Contact not found"); return; }
    uint32_t ack = 0, timeout = 0;
    int r = the_mesh.sendMessage(*ci, ts, 0, text, ack, timeout);
    addMsg(_open_conv, true, nullptr, text, now_epoch(), ack, r == MSG_SEND_FAILED ? 3 : 1);
  }
  _msgs[(_msg_head + CYD_MAX_MSGS - 1) % CYD_MAX_MSGS].tag = tag;
  show(CydScreen::Conversation);
}

void UITask::setNodeName(const char* name) {
  if (!name || !name[0]) return;
  strncpy(_node_prefs->node_name, name, sizeof(_node_prefs->node_name) - 1);
  _node_prefs->node_name[sizeof(_node_prefs->node_name) - 1] = 0;
  the_mesh.savePrefs();
  toast("Name saved");
}

void UITask::toggleBluetooth() {
  if (isBluetoothEnabled()) disableBluetooth(); else enableBluetooth();
  _dirty = true;
}

// =====================================================================
// screens
// =====================================================================
void UITask::buildHome() {
  lv_obj_t* root = screen_root();

  // status bar
  lv_obj_t* sb = row(root, 20, 5);
  lv_obj_set_style_pad_hor(sb, 8, 0);
  label(sb, _node_prefs->node_name, &F12B, C_TXT);
  spacer(sb);
  bool conn = hasConnection();
  if (isBluetoothEnabled()) label(sb, I_BT, &icons_14, conn ? C_ACCENT : C_TXT3);
  uint16_t mv = getBattMilliVolts();
  int pct = batt_percent(mv);
  char b[16]; snprintf(b, sizeof(b), "%d%%", pct);
  lv_obj_t* bl = label(sb, b, &F10, C_TXT2);
  lv_obj_set_style_text_font(bl, &inter_10, 0);
  label(sb, I_BATT[pct > 87 ? 4 : pct > 62 ? 3 : pct > 37 ? 2 : pct > 12 ? 1 : 0], &icons_14, pct > 20 ? C_GREEN : C_RED);

  lv_obj_t* body = col(root, 240, 1, 8);
  lv_obj_set_flex_grow(body, 1);
  lv_obj_set_style_pad_hor(body, 10, 0);
  lv_obj_set_style_pad_top(body, 6, 0);
  lv_obj_set_style_pad_bottom(body, 8, 0);

  // clock
  lv_obj_t* clk = col(body, lv_pct(100), LV_SIZE_CONTENT, 2);
  lv_obj_set_flex_align(clk, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  char hm[8]; fmt_hm(clock_valid() ? now_epoch() : 0, hm, sizeof(hm));
  label(clk, hm, &inter_46b, C_TXT);
  char date[40];
  if (clock_valid()) {
    static const char* WD[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
    static const char* MO[] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
    struct tm tm; local_tm(now_epoch(), &tm);
    snprintf(date, sizeof(date), "%s, %d %s", WD[tm.tm_wday], tm.tm_mday, MO[tm.tm_mon]);
  } else {
    snprintf(date, sizeof(date), "Clock not set \xE2\x80\x94 waiting for phone or mesh");
  }
  label(clk, date, &F12, C_TXT2);
  if (isBluetoothEnabled() && !conn) {
    char pin[40]; snprintf(pin, sizeof(pin), I_BT "  Pair with PIN %06lu", (unsigned long)the_mesh.getBLEPin());
    label(clk, pin, &F12, C_ACCENT);
  }

  // unread card
  int unread = totalUnread();
  if (unread > 0) {
    lv_obj_t* card = row(body, 40, 8);
    lv_obj_set_style_bg_color(card, lv_color_hex(C_AMBER_BG), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_pad_hor(card, 10, 0);
    pressable(card, 0x3A2E16);
    lv_obj_add_event_cb(card, nav_cb, LV_EVENT_CLICKED, (void*)(intptr_t)CydScreen::Chats);
    lv_obj_t* n = box(card, 22, 22, C_AMBER, 11);
    char nb[8]; snprintf(nb, sizeof(nb), "%d", unread > 99 ? 99 : unread);
    lv_obj_center(label(n, nb, &F12B, C_AMBER_INK));
    lv_obj_t* tc = col(card, 1, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(tc, 1);
    label(tc, unread == 1 ? "New message" : "New messages", &F12B, C_AMBER_TXT);
    char who[64] = "";
    for (int i = 0; i < CYD_MAX_CONVS; i++) {
      if (_convs[i].used && _convs[i].unread) {
        if (who[0]) strncat(who, ", ", sizeof(who) - strlen(who) - 1);
        strncat(who, _convs[i].name, sizeof(who) - strlen(who) - 1);
      }
    }
    lv_obj_t* wl = label(tc, who, &F10, 0xC9A45A);
    lv_obj_set_style_text_font(wl, &inter_10, 0);
    lv_label_set_long_mode(wl, LV_LABEL_LONG_DOT);
    lv_obj_set_width(wl, lv_pct(100));
    label(card, I_RIGHT, &icons_14, C_AMBER_TXT);
  }

  // stats
  // Nodes heard in the last 24 h. (MyMesh::getRecentlyHeard() always returns its table size,
  // empty slots included, so it can't be used as a count.)
  int nheard = 0;
  if (clock_valid()) {
    uint32_t since = now_epoch() - 86400UL;
    for (int i = MAX_ANON_CONTACTS; i < the_mesh.getTotalContactSlots(); i++) {
      ContactInfo ci;
      if (the_mesh.getContactByIdx(i, ci) && ci.name[0] && ci.lastmod >= since) nheard++;
    }
  } else {
    AdvertPath* heard = (AdvertPath*)malloc(16 * sizeof(AdvertPath));
    if (heard) {
      int n = the_mesh.getRecentlyHeard(heard, 16);
      for (int i = 0; i < n; i++) if (heard[i].recv_timestamp && heard[i].name[0]) nheard++;
      free(heard);
    }
  }
  lv_obj_t* stats = row(body, 48, 6);
  struct S { const char* k; char v[12]; const char* unit; } st[2] = { { "Heard \xC2\xB7 24 h", "", "nodes" }, { "Last RX", "", "dBm" } };
  snprintf(st[0].v, sizeof(st[0].v), "%d", nheard);
  float rssi = radio_driver.getLastRSSI();
  if (rssi < -0.5f) snprintf(st[1].v, sizeof(st[1].v), "\xE2\x88\x92%d", (int)(-rssi + 0.5f));
  else snprintf(st[1].v, sizeof(st[1].v), "--");
  for (auto& s : st) {
    lv_obj_t* c = col(stats, 1, 48, 1);
    lv_obj_set_flex_grow(c, 1);
    lv_obj_set_style_bg_color(c, lv_color_hex(C_CARD), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, 10, 0);
    lv_obj_set_style_pad_hor(c, 9, 0);
    lv_obj_set_style_pad_ver(c, 7, 0);
    lv_obj_t* kl = label(c, s.k, &F10, C_TXT2);
    lv_obj_set_style_text_font(kl, &inter_10, 0);
    lv_obj_t* vr = row(c, LV_SIZE_CONTENT, 3);
    lv_obj_set_flex_align(vr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    label(vr, s.v, &inter_17b, C_TXT);
    lv_obj_t* ul = label(vr, s.unit, &F10, C_TXT2);
    lv_obj_set_style_text_font(ul, &inter_10, 0);
    lv_obj_set_style_pad_bottom(ul, 2, 0);
  }

  spacer(body);

  // advert button
  lv_obj_t* adv = row(body, 38, 7);
  lv_obj_set_style_bg_color(adv, lv_color_hex(C_ACCENT), 0);
  lv_obj_set_style_bg_opa(adv, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(adv, 10, 0);
  lv_obj_set_flex_align(adv, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  pressable(adv, 0x8AD8FF);
  lv_obj_add_event_cb(adv, [](lv_event_t* e) { g_ui->sendAdvert(); }, LV_EVENT_CLICKED, nullptr);
  label(adv, I_TOWER, &icons_14, C_ACC_INK);
  label(adv, "Send advert", &F12B, C_ACC_INK);

  tab_bar(root, CydScreen::Home, unread);
}

static void conv_click_cb(lv_event_t* e) { g_ui->openConversation((int)(intptr_t)lv_event_get_user_data(e)); }

void UITask::buildChats() {
  refreshChannels();
  lv_obj_t* root = screen_root();
  lv_obj_t* tb = title_bar(root, "Chats");
  char hm[8]; fmt_hm(clock_valid() ? now_epoch() : 0, hm, sizeof(hm));
  lv_obj_t* tl = label(tb, hm, &F10, C_TXT2);
  lv_obj_set_style_text_font(tl, &inter_10, 0);

  lv_obj_t* list = col(root, 240, 1, 2);
  lv_obj_set_flex_grow(list, 1);
  lv_obj_set_style_pad_hor(list, 6, 0);
  lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  style_scrollbar(list);

  // order: most recent first; channels without messages last
  int order[CYD_MAX_CONVS], n = 0;
  for (int i = 0; i < CYD_MAX_CONVS; i++) if (_convs[i].used) order[n++] = i;
  for (int i = 1; i < n; i++) {
    int v = order[i], j = i - 1;
    while (j >= 0 && _convs[order[j]].last_ts < _convs[v].last_ts) { order[j + 1] = order[j]; j--; }
    order[j + 1] = v;
  }
  if (n == 0) {
    lv_obj_t* e = label(list, "No chats yet.\nAdd channels in the phone app,\nor open a contact.", &F12, C_TXT3);
    lv_obj_set_style_pad_top(e, 20, 0);
    lv_obj_set_style_text_align(e, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(e, lv_pct(100));
  }
  for (int i = 0; i < n; i++) {
    const CydConv& c = _convs[order[i]];
    lv_obj_t* r = row(list, 40, 8);
    lv_obj_set_style_pad_hor(r, 6, 0);
    lv_obj_set_style_radius(r, 9, 0);
    if (c.unread) { lv_obj_set_style_bg_color(r, lv_color_hex(C_CARD), 0); lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0); }
    pressable(r, C_CARD2);
    lv_obj_add_event_cb(r, conv_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)order[i]);
    avatar(r, 28, c.name, false, c.is_channel);
    lv_obj_t* tc = col(r, 1, LV_SIZE_CONTENT, 1);
    lv_obj_set_flex_grow(tc, 1);
    lv_obj_t* l1 = row(tc, LV_SIZE_CONTENT, 6);
    lv_obj_t* nm = label(l1, c.name, &F12B, C_TXT);
    lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(nm, 1);
    char w[12]; fmt_when(c.last_ts, w, sizeof(w));
    lv_obj_t* wl = label(l1, w, &F10, c.unread ? C_AMBER_TXT : C_TXT3);
    lv_obj_set_style_text_font(wl, &inter_10, 0);
    lv_obj_t* l2 = row(tc, LV_SIZE_CONTENT, 6);
    lv_obj_t* last = label(l2, c.last[0] ? c.last : (c.is_channel ? "Channel" : "No messages"), &F12, C_TXT2);
    lv_obj_set_style_text_font(last, &inter_10, 0);
    lv_label_set_long_mode(last, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(last, 1);
    if (c.unread) badge(l2, c.unread);
  }
  tab_bar(root, CydScreen::Chats, totalUnread());
}

void UITask::buildConversation() {
  if (_open_conv < 0) { show(CydScreen::Chats); return; }
  const CydConv& c = _convs[_open_conv];
  lv_obj_t* root = screen_root();

  // header
  lv_obj_t* hd = row(root, 34, 6);
  lv_obj_set_style_bg_color(hd, lv_color_hex(C_CARD), 0);
  lv_obj_set_style_bg_opa(hd, LV_OPA_COVER, 0);
  lv_obj_set_style_border_side(hd, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(hd, 1, 0);
  lv_obj_set_style_border_color(hd, lv_color_hex(C_BORDER), 0);
  lv_obj_set_style_pad_left(hd, 2, 0);
  lv_obj_set_style_pad_right(hd, 8, 0);
  icon_btn(hd, I_LEFT, 30, 30, 0, C_TXT, nav_cb, (void*)(intptr_t)CydScreen::Chats);
  avatar(hd, 24, c.name, false, c.is_channel);
  lv_obj_t* tc = col(hd, 1, LV_SIZE_CONTENT);
  lv_obj_set_flex_grow(tc, 1);
  lv_obj_t* nm = label(tc, c.name, &F12B, C_TXT);
  lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
  lv_obj_set_width(nm, lv_pct(100));
  char sub[40];
  if (c.is_channel) snprintf(sub, sizeof(sub), "channel");
  else {
    ContactInfo* ci = the_mesh.lookupContactByPubKey(c.pub_prefix, 6);
    if (ci) route_text(*ci, sub, sizeof(sub));
    else snprintf(sub, sizeof(sub), "flood");
  }
  lv_obj_t* sl = label(tc, sub, &F10, c.is_channel ? C_TXT2 : C_GREEN);
  lv_obj_set_style_text_font(sl, &inter_10, 0);
  lv_label_set_long_mode(sl, LV_LABEL_LONG_DOT);
  lv_obj_set_width(sl, lv_pct(100));

  // messages
  lv_obj_t* list = col(root, 240, 1, 5);
  lv_obj_set_flex_grow(list, 1);
  lv_obj_set_style_pad_hor(list, 8, 0);
  lv_obj_set_style_pad_ver(list, 6, 0);
  lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  style_scrollbar(list);
  lv_obj_set_flex_align(list, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  lv_obj_t* last = nullptr;
  int start = (_msg_head + CYD_MAX_MSGS - _msg_count) % CYD_MAX_MSGS;
  int shown = 0;
  for (int i = 0; i < _msg_count; i++) {
    const CydMsg& m = _msgs[(start + i) % CYD_MAX_MSGS];
    if (m.conv != _open_conv) continue;
    shown++;
    lv_obj_t* b = col(list, LV_SIZE_CONTENT, LV_SIZE_CONTENT, 2);
    lv_obj_set_style_bg_color(b, lv_color_hex(m.out ? C_OUT : C_CARD2), 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, 12, 0);
    lv_obj_set_style_pad_hor(b, 9, 0);
    lv_obj_set_style_pad_ver(b, 6, 0);
    if (c.is_channel && !m.out && m.sender[0]) {
      lv_obj_t* sn = label(b, m.sender, &F10, C_ACCENT);
      lv_obj_set_style_text_font(sn, &inter_10, 0);
    }
    lv_obj_t* t = label(b, m.text, &F12, C_TXT);
    lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    lv_point_t sz;   // labels sized to content ignore max_width, so size explicitly
    lv_txt_get_size(&sz, m.text, &F12, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    lv_obj_set_width(t, LV_MIN(sz.x + 1, 172));
    char meta[40]; char hm[8]; fmt_hm(m.ts, hm, sizeof(hm));
    if (m.out) {
      char st[32];
      if (m.status == 3) snprintf(st, sizeof(st), "failed");
      else if (c.is_channel) {
        if (m.echoes) snprintf(st, sizeof(st), "heard %u repeat%s", m.echoes, m.echoes == 1 ? "" : "s");
        else snprintf(st, sizeof(st), "sent");
      } else if (m.status == 2) snprintf(st, sizeof(st), "delivered \xE2\x9C\x93");
      else if (m.status == 0) snprintf(st, sizeof(st), "sent");
      else if (m.echoes) snprintf(st, sizeof(st), "heard %u repeat%s", m.echoes, m.echoes == 1 ? "" : "s");
      else snprintf(st, sizeof(st), "sending\xE2\x80\xA6");
      snprintf(meta, sizeof(meta), "%s \xC2\xB7 %s", hm, st);
    } else snprintf(meta, sizeof(meta), "%s", hm);
    lv_obj_t* ml = label(b, meta, &F10, m.out ? (m.status == 3 ? C_RED : C_OUT_META) : C_TXT3);
    lv_obj_set_style_text_font(ml, &inter_10, 0);
    if (m.out) lv_obj_set_style_text_align(ml, LV_TEXT_ALIGN_RIGHT, 0);
    // right-align our own bubbles inside the column
    if (m.out) {
      lv_obj_t* wrap = row(list, LV_SIZE_CONTENT, 0);
      lv_obj_set_flex_align(wrap, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
      lv_obj_set_parent(b, wrap);
      last = wrap;
    } else last = b;
  }
  if (!shown) {
    lv_obj_t* e = label(list, c.is_channel ? "No messages on this channel yet" : "Say hello \xE2\x80\x94 messages appear here", &F12, C_TXT3);
    lv_obj_set_width(e, lv_pct(100));
    lv_obj_set_style_text_align(e, LV_TEXT_ALIGN_CENTER, 0);
  }

  // input bar
  lv_obj_t* ib = row(root, 40, 6);
  lv_obj_set_style_bg_color(ib, lv_color_hex(C_CARD), 0);
  lv_obj_set_style_bg_opa(ib, LV_OPA_COVER, 0);
  lv_obj_set_style_border_side(ib, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_width(ib, 1, 0);
  lv_obj_set_style_border_color(ib, lv_color_hex(C_BORDER), 0);
  lv_obj_set_style_pad_hor(ib, 6, 0);
  lv_obj_t* fake = row(ib, 30, 0);
  lv_obj_set_flex_grow(fake, 1);
  lv_obj_set_style_bg_color(fake, lv_color_hex(C_BG), 0);
  lv_obj_set_style_bg_opa(fake, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(fake, 15, 0);
  lv_obj_set_style_border_width(fake, 1, 0);
  lv_obj_set_style_border_color(fake, lv_color_hex(0x2D3640), 0);
  lv_obj_set_style_pad_hor(fake, 12, 0);
  pressable(fake, C_CARD2);
  lv_obj_add_event_cb(fake, nav_cb, LV_EVENT_CLICKED, (void*)(intptr_t)CydScreen::Compose);
  char ph[48]; snprintf(ph, sizeof(ph), "Message %s\xE2\x80\xA6", c.name);
  lv_obj_t* pl = label(fake, ph, &F12, C_TXT3);
  lv_label_set_long_mode(pl, LV_LABEL_LONG_DOT);
  lv_obj_set_flex_grow(pl, 1);
  icon_btn(ib, I_SEND, 36, 30, C_ACCENT, C_ACC_INK, nav_cb, (void*)(intptr_t)CydScreen::Compose, 15);

  if (last) { lv_obj_update_layout(list); lv_obj_scroll_to_view(last, LV_ANIM_OFF); }
}

// ---- keyboard (shared by Compose and EditName) ----
static const char* KB_LOWER[] = { "q","w","e","r","t","y","u","i","o","p","\n",
                                  "a","s","d","f","g","h","j","k","l","\n",
                                  I_SHIFT,"z","x","c","v","b","n","m",I_BKSP,"\n",
                                  "123",",","space",".",I_ENTER,"" };
static const char* KB_UPPER[] = { "Q","W","E","R","T","Y","U","I","O","P","\n",
                                  "A","S","D","F","G","H","J","K","L","\n",
                                  I_SHIFT,"Z","X","C","V","B","N","M",I_BKSP,"\n",
                                  "123",",","space",".",I_ENTER,"" };
static const char* KB_NUMS[]  = { "1","2","3","4","5","6","7","8","9","0","\n",
                                  "-","/",":",";","(",")","@","&","\"","\n",
                                  "#+=","?","!","'","*","%","+","=",I_BKSP,"\n",
                                  "abc",",","space",".",I_ENTER,"" };
static const char* KB_SYMS[]  = { "[","]","{","}","#","^","_","\\","|","~","\n",
                                  "<",">","$","`","\xC2\xA3","\xC2\xA5","\xC2\xA7","\xC2\xB0","\xC2\xB7","\n",
                                  "123","-","/",":",";","@","&","\"",I_BKSP,"\n",
                                  "abc",",","space",".",I_ENTER,"" };

static void kb_apply_widths(lv_obj_t* kb) {
  // rows 1-2: equal; row 3: shift/backspace wider; row 4: space wide
  lv_btnmatrix_set_btn_width(kb, 19, 3);   // shift / '#'
  lv_btnmatrix_set_btn_width(kb, 27, 3);   // backspace
  lv_btnmatrix_set_btn_width(kb, 28, 3);   // 123 / abc
  lv_btnmatrix_set_btn_width(kb, 29, 2);   // ,
  lv_btnmatrix_set_btn_width(kb, 30, 8);   // space
  lv_btnmatrix_set_btn_width(kb, 31, 2);   // .
  lv_btnmatrix_set_btn_width(kb, 32, 3);   // enter
}

static void kb_set_map(lv_obj_t* kb) {
  lv_btnmatrix_set_map(kb, g_syms ? KB_SYMS : g_nums ? KB_NUMS : (g_shift ? KB_UPPER : KB_LOWER));
  kb_apply_widths(kb);
}

static void update_count() {
  if (!g_count || !g_ta) return;
  char b[16]; snprintf(b, sizeof(b), "%d / 140", (int)strlen(lv_textarea_get_text(g_ta)));
  lv_label_set_text(g_count, b);
}

// screen user_data = input mode: 1 message, 2 node name, 3 admin password, 4 console command
static void submit_text() {
  if (!g_ta) return;
  static char buf[CYD_MSG_LEN];
  strncpy(buf, lv_textarea_get_text(g_ta), sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = 0;
  int mode = (int)(intptr_t)lv_obj_get_user_data(lv_scr_act());
  int n = strlen(buf);
  while (n && buf[n - 1] == ' ') buf[--n] = 0;
  if (!n && mode != 3 && mode != 5) return;   // empty password (guest) / empty route (direct) are allowed
  g_ta = nullptr; g_count = nullptr;
  switch (mode) {
    case 1: g_ui->sendCompose(buf); break;
    case 2: g_ui->setNodeName(buf); g_ui->show(CydScreen::Settings); break;
    case 3: g_ui->submitLogin(buf); break;
    case 4: g_ui->sendCli(buf); break;
    case 5: g_ui->pathEditTyped(buf); break;
  }
}

static void kb_cb(lv_event_t* e) {
  lv_obj_t* kb = lv_event_get_target(e);
  uint16_t id = lv_btnmatrix_get_selected_btn(kb);
  if (id == LV_BTNMATRIX_BTN_NONE || !g_ta) return;
  const char* t = lv_btnmatrix_get_btn_text(kb, id);
  if (!t) return;
  if (!strcmp(t, I_SHIFT)) { g_shift = !g_shift; kb_set_map(kb); return; }
  if (!strcmp(t, "123")) { g_nums = true; g_syms = false; kb_set_map(kb); return; }
  if (!strcmp(t, "#+=")) { g_syms = true; kb_set_map(kb); return; }
  if (!strcmp(t, "abc")) { g_nums = false; g_syms = false; kb_set_map(kb); return; }
  if (!strcmp(t, I_BKSP)) { lv_textarea_del_char(g_ta); update_count(); return; }
  if (!strcmp(t, I_ENTER)) { submit_text(); return; }
  if (!strcmp(t, "space")) lv_textarea_add_char(g_ta, ' ');
  else lv_textarea_add_text(g_ta, t);
  if (g_shift && !g_nums && !g_syms) { g_shift = false; kb_set_map(kb); }
  update_count();
}

static lv_obj_t* make_keyboard(lv_obj_t* root) {
  lv_obj_t* kb = lv_btnmatrix_create(root);
  lv_obj_set_size(kb, 240, 176);
  lv_obj_set_style_bg_color(kb, lv_color_hex(C_CARD), 0);
  lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(kb, 0, 0);
  lv_obj_set_style_radius(kb, 0, 0);
  lv_obj_set_style_pad_all(kb, 4, 0);
  lv_obj_set_style_pad_row(kb, 5, 0);
  lv_obj_set_style_pad_column(kb, 3, 0);
  lv_obj_set_style_bg_color(kb, lv_color_hex(C_KEY), LV_PART_ITEMS);
  lv_obj_set_style_bg_opa(kb, LV_OPA_COVER, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(kb, lv_color_hex(0x46505C), LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_radius(kb, 6, LV_PART_ITEMS);
  lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
  lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
  lv_obj_set_style_text_color(kb, lv_color_hex(C_TXT), LV_PART_ITEMS);
  lv_obj_set_style_text_font(kb, &F14, LV_PART_ITEMS);
  lv_obj_set_style_outline_width(kb, 0, LV_PART_ITEMS | LV_STATE_FOCUS_KEY);
  lv_obj_set_style_outline_width(kb, 0, LV_PART_ITEMS | LV_STATE_FOCUSED);
  g_shift = false; g_nums = false; g_syms = false;
  kb_set_map(kb);
  lv_obj_add_event_cb(kb, kb_cb, LV_EVENT_VALUE_CHANGED, nullptr);
  return kb;
}

static lv_obj_t* make_textarea(lv_obj_t* p, lv_coord_t h, const char* initial, int max_len) {
  lv_obj_t* ta = lv_textarea_create(p);
  lv_obj_set_height(ta, h);
  lv_obj_set_flex_grow(ta, 1);
  lv_obj_set_style_bg_color(ta, lv_color_hex(C_CARD), 0);
  lv_obj_set_style_border_color(ta, lv_color_hex(C_ACCENT), 0);
  lv_obj_set_style_border_width(ta, 1, 0);
  lv_obj_set_style_radius(ta, 10, 0);
  lv_obj_set_style_pad_all(ta, 7, 0);
  lv_obj_set_style_text_color(ta, lv_color_hex(C_TXT), 0);
  lv_obj_set_style_text_font(ta, &F12, 0);
  lv_obj_set_style_bg_color(ta, lv_color_hex(C_ACCENT), LV_PART_CURSOR);
  lv_obj_set_style_border_color(ta, lv_color_hex(C_ACCENT), LV_PART_CURSOR);
  lv_obj_set_style_text_color(ta, lv_color_hex(C_TXT3), LV_PART_TEXTAREA_PLACEHOLDER);
  style_scrollbar(ta);
  lv_textarea_set_max_length(ta, max_len);
  lv_textarea_set_text(ta, initial);
  lv_obj_add_state(ta, LV_STATE_FOCUSED);   // keeps the cursor visible
  return ta;
}

void UITask::buildCompose() {
  if (_open_conv < 0) { show(CydScreen::Chats); return; }
  const CydConv& c = _convs[_open_conv];
  lv_obj_t* root = screen_root();
  lv_obj_set_user_data(lv_scr_act(), (void*)1);

  lv_obj_t* hd = row(root, 26, 6);
  lv_obj_set_style_pad_left(hd, 2, 0);
  lv_obj_set_style_pad_right(hd, 8, 0);
  icon_btn(hd, I_CLOSE, 28, 24, 0, C_TXT2, nav_cb, (void*)(intptr_t)CydScreen::Conversation);
  char to[48]; snprintf(to, sizeof(to), "To  %s", c.name);
  lv_obj_t* tl = label(hd, to, &F12B, C_TXT);
  lv_label_set_long_mode(tl, LV_LABEL_LONG_DOT);
  lv_obj_set_flex_grow(tl, 1);
  g_count = label(hd, "0 / 140", &F10, C_TXT3);
  lv_obj_set_style_text_font(g_count, &inter_10, 0);

  lv_obj_t* in = row(root, 1, 6);
  lv_obj_set_flex_grow(in, 1);
  lv_obj_set_style_pad_hor(in, 6, 0);
  lv_obj_set_style_pad_bottom(in, 6, 0);
  lv_obj_set_flex_align(in, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  g_ta = make_textarea(in, lv_pct(100), "", 140);
  lv_obj_t* send = icon_btn(in, I_SEND, 42, lv_pct(100), C_ACCENT, C_ACC_INK,
                            [](lv_event_t* e) { submit_text(); }, nullptr, 10);
  (void)send;
  make_keyboard(root);
}

static void input_screen(const char* title, const char* initial, int max_len, bool password, int mode,
                         CydScreen back, const char* hint) {
  lv_obj_t* root = screen_root();
  lv_obj_set_user_data(lv_scr_act(), (void*)(intptr_t)mode);
  lv_obj_t* hd = row(root, 26, 6);
  lv_obj_set_style_pad_left(hd, 2, 0);
  lv_obj_set_style_pad_right(hd, 8, 0);
  icon_btn(hd, I_CLOSE, 28, 24, 0, C_TXT2, nav_cb, (void*)(intptr_t)back);
  lv_obj_t* tl = label(hd, title, &F12B, C_TXT);
  lv_label_set_long_mode(tl, LV_LABEL_LONG_DOT);
  lv_obj_set_flex_grow(tl, 1);
  g_count = nullptr;
  lv_obj_t* area = col(root, 240, 1, 6);
  lv_obj_set_flex_grow(area, 1);
  lv_obj_set_style_pad_hor(area, 6, 0);
  lv_obj_t* in = row(area, 36, 6);
  g_ta = make_textarea(in, 36, initial, max_len);
  lv_textarea_set_one_line(g_ta, true);
  if (password) {
    lv_textarea_set_password_mode(g_ta, true);
    lv_obj_t* sh = box(hd, 46, 22, C_CARD2, 11);
    pressable(sh, C_KEY);
    lv_obj_t* sl = label(sh, "Show", &F10, C_ACCENT);
    lv_obj_set_style_text_font(sl, &inter_10, 0);
    lv_obj_center(sl);
    lv_obj_add_event_cb(sh, [](lv_event_t* e) {
      if (!g_ta) return;
      bool hidden = lv_textarea_get_password_mode(g_ta);
      lv_textarea_set_password_mode(g_ta, !hidden);
      lv_label_set_text((lv_obj_t*)lv_event_get_user_data(e), hidden ? "Hide" : "Show");
    }, LV_EVENT_CLICKED, sl);
  }
  icon_btn(in, I_CHECK, 42, 36, C_ACCENT, C_ACC_INK, [](lv_event_t* e) { submit_text(); }, nullptr, 10);
  if (hint && hint[0]) {
    lv_obj_t* h = label(area, hint, &F10, C_TXT2);
    lv_obj_set_style_text_font(h, &inter_10, 0);
    lv_label_set_long_mode(h, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(h, lv_pct(100));
  }
  make_keyboard(root);
}

void UITask::buildEditName() {
  input_screen("Node name", _node_prefs->node_name, 31, false, 2, CydScreen::Settings, "");
}

void UITask::buildLogin() {
  ContactInfo* ci = the_mesh.lookupContactByPubKey(_detail_pub, 32);
  char t[48]; snprintf(t, sizeof(t), "Log in to %s", ci ? ci->name : "node");
  input_screen(t, "", sizeof(_admin_pw) - 1, true, 3, CydScreen::ContactDetail,
               _login_msg[0] ? _login_msg : "Admin password. Leave empty for guest access.");
}

void UITask::buildCliInput() {
  input_screen("Command", "", 120, false, 4, CydScreen::Console, "e.g. ver, clock, neighbors, get radio, set tx 22");
}

static void contact_click_cb(lv_event_t* e) { g_ui->openContact((int)(intptr_t)lv_event_get_user_data(e)); }
static void filter_cb(lv_event_t* e) { g_ui->setContactFilter((int)(intptr_t)lv_event_get_user_data(e)); }

void UITask::buildContacts() {
  lv_obj_t* root = screen_root();
  int total = the_mesh.getNumContacts();
  lv_obj_t* tb = title_bar(root, "Contacts");
  char kn[24]; snprintf(kn, sizeof(kn), "%d known", total);
  lv_obj_t* kl = label(tb, kn, &F10, C_TXT2);
  lv_obj_set_style_text_font(kl, &inter_10, 0);

  // segmented filter
  lv_obj_t* segw = row(root, 32, 0);
  lv_obj_set_style_pad_hor(segw, 8, 0);
  lv_obj_set_style_pad_bottom(segw, 6, 0);
  lv_obj_t* seg = row(segw, 26, 0);
  lv_obj_set_style_bg_color(seg, lv_color_hex(C_CARD), 0);
  lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(seg, 8, 0);
  lv_obj_set_style_pad_all(seg, 2, 0);
  const char* names[] = { "All", "People", "Repeaters" };
  for (int i = 0; i < 3; i++) {
    bool on = i == _contact_filter;
    lv_obj_t* s = box(seg, 1, 22, on ? C_KEY : 0, 6, on);
    lv_obj_set_flex_grow(s, 1);
    pressable(s, C_CARD2);
    lv_obj_add_event_cb(s, filter_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    lv_obj_t* l = label(s, names[i], &F10, on ? C_TXT : C_TXT2);
    lv_obj_set_style_text_font(l, &inter_10, 0);
    lv_obj_center(l);
  }

  lv_obj_t* list = col(root, 240, 1, 0);
  lv_obj_set_flex_grow(list, 1);
  lv_obj_set_style_pad_hor(list, 6, 0);
  lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  style_scrollbar(list);

  // collect + sort by lastmod (newest first), cap for RAM
  const int CAP = 24;   // each row is ~6 LVGL objects; keep the screen well inside the heap
  static int idx[CAP]; static uint32_t mod[CAP];
  int n = 0;
  // contacts[] starts with MAX_ANON_CONTACTS reserved slots; getContactByIdx() takes raw slot indexes
  for (int i = MAX_ANON_CONTACTS; i < the_mesh.getTotalContactSlots(); i++) {
    ContactInfo ci;
    if (!the_mesh.getContactByIdx(i, ci) || !ci.name[0]) continue;
    bool person = ci.type == ADV_TYPE_CHAT;
    if (_contact_filter == 1 && !person) continue;
    if (_contact_filter == 2 && person) continue;
    if (n < CAP) { idx[n] = i; mod[n] = ci.lastmod; n++; }
    else {
      int w = 0; for (int k = 1; k < CAP; k++) if (mod[k] < mod[w]) w = k;
      if (ci.lastmod > mod[w]) { idx[w] = i; mod[w] = ci.lastmod; }
    }
  }
  for (int i = 1; i < n; i++) {
    int vi = idx[i]; uint32_t vm = mod[i]; int j = i - 1;
    while (j >= 0 && mod[j] < vm) { idx[j + 1] = idx[j]; mod[j + 1] = mod[j]; j--; }
    idx[j + 1] = vi; mod[j + 1] = vm;
  }
  if (n == 0) {
    lv_obj_t* e = label(list, "Nobody heard yet.\nSend an advert from Home.", &F12, C_TXT3);
    lv_obj_set_width(e, lv_pct(100));
    lv_obj_set_style_text_align(e, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(e, 20, 0);
  }
  for (int i = 0; i < n; i++) {
    ContactInfo ci;
    if (!the_mesh.getContactByIdx(idx[i], ci)) continue;
    lv_obj_t* r = row(list, 38, 8);
    lv_obj_set_style_pad_hor(r, 6, 0);
    lv_obj_set_style_border_side(r, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(r, 1, 0);
    lv_obj_set_style_border_color(r, lv_color_hex(C_DIVIDER), 0);
    pressable(r, C_CARD);
    lv_obj_add_event_cb(r, contact_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)idx[i]);
    bool person = ci.type == ADV_TYPE_CHAT;
    avatar(r, 26, ci.name, !person, false);
    lv_obj_t* tc = col(r, 1, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(tc, 1);
    lv_obj_t* nm = label(tc, ci.name, &F12B, C_TXT);
    lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
    lv_obj_set_width(nm, lv_pct(100));
    const char* kind = ci.type == ADV_TYPE_REPEATER ? "Repeater \xC2\xB7 " : ci.type == ADV_TYPE_ROOM ? "Room \xC2\xB7 " : ci.type == ADV_TYPE_SENSOR ? "Sensor \xC2\xB7 " : "";
    char route[40], meta[56];
    route_text(ci, route, sizeof(route));
    snprintf(meta, sizeof(meta), "%s%s", kind, route);
    lv_obj_t* ml = label(tc, meta, &F10, C_TXT2);
    lv_obj_set_style_text_font(ml, &inter_10, 0);
    lv_label_set_long_mode(ml, LV_LABEL_LONG_DOT);
    lv_obj_set_width(ml, lv_pct(100));
    char ago[12]; fmt_ago(ci.lastmod, ago, sizeof(ago));
    lv_obj_t* al = label(r, ago, &F10, C_TXT3);
    lv_obj_set_style_text_font(al, &inter_10, 0);
  }
  tab_bar(root, CydScreen::Contacts, totalUnread());
}

static lv_obj_t* action_btn(lv_obj_t* p, const char* icon, const char* text, uint32_t bg, uint32_t fg, lv_event_cb_t cb) {
  lv_obj_t* b = row(p, 40, 6);
  lv_obj_set_width(b, 110);
  lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(b, 10, 0);
  lv_obj_set_flex_align(b, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  pressable(b, bg == C_ACCENT ? 0x8AD8FF : C_KEY);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
  label(b, icon, &icons_14, fg);
  label(b, text, &F12B, fg);
  return b;
}

void UITask::buildContactDetail() {
  ContactInfo* cp = the_mesh.lookupContactByPubKey(_detail_pub, 32);
  if (!cp) { show(CydScreen::Contacts); return; }
  ContactInfo ci = *cp;
  lv_obj_t* root = screen_root();

  lv_obj_t* hd = row(root, 34, 4);
  lv_obj_set_style_pad_left(hd, 2, 0);
  icon_btn(hd, I_LEFT, 30, 30, 0, C_TXT, nav_cb, (void*)(intptr_t)CydScreen::Contacts);
  label(hd, "Contact", &F16B, C_TXT);

  lv_obj_t* body = col(root, 240, 1, 8);
  lv_obj_set_flex_grow(body, 1);
  lv_obj_set_style_pad_hor(body, 10, 0);
  lv_obj_set_style_pad_bottom(body, 8, 0);
  lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(body, LV_DIR_VER);
  style_scrollbar(body);

  bool person = ci.type == ADV_TYPE_CHAT;
  lv_obj_t* top = row(body, 52, 10);
  avatar(top, 48, ci.name, !person, false);
  lv_obj_t* tc = col(top, 1, LV_SIZE_CONTENT, 2);
  lv_obj_set_flex_grow(tc, 1);
  lv_obj_t* nm = label(tc, ci.name, &F16B, C_TXT);
  lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
  lv_obj_set_width(nm, lv_pct(100));
  const char* kind = ci.type == ADV_TYPE_REPEATER ? "Repeater" : ci.type == ADV_TYPE_ROOM ? "Room server" : ci.type == ADV_TYPE_SENSOR ? "Sensor" : "Person";
  label(tc, kind, &F12, C_TXT2);

  // facts card
  lv_obj_t* card = col(body, lv_pct(100), LV_SIZE_CONTENT, 0);
  lv_obj_set_style_bg_color(card, lv_color_hex(C_CARD), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(card, 10, 0);
  lv_obj_set_style_pad_hor(card, 10, 0);
  char route[48]; route_text(ci, route, sizeof(route));
  char ago[16]; fmt_ago(ci.lastmod, ago, sizeof(ago));
  char key[20]; snprintf(key, sizeof(key), "%02X%02X%02X%02X", ci.id.pub_key[0], ci.id.pub_key[1], ci.id.pub_key[2], ci.id.pub_key[3]);
  const char* rows_k[] = { "Route", "Last heard", "Key", "Ping" };
  const char* rows_v[] = { route, ago[0] ? ago : "\xE2\x80\x94", key, _ping_result[0] ? _ping_result : "\xE2\x80\x94" };
  for (int i = 0; i < 4; i++) {
    lv_obj_t* r = row(card, 28, 8);
    if (i < 3) {
      lv_obj_set_style_border_side(r, LV_BORDER_SIDE_BOTTOM, 0);
      lv_obj_set_style_border_width(r, 1, 0);
      lv_obj_set_style_border_color(r, lv_color_hex(C_BORDER), 0);
    }
    lv_obj_t* k = label(r, rows_k[i], &F12, C_TXT2);
    lv_obj_set_width(k, 70);
    lv_obj_t* v = label(r, rows_v[i], &F12, i == 3 && _ping_result[0] ? C_ACCENT : C_TXT);
    lv_label_set_long_mode(v, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(v, 1);
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
  }

  // actions: two per row
  bool node = ci.type == ADV_TYPE_REPEATER || ci.type == ADV_TYPE_ROOM;
  auto pair = [&](void) {
    lv_obj_t* r = row(body, 40, 0);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return r;
  };
  if (node) {
    lv_obj_t* a0 = pair();
    action_btn(a0, I_INFO, "Status", C_ACCENT, C_ACC_INK, [](lv_event_t* e) { g_ui->adminOpen(CydScreen::Status); });
    action_btn(a0, I_SLIDERS, "Console", C_ACCENT, C_ACC_INK, [](lv_event_t* e) { g_ui->adminOpen(CydScreen::Console); });
  }
  lv_obj_t* a1 = pair();
  if (person || ci.type == ADV_TYPE_ROOM)
    action_btn(a1, I_CHAT, "Message", node ? C_CARD2 : C_ACCENT, node ? C_TXT : C_ACC_INK, [](lv_event_t* e) { g_ui->contactMessage(); });
  action_btn(a1, I_TOWER, "Ping", C_CARD2, C_TXT, [](lv_event_t* e) { g_ui->contactPing(); });
  lv_obj_t* a2 = pair();
  action_btn(a2, I_SYNC, "Reset path", C_CARD2, C_TXT, [](lv_event_t* e) { g_ui->contactResetPath(); });
  action_btn(a2, I_SIGNAL, "Edit path", C_CARD2, C_TXT, [](lv_event_t* e) { g_ui->pathEditOpen(); });
  a2 = pair();
  if (_delete_armed)
    action_btn(a2, I_CLOSE, "Confirm", 0x5A1F1F, C_RED, [](lv_event_t* e) { g_ui->contactDelete(); });
  else
    action_btn(a2, I_CLOSE, "Delete", C_CARD2, C_RED, [](lv_event_t* e) { g_ui->contactDelete(); });
  if (node && (hasSavedPassword(ci.id.pub_key) || isAdminFor(ci.id.pub_key))) {
    lv_obj_t* fr = row(body, 34, 6);
    lv_obj_set_flex_align(fr, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_radius(fr, 10, 0);
    pressable(fr, C_CARD);
    lv_obj_add_event_cb(fr, [](lv_event_t* e) { g_ui->forgetPassword(); }, LV_EVENT_CLICKED, nullptr);
    label(fr, I_LOCK, &icons_14, C_TXT2);
    label(fr, hasSavedPassword(ci.id.pub_key) ? "Forget saved password" : "Log out", &F12, C_TXT2);
  }

  tab_bar(root, CydScreen::Contacts, totalUnread());
}

static lv_obj_t* sub_header(lv_obj_t* root, const char* title, CydScreen back) {
  lv_obj_t* hd = row(root, 34, 4);
  lv_obj_set_style_pad_left(hd, 2, 0);
  lv_obj_set_style_pad_right(hd, 8, 0);
  icon_btn(hd, I_LEFT, 30, 30, 0, C_TXT, nav_cb, (void*)(intptr_t)back);
  lv_obj_t* t = label(hd, title, &F16B, C_TXT);
  lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
  lv_obj_set_flex_grow(t, 1);
  return hd;
}

static void fmt_dur(uint32_t s, char* b, size_t n) {
  if (s >= 86400) snprintf(b, n, "%lud %luh", (unsigned long)(s / 86400), (unsigned long)(s % 86400 / 3600));
  else if (s >= 3600) snprintf(b, n, "%luh %lum", (unsigned long)(s / 3600), (unsigned long)(s % 3600 / 60));
  else snprintf(b, n, "%lum %lus", (unsigned long)(s / 60), (unsigned long)(s % 60));
}

void UITask::buildStatus() {
  ContactInfo* ci = the_mesh.lookupContactByPubKey(_detail_pub, 32);
  lv_obj_t* root = screen_root();
  char t[48]; snprintf(t, sizeof(t), "%s", ci ? ci->name : "Status");
  lv_obj_t* hd = sub_header(root, t, CydScreen::ContactDetail);
  icon_btn(hd, I_SYNC, 30, 26, C_CARD2, C_ACCENT, [](lv_event_t* e) { g_ui->requestStatus(); }, nullptr);

  lv_obj_t* body = col(root, 240, 1, 6);
  lv_obj_set_flex_grow(body, 1);
  lv_obj_set_style_pad_hor(body, 8, 0);
  lv_obj_set_style_pad_bottom(body, 8, 0);
  lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(body, LV_DIR_VER);
  style_scrollbar(body);

  if (_status_len < 48) {
    lv_obj_t* l = label(body, _status_pending ? "Requesting status\xE2\x80\xA6" : "No status yet \xE2\x80\x94 tap refresh", &F12, C_TXT2);
    lv_obj_set_style_pad_top(l, 20, 0);
    lv_obj_set_width(l, lv_pct(100));
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    tab_bar(root, CydScreen::Contacts, totalUnread());
    return;
  }
  // RepeaterStats layout (simple_repeater/MyMesh.h)
  auto u16 = [&](int o) { uint16_t v; memcpy(&v, &_status[o], 2); return v; };
  auto i16 = [&](int o) { int16_t v; memcpy(&v, &_status[o], 2); return v; };
  auto u32 = [&](int o) { uint32_t v; memcpy(&v, &_status[o], 4); return v; };
  char v[12][24];
  snprintf(v[0], 24, "%u.%02u V", u16(0) / 1000, u16(0) % 1000 / 10);
  fmt_dur(u32(20), v[1], 24);
  snprintf(v[2], 24, "%d dBm", i16(4));
  snprintf(v[3], 24, "%d dBm", i16(6));
  snprintf(v[4], 24, "%d dB", i16(42) / 4);
  fmt_dur(u32(16), v[5], 24);
  snprintf(v[6], 24, "%lu", (unsigned long)u32(8));
  snprintf(v[7], 24, "%lu", (unsigned long)u32(12));
  snprintf(v[8], 24, "%lu / %lu", (unsigned long)u32(24), (unsigned long)u32(28));
  snprintf(v[9], 24, "%lu / %lu", (unsigned long)u32(32), (unsigned long)u32(36));
  snprintf(v[10], 24, "%u / %u", u16(44), u16(46));
  snprintf(v[11], 24, "%u \xC2\xB7 q %u", u16(40), u16(2));
  const char* k[12] = { "Battery", "Uptime", "Noise floor", "Last RSSI", "Last SNR", "TX airtime",
                        "Packets in", "Packets out", "Sent fl / dir", "Recv fl / dir", "Dups dir / fl", "Errors \xC2\xB7 queue" };
  for (int i = 0; i < 12; i += 2) {
    lv_obj_t* r = row(body, 46, 6);
    for (int j = i; j < i + 2; j++) {
      lv_obj_t* c = col(r, 1, 46, 1);
      lv_obj_set_flex_grow(c, 1);
      lv_obj_set_style_bg_color(c, lv_color_hex(C_CARD), 0);
      lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
      lv_obj_set_style_radius(c, 10, 0);
      lv_obj_set_style_pad_hor(c, 8, 0);
      lv_obj_set_style_pad_ver(c, 6, 0);
      lv_obj_t* kl = label(c, k[j], &F10, C_TXT2);
      lv_obj_set_style_text_font(kl, &inter_10, 0);
      label(c, v[j], &F12B, C_TXT);
    }
  }
  char upd[40]; unsigned long ago = (millis() - _status_at) / 1000;
  snprintf(upd, sizeof(upd), _status_pending ? "Refreshing\xE2\x80\xA6" : "Updated %lus ago", ago);
  lv_obj_t* ul = label(body, upd, &F10, C_TXT3);
  lv_obj_set_style_text_font(ul, &inter_10, 0);
  tab_bar(root, CydScreen::Contacts, totalUnread());
}

static void cli_chip_cb(lv_event_t* e) { g_ui->sendCli((const char*)lv_event_get_user_data(e)); }

void UITask::buildConsole() {
  ContactInfo* ci = the_mesh.lookupContactByPubKey(_detail_pub, 32);
  lv_obj_t* root = screen_root();
  char t[48]; snprintf(t, sizeof(t), "Console \xC2\xB7 %s", ci ? ci->name : "");
  sub_header(root, t, CydScreen::ContactDetail);

  lv_obj_t* log = col(root, 240, 1, 0);
  lv_obj_set_flex_grow(log, 1);
  lv_obj_set_style_bg_color(log, lv_color_hex(0x080B10), 0);
  lv_obj_set_style_bg_opa(log, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(log, 8, 0);
  lv_obj_add_flag(log, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(log, LV_DIR_VER);
  style_scrollbar(log);
  lv_obj_t* last = nullptr;
  if (!_console || !_console[0]) {
    last = label(log, "Run a command on this node.\nReplies appear here.", &F12, C_TXT3);
  } else {
    // one label per line so commands can be coloured
    const char* p = _console;
    while (*p) {
      const char* nl = strchr(p, '\n');
      size_t n = nl ? (size_t)(nl - p) : strlen(p);
      char line[200]; if (n >= sizeof(line)) n = sizeof(line) - 1;
      memcpy(line, p, n); line[n] = 0;
      bool cmd = line[0] == '>' && line[1] == ' ';
      lv_obj_t* l = label(log, line, &F12, cmd ? C_ACCENT : C_TXT);
      lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
      lv_obj_set_width(l, lv_pct(100));
      if (cmd) lv_obj_set_style_pad_top(l, 4, 0);
      last = l;
      p += n + (nl ? 1 : 0);
    }
    if (_cli_pending) last = label(log, "\xE2\x80\xA6", &F12, C_TXT3);
  }

  // quick commands
  lv_obj_t* chips = row(root, 34, 6);
  lv_obj_set_style_pad_hor(chips, 6, 0);
  lv_obj_add_flag(chips, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(chips, LV_DIR_HOR);
  lv_obj_set_scrollbar_mode(chips, LV_SCROLLBAR_MODE_OFF);
  static const char* CMDS[] = { "ver", "clock", "neighbors", "get radio", "advert", "clock sync" };
  for (auto c : CMDS) {
    lv_obj_t* b = box(chips, LV_SIZE_CONTENT, 26, C_CARD2, 13);
    lv_obj_set_style_pad_hor(b, 10, 0);
    pressable(b, C_KEY);
    lv_obj_add_event_cb(b, cli_chip_cb, LV_EVENT_CLICKED, (void*)c);
    lv_obj_center(label(b, c, &F12, C_TXT));
  }

  lv_obj_t* ib = row(root, 40, 6);
  lv_obj_set_style_bg_color(ib, lv_color_hex(C_CARD), 0);
  lv_obj_set_style_bg_opa(ib, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_hor(ib, 6, 0);
  lv_obj_t* fake = row(ib, 30, 0);
  lv_obj_set_flex_grow(fake, 1);
  lv_obj_set_style_bg_color(fake, lv_color_hex(C_BG), 0);
  lv_obj_set_style_bg_opa(fake, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(fake, 15, 0);
  lv_obj_set_style_border_width(fake, 1, 0);
  lv_obj_set_style_border_color(fake, lv_color_hex(0x2D3640), 0);
  lv_obj_set_style_pad_hor(fake, 12, 0);
  pressable(fake, C_CARD2);
  lv_obj_add_event_cb(fake, nav_cb, LV_EVENT_CLICKED, (void*)(intptr_t)CydScreen::CliInput);
  label(fake, "Type a command\xE2\x80\xA6", &F12, C_TXT3);

  if (last) { lv_obj_update_layout(log); lv_obj_scroll_to_view(last, LV_ANIM_OFF); }
}

static void pe_add_cb(lv_event_t* e) { g_ui->pathEditAdd((int)(intptr_t)lv_event_get_user_data(e)); }
static void pe_rm_cb(lv_event_t* e) { g_ui->pathEditRemove((int)(intptr_t)lv_event_get_user_data(e)); }

static lv_obj_t* chip(lv_obj_t* p, const char* text, uint32_t bg, uint32_t fg) {
  lv_obj_t* c = box(p, LV_SIZE_CONTENT, 24, bg, 12);
  lv_obj_set_style_pad_hor(c, 9, 0);
  lv_obj_t* l = label(c, text, &F12, fg);
  lv_obj_center(l);
  return c;
}

void UITask::buildPathEdit() {
  ContactInfo* cp = the_mesh.lookupContactByPubKey(_detail_pub, 32);
  if (!cp) { show(CydScreen::Contacts); return; }
  lv_obj_t* root = screen_root();
  char t[48]; snprintf(t, sizeof(t), "Route to %s", cp->name);
  sub_header(root, t, CydScreen::ContactDetail);

  lv_obj_t* body = col(root, 240, 1, 6);
  lv_obj_set_flex_grow(body, 1);
  lv_obj_set_style_pad_hor(body, 8, 0);

  // current route as chips (tap a hop to remove it)
  lv_obj_t* rc = box(body, lv_pct(100), LV_SIZE_CONTENT, C_CARD, 10);
  lv_obj_set_flex_flow(rc, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(rc, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_all(rc, 8, 0);
  lv_obj_set_style_pad_row(rc, 6, 0);
  lv_obj_set_style_pad_column(rc, 4, 0);
  chip(rc, "You", C_ACC_SOFT, C_ACCENT);
  for (int i = 0; i < _pe_n; i++) {
    label(rc, I_RIGHT, &icons_14, C_TXT3);
    char nm[28];
    ContactInfo* r = the_mesh.lookupContactByPubKey(_pe_path[i], _pe_hsz);
    if (r && r->name[0]) snprintf(nm, sizeof(nm), "%s " I_CLOSE, r->name);
    else snprintf(nm, sizeof(nm), "%02X " I_CLOSE, _pe_path[i][0]);
    lv_obj_t* c = chip(rc, nm, C_KEY, C_TXT);
    pressable(c, 0x5A1F1F);
    lv_obj_add_event_cb(c, pe_rm_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
  }
  label(rc, I_RIGHT, &icons_14, C_TXT3);
  chip(rc, cp->name, C_CARD2, C_TXT);

  lv_obj_t* hint = label(body, _pe_n ? "Tap a hop to remove it. Add repeaters, or type IDs:" : "No hops = direct. Add repeaters, or type IDs:", &F10, C_TXT2);
  lv_obj_set_style_text_font(hint, &inter_10, 0);

  // repeater picker, most recently heard first
  lv_obj_t* list = col(body, lv_pct(100), 1, 0);
  lv_obj_set_flex_grow(list, 1);
  lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(list, LV_DIR_VER);
  style_scrollbar(list);
  const int CAP = 16;
  int idx[CAP]; uint32_t mod[CAP]; int n = 0;
  for (int i = MAX_ANON_CONTACTS; i < the_mesh.getTotalContactSlots(); i++) {
    ContactInfo r;
    if (!the_mesh.getContactByIdx(i, r) || !r.name[0]) continue;
    if (r.type != ADV_TYPE_REPEATER && r.type != ADV_TYPE_ROOM) continue;
    if (memcmp(r.id.pub_key, _detail_pub, 32) == 0) continue;
    if (n < CAP) { idx[n] = i; mod[n] = r.lastmod; n++; }
    else { int w = 0; for (int k = 1; k < CAP; k++) if (mod[k] < mod[w]) w = k; if (r.lastmod > mod[w]) { idx[w] = i; mod[w] = r.lastmod; } }
  }
  for (int i = 1; i < n; i++) { int vi = idx[i]; uint32_t vm = mod[i]; int j = i - 1; while (j >= 0 && mod[j] < vm) { idx[j+1] = idx[j]; mod[j+1] = mod[j]; j--; } idx[j+1] = vi; mod[j+1] = vm; }
  if (!n) label(list, "No repeaters known yet", &F12, C_TXT3);
  for (int i = 0; i < n; i++) {
    ContactInfo r; the_mesh.getContactByIdx(idx[i], r);
    lv_obj_t* row_ = row(list, 34, 8);
    lv_obj_set_style_pad_hor(row_, 6, 0);
    lv_obj_set_style_border_side(row_, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(row_, 1, 0);
    lv_obj_set_style_border_color(row_, lv_color_hex(C_DIVIDER), 0);
    pressable(row_, C_CARD);
    lv_obj_add_event_cb(row_, pe_add_cb, LV_EVENT_CLICKED, (void*)(intptr_t)idx[i]);
    avatar(row_, 22, r.name, true, false);
    lv_obj_t* nm = label(row_, r.name, &F12, C_TXT);
    lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
    lv_obj_set_flex_grow(nm, 1);
    char ago[12]; fmt_ago(r.lastmod, ago, sizeof(ago));
    lv_obj_t* al = label(row_, ago, &F10, C_TXT3);
    lv_obj_set_style_text_font(al, &inter_10, 0);
    label(row_, I_PLUS, &icons_14, C_ACCENT);
  }

  // save bar
  lv_obj_t* bar = row(root, 46, 8);
  lv_obj_set_style_pad_hor(bar, 8, 0);
  lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t* b1 = action_btn(bar, I_SYNC, "Flood", C_CARD2, C_TXT, [](lv_event_t* e) { g_ui->pathEditSave(true); });
  lv_obj_t* b2 = action_btn(bar, I_HASH, "IDs", C_CARD2, C_TXT, nav_cb);
  lv_obj_remove_event_cb(b2, nav_cb);
  lv_obj_add_event_cb(b2, nav_cb, LV_EVENT_CLICKED, (void*)(intptr_t)CydScreen::PathInput);
  lv_obj_t* b3 = action_btn(bar, I_CHECK, "Save", C_ACCENT, C_ACC_INK, [](lv_event_t* e) { g_ui->pathEditSave(false); });
  lv_obj_set_width(b1, 70); lv_obj_set_width(b2, 64); lv_obj_set_width(b3, 80);
}

// ---- settings ----
static lv_obj_t* g_modal = nullptr;

static lv_obj_t* settings_row(lv_obj_t* group, const char* icon, uint32_t icon_bg, const char* text, const char* value,
                              bool last, lv_event_cb_t cb, bool chevron = true) {
  lv_obj_t* r = row(group, 34, 9);
  lv_obj_set_style_pad_hor(r, 10, 0);
  if (!last) {
    lv_obj_set_style_border_side(r, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(r, 1, 0);
    lv_obj_set_style_border_color(r, lv_color_hex(C_BORDER), 0);
  }
  if (cb) { pressable(r, C_CARD2); lv_obj_add_event_cb(r, cb, LV_EVENT_CLICKED, nullptr); }
  lv_obj_t* ic = box(r, 20, 20, icon_bg, 6);
  lv_obj_center(label(ic, icon, &icons_14, C_BG));
  lv_obj_t* t = label(r, text, &F12, C_TXT);
  lv_obj_set_flex_grow(t, 1);
  if (value) {
    lv_obj_t* v = label(r, value, &F12, C_TXT2);
    lv_obj_set_style_text_font(v, &inter_12, 0);
  }
  if (chevron && cb) label(r, I_RIGHT, &icons_14, C_TXT3);
  return r;
}

static lv_obj_t* group_box(lv_obj_t* p) {
  lv_obj_t* g = col(p, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(g, lv_color_hex(C_CARD), 0);
  lv_obj_set_style_bg_opa(g, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(g, 10, 0);
  lv_obj_set_style_clip_corner(g, true, 0);
  return g;
}

static void close_modal() { if (g_modal) { lv_obj_del(g_modal); g_modal = nullptr; } }

static void tz_modal() {
  close_modal();
  g_modal = box(lv_layer_top(), 240, 320, 0x000000, 0);
  lv_obj_set_style_bg_opa(g_modal, LV_OPA_70, 0);
  lv_obj_add_flag(g_modal, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_t* card = col(g_modal, 200, LV_SIZE_CONTENT, 10);
  lv_obj_center(card);
  lv_obj_set_style_bg_color(card, lv_color_hex(C_CARD), 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(card, 12, 0);
  lv_obj_set_style_pad_all(card, 12, 0);
  label(card, "Time zone", &F16B, C_TXT);
  static char opts[27 * 8];
  opts[0] = 0;
  for (int h = -12; h <= 14; h++) {
    char o[12]; snprintf(o, sizeof(o), h == 0 ? "UTC" : "UTC%+d", h);
    strcat(opts, o); if (h < 14) strcat(opts, "\n");
  }
  lv_obj_t* rl = lv_roller_create(card);
  lv_roller_set_options(rl, opts, LV_ROLLER_MODE_NORMAL);
  lv_roller_set_visible_row_count(rl, 4);
  lv_obj_set_width(rl, lv_pct(100));
  lv_obj_set_style_bg_color(rl, lv_color_hex(C_BG), 0);
  lv_obj_set_style_border_width(rl, 0, 0);
  lv_obj_set_style_text_color(rl, lv_color_hex(C_TXT2), 0);
  lv_obj_set_style_text_font(rl, &F16B, 0);
  lv_obj_set_style_bg_color(rl, lv_color_hex(C_ACC_SOFT), LV_PART_SELECTED);
  lv_obj_set_style_text_color(rl, lv_color_hex(C_ACCENT), LV_PART_SELECTED);
  lv_roller_set_selected(rl, g_ui->prefs().tz_hours + 12, LV_ANIM_OFF);
  lv_obj_t* ok = row(card, 36, 0);
  lv_obj_set_style_bg_color(ok, lv_color_hex(C_ACCENT), 0);
  lv_obj_set_style_bg_opa(ok, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(ok, 10, 0);
  lv_obj_set_flex_align(ok, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  pressable(ok, 0x8AD8FF);
  label(ok, "Done", &F12B, C_ACC_INK);
  lv_obj_add_event_cb(ok, [](lv_event_t* e) {
    lv_obj_t* rl = (lv_obj_t*)lv_event_get_user_data(e);
    g_ui->prefs().tz_hours = (int8_t)lv_roller_get_selected(rl) - 12;
    g_ui->prefsChanged();
    close_modal();
    g_ui->show(CydScreen::Settings);
  }, LV_EVENT_CLICKED, rl);
}

void UITask::buildSettings() {
  close_modal();
  lv_obj_t* root = screen_root();
  title_bar(root, "Settings");
  lv_obj_t* body = col(root, 240, 1, 6);
  lv_obj_set_flex_grow(body, 1);
  lv_obj_set_style_pad_hor(body, 8, 0);
  lv_obj_set_style_pad_bottom(body, 6, 0);
  lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(body, LV_DIR_VER);
  style_scrollbar(body);

  lv_obj_t* g1 = group_box(body);
  settings_row(g1, I_USER, C_ACCENT, "Node name", _node_prefs->node_name, false,
               [](lv_event_t* e) { g_ui->show(CydScreen::EditName); });
  char radio[40];
  snprintf(radio, sizeof(radio), "%.3f \xC2\xB7 SF%d", _node_prefs->freq, _node_prefs->sf);
  settings_row(g1, I_SIGNAL, C_GREEN, "Radio", radio, false, nullptr);
  char bt[24];
  if (!isBluetoothEnabled()) snprintf(bt, sizeof(bt), "Off");
  else if (hasConnection()) snprintf(bt, sizeof(bt), "Connected");
  else snprintf(bt, sizeof(bt), "PIN %06lu", (unsigned long)the_mesh.getBLEPin());
  settings_row(g1, I_BT, C_AMBER, "Bluetooth", bt, true, [](lv_event_t* e) { g_ui->toggleBluetooth(); }, false);

  // brightness
  lv_obj_t* g2 = group_box(body);
  lv_obj_t* br = row(g2, 36, 9);
  lv_obj_set_style_pad_hor(br, 10, 0);
  lv_obj_t* ic = box(br, 20, 20, 0xB392F0, 6);
  lv_obj_center(label(ic, I_SUN, &icons_14, C_BG));
  label(br, "Brightness", &F12, C_TXT);
  lv_obj_t* sl = lv_slider_create(br);
  lv_obj_set_flex_grow(sl, 1);
  lv_obj_set_height(sl, 6);
  lv_slider_set_range(sl, 10, 255);
  lv_slider_set_value(sl, _prefs.brightness, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(sl, lv_color_hex(0x2D3640), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(sl, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(sl, lv_color_hex(C_ACCENT), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(sl, lv_color_hex(C_TXT), LV_PART_KNOB);
  lv_obj_set_style_pad_all(sl, 6, LV_PART_KNOB);
  lv_obj_set_ext_click_area(sl, 14);
  lv_obj_add_event_cb(sl, [](lv_event_t* e) {
    lv_obj_t* s = lv_event_get_target(e);
    g_ui->prefs().brightness = lv_slider_get_value(s);
    display.gfx()->setBrightness(g_ui->prefs().brightness);
  }, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(sl, [](lv_event_t* e) { g_ui->prefsChanged(); }, LV_EVENT_RELEASED, nullptr);

  lv_obj_t* g3 = group_box(body);
  settings_row(g3, I_LOCK, 0xB392F0, "Screen off", SCREEN_OFF_TXT[_prefs.screen_off_idx], false, [](lv_event_t* e) {
    g_ui->prefs().screen_off_idx = (g_ui->prefs().screen_off_idx + 1) % 5;
    g_ui->prefsChanged();
    g_ui->show(CydScreen::Settings);
  }, false);
  char tz[12]; snprintf(tz, sizeof(tz), _prefs.tz_hours == 0 ? "UTC" : "UTC%+d", _prefs.tz_hours);
  settings_row(g3, I_CLOCK, C_ACCENT, "Time zone", tz, true, [](lv_event_t* e) { tz_modal(); });

  lv_obj_t* g4 = group_box(body);
  char about[48];
  snprintf(about, sizeof(about), "MeshSyd " MESHSYD_VERSION " \xC2\xB7 %u mV", getBattMilliVolts());
  settings_row(g4, I_INFO, C_TXT2, "About", about, true, nullptr);

  tab_bar(root, CydScreen::Settings, totalUnread());
}

// =====================================================================
// navigation + loop
// =====================================================================
void UITask::show(CydScreen s) {
  if (g_toast && s != _screen) { /* keep toast across screens */ }
  _screen = s;
  g_ta = nullptr; g_count = nullptr;
  lv_obj_set_user_data(lv_scr_act(), nullptr);
  switch (s) {
    case CydScreen::Home:         buildHome(); break;
    case CydScreen::Chats:        buildChats(); break;
    case CydScreen::Conversation: if (_open_conv >= 0) _convs[_open_conv].unread = 0; buildConversation(); break;
    case CydScreen::Compose:      buildCompose(); break;
    case CydScreen::Contacts:     buildContacts(); break;
    case CydScreen::Settings:     buildSettings(); break;
    case CydScreen::EditName:     buildEditName(); break;
    case CydScreen::ContactDetail: buildContactDetail(); break;
    case CydScreen::Login:        buildLogin(); break;
    case CydScreen::Status:       buildStatus(); break;
    case CydScreen::Console:      buildConsole(); break;
    case CydScreen::CliInput:     buildCliInput(); break;
    case CydScreen::PathEdit:     buildPathEdit(); break;
    case CydScreen::PathInput:    buildPathInput(); break;
  }
  _dirty = false;
#ifdef MESH_DEBUG
  Serial.printf("MeshSyd: screen %d built, heap free %u, largest %u, min %u\n", (int)s, ESP.getFreeHeap(),
                heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), ESP.getMinFreeHeap());
#endif
}

static void splash() {
  lv_obj_t* root = screen_root();
  lv_obj_set_flex_align(root, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(root, 8, 0);
  lv_obj_t* logo = box(root, 64, 64, C_ACC_SOFT, 18);
  lv_obj_center(label(logo, I_TOWER, &icons_18, C_ACCENT));
  label(root, "MeshSyd", &F16B, C_TXT);
  lv_obj_t* v = label(root, "MeshCore \xC2\xB7 " MESHSYD_VERSION, &F10, C_TXT3);
  lv_obj_set_style_text_font(v, &inter_10, 0);
  lv_refr_now(NULL);
}

void UITask::begin(DisplayDriver* disp, SensorManager* sensors, NodePrefs* node_prefs) {
  g_ui = this;
  _display = disp;
  _sensors = sensors;
  _node_prefs = node_prefs;
  loadPrefs();
  loadPasswords();

  F10 = inter_10;   F10.fallback = &icons_14;
  F12 = inter_12;   F12.fallback = &icons_14;
  F12B = inter_12b; F12B.fallback = &icons_14;
  F14 = inter_14;   F14.fallback = &icons_14;
  F16B = inter_16b; F16B.fallback = &icons_18;

  _msgs = (CydMsg*)calloc(CYD_MAX_MSGS, sizeof(CydMsg));
  if (_msgs) loadHistory();
  buf1 = (lv_color_t*)heap_caps_malloc(240 * BUF_LINES * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
  lv_init();
  lv_disp_draw_buf_init(&draw_buf, buf1, nullptr, 240 * BUF_LINES);
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = 240;
  disp_drv.ver_res = 320;
  disp_drv.flush_cb = flush_cb;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = touch_cb;
  indev_drv.scroll_limit = 6;   // start scrolling sooner on the resistive panel
  lv_indev_drv_register(&indev_drv);

  user_btn.begin();
  applyBrightness();
  splash();
  refreshChannels();
  _last_touch = millis();
  _next_tick = millis() + 1200;   // let the splash breathe
  _screen = CydScreen::Home;
  _dirty = true;
#ifdef MESH_DEBUG
  Serial.printf("MeshSyd: heap free %u, largest %u, min %u\n", ESP.getFreeHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), ESP.getMinFreeHeap());
#endif
}

void UITask::loop() {
  lv_timer_handler();
  unsigned long now = millis();

  // BOOT button: click = screen on/off, hold = wake and go Home
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    if (_screen_on) {
      _screen_on = false;
      display.gfx()->setBrightness(0);
    } else wake();
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    wake();
    show(CydScreen::Home);
  }

  if (now < _next_tick) return;
  _next_tick = now + 5;

  if (g_toast && now > _toast_until) { lv_obj_del(g_toast); g_toast = nullptr; }
  if (_save_due && now > _save_due) saveHistory();
  if (_login_pending && now > _login_deadline) {
    _login_pending = false;
    snprintf(_login_msg, sizeof(_login_msg), "No reply. Wrong password, or node out of reach.");
    show(CydScreen::Login);
  }
  if (_status_pending && now > _status_deadline) { _status_pending = false; toast("No status reply"); _dirty = true; }
  if (_cli_pending && now > _cli_deadline) { _cli_pending = false; consoleAppend("", "(no reply)"); }
  if (_ping_tag && now > _ping_deadline) {
    _ping_tag = 0;
    snprintf(_ping_result, sizeof(_ping_result), "No reply");
    if (_screen == CydScreen::ContactDetail) _dirty = true;
  }

  // While dark, poll the touch controller directly (not via LVGL) so any press wakes the screen.
  if (!_screen_on) {
    static unsigned long next_poll = 0;
    if (now >= next_poll) {
      next_poll = now + 50;
      uint16_t tx, ty;
      if (display.gfx()->getTouch(&tx, &ty) > 0) {
#ifdef MESH_DEBUG
        Serial.printf("MeshSyd: wake by touch %u,%u\n", tx, ty);
#endif
        wake();
        _swallow_touch = true;   // the waking press must not click whatever is under it
      }
    }
  }

  // screen timeout
  unsigned long off = screenOffMillis();
  if (_screen_on && off && now - _last_touch > off) {
    _screen_on = false;
    display.gfx()->setBrightness(0);
    if (_screen == CydScreen::Compose || _screen == CydScreen::EditName) { /* keep */ }
  }

#ifdef MESH_DEBUG
  static unsigned long hb = 0;
  if (now - hb > 30000) {
    hb = now;
    uint16_t tx, ty; uint8_t t = display.gfx()->getTouch(&tx, &ty);
    Serial.printf("MeshSyd: alive, screen %d %s, touch %u, reads %lu, heap %u (min %u)\n", (int)_screen, _screen_on ? "on" : "off",
                  t, (unsigned long)g_touch_reads, ESP.getFreeHeap(), ESP.getMinFreeHeap());
    g_touch_reads = 0;
  }
#endif
  // minute tick refreshes clocks / "ago" labels
  uint32_t minute = now_epoch() / 60;
  if (minute != _last_minute) {
    _last_minute = minute;
    if (_screen == CydScreen::Home || _screen == CydScreen::Chats) _dirty = true;
  }

  // rebuild on data changes, but never under the user's finger or while typing
  static unsigned long last_build = 0;
  bool typing = _screen == CydScreen::Compose || _screen == CydScreen::EditName || _screen == CydScreen::Login ||
                _screen == CydScreen::CliInput || _screen == CydScreen::PathInput || g_modal;
  if (_dirty && _screen_on && !typing && now - _last_touch > 600 && now - last_build > 400) {
    last_build = now;
    show(_screen);
  }
}
