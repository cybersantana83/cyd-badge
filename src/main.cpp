// CYD Badge - cracha digital configuravel para ESP32-2432S028 (Cheap Yellow Display)
//
// Tudo e lido do cartao SD em /badge/config.json, entao da para trocar nome, foto,
// paginas e QR codes sem recompilar. Feito para ser instalado pelo Launcher.
//
// Controles:
//   Toque no lado direito ou deslize para a esquerda ... proxima pagina
//   Toque no lado esquerdo ou deslize para a direita ... pagina anterior
//   Segurar o toque 1 a 3 s e soltar ................... pausa / retoma o slideshow
//   Segurar o toque 3 s ................................ modo configuracao (Wi-Fi + painel web)
//   Botao BOOT (clique) ................................ proxima pagina
//   Botao BOOT (clique duplo) .......................... vai para a credencial de evento
//   Botao BOOT (segurar ~1 s) .......................... alterna o brilho
//   Botao BOOT (segurar 3 s) ........................... entra / sai do modo configuracao

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <ArduinoJson.h>
#include <vector>
#include "lgfx_cyd.h"
#include "touch_xpt2046.h"
#include "fonts_embedded.h"
#include "web_ui.h"
#include "jsqr_gz.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>

#define FW_VERSION "1.3.0"
#define CONFIG_PATH "/badge/config.json"

// Pinos da CYD
static constexpr int PIN_SD_CS = 5;
static constexpr int PIN_SD_SCK = 18;
static constexpr int PIN_SD_MISO = 19;
static constexpr int PIN_SD_MOSI = 23;
static constexpr int PIN_BOOT = 0;
static constexpr int PIN_LED_R = 4;
static constexpr int PIN_LED_G = 16;
static constexpr int PIN_LED_B = 17;

static LGFX_CYD tft;
static TouchXPT2046 touch;
static SPIClass sdSPI(VSPI);
static JsonDocument cfg;
static JsonArray pages;

// ---------- Tema ----------
struct Theme {
  uint16_t bg, fg, muted, accent;
} theme;

struct Settings {
  uint8_t rotation = 1;
  uint8_t brightness = 180;
  bool invertColors = false;
  bool touchInvertX = false;
  uint32_t slideshowMs = 0;
  bool showDots = true;
  bool ledFollowAccent = false;
} settings;

// ---------- Fontes ----------
// As fontes Poppins vem embutidas no firmware (com acentos). Opcionalmente, o
// config pode apontar para fontes .vlw no SD para trocar o visual.
enum FontRole { FONT_BIG = 0, FONT_MED = 1, FONT_SMALL = 2, FONT_COUNT };
static String fontPath[FONT_COUNT];
static bool fontSdOk[FONT_COUNT] = {false, false, false};
static bool fontVlwOk[FONT_COUNT] = {true, true, true};
static int currentFont = -1;
static const uint8_t *embeddedFont[FONT_COUNT] = {FONT_BIG_VLW, FONT_MEDIUM_VLW, FONT_SMALL_VLW};
static const lgfx::IFont *fallbackFont[FONT_COUNT] = {
    &lgfx::fonts::FreeSansBold18pt7b,
    &lgfx::fonts::FreeSansBold12pt7b,
    &lgfx::fonts::FreeSans9pt7b,
};

static bool useFont(FontRole role) {
  if (currentFont == role) return fontVlwOk[role];
  tft.unloadFont();
  bool ok = false;
  if (fontSdOk[role]) ok = tft.loadFont(SD, fontPath[role].c_str());
  if (!ok) ok = tft.loadFont(embeddedFont[role]);
  if (!ok) tft.setFont(fallbackFont[role]);
  fontVlwOk[role] = ok;
  tft.setTextSize(1);
  currentFont = role;
  return ok;
}

static void resetFonts() {
  tft.unloadFont();
  currentFont = -1;
}

// Converte UTF-8 com acentos para ASCII (usado so com as fontes internas)
static String asciiFold(const String &in) {
  static const char *from[] = {"á", "à", "â", "ã", "ä", "Á", "À", "Â", "Ã", "Ä", "é", "è", "ê", "É", "È", "Ê",
                               "í", "ì", "î", "Í", "Ì", "Î", "ó", "ò", "ô", "õ", "ö", "Ó", "Ò", "Ô", "Õ", "Ö",
                               "ú", "ù", "û", "ü", "Ú", "Ù", "Û", "Ü", "ç", "Ç", "ñ", "Ñ", "•", "–", "—", "…"};
  static const char *to[] = {"a", "a", "a", "a", "a", "A", "A", "A", "A", "A", "e", "e", "e", "E", "E", "E",
                             "i", "i", "i", "I", "I", "I", "o", "o", "o", "o", "o", "O", "O", "O", "O", "O",
                             "u", "u", "u", "u", "U", "U", "U", "U", "c", "C", "n", "N", "|", "-", "-", "..."};
  String out = in;
  for (size_t i = 0; i < sizeof(from) / sizeof(from[0]); i++) out.replace(from[i], to[i]);
  String clean;
  for (size_t i = 0; i < out.length(); i++) {
    char c = out[i];
    if ((uint8_t)c >= 32 && (uint8_t)c < 127) clean += c;
  }
  return clean;
}

static String prep(const char *s, FontRole role) {
  String str = s ? s : "";
  useFont(role);
  return fontVlwOk[role] ? str : asciiFold(str);
}

