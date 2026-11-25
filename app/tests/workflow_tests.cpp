#include "audio.hpp"
#include "core.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <vector>
using namespace amalthea;
void require(bool ok, const char *text) {
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", text);
    exit(1);
  }
}
int main() {
  Editor e;
  e.project = demoProject();
  require(e.project.length == 16, "16-step default");
  e.row = 15;
  e.action(Action::Down);
  require(e.row == 0, "16-step cursor wrap");
  e.action(Action::ShiftDown);
  require(e.shift && e.track == 0, "left shoulder only shifts");
  e.action(Action::ShiftUp);
  require(!e.shift, "shift release");
  e.action(Action::Shift2Down);
  require(e.shift2 && e.track == 0, "second shift");
  e.action(Action::Shift2Up);
  e.action(Action::NextTrack);
  require(e.track == 1 && e.trackBanner, "track navigation banner");
  for (int column = 0; column < Columns; column++) {
    e.column = column;
    auto original = e.project;
    e.action(Action::RandomizeColumn);
    for (int tr = 0; tr < Tracks; tr++)
      for (int row = 0; row < MaxRows; row++) {
        const auto &a = original.tracks[tr].steps[row];
        auto b = e.project.tracks[tr].steps[row];
        bool changed = tr == e.track && row < 16;
        if (changed) {
          if (column == 0) {
            require(b.instrument==original.tracks[e.track].instrument,"generated rows use the track default instrument");
            b.note = a.note;
            b.instrument = a.instrument;
          }
          if (column == 1)
            b.instrument = a.instrument;
          if (column == 2 || column == 3)
            b.fx[column - 2] = a.fx[column - 2];
          if (column == 4)
            b.condition = a.condition;
          if (column == 4)
            b.modifier = a.modifier;
        }
        require(b.note == a.note && b.instrument == a.instrument &&
                    b.condition.every == a.condition.every &&
                    b.condition.occurrence == a.condition.occurrence &&
                    b.modifier.semitones == a.modifier.semitones &&
                    b.modifier.type == a.modifier.type,
                "column scope");
        for (int slot = 0; slot < 2; slot++)
          require(b.fx[slot].type == a.fx[slot].type &&
                      b.fx[slot].value == a.fx[slot].value,
                  "other FX slot preserved");
      }
    e.action(Action::Undo);
  }
  e.column = 0;
  e.action(Action::RandomizeColumn);
  {
    // Successive NOTE draws differ in kind, never copy a template, stay in scale.
    Editor g;g.project=blankProject();g.project.root=2;g.project.scale=4;g.track=0;g.column=0;g.density[0][0]=60;
    std::set<std::vector<int>> patterns;std::set<std::string> idioms;int ties=0;
    for(int draw=0;draw<64;draw++){
      g.action(Action::RandomizeColumn);std::vector<int> line;
      for(int r=0;r<16;r++){int n=g.project.tracks[0].steps[r].note;line.push_back(n);ties+=n==-3;
        if(n>=0)require(scaleNote(n,g.project)==n,"generated note in root/scale");}
      patterns.insert(line);idioms.insert(g.status.substr(0,g.status.find(" GENERATED")));
    }
    require(patterns.size()>=60,"note draws do not repeat");
    require(idioms.size()>=6,"note draws span several idioms");
    require(ties>0,"sustained idioms tie into empty rows");
    Editor kit;kit.project=blankProject();kit.project.patches[0].engine=1;kit.density[0][0]=75;
    kit.action(Action::RandomizeColumn);int pieces=0;
    for(int r=0;r<16;r++){int n=kit.project.tracks[0].steps[r].note;pieces+=n==60||n==62||n==64||n==66||n==68||n==70;}
    require(kit.status.rfind("KIT",0)==0&&pieces>0,"PERC track draws a kit pattern on the kit note map");
    for(int draw=0;draw<12;draw++){
      Editor dr;dr.project=blankProject();dr.project.patches[0].engine=FmDrumEngine;dr.project.seed=1000+draw;dr.density[0][0]=50;
      dr.action(Action::RandomizeColumn);int hits=0;
      for(int r=0;r<16;r++){int n=dr.project.tracks[0].steps[r].note;if(n>=0){hits++;require(n>=36&&n<=84,"FM DRUM hits stay near the recipe pitch");}}
      require(dr.status.rfind("HITS",0)==0&&hits==8,"FM DRUM track draws a one-voice rhythm at the set density");
    }
    {Editor quiet;quiet.project=blankProject();quiet.column=2;auto serial=quiet.auditionSerial;quiet.action(Action::Enter);quiet.action(Action::Right);
     require(quiet.editing&&quiet.auditionSerial==serial,"editing an FX row never sounds a note");
     Editor empty;empty.project=blankProject();empty.column=1;serial=empty.auditionSerial;empty.action(Action::Enter);empty.action(Action::Up);
     require(empty.auditionSerial==serial,"editing the instrument of an empty row stays silent");}
    {Editor fx;fx.project=blankProject();fx.column=2;fx.row=3;auto before=encodeProject(fx.project);fx.action(Action::Enter);fx.action(Action::RandomizeColumn);
     require(fx.editing&&fx.pending.fx[0].type!=FxType::None&&fx.pending.fx[0].value>0&&encodeProject(fx.project)==before,"hold X while editing an FX row draws a pending random effect");
     auto drawn=fx.pending.fx[0];fx.action(Action::Enter);
     for(int r=0;r<16;r++)require(r==3?(fx.project.tracks[0].steps[r].fx[0].type==drawn.type&&fx.project.tracks[0].steps[r].fx[0].value==drawn.value):fx.project.tracks[0].steps[r].fx[0].type==FxType::None,"random effect lands on that row only");
     Editor c;c.project=blankProject();c.column=3;before=encodeProject(c.project);c.action(Action::Enter);c.action(Action::RandomizeColumn);c.action(Action::Cancel);
     require(encodeProject(c.project)==before,"cancel discards the random effect and its settings");}
    {Editor pop;pop.project=blankProject();pop.action(Action::TrackSettings);for(int i=0;i<4;i++)pop.action(Action::Down);pop.action(Action::Left);
     require(pop.notice=="VOLUME"&&pop.noticeDetail=="89%"&&pop.noticeDetailSerial==pop.noticeSerial,"dialling a track setting raises its value popup");
     auto serial=pop.noticeSerial;pop.action(Action::Down);require(pop.noticeSerial==serial,"moving the focus raises no popup");}
    {Project sc=blankProject();sc.master.sidechain={1,7};Project back;std::string err;
     require(decodeProject(back,encodeProject(sc),err)&&back.master.sidechain[0]==1&&back.master.sidechain[1]==7,"compressor sidechain survives save/load");}
    {Editor off;off.project=blankProject();off.project.tracks[0].steps[0].note=60;off.column=4;off.row=0;off.action(Action::Enter);
     auto serial=off.auditionSerial;off.action(Action::Up);off.action(Action::Up);off.action(Action::Up);
     require(off.auditionSerial==serial+3&&off.previewStep().note==63,"dialling OFFSET auditions the note it produces");
     Editor ctx;ctx.project=blankProject();ctx.project.tracks[0].steps[1].note=50;ctx.column=4;ctx.row=3;ctx.action(Action::Enter);ctx.action(Action::Down);
     require(ctx.previewStep().note==49,"OFFSET on a row without a note acts on the nearest earlier note");}
    require(std::string(instrumentName(FmDrumEngine))=="FM DRUM"&&!nativeEngine(FmDrumEngine),"FM DRUM is a selectable, non-native engine");
    Editor fresh;fresh.entropy=12345;fresh.project=blankProject();fresh.randomizeBlankPatches();
    for(int t=0;t<Tracks;t++)require(trackPatch(fresh.project,t,fresh.project.tracks[t].instrument).seed!=0,"blank session instruments drawn");
    Editor a,b;a.entropy=1;b.entropy=2;a.project=b.project=blankProject();a.action(Action::RandomizeColumn);b.action(Action::RandomizeColumn);
    bool differs=false;for(int r=0;r<16;r++)differs|=a.project.tracks[0].steps[r].note!=b.project.tracks[0].steps[r].note;
    require(differs,"boot entropy changes the draw sequence");
  }
  Step s;
  s.note = 60;
  s.condition = {1, 2};
  s.modifier={2,ModifierType::Offset};
  require(triggeredNote(s, 0) == 62 && triggeredNote(s, 1) == -1 &&
              triggeredNote(s, 2) == 62,
          "conditional transposition and gating");
  s.modifier.type = ModifierType::Transpose;   // (a TRANSPOSE cell is no offset of its own row)
  require(triggeredNote(s,1)==-1,"modifier mode never bypasses note condition");
  for (int every = 1; every <= 16; every++)
    for (int occurrence = 1; occurrence <= every; occurrence++)
      for (int cycle = 0; cycle < every * 3; cycle++)
        require(conditionMatches({uint8_t(occurrence), uint8_t(every)},
                                 cycle) == (cycle % every == occurrence - 1),
                "all cycle phases");
  e.column = 0;
  e.action(Action::Enter);
  e.action(Action::NextTrack);   // LENGTH
  e.action(Action::NextTrack);   // CONDITION
  e.action(Action::Up);
  e.action(Action::Up);
  e.action(Action::Enter);
  require(e.project.tracks[e.track].steps[e.row].condition.every == 2 &&
              e.project.tracks[e.track].steps[e.row].condition.occurrence == 2,
          "edit cycle period and occurrence");
  Project p;
  auto &step = p.tracks[0].steps[0];
  step.note = 60;
  step.condition = {1, 2};
  step.modifier={3,ModifierType::Offset};
  Audio audio;
  audio.submit(p, true);
  std::vector<float> data(352800 * 2);
  audio.render(data.data(), 352800);
  require(audio.stats.triggers[0].load() == 2 &&
              audio.stats.lastNote[0].load() == 63,
          "actual transport condition every second 16-step cycle");
  std::string error;
  Project legacy;
  auto legacyPath =
      std::filesystem::path(__FILE__).parent_path().parent_path() /
      "backups/pre-0.2/session.amalthea";
  if (std::filesystem::exists(legacyPath)) {
    require(loadProject(legacy, legacyPath.string(), error),
            "version 1 project migration");
    require(legacy.length == 16, "migration limits active length");
  }
  auto path = "workflow-v2.amalthea";
  require(saveProject(e.project, path, error), "v2 save");
  Project loaded;
  require(loadProject(loaded, path, error), "v2 load");
  require(loaded.tracks[e.track].steps[e.row].condition.every == 2,
          "v2 preserves condition");
  remove(path);
  puts("PASS: controls, column isolation, Fugitive motif, cycle/modify "
       "playback and migration");
}
