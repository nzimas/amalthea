#include "audio.hpp"
#include "core.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <numeric>
#include <thread>
using namespace amalthea;
static int checks = 0;
static void check(bool ok, const char *msg) {
  checks++;
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
  }
}
int main() {
  for(auto page:{Page::Pattern,Page::Session,Page::Master,Page::Track,Page::Randomizer,Page::Sessions,Page::Column,Page::Macro,Page::Patterns,Page::PatternSettings,Page::Mixer}){
    Editor home;home.project=blankProject();home.track=7;home.row=3;home.column=2;
    home.page=page;home.playing=true;home.editing=home.mixer=home.powerPrompt=true;
    home.trackBanner=home.effectBanner=home.volumeBanner=true;home.notice="OLD OVERLAY";
    home.pending.note=76;auto before=encodeProject(home.project);home.action(Action::Home);
    check(home.page==Page::Pattern&&!home.editing&&!home.mixer&&!home.powerPrompt,"Home returns directly from every page and modal");
    check(home.track==7&&home.row==3&&home.column==2&&home.playing,"Home preserves current track, cursor and playback");
    check(encodeProject(home.project)==before&&!home.trackBanner&&!home.effectBanner&&!home.volumeBanner&&home.notice.empty(),"Home neither commits pending edits nor leaves overlays");
  }

  Editor a, b;
  a.project = b.project = demoProject();
  a.track = b.track = 3;
  auto original = a.project;
  a.action(Action::RandomizeColumn);
  b.action(Action::RandomizeColumn);
  for (int r = 0; r < a.project.length; r++) {
    check(a.project.tracks[3].steps[r].note ==
              b.project.tracks[3].steps[r].note,
          "seeded pitches repeat");
    check(a.project.tracks[2].steps[r].note == original.tracks[2].steps[r].note,
          "other tracks preserved");
  }
  a.action(Action::Undo);
  check(a.project.seed == original.seed && a.project.tracks[3].steps[0].note ==
                                               original.tracks[3].steps[0].note,
        "randomizer undo restores seed and notes");
  a.action(Action::Redo);
  check(a.project.seed == b.project.seed, "redo restores randomizer");
  a.column=2;
  a.action(Action::RandomizeColumn);
  int empty = 0, filled = 0;
  for (int r = 0; r < a.project.length; r++)
    for (auto f : a.project.tracks[3].steps[r].fx) {
      empty += f.type == FxType::None;
      filled += f.type != FxType::None;
      check(f.value <= 100, "effect bounds");
    }
  check(empty && filled, "sparse effect slots");
  Editor e;
  e.project = original;
  auto n = e.project.tracks[0].steps[0].note;
  e.action(Action::Enter);
  e.action(Action::Up);
  check(e.project.tracks[0].steps[0].note == n, "pending edits isolated");
  e.action(Action::Cancel);
  check(e.project.tracks[0].steps[0].note == n, "cancel preserves original");
  e.action(Action::Enter);
  e.action(Action::Up);
  e.action(Action::Enter);
  check(e.project.tracks[0].steps[0].note == n + 1, "commit applies edit");
  e.action(Action::Undo);
  check(e.project.tracks[0].steps[0].note == n, "undo committed edit");
  e.column = 2;
  e.action(Action::Enter);
  e.action(Action::Up);
  e.action(Action::Right);
  e.action(Action::Up);
  e.action(Action::Enter);
  check(e.project.tracks[0].steps[0].fx[0].type == FxType::Wavefold &&
            e.project.tracks[0].steps[0].fx[0].value == 2,
        "edit command and value separately");
  std::string err;
  std::string path = "test-roundtrip.amalthea";
  check(saveProject(a.project, path, err), "atomic save");
  Project p;
  check(loadProject(p, path, err), "load");
  check(p.seed == a.project.seed && p.tracks[3].steps[0].fx[0].type ==
                                        a.project.tracks[3].steps[0].fx[0].type,
        "roundtrip state");
  {
    std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
    f.seekp(33);
    f.put(127);
  }
  auto seed = p.seed;
  check(!loadProject(p, path, err) && p.seed == seed,
        "corruption rejected without changing project");
  remove(path.c_str());
  std::vector<float> x(44100 * 2), y(x.size());
  for (int engine = 0; engine < 8; engine++) {
    Project q;
    q.length = 16;
    q.tracks[0].steps[0].note = engine == 6 ? 36 : 60;
    q.tracks[0].steps[0].instrument = engine;
    Audio audio;
    check(audio.submit(q, true), "audio queue");
    audio.render(x.data(), 44100);
    double power = 0;
    for (auto v : x) {
      check(std::isfinite(v) && std::abs(v) <= .951f, "finite limited audio");
      power += v * v;
    }
    check(power > 1e-7, "every engine produces sound");
    printf("engine %s RMS %.6f\n", instrumentName(q.patches[engine].engine),
           sqrt(power / x.size()));
  }
  Project q = demoProject();
  Audio c, d;
  c.submit(q, true);
  d.submit(q, true);
  c.render(x.data(), 44100);
  for (int pos = 0; pos < 44100;) {
    int frames = std::min(127, 44100 - pos);
    d.render(y.data() + pos * 2, frames);
    pos += frames;
  }
  check(x == y, "callback sizes do not alter output");
  Audio queue;
  int accepted = 0;
  while (queue.submit(q, false))
    accepted++;
  check(accepted == 8, "bounded queue rejects overflow");
  queue.render(x.data(), 32);
  check(queue.submit(q, true), "queue recovers after drain");
  printf("PASS: %d checks\n", checks);
}