// ---------- Utilidades ----------
static uint16_t parseColor(const char *hex, uint16_t def) {
  if (!hex || !*hex) return def;
  if (*hex == '#') hex++;
  if (strlen(hex) != 6) return def;
  uint32_t v = strtoul(hex, nullptr, 16);
  return lgfx::color565((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

static uint32_t parseColor888(const char *hex) {
  if (!hex || !*hex) return 0;
  if (*hex == '#') hex++;
  if (strlen(hex) != 6) return 0;
  return strtoul(hex, nullptr, 16);
}

static void setLed(uint32_t rgb) {
  // LED RGB da CYD e ativo em nivel baixo, sem PWM aqui: liga cada canal acima de 50%
  digitalWrite(PIN_LED_R, ((rgb >> 16) & 0xFF) > 127 ? LOW : HIGH);
  digitalWrite(PIN_LED_G, ((rgb >> 8) & 0xFF) > 127 ? LOW : HIGH);
  digitalWrite(PIN_LED_B, (rgb & 0xFF) > 127 ? LOW : HIGH);
}

static String lowerExt(const String &path) {
  int dot = path.lastIndexOf('.');
  if (dot < 0) return "";
  String e = path.substring(dot + 1);
  e.toLowerCase();
  return e;
}

static bool drawImage(const char *path, int x, int y, int maxW = 0, int maxH = 0, float scale = 1.0f) {
  if (!path || !*path || !SD.exists(path)) return false;
  String ext = lowerExt(path);
  if (ext == "jpg" || ext == "jpeg") return tft.drawJpgFile(SD, path, x, y, maxW, maxH, 0, 0, scale, scale);
  if (ext == "png") return tft.drawPngFile(SD, path, x, y, maxW, maxH, 0, 0, scale, scale);
  if (ext == "bmp") return tft.drawBmpFile(SD, path, x, y, maxW, maxH, 0, 0, scale, scale);
  return false;
}

// Quebra uma palavra maior que maxW em pedacos, sem cortar caracteres UTF-8 ao meio.
// Prefere quebrar logo depois de '/', '.', '_' ou '-' (bom para links e @).
static void splitLongWord(const String &word, int maxW, std::vector<String> &out) {
  String piece;
  size_t i = 0;
  while (i < word.length()) {
    size_t len = 1;
    uint8_t c = word[i];
    if (c >= 0xF0) len = 4;
    else if (c >= 0xE0) len = 3;
    else if (c >= 0xC0) len = 2;
    String ch = word.substring(i, i + len);
    if (piece.length() && tft.textWidth(piece + ch) > maxW) {
      int cut = -1;
      for (int k = piece.length() - 1; k > (int)piece.length() / 3; k--) {
        char pc = piece[k];
        if (pc == '/' || pc == '.' || pc == '_' || pc == '-') {
          cut = k + 1;
          break;
        }
      }
      if (cut > 0 && cut < (int)piece.length()) {
        out.push_back(piece.substring(0, cut));
        piece = piece.substring(cut);
      } else {
        out.push_back(piece);
        piece = "";
      }
    }
    piece += ch;
    i += len;
  }
  if (piece.length()) out.push_back(piece);
}

// Desenha texto com quebra de linha automatica. Retorna o y depois da ultima linha.
// Com draw=false so mede (para centralizar blocos de texto).
static int drawWrapped(const char *text, int x, int y, int maxW, FontRole role, uint16_t fg, uint16_t bg,
                       int maxLines = 3, textdatum_t datum = top_left, bool draw = true) {
  String s = prep(text, role);
  if (s.length() == 0) return y;
  tft.setTextColor(fg, bg);
  tft.setTextDatum(datum);
  int lineH = tft.fontHeight() + 2;

  std::vector<String> lines;
  int start = 0;
  while (start <= (int)s.length()) {
    int nl = s.indexOf('\n', start);
    if (nl < 0) nl = s.length();
    String para = s.substring(start, nl);
    start = nl + 1;

    String line;
    int ws = 0;
    while (ws <= (int)para.length()) {
      int sp = para.indexOf(' ', ws);
      if (sp < 0) sp = para.length();
      String word = para.substring(ws, sp);
      ws = sp + 1;
      if (!word.length()) continue;

      std::vector<String> parts;
      if (tft.textWidth(word) > maxW) splitLongWord(word, maxW, parts);
      else parts.push_back(word);

      for (auto &w : parts) {
        String test = line.length() ? line + " " + w : w;
        if (line.length() && tft.textWidth(test) > maxW) {
          lines.push_back(line);
          line = w;
        } else {
          line = test;
        }
      }
    }
    if (line.length()) lines.push_back(line);
  }

  for (int i = 0; i < (int)lines.size() && i < maxLines; i++) {
    String l = lines[i];
    if (i == maxLines - 1 && (int)lines.size() > maxLines) l += "...";
    if (draw) tft.drawString(l, x, y);
    y += lineH;
  }
  return y;
}

static int measureWrapped(const char *text, int maxW, FontRole role, int maxLines) {
  return drawWrapped(text, 0, 0, maxW, role, 0, 0, maxLines, top_left, false);
}

// Escolhe a maior fonte em que o texto cabe numa linha (para @ e links)
static FontRole fitFont(const char *text, int maxW) {
  String s = prep(text, FONT_MED);
  if (tft.textWidth(s) <= maxW) return FONT_MED;
  s = prep(text, FONT_SMALL);
  if (tft.textWidth(s) <= maxW) return FONT_SMALL;
  return FONT_MED;
}

static void drawDots(int count, int active, uint16_t accent, uint16_t muted) {
  if (!settings.showDots || count < 2) return;
  const int gap = 12;
  int total = (count - 1) * gap;
  int x0 = tft.width() / 2 - total / 2;
  int y = tft.height() - 9;
  for (int i = 0; i < count; i++) {
    if (i == active)
      tft.fillCircle(x0 + i * gap, y, 3, accent);
    else
      tft.fillCircle(x0 + i * gap, y, 2, muted);
  }
}

static String initials(const char *name) {
  String n = asciiFold(name ? name : "");
  String out;
  bool start = true;
  for (size_t i = 0; i < n.length() && out.length() < 2; i++) {
    if (n[i] == ' ') {
      start = true;
    } else if (start) {
      out += (char)toupper(n[i]);
      start = false;
    }
  }
  return out.length() ? out : "?";
}

// Foto do perfil, redimensionada para "size". srcPx = lado da imagem no SD.
static void drawAvatar(const char *path, const char *name, int x, int y, int size, int srcPx, const char *shape,
                       uint16_t bg, uint16_t accent) {
  bool circle = !shape || strcmp(shape, "square") != 0;
  float scale = srcPx > 0 ? (float)size / srcPx : 1.0f;
  bool ok = drawImage(path, x, y, size, size, scale);
  int r = size / 2;
  int cx = x + r, cy = y + r;
  if (!ok) {
    tft.fillCircle(cx, cy, r, accent);
    useFont(FONT_BIG);
    tft.setTextColor(bg, accent);
    tft.setTextDatum(middle_center);
    tft.drawString(initials(name), cx, cy);
    return;
  }
  if (circle) {
    // Mascara: pinta o fundo fora do circulo, linha a linha
    for (int j = 0; j < size; j++) {
      float dy = j + 0.5f - r;
      float h = sqrtf(fmaxf(0, (float)r * r - dy * dy));
      int left = (int)(r - h);
      if (left > 0) {
        tft.drawFastHLine(x, y + j, left, bg);
        tft.drawFastHLine(x + size - left, y + j, left, bg);
      }
    }
    for (int k = -1; k < 3; k++) tft.drawCircle(cx, cy, r + k, accent);
  } else {
    for (int k = 0; k < 3; k++) tft.drawRect(x - 1 - k, y - 1 - k, size + 2 + 2 * k, size + 2 + 2 * k, accent);
  }
}

static void pageColors(JsonObject p, uint16_t &bg, uint16_t &fg, uint16_t &muted, uint16_t &accent) {
  bg = parseColor(p["bg"] | "", theme.bg);
  fg = parseColor(p["fg"] | "", theme.fg);
  muted = parseColor(p["muted"] | "", theme.muted);
  accent = parseColor(p["accent"] | "", theme.accent);
}

// ---------- Paginas ----------
static const int DOTS_H = 18;  // faixa reservada para os pontos no rodape

static void renderProfile(JsonObject p, uint16_t bg, uint16_t fg, uint16_t muted, uint16_t accent) {
  const int W = tft.width(), H = tft.height();
  const char *name = p["name"] | "";
  const char *title = p["title"] | "";
  const char *subtitle = p["subtitle"] | "";
  const char *layout = p["layout"] | "center";
  const char *shape = p["image_shape"] | "circle";
  const char *image = p["image"] | "";
  int srcPx = p["image_px"] | (p["image_size"] | 100);

  if (strcmp(layout, "side") == 0 && W > H) {
    int size = 110;
    int ax = 14, tx = ax + size + 16, tw = W - tx - 8;
    int hName = measureWrapped(name, tw, FONT_BIG, 2);
    int hTitle = measureWrapped(title, tw, FONT_MED, 2);
    int hSub = measureWrapped(subtitle, tw, FONT_SMALL, 3);
    int textH = hName + 4 + 13 + hTitle + 4 + hSub;
    int avail = H - DOTS_H;
    drawAvatar(image, name, ax, (avail - size) / 2, size, srcPx, shape, bg, accent);
    int y = max(6, (avail - textH) / 2);
    y = drawWrapped(name, tx, y, tw, FONT_BIG, fg, bg, 2) + 4;
    tft.fillRect(tx, y, 40, 3, accent);
    y += 13;
    y = drawWrapped(title, tx, y, tw, FONT_MED, accent, bg, 2) + 4;
    drawWrapped(subtitle, tx, y, tw, FONT_SMALL, muted, bg, 3);
    return;
  }

  // Foto em cima: em retrato a foto cresce e tudo fica centralizado na vertical
  bool portrait = H > W;
  int maxW = W - 20;
  int subLines = portrait ? 3 : 2;
  int hName = measureWrapped(name, maxW, FONT_BIG, 2);
  int hTitle = measureWrapped(title, maxW, FONT_MED, 2);
  int hSub = measureWrapped(subtitle, maxW, FONT_SMALL, subLines);
  const int gap = portrait ? 14 : 8;
  int textH = hName + (hTitle ? 2 + hTitle : 0) + (hSub ? 4 + hSub : 0);
  int avail = H - DOTS_H - 8;
  int size = portrait ? 140 : 96;
  if (size + gap + textH > avail) size = max(64, avail - gap - textH);
  int total = size + gap + textH;
  int y = 6 + max(0, (avail - total) / 2);

  drawAvatar(image, name, (W - size) / 2, y, size, srcPx, shape, bg, accent);
  y += size + gap;
  y = drawWrapped(name, W / 2, y, maxW, FONT_BIG, fg, bg, 2, top_center);
  if (hTitle) y = drawWrapped(title, W / 2, y + 2, maxW, FONT_MED, accent, bg, 2, top_center);
  if (hSub) drawWrapped(subtitle, W / 2, y + 4, maxW, FONT_SMALL, muted, bg, subLines, top_center);
}

static void drawHeader(const char *label, uint16_t accent, uint16_t textColor) {
  const int W = tft.width();
  tft.fillRect(0, 0, W, 34, accent);
  String s = prep(label, FONT_MED);
  tft.setTextColor(textColor, accent);
  tft.setTextDatum(middle_left);
  tft.drawString(s, 12, 17);
}

static void drawQrBox(const char *data, int x, int y, int size) {
  tft.fillRoundRect(x, y, size, size, 8, TFT_WHITE);
  const int pad = 8;
  tft.qrcode(data, x + pad, y + pad, size - 2 * pad, 1);
}

static void renderQr(JsonObject p, const char *data, uint16_t bg, uint16_t fg, uint16_t muted, uint16_t accent) {
  const int W = tft.width(), H = tft.height();
  uint16_t headerText = parseColor(p["header_fg"] | "", TFT_WHITE);
  drawHeader(p["label"] | "", accent, headerText);

  const char *handle = p["handle"] | "";
  const char *note = p["note"] | "";
  const char *icon = p["icon"] | "";
  const int top = 34;

  if (W > H) {
    int qs = H - top - 10 - DOTS_H;
    int qx = 10, qy = top + 6;
    drawQrBox(data, qx, qy, qs);
    int tx = qx + qs + 12, tw = W - tx - 6;
    FontRole hf = fitFont(handle, tw);
    int hIcon = (*icon && SD.exists(icon)) ? 56 : 0;
    int textH = hIcon + measureWrapped(handle, tw, hf, 3) + 6 + measureWrapped(note, tw, FONT_SMALL, 5);
    int y = qy + max(0, (qs - textH) / 2);
    if (hIcon && drawImage(icon, tx, y, 48, 48)) y += hIcon;
    y = drawWrapped(handle, tx, y, tw, hf, fg, bg, 3) + 6;
    drawWrapped(note, tx, y, tw, FONT_SMALL, muted, bg, 5);
  } else {
    int tw = W - 16;
    FontRole hf = fitFont(handle, tw);
    int hHandle = measureWrapped(handle, tw, hf, 2);
    int hNote = measureWrapped(note, tw, FONT_SMALL, 3);
    int avail = H - top - DOTS_H;
    bool big = strcmp(p["layout"] | "", "big") == 0;
    int qs = big ? min(W - 12, avail - hHandle - hNote - 18) : min(W - 32, avail - hHandle - hNote - 26);
    int total = qs + 10 + hHandle + (hNote ? 4 + hNote : 0);
    int y = top + max(6, (avail - total) / 2);
    drawQrBox(data, (W - qs) / 2, y, qs);
    y += qs + 10;
    y = drawWrapped(handle, W / 2, y, tw, hf, fg, bg, 2, top_center);
    drawWrapped(note, W / 2, y + 4, tw, FONT_SMALL, muted, bg, 3, top_center);
  }
}

// MECARD: formato de contato lido pelas cameras do iOS e Android
static String escapeMecard(const char *s) {
  String out;
  for (const char *c = s; c && *c; c++) {
    if (*c == ';' || *c == ':' || *c == ',' || *c == '\\') out += '\\';
    out += *c;
  }
  return out;
}

static String buildMecard(JsonObject p) {
  String m = "MECARD:";
  auto add = [&](const char *key, const char *field) {
    const char *v = p[field] | "";
    if (*v) {
      m += key;
      m += ':';
      m += escapeMecard(v);
      m += ';';
    }
  };
  add("N", "name");
  add("ORG", "org");
  add("TEL", "phone");
  add("EMAIL", "email");
  add("URL", "url");
  add("NOTE", "note_card");
  m += ';';
  return m;
}

static void renderImage(JsonObject p, uint16_t bg, uint16_t fg) {
  const char *path = p["image"] | "";
  if (!drawImage(path, 0, 0, tft.width(), tft.height())) {
    useFont(FONT_SMALL);
    tft.setTextColor(fg, bg);
    tft.setTextDatum(middle_center);
    tft.drawString(String("Imagem nao encontrada: ") + path, tft.width() / 2, tft.height() / 2);
  }
  const char *caption = p["caption"] | "";
  if (*caption) {
    int h = 26;
    tft.fillRect(0, tft.height() - h - 16, tft.width(), h, bg);
    drawWrapped(caption, tft.width() / 2, tft.height() - h - 12, tft.width() - 16, FONT_SMALL, fg, bg, 1,
                top_center);
  }
}

static void renderText(JsonObject p, uint16_t bg, uint16_t fg, uint16_t muted, uint16_t accent) {
  uint16_t headerText = parseColor(p["header_fg"] | "", TFT_WHITE);
  drawHeader(p["label"] | "", accent, headerText);
  int y = 48;
  y = drawWrapped(p["title"] | "", 12, y, tft.width() - 24, FONT_MED, fg, bg, 2);
  y += 6;
  drawWrapped(p["body"] | "", 12, y, tft.width() - 24, FONT_SMALL, muted, bg, 8);
}

static bool configMode = false;
static void drawWifiChip();
static uint8_t userBrightness = 180;  // brilho escolhido (config ou botao BOOT)
static bool boosted = false;          // pagina atual pediu brilho maximo

// Paginas com "boost" (ex.: credencial de evento) usam brilho maximo para o
// leitor do expositor conseguir ler o QR na tela
static void applyPageBrightness(JsonObject p) {
  bool want = p["boost"] | false;
  if (want != boosted || !want) tft.setBrightness(want ? 255 : userBrightness);
  boosted = want;
}

static void renderPage(int index) {
  tft.setRotation(settings.rotation);
  JsonObject p = pages[index];
  applyPageBrightness(p);
  uint16_t bg, fg, muted, accent;
  pageColors(p, bg, fg, muted, accent);
  tft.startWrite();
  tft.fillScreen(bg);

  const char *type = p["type"] | "profile";
  if (strcmp(type, "profile") == 0) {
    renderProfile(p, bg, fg, muted, accent);
  } else if (strcmp(type, "qr") == 0) {
    const char *data = p["url"] | (p["data"] | "");
    renderQr(p, data, bg, fg, muted, accent);
  } else if (strcmp(type, "vcard") == 0) {
    if (!p["label"].is<const char *>()) p["label"] = "Contato";
    String mecard = buildMecard(p);
    renderQr(p, mecard.c_str(), bg, fg, muted, accent);
  } else if (strcmp(type, "image") == 0) {
    renderImage(p, bg, fg);
  } else if (strcmp(type, "text") == 0) {
    renderText(p, bg, fg, muted, accent);
  } else {
    drawWrapped((String("Tipo de pagina desconhecido: ") + type).c_str(), 12, 100, tft.width() - 24, FONT_SMALL,
                fg, bg);
  }

  if (!(p["hide_dots"] | false)) drawDots(pages.size(), index, accent, muted);
  if (configMode) drawWifiChip();
  tft.endWrite();

  if (settings.ledFollowAccent) setLed(parseColor888(p["led"] | (p["accent"] | "")));
}

// ---------- Tela de erro / ajuda ----------
static void showError(const String &title, const String &detail) {
  tft.fillScreen(TFT_BLACK);
  tft.setFont(&lgfx::fonts::FreeSansBold12pt7b);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.setTextDatum(top_left);
  tft.drawString(asciiFold(title), 10, 12);
  tft.setFont(&lgfx::fonts::FreeSans9pt7b);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  int y = 50;
  String d = asciiFold(detail);
  int start = 0;
  while (start < (int)d.length()) {
    int nl = d.indexOf('\n', start);
    if (nl < 0) nl = d.length();
    tft.drawString(d.substring(start, nl), 10, y);
    y += 20;
    start = nl + 1;
  }
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("CYD Badge v" FW_VERSION, 10, tft.height() - 22);
}

// ---------- Configuracao ----------
static const char DEFAULT_CONFIG[] PROGMEM = R"json({
  "settings": { "rotation": 1, "brightness": 180, "slideshow_seconds": 0 },
  "pages": [
    { "type": "profile", "kind": "profile", "name": "Seu Nome", "title": "Seu cargo",
      "subtitle": "Segure o dedo 3 s na tela para configurar", "image_shape": "circle", "layout": "center" }
  ]
})json";

static bool writeFile(const char *path, const String &content) {
  String tmp = String(path) + ".tmp";
  File f = SD.open(tmp, FILE_WRITE);
  if (!f) return false;
  size_t n = f.print(content);
  f.close();
  if (n != content.length()) return false;
  SD.remove(path);
  return SD.rename(tmp, path);
}

// Primeiro uso: cria /badge e um config padrao. Retorna true se criou.
static bool ensureDefaultConfig() {
  if (!SD.exists("/badge")) SD.mkdir("/badge");
  if (!SD.exists("/badge/img")) SD.mkdir("/badge/img");
  if (SD.exists(CONFIG_PATH)) return false;
  return writeFile(CONFIG_PATH, FPSTR(DEFAULT_CONFIG));
}

static bool loadConfig(String &err) {
  if (!SD.exists(CONFIG_PATH)) {
    err = "Arquivo nao encontrado:\n" CONFIG_PATH;
    return false;
  }
  cfg.clear();
  File f = SD.open(CONFIG_PATH);
  DeserializationError e = deserializeJson(cfg, f);
  f.close();
  if (e) {
    err = String("JSON invalido: ") + e.c_str() + "\nConfira virgulas e aspas.";
    return false;
  }
  pages = cfg["pages"].as<JsonArray>();
  if (pages.isNull() || pages.size() == 0) {
    err = "Nenhuma pagina em \"pages\".";
    return false;
  }

  JsonObject s = cfg["settings"];
  settings.rotation = (s["rotation"] | 1) & 3;
  settings.brightness = s["brightness"] | 180;
  settings.invertColors = s["invert_colors"] | false;
  settings.touchInvertX = s["touch_invert_x"] | false;
  settings.slideshowMs = (uint32_t)(s["slideshow_seconds"] | 0) * 1000UL;
  settings.showDots = s["show_dots"] | true;
  settings.ledFollowAccent = s["led_follow_accent"] | false;

  theme.bg = parseColor(s["bg"] | "", lgfx::color565(0x0D, 0x11, 0x17));
  theme.fg = parseColor(s["fg"] | "", lgfx::color565(0xE6, 0xED, 0xF3));
  theme.muted = parseColor(s["muted"] | "", lgfx::color565(0x8B, 0x94, 0x9E));
  theme.accent = parseColor(s["accent"] | "", lgfx::color565(0xF5, 0xA6, 0x23));

  const char *keys[FONT_COUNT] = {"font_big", "font_medium", "font_small"};
  for (int i = 0; i < FONT_COUNT; i++) {
    fontPath[i] = s[keys[i]] | "";
    fontSdOk[i] = fontPath[i].length() && SD.exists(fontPath[i]);
  }
  return true;
}

// ---------- Estado ----------
static int pageIndex = 0;
static bool needRedraw = true;
static bool slideshowPaused = false;
static uint32_t lastPageChange = 0;

static void goPage(int delta) {
  int n = pages.size();
  pageIndex = ((pageIndex + delta) % n + n) % n;
  needRedraw = true;
  lastPageChange = millis();
}

static void toast(const char *msg) {
  useFont(FONT_SMALL);
  String s = prep(msg, FONT_SMALL);
  int w = tft.textWidth(s) + 24, h = tft.fontHeight() + 12;
  int x = (tft.width() - w) / 2, y = tft.height() / 2 - h / 2;
  tft.fillRoundRect(x, y, w, h, 8, TFT_BLACK);
  tft.drawRoundRect(x, y, w, h, 8, theme.accent);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(middle_center);
  tft.drawString(s, tft.width() / 2, y + h / 2);
  delay(900);
  needRedraw = true;
}

static void applySettings() {
  tft.setRotation(settings.rotation);
  tft.invertDisplay(settings.invertColors);
  userBrightness = settings.brightness;
  tft.setBrightness(boosted ? 255 : userBrightness);
  if (!settings.ledFollowAccent) setLed(0);
}

// ---------- Modo configuracao (Wi-Fi + painel web) ----------
static WebServer server(80);
static DNSServer dns;
static String apSsid, apPass;
static uint32_t lastWebActivity = 0;
static bool showingConfigScreen = false;
static const uint32_t CONFIG_IDLE_TIMEOUT_MS = 15UL * 60UL * 1000UL;  // desliga o Wi-Fi apos 15 min sem uso
static File uploadFile;
static bool uploadOk = false;
static bool exitRequested = false;
static uint32_t exitRequestedAt = 0;

static void drawWifiChip() {
  int W = tft.width();
  tft.fillRoundRect(W - 46, 4, 42, 16, 8, TFT_BLACK);
  tft.drawRoundRect(W - 46, 4, 42, 16, 8, theme.accent);
  useFont(FONT_SMALL);
  tft.setTextColor(theme.accent, TFT_BLACK);
  tft.setTextDatum(middle_center);
  tft.drawString("Wi-Fi", W - 25, 12);
}

static void drawConfigScreen() {
  tft.setRotation(settings.rotation);  // segue a orientacao escolhida (retrato ou paisagem)
  const int W = tft.width(), H = tft.height();
  const uint16_t bg = lgfx::color565(0x0D, 0x11, 0x17);
  const uint16_t fg = lgfx::color565(0xE6, 0xED, 0xF3);
  const uint16_t mut = lgfx::color565(0x8B, 0x94, 0x9E);
  const uint16_t acc = lgfx::color565(0xF5, 0xA6, 0x23);
  tft.startWrite();
  tft.fillScreen(bg);
  tft.fillRect(0, 0, W, 30, acc);
  useFont(FONT_MED);
  tft.setTextColor(TFT_BLACK, acc);
  tft.setTextDatum(middle_left);
  tft.drawString(prep("Modo configuração", FONT_MED), 10, 15);

  String wifiQr = "WIFI:T:WPA;S:" + apSsid + ";P:" + apPass + ";;";
  const int qs = 104, y = 38;
  const int x1 = W / 4 - qs / 2, x2 = 3 * W / 4 - qs / 2;
  tft.fillRoundRect(x1, y, qs, qs, 6, TFT_WHITE);
  tft.qrcode(wifiQr.c_str(), x1 + 6, y + 6, qs - 12, 1);
  tft.fillRoundRect(x2, y, qs, qs, 6, TFT_WHITE);
  tft.qrcode("http://192.168.4.1", x2 + 6, y + 6, qs - 12, 1);

  useFont(FONT_SMALL);
  tft.setTextDatum(top_center);
  tft.setTextColor(fg, bg);
  tft.drawString(prep(W > H ? "1. Conecte no Wi-Fi" : "1. Wi-Fi", FONT_SMALL), W / 4, y + qs + 4);
  tft.drawString(prep(W > H ? "2. Abra o painel" : "2. Painel", FONT_SMALL), 3 * W / 4, y + qs + 4);

  tft.setTextDatum(top_left);
  int ty = y + qs + 26;
  tft.setTextColor(mut, bg);
  tft.drawString("Rede:", 10, ty);
  tft.drawString("Senha:", 10, ty + 18);
  tft.drawString("Painel:", 10, ty + 36);
  tft.setTextColor(fg, bg);
  tft.drawString(apSsid, 74, ty);
  tft.drawString(apPass, 74, ty + 18);
  tft.drawString("http://192.168.4.1", 74, ty + 36);
  tft.setTextColor(mut, bg);
  tft.setTextDatum(bottom_right);
  tft.drawString(prep("BOOT 3 s: sair", FONT_SMALL), W - 8, H - 4);
  tft.endWrite();
  showingConfigScreen = true;
}

static bool safePath(const String &p, const char *prefix) {
  return p.startsWith(prefix) && p.indexOf("..") < 0 && p.length() < 96;
}

static void reloadAndShow() {
  String err;
  JsonDocument backup = cfg;
  if (!loadConfig(err)) {
    cfg = backup;
    pages = cfg["pages"].as<JsonArray>();
  }
  resetFonts();
  applySettings();
  if (pageIndex >= (int)pages.size()) pageIndex = 0;
  showingConfigScreen = false;
  needRedraw = true;
}

// Remove imagens enviadas pelo painel (prefixo u_) que nao estao mais em uso
static void cleanupUploads(const String &json) {
  File dir = SD.open("/badge/img");
  if (!dir) return;
  std::vector<String> toDelete;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String name = f.name();
    int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    f.close();
    if (name.startsWith("u_") && json.indexOf(name) < 0) toDelete.push_back("/badge/img/" + name);
  }
  dir.close();
  for (auto &p : toDelete) SD.remove(p);
}

