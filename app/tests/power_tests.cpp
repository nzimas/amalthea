#include "ui.hpp"
#include <cstdio>
#include <cstdlib>
using namespace amalthea;
static void require(bool ok,const char *why){if(!ok){fprintf(stderr,"FAIL: %s\n",why);exit(1);}}
int main(){
  PowerButton button;Editor e;e.project=demoProject();e.playing=true;
  require(button.release(0)==Action::Reserved,"orphan release ignored");
  button.press(0);require(button.tick(799)==Action::Reserved,"hold threshold");
  auto open=button.tick(800);require(open==Action::PowerPrompt,"long hold opens prompt");e.action(open);
  require(e.powerPrompt&&!e.powerYes&&!e.poweringOff&&e.playing,"prompt starts on No and preserves playback");
  require(button.tick(900)==Action::Reserved&&button.release(1000)==Action::Reserved,"opening release cannot confirm");
  auto before=e.project;e.action(Action::RandomizeColumn);e.action(Action::TrackSettings);e.action(Action::Save);
  require(e.project.seed==before.seed&&e.page==Page::Pattern,"dialog blocks tracker edits");
  e.action(Action::Left);require(e.powerYes,"left selects Yes");e.action(Action::Enter);
  require(!e.poweringOff&&e.powerPrompt,"Enter cannot confirm shutdown");
  button.press(1100);button.press(1200);e.action(button.tick(1900));
  require(e.powerYes&&!e.poweringOff,"another long press cannot confirm Yes");
  require(button.release(2000)==Action::Reserved,"second long release ignored");
  e.action(Action::Down);require(!e.powerYes,"down selects No");
  button.press(2100);e.action(button.release(2200));
  require(!e.powerPrompt&&!e.poweringOff&&e.playing,"short Power confirms No without stopping playback");
  button.press(2300);e.action(button.release(2400));require(!e.powerPrompt&&e.playing,"short Power outside prompt does nothing");
  // Even a delayed event loop must treat a held release as an opener, not confirmation.
  button.press(2500);e.action(button.release(3500));require(e.powerPrompt&&!e.powerYes,"late hold release opens No");
  e.action(Action::Up);button.press(3600);e.action(button.release(3700));
  require(e.poweringOff&&!e.playing,"short Power confirms Yes and stops playback");
  e.action(Action::Play);require(!e.playing,"shutdown blocks new playback");
  for(auto page:{Page::Pattern,Page::Session,Page::Master,Page::Track}){
    Editor modal;modal.page=page;modal.editing=true;modal.pending.note=77;modal.dirty=true;
    modal.action(Action::PowerPrompt);modal.action(Action::Right);modal.action(Action::PowerConfirm);
    require(modal.page==page&&modal.editing&&modal.pending.note==77&&modal.dirty,"No preserves page, pending edits and dirty status");
  }
  for(int width:{640,1024}){
    Canvas canvas(width,width*3/4);Editor modal;AudioStats stats;
    for(bool yes:{false,true}){
      modal.action(Action::PowerPrompt);modal.powerYes=yes;modal.dirty=true;drawUI(canvas,modal,stats);
      for(auto pixel:canvas.pixels)require((pixel>>24)==255,"power overlay is fully opaque");
      if(width==1024)writePPM(canvas,yes?"power-yes.ppm":"power-no.ppm");
    }
    modal.action(Action::PowerConfirm);drawUI(canvas,modal,stats);
    if(width==1024)writePPM(canvas,"power-off.ppm");
  }
  puts("PASS: hold/release separation, explicit short-Power confirmation, modal routing and layouts");
}
