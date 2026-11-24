#include "ui.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>
namespace amalthea {
namespace {
using Pt = std::array<float, 2>;
using Shape = std::vector<Pt>;
constexpr float Pi = 3.14159265f;
std::vector<std::pair<Shape, uint32_t>> *recording = nullptr;   // SVG export

// Anti-aliased even-odd fill of a shape given on a 128-unit grid, drawn into the
// size x size square at (x, y). Four sub-rows per pixel, exact horizontal coverage.
void fill(Canvas &c, int x, int y, int size, const Shape &shape, uint32_t color) {
  if (recording) { recording->push_back({shape, color}); return; }
  if (shape.size() < 3 || size <= 0) return;
  const float k = size / 128.f;
  float top = 1e9f, bottom = -1e9f;
  for (auto p : shape) { top = std::min(top, p[1]); bottom = std::max(bottom, p[1]); }
  int first = std::max(0, int(std::floor(top * k))), last = std::min(size - 1, int(std::ceil(bottom * k)));
  std::vector<float> cover(size + 2), cross;
  for (int row = first; row <= last; row++) {
    int yy = y + row;
    if (yy < 0 || yy >= c.h) continue;
    std::fill(cover.begin(), cover.end(), 0.f);
    bool any = false;
    for (int sub = 0; sub < 4; sub++) {
      float sy = (row + (sub + .5f) / 4) / k;
      cross.clear();
      Pt prev = shape.back();
      for (auto p : shape) {
        if ((p[1] > sy) != (prev[1] > sy)) cross.push_back((p[0] + (sy - p[1]) * (prev[0] - p[0]) / (prev[1] - p[1])) * k);
        prev = p;
      }
      std::sort(cross.begin(), cross.end());
      for (size_t i = 0; i + 1 < cross.size(); i += 2) {
        float a = std::clamp(cross[i], 0.f, float(size)), b = std::clamp(cross[i + 1], 0.f, float(size));
        for (int px = int(a); px < size && px < b; px++) {
          cover[px] += (std::min(b, px + 1.f) - std::max(a, float(px))) * .25f;
          any = true;
        }
      }
    }
    if (!any) continue;
    for (int px = 0; px < size; px++) {
      float alpha = std::min(1.f, cover[px]);
      int xx = x + px;
      if (alpha <= 0 || xx < 0 || xx >= c.w) continue;
      auto &out = c.pixels[yy * c.w + xx];
      uint32_t blended = 0xff000000u;
      for (int sh = 0; sh < 24; sh += 8)
        blended |= uint32_t(((color >> sh) & 255) * alpha + ((out >> sh) & 255) * (1 - alpha) + .5f) << sh;
      out = blended;
    }
  }
}
Shape circle(float cx, float cy, float r, int n = 28) {
  Shape s;
  for (int i = 0; i < n; i++) s.push_back({cx + r * std::cos(2 * Pi * i / n), cy + r * std::sin(2 * Pi * i / n)});
  return s;
}
Shape roundRect(float x, float y, float w, float h, float r) {
  Shape s;
  const float cx[4] = {x + w - r, x + r, x + r, x + w - r}, cy[4] = {y + r, y + r, y + h - r, y + h - r};
  for (int corner = 0; corner < 4; corner++)
    for (int i = 0; i <= 6; i++) {
      float a = -Pi / 2 * (corner + i / 6.f);
      s.push_back({cx[corner] + r * std::cos(a), cy[corner] + r * std::sin(a)});
    }
  return s;
}
// A band along an arc (angles in degrees, y down).
Shape arcBand(float cx, float cy, float r, float thick, float from, float to) {
  Shape s;
  const int n = 18;
  for (int i = 0; i <= n; i++) { float a = (from + (to - from) * i / n) * Pi / 180; s.push_back({cx + (r + thick / 2) * std::cos(a), cy + (r + thick / 2) * std::sin(a)}); }
  for (int i = n; i >= 0; i--) { float a = (from + (to - from) * i / n) * Pi / 180; s.push_back({cx + (r - thick / 2) * std::cos(a), cy + (r - thick / 2) * std::sin(a)}); }
  return s;
}

// AMALTHEA: the goat's horn that became the cornucopia. A ribbed, curling horn
// (its growth rings read as tracker rows; its curve as a crescent moon) pours
// widening waves from its open mouth: an instrument of plenty.
void logo(Canvas &c, int x, int y, int size, uint32_t accent) {
  const uint32_t ivory = 0xf2ede2;
  std::vector<std::pair<Shape, uint32_t>> parts;
  // Centre line: an opening spiral. The tip curls inside, the horn sweeps
  // round the left and the bottom, and the mouth rises on the right.
  const float sweep = 285, start = 660;
  auto at = [&](float t) {
    float a = (start - sweep * t) * Pi / 180, r = 12 + 34 * t;
    return Pt{r * std::cos(a), r * std::sin(a)};
  };
  auto tangent = [&](float t) {
    Pt a = at(std::max(0.f, t - .003f)), b = at(std::min(1.f, t + .003f));
    float dx = b[0] - a[0], dy = b[1] - a[1], len = std::hypot(dx, dy);
    return Pt{dx / len, dy / len};
  };
  auto half = [](float t) { return .8f + 15.5f * std::pow(t, 1.5f); };
  auto edge = [&](float t, float side) { auto p = at(t); auto d = tangent(t); float h = half(t) * side; return Pt{p[0] - d[1] * h, p[1] + d[0] * h}; };
  // Growth rings, lengthening toward the mouth, parted by thin gaps.
  const int rings = 8;
  for (int i = 0; i < rings; i++) {
    float t0 = std::pow(float(i) / rings, .75f), t1 = std::pow(float(i + 1) / rings, .75f);
    if (i) t0 += .011f;
    if (i + 1 < rings) t1 -= .011f;
    Shape s;
    const int n = 12;
    for (int j = 0; j <= n; j++) s.push_back(edge(t0 + (t1 - t0) * j / n, 1));
    for (int j = n; j >= 0; j--) s.push_back(edge(t0 + (t1 - t0) * j / n, -1));
    parts.push_back({s, i == rings - 1 ? accent : ivory});
  }
  // Plenty pours from the mouth: a fan of rays of uneven length, a burst that
  // is also a spectrum.
  Pt d = tangent(1), lip0 = edge(1, -1), lip1 = edge(1, 1);
  float heading = std::atan2(d[1], d[0]);
  static const float lengths[7] = {11, 19, 14, 26, 17, 22, 12};
  for (int i = 0; i < 7; i++) {
    float f = (i + .5f) / 7, a = heading + (f - .5f) * 1.25f, w = 1.9f;
    Pt u{std::cos(a), std::sin(a)}, n{-u[1], u[0]};
    Pt base{lip0[0] + (lip1[0] - lip0[0]) * f + u[0] * 6, lip0[1] + (lip1[1] - lip0[1]) * f + u[1] * 6};
    float len = lengths[i];
    Shape ray;
    for (int k = 0; k <= 8; k++) { float q = Pi / 2 - Pi * k / 8; Pt tip{base[0] + u[0] * len, base[1] + u[1] * len}; ray.push_back({tip[0] + (u[0] * std::cos(q) + n[0] * std::sin(q)) * w, tip[1] + (u[1] * std::cos(q) + n[1] * std::sin(q)) * w}); }
    for (int k = 0; k <= 8; k++) { float q = -Pi / 2 - Pi * k / 8; ray.push_back({base[0] + (u[0] * std::cos(q) + n[0] * std::sin(q)) * w, base[1] + (u[1] * std::cos(q) + n[1] * std::sin(q)) * w}); }
    parts.push_back({ray, accent});
  }
  // Fit the whole mark, centred, into the 128-unit square.
  float l = 1e9f, r = -1e9f, t = 1e9f, b = -1e9f;
  for (auto &p : parts) for (auto q : p.first) { l = std::min(l, q[0]); r = std::max(r, q[0]); t = std::min(t, q[1]); b = std::max(b, q[1]); }
  float k = 120 / std::max(r - l, b - t), ox = 64 - (l + r) / 2 * k, oy = 64 - (t + b) / 2 * k;
  for (auto &p : parts) { for (auto &q : p.first) q = {ox + q[0] * k, oy + q[1] * k}; fill(c, x, y, size, p.first, p.second); }
}

// TRACKER: the classic pattern screen - rows of hex columns with a cursor row.
void trackerIcon(Canvas &c, int x, int y, int size, uint32_t accent) {
  const uint32_t ivory = 0xf2ede2, dim = 0x55636b;
  fill(c, x, y, size, roundRect(6, 10, 116, 108, 14), ivory);
  fill(c, x, y, size, roundRect(13, 17, 102, 94, 9), 0x0a0f13);
  static const int8_t lens[7][3] = {{3, 2, 3}, {0, 2, 1}, {3, 0, 3}, {2, 2, 0}, {3, 1, 3}, {0, 2, 2}, {3, 0, 1}};
  for (int r = 0; r < 7; r++) {
    float yy = 25 + r * 12;
    bool cursor = r == 3;
    if (cursor) fill(c, x, y, size, roundRect(17, yy - 3.5f, 94, 12, 3), accent);
    for (int col = 0; col < 3; col++) {
      float xx = 23 + col * 31;
      int n = lens[r][col];
      uint32_t ink = cursor ? 0x0a0f13 : col == 0 ? ivory : dim;
      if (!n) { fill(c, x, y, size, roundRect(xx, yy + 1.5f, 22, 2, 1), cursor ? ink : 0x2a353b); continue; }
      for (int k = 0; k < n; k++) fill(c, x, y, size, roundRect(xx + k * 8, yy - .5f, 6, 6, 1.5f), ink);
    }
  }
}

// FX: a stylised pedalboard - three stomp boxes joined by a cable.
void fxIcon(Canvas &c, int x, int y, int size, uint32_t accent) {
  const uint32_t ivory = 0xf2ede2, ink = 0x0a0f13;
  fill(c, x, y, size, roundRect(2, 98, 124, 12, 5), 0x3a4650);   // the board
  for (int i = 0; i < 3; i++) {
    float left = 6 + i * 40;
    bool hot = i == 1;
    fill(c, x, y, size, roundRect(left, 26, 36, 70, 7), hot ? accent : ivory);
    fill(c, x, y, size, circle(left + 11, 40, 5.5f), ink);
    fill(c, x, y, size, circle(left + 25, 40, 5.5f), ink);
    fill(c, x, y, size, circle(left + 18, 52, 2.6f), hot ? ivory : accent);   // status lamp
    fill(c, x, y, size, circle(left + 18, 76, 8.5f), ink);                     // footswitch
    fill(c, x, y, size, circle(left + 18, 76, 4.8f), hot ? accent : ivory);
  }
  // patch cables across the tops
  for (int i = 0; i < 2; i++) fill(c, x, y, size, arcBand(44 + i * 40, 26, 7, 3.2f, 180, 360), ivory);
}
} // namespace

void drawAmaltheaMark(Canvas &c, int x, int y, int size, uint32_t accent, int variant) {
  if (variant == 1) trackerIcon(c, x, y, size, accent);
  else if (variant == 2) fxIcon(c, x, y, size, accent);
  else logo(c, x, y, size, accent);
}

std::string amaltheaMarkSVG(int variant, uint32_t accent, const std::string &title) {
  std::vector<std::pair<Shape, uint32_t>> shapes;
  recording = &shapes;
  Canvas none(1, 1);
  drawAmaltheaMark(none, 0, 0, 128, accent, variant);
  recording = nullptr;
  std::string svg = "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 128 128\"><title>" + title + "</title>";
  char buffer[64];
  for (auto &[shape, color] : shapes) {
    snprintf(buffer, sizeof buffer, "<polygon fill=\"#%06x\" points=\"", color & 0xffffff);
    svg += buffer;
    for (auto p : shape) { snprintf(buffer, sizeof buffer, "%.2f,%.2f ", p[0], p[1]); svg += buffer; }
    svg.back() = '"';
    svg += "/>";
  }
  return svg + "</svg>\n";
}

void drawBattery(Canvas &c, int x, int y, int height, int percent, bool charging, uint32_t color) {
  if (percent < 0) return;
  percent = std::clamp(percent, 0, 100);
  const int w = height * 2, line = std::max(1, height / 9);
  const uint32_t level = percent <= 15 && !charging ? 0xff5040 : color;
  c.rect(x, y, w, line, color);
  c.rect(x, y + height - line, w, line, color);
  c.rect(x, y, line, height, color);
  c.rect(x + w - line, y, line, height, color);
  c.rect(x + w, y + height / 3, std::max(2, line * 2), height - 2 * (height / 3), color);   // terminal
  int inner = w - 4 * line;
  c.rect(x + 2 * line, y + 2 * line, std::max(percent ? 1 : 0, inner * percent / 100), height - 4 * line, level);
  if (charging) {   // a bolt across the cell, in the background colour, outlined by the fill
    int cx = x + w / 2, top = y + 2 * line, h = height - 4 * line;
    for (int r = 0; r < h; r++) {
      int off = r < h / 2 ? (h / 2 - r) / 2 : -(r - h / 2) / 2;
      c.rect(cx + off - line, top + r, 2 * line + 1, 1, 0x000000);
    }
  }
  std::string text = std::to_string(percent) + "%";
  c.text(x + w + std::max(3, line * 3) + 2, y + height / 2 - height * 44 / 32 * 9 / 20, text, height * 9 / 10, color);
}

void drawSystemUI(Canvas &c, const Editor &e) {
  const uint32_t ivory = 0xf5f1e8, dim = 0x91a2ac, cyan = 0x6ee7dc, violet = 0xb39aff, ember = 0xe8603c;
  c.rect(0, 0, c.w, c.h, 0);
  float scale = c.w / 1024.f;
  auto X = [&](int v) { return int(v * scale); };
  auto box = [&](int x, int y, int w, int h, uint32_t color) { c.rect(X(x), X(y), X(w), X(h), color); };
  auto text = [&](int x, int y, const std::string &s, int size, uint32_t color) { c.text(X(x), X(y), s, X(size), color); };
  if (e.splash) {
    drawAmaltheaMark(c, X(372), X(95), X(280), ember);
    text(224, 415, "AMALTHEA", 96, ivory);
    text(294, 569, "MUSICAL INSTRUMENTS", 32, dim);
    box(440, 674, 144, 3, ember);
    return;
  }
  drawAmaltheaMark(c, X(40), X(28), X(96), ember);
  text(162, 46, "AMALTHEA", 64, ivory);
  drawBattery(c, X(848), X(60), X(26), e.battery, e.charging, dim);
  box(40, 157, 944, 2, 0x25323a);
  // Three appliances: TRACKER, FX, LIFEBLOOD.
  static const char *names[] = {"TRACKER", "FX", "LIFEBLOOD"}, *lines[] = {"12-TRACK SYNTH", "USB EFFECT RACK", "EFFECT NETWORKS"};
  const uint32_t accents[] = {cyan, violet, ember};
  for (int tile = 0; tile < 3; tile++) {
    int left = 40 + tile * 324;
    auto accent = accents[tile];
    bool active = e.applianceItem == tile;
    box(left, 218, 296, 450, active ? accent : 0x25323a);
    box(left + 3, 221, 290, 444, active ? 0x101a20 : 0x080d11);
    text(left + 24, 243, tile == 0 ? "01" : tile == 1 ? "02" : "03", 30, active ? accent : dim);
    if (tile == 2) drawLifebloodMark(c, X(left + 58), X(290), X(180), accent);
    else drawAmaltheaMark(c, X(left + 58), X(290), X(180), accent, tile + 1);
    auto centred = [&](int top, const std::string &s, int size, uint32_t color) { text(left + (296 - int(s.size()) * size * 24 / 32) / 2, top, s, size, color); };
    centred(530, names[tile], 40, ivory);
    centred(604, lines[tile], 22, active ? accent : dim);
  }
}
} // namespace amalthea