static void stopConfigMode();

static void setupRoutes() {
  auto touchActivity = []() { lastWebActivity = millis(); };

  server.on("/", HTTP_GET, [touchActivity]() {
    touchActivity();
    server.sendHeader("Cache-Control", "no-store");
    server.send_P(200, "text/html; charset=utf-8", WEB_UI_HTML);
  });

  server.on("/jsqr.js", HTTP_GET, [touchActivity]() {
    touchActivity();
    server.sendHeader("Content-Encoding", "gzip");
    server.sendHeader("Cache-Control", "max-age=86400");
    server.send_P(200, "application/javascript", (const char *)JSQR_GZ, sizeof(JSQR_GZ));
  });

  server.on("/api/config", HTTP_GET, [touchActivity]() {
    touchActivity();
    File f = SD.open(CONFIG_PATH);
    if (!f) {
      server.send_P(200, "application/json", DEFAULT_CONFIG);
      return;
    }
    server.streamFile(f, "application/json");
    f.close();
  });

  server.on("/api/config", HTTP_POST, [touchActivity]() {
    touchActivity();
    String body = server.arg("plain");
    JsonDocument test;
    DeserializationError e = deserializeJson(test, body);
    if (e) {
      server.send(400, "text/plain", String("JSON invalido: ") + e.c_str());
      return;
    }
    if (!test["pages"].is<JsonArray>() || test["pages"].size() == 0) {
      server.send(400, "text/plain", "Adicione pelo menos uma pagina");
      return;
    }
    if (!writeFile(CONFIG_PATH, body)) {
      server.send(500, "text/plain", "Falha ao gravar no cartao SD");
      return;
    }
    cleanupUploads(body);
    server.send(200, "application/json", "{\"ok\":true}");
    reloadAndShow();
  });

  server.on(
      "/api/upload", HTTP_POST,
      [touchActivity]() {
        touchActivity();
        server.send(uploadOk ? 200 : 400, "application/json", uploadOk ? "{\"ok\":true}" : "{\"ok\":false}");
      },
      [touchActivity]() {
        touchActivity();
        HTTPUpload &up = server.upload();
        if (up.status == UPLOAD_FILE_START) {
          String path = server.arg("path");
          uploadOk = safePath(path, "/badge/img/");
          if (uploadOk) {
            if (!SD.exists("/badge/img")) SD.mkdir("/badge/img");
            uploadFile = SD.open(path, FILE_WRITE);
            uploadOk = (bool)uploadFile;
          }
        } else if (up.status == UPLOAD_FILE_WRITE) {
          if (uploadOk && uploadFile && uploadFile.write(up.buf, up.currentSize) != up.currentSize) uploadOk = false;
        } else if (up.status == UPLOAD_FILE_END) {
          if (uploadFile) uploadFile.close();
        } else if (up.status == UPLOAD_FILE_ABORTED) {
          if (uploadFile) uploadFile.close();
          uploadOk = false;
        }
      });

  server.on("/api/file", HTTP_GET, [touchActivity]() {
    touchActivity();
    String path = server.arg("path");
    if (!safePath(path, "/badge/") || !SD.exists(path)) {
      server.send(404, "text/plain", "nao encontrado");
      return;
    }
    File f = SD.open(path);
    String ext = lowerExt(path);
    const char *type = ext == "png" ? "image/png" : ext == "bmp" ? "image/bmp" : "image/jpeg";
    server.streamFile(f, type);
    f.close();
  });

  // Nao desligamos o Wi-Fi aqui dentro: parar o servidor no meio da propria
  // requisicao travava a placa. So marcamos, e o loop desliga logo depois.
  server.on("/api/exit", HTTP_POST, []() {
    server.send(200, "application/json", "{\"ok\":true}");
    exitRequested = true;
    exitRequestedAt = millis();
  });

  // Testes de conectividade dos celulares: respondemos "tem internet" para o
  // sistema NAO abrir a janela de login (que bloqueia upload de fotos) e NAO
  // derrubar a conexao com a placa. O painel e aberto pelo QR 2 no navegador.
  server.on("/generate_204", HTTP_GET, []() { server.send(204, "text/plain", ""); });
  server.on("/gen_204", HTTP_GET, []() { server.send(204, "text/plain", ""); });
  server.on("/hotspot-detect.html", HTTP_GET, []() {
    server.send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
  });
  server.on("/library/test/success.html", HTTP_GET, []() {
    server.send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
  });
  server.on("/connecttest.txt", HTTP_GET, []() { server.send(200, "text/plain", "Microsoft Connect Test"); });
  server.on("/ncsi.txt", HTTP_GET, []() { server.send(200, "text/plain", "Microsoft NCSI"); });
  server.on("/success.txt", HTTP_GET, []() { server.send(200, "text/plain", "success\n"); });
  server.on("/canonical.html", HTTP_GET, []() { server.send(200, "text/html", "<meta http-equiv=\"refresh\" content=\"0;url=https://support.mozilla.org/kb/captive-portal\"/>"); });
  server.on("/favicon.ico", HTTP_GET, []() { server.send(204, "text/plain", ""); });

  // Qualquer outro endereco leva ao painel
  server.onNotFound([]() {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
  });
}

