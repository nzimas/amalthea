#include "ui.hpp"
#include <cstdio>
#include <cstdlib>
using namespace amalthea;
int main() {
  Editor editor;
  editor.project = demoProject();
  AudioStats stats;
  for (int width : {640, 1024}) {
    Canvas canvas(width, width * 3 / 4);
    for (int rows : {8, 16})
      for (int page = 0; page < 10; page++) {
        editor.visibleRows = rows;
        editor.page = page==5?Page::Session:page==6?Page::Master:Page::Pattern;
        editor.effectBanner=page==7;
        editor.notice=page==9?"SAVING SESSION":"";
        editor.volumeBanner=page==8;
        editor.column=page==7?2:0;
        editor.trackBanner = page == 4;
        editor.mixer = page == 1;
        editor.editing = page == 2 || page==7;
        drawUI(canvas, editor, stats, page == 3);
        if(width==1024 && rows==16 && page>=5) writePPM(canvas,"page-"+std::to_string(page)+".ppm");
        for (auto pixel : canvas.pixels)
          if ((pixel >> 24) != 255) {
            fprintf(stderr, "Transparent pixel in %dpx / %d rows / page %d\n",
                    width, rows, page);
            return 1;
          }
      }
  }
  Canvas clearCursor(1024,768);
  editor=Editor{};editor.track=4;drawUI(clearCursor,editor,stats);
  if(clearCursor.pixels[168*1024+95]!=(0xff000000u|trackColor(4)))return 3;
  for(int column=0;column<Columns;column++) {
    editor.column=column;editor.action(Action::Enter);editor.action(Action::Up);
    drawUI(clearCursor,editor,stats);writePPM(clearCursor,"magnify-"+std::to_string(column)+".ppm");
    editor.action(Action::Cancel);
  }
  for (int a = 0; a < 12; a++)
    for (int b = a + 1; b < 12; b++)
      if (trackColor(a) == trackColor(b))
        return 2;
  puts("PASS: all layouts/pages are fully opaque for the Brick pixel-alpha "
       "display layer");
}
