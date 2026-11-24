#pragma once
#include "audio.hpp"
#include "core.hpp"
#include <vector>
namespace amalthea {
struct Canvas {
  int w, h;
  std::vector<uint32_t> pixels;
  Canvas(int width, int height) : w(width), h(height), pixels(width * height) {}
  void blit(const Canvas &,int x,int y);
  void rect(int x, int y, int width, int height, uint32_t color);
  void textFit(int x,int y,int width,const std::string &,int size,uint32_t color);
  void text(int x, int y, const std::string &, int size, uint32_t color);
};
void drawAmaltheaMark(Canvas &,int x,int y,int size,uint32_t accent,int variant=0);
// The mark as an SVG master (128-unit view box): 0 logo, 1 tracker, 2 FX.
std::string amaltheaMarkSVG(int variant,uint32_t accent,const std::string &title);
// Battery: an outlined cell and its percentage; nothing when percent < 0.
void drawBattery(Canvas &,int x,int y,int height,int percent,bool charging,uint32_t color);
void drawFxUI(Canvas &,const Editor &,const AudioStats &);
void drawLifebloodUI(Canvas &,const Editor &,const AudioStats &);
// LIFEBLOOD's mark: nodes joined by vessels, in a size x size square.
void drawLifebloodMark(Canvas &,int x,int y,int size,uint32_t accent);
void drawSystemUI(Canvas &,const Editor &);
uint32_t trackColor(int track);
void drawUI(Canvas &, const Editor &, const AudioStats &, bool help = false);
void writePPM(const Canvas &, const std::string &);
} // namespace amalthea