static String randomPass(int len) {
  const char *chars = "abcdefghjkmnpqrstuvwxyz23456789";  // sem caracteres ambiguos (l, 1, o, 0)
  String s;
  for (int i = 0; i < len; i++) s += chars[esp_random() % strlen(chars)];
  return s;
}

static void startConfigMode() {
  if (configMode) {
    drawConfigScreen();
    return;
  }
  uint8_t mac[6];
  WiFi.mode(WIFI_AP);
  WiFi.macAddress(mac);
  char ssid[24];
  snprintf(ssid, sizeof(ssid), "CYD-Badge-%02X%02X", mac[4], mac[5]);
  apSsid = ssid;
  apPass = randomPass(8);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(apSsid.c_str(), apPass.c_str());
  dns.start(53, "*", IPAddress(192, 168, 4, 1));
  static bool routesReady = false;
  if (!routesReady) {
    setupRoutes();
    routesReady = true;
  }
  server.begin();
  configMode = true;
  lastWebActivity = millis();
  slideshowPaused = true;
  needRedraw = false;
  drawConfigScreen();
  Serial.printf("Modo configuracao: %s / %s\n", apSsid.c_str(), apPass.c_str());
}

static void stopConfigMode() {
  exitRequested = false;
  if (!configMode) return;
  server.stop();
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  configMode = false;
  slideshowPaused = false;
  showingConfigScreen = false;
  needRedraw = true;
  lastPageChange = millis();
}

// ---------- Entrada ----------
// Converte o eixo bruto do touch em coordenada X da tela
static int touchToScreenX(uint16_t rawX, uint16_t rawY) {
  int W = tft.width();
  int sx;
  switch (tft.getRotation() & 3) {
    case 1: sx = map(rawY, 200, 3700, 0, W); break;
    case 3: sx = W - map(rawY, 200, 3700, 0, W); break;
    case 0: sx = W - map(rawX, 200, 3700, 0, W); break;
    default: sx = map(rawX, 200, 3700, 0, W); break;
  }
  if (settings.touchInvertX) sx = W - sx;
  return constrain(sx, 0, W - 1);
}

static void handleTouch() {
  static bool down = false;
  static uint32_t downAt = 0, upAt = 0;
  static int startX = 0, lastX = 0;
  static bool configFired = false;

  uint16_t rx, ry;
  bool pressed = touch.read(rx, ry);
  uint32_t now = millis();

  if (pressed) {
    upAt = 0;
    int sx = touchToScreenX(rx, ry);
    if (!down) {
      down = true;
      downAt = now;
      startX = lastX = sx;
      configFired = false;
    } else {
      lastX = sx;
      if (!configFired && now - downAt > 3000 && abs(lastX - startX) < 40) {
        configFired = true;
        startConfigMode();
      }
    }
    return;
  }
  if (!down) return;

  // pequeno atraso para nao confundir uma leitura falha com soltar o dedo
  if (upAt == 0) {
    upAt = now;
    return;
  }
  if (now - upAt < 40) return;
  upAt = 0;
  down = false;
  if (configFired) return;

  uint32_t held = now - downAt;
  int dx = lastX - startX;
  if (held > 1000 && abs(dx) < 40) {
    if (settings.slideshowMs && !configMode) {
      slideshowPaused = !slideshowPaused;
      toast(slideshowPaused ? "Slideshow pausado" : "Slideshow ativo");
    }
    return;
  }
  if (abs(dx) > 60)
    goPage(dx < 0 ? +1 : -1);  // deslizar para a esquerda avanca
  else
    goPage(startX < tft.width() / 3 ? -1 : +1);
}

// Primeira pagina marcada como credencial (kind "event" ou boost), ou -1
static int findEventPage() {
  for (int i = 0; i < (int)pages.size(); i++) {
    JsonObject p = pages[i];
    if (strcmp(p["kind"] | "", "event") == 0 || (p["boost"] | false)) return i;
  }
  return -1;
}

static bool pendingClick = false;
static uint32_t pendingClickAt = 0;
static const uint32_t DOUBLE_CLICK_MS = 350;

static void handlePendingClick() {
  if (pendingClick && millis() - pendingClickAt > DOUBLE_CLICK_MS) {
    pendingClick = false;
    goPage(+1);
  }
}

static void handleBootButton() {
  static bool wasDown = false;
  static uint32_t downAt = 0;
  static bool configFired = false;
  static const uint8_t levels[] = {40, 90, 160, 255};
  static uint8_t level = 2;

  bool isDown = digitalRead(PIN_BOOT) == LOW;
  uint32_t now = millis();
  if (isDown && !wasDown) {
    downAt = now;
    configFired = false;
  } else if (isDown && !configFired && now - downAt > 3000) {
    configFired = true;
    if (configMode) stopConfigMode();
    else startConfigMode();
  } else if (!isDown && wasDown && !configFired) {
    uint32_t held = now - downAt;
    if (held > 700) {
      level = (level + 1) % sizeof(levels);
      userBrightness = levels[level];
      if (!boosted) tft.setBrightness(userBrightness);
    } else if (held > 30) {
      int ev = findEventPage();
      if (ev < 0) {
        goPage(+1);  // sem credencial: clique simples age na hora
      } else if (pendingClick && now - pendingClickAt <= DOUBLE_CLICK_MS) {
        pendingClick = false;  // clique duplo: vai direto para a credencial
        pageIndex = ev;
        needRedraw = true;
        lastPageChange = now;
      } else {
        pendingClick = true;
        pendingClickAt = now;
      }
    }
  }
  wasDown = isDown;
}

// ---------- Setup / Loop ----------
void setup() {
  Serial.begin(115200);
  pinMode(PIN_BOOT, INPUT_PULLUP);
  pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);
  setLed(0);

  tft.init();
  tft.setRotation(1);
  tft.setBrightness(180);
  tft.fillScreen(TFT_BLACK);
  touch.begin();

  sdSPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  if (!SD.begin(PIN_SD_CS, sdSPI, 20000000)) {
    showError("Sem cartao SD", "Insira um cartao SD (FAT32)\ne reinicie a placa.\n\nO badge cria tudo sozinho\nno primeiro uso.");
    while (true) delay(1000);
  }

  bool firstRun = ensureDefaultConfig();
  String err;
  if (!loadConfig(err)) {
    showError("Erro na configuracao", err + "\n\nSegure BOOT 3 s para abrir\no painel e corrigir.");
    // permite consertar pelo painel mesmo com o arquivo quebrado
    while (true) {
      static bool down = false;
      static uint32_t at = 0;
      bool d = digitalRead(PIN_BOOT) == LOW;
      if (d && !down) at = millis();
      down = d;
      if (d && millis() - at > 3000) break;
      delay(10);
    }
    deserializeJson(cfg, FPSTR(DEFAULT_CONFIG));
    pages = cfg["pages"].as<JsonArray>();
    startConfigMode();
  }

  applySettings();
  Serial.printf("CYD Badge %s: %u paginas\n", FW_VERSION, pages.size());
  lastPageChange = millis();
  if (firstRun) startConfigMode();
  // Watchdog: se o loop travar por mais de ~5 s, a placa reinicia sozinha
  enableLoopWDT();
}

void loop() {
  if (configMode) {
    dns.processNextRequest();
    server.handleClient();
    if (exitRequested && millis() - exitRequestedAt > 600) stopConfigMode();
    else if (millis() - lastWebActivity > CONFIG_IDLE_TIMEOUT_MS) stopConfigMode();
  }

  handleTouch();
  handleBootButton();
  handlePendingClick();

  if (settings.slideshowMs && !slideshowPaused && millis() - lastPageChange > settings.slideshowMs) goPage(+1);

  if (needRedraw) {
    needRedraw = false;
    renderPage(pageIndex);
    showingConfigScreen = false;
  }
  delay(configMode ? 2 : 10);
}
