#include "audio.hpp"
#include "fx_audio.hpp"
#include "lifeblood.hpp"
#include "usb_audio_out.hpp"
#include "core.hpp"
#include "sessions.hpp"
#include "ui.hpp"
#include "performance.hpp"
#include <SDL.h>
#include <algorithm>
#include <chrono>
#include <random>
#include <cmath>
#include <csignal>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <thread>
#ifdef AMALTHEA_BRICK
#include <alsa/asoundlib.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <spawn.h>
extern char **environ;
#include <unistd.h>
#endif
using namespace amalthea;
static volatile sig_atomic_t running = 1;
static void stop(int) { running = 0; }
struct AudioRouter {Audio&tracker;FxAudio&fx;void render(float*out,int n){if(fx.active())fx.render(out,n);else tracker.render(out,n);}};
static void callback(void *p, Uint8 *stream, int len) {static_cast<AudioRouter*>(p)->render(reinterpret_cast<float*>(stream),len/8);}
struct Button {
  int x, y, w, h;
  const char *label;
  Action action;
};
static std::vector<Button> buttons(int w, int h) {
  std::vector<Button> result = {
      {w + 76, 40, 78, 42, "UP", Action::Up},
      {w + 8, 90, 78, 42, "LEFT", Action::Left},
      {w + 148, 90, 78, 42, "RIGHT", Action::Right},
      {w + 76, 140, 78, 42, "DOWN", Action::Down},
      {w + 76, 210, 78, 42, "X", Action::RandomizeColumn},
      {w + 8, 260, 78, 42, "Y", Action::Master},
      {w + 148, 260, 78, 42, "A", Action::ColumnSettings},
      {w + 76, 310, 78, 42, "B", Action::TrackSettings},
      {w + 8, 385, 100, 42, "SHIFT", Action::ShiftDown},
      {w + 126, 385, 100, 42, "FUNC", Action::Shift2Down},
      {w + 8, 440, 100, 42, "F1", Action::PrevTrack},
      {w + 126, 440, 100, 42, "F2", Action::NextTrack},
      {w + 8, 495, 100, 42, "DASH", Action::Dashboard},
      {w + 126, 495, 100, 42, "HOME", Action::Home},
      {w + 8, 550, 100, 42, "PLAY", Action::Play},
      {w + 126, 550, 100, 42, "ENTER", Action::Enter},
      {w + 8, 605, 218, 40, "PATTERNS", Action::Session}};
  for (auto &b : result) {
    b.y = b.y * h / 768;
    b.h = b.h * h / 768;
  }
  return result;
}
static std::optional<Action> key(SDL_Keycode k) {
  switch (k) {
  case SDLK_LSHIFT:
    return Action::ShiftDown;
  case SDLK_RSHIFT:
    return Action::Shift2Down;
  case SDLK_UP:
    return Action::Up;
  case SDLK_DOWN:
    return Action::Down;
  case SDLK_LEFT:
    return Action::Left;
  case SDLK_RIGHT:
    return Action::Right;
  case SDLK_RETURN:
    return Action::Enter;
  case SDLK_HOME:
    return Action::Home;
  case SDLK_ESCAPE:
    return Action::Cancel;
  case SDLK_a:
    return Action::ColumnSettings;
  case SDLK_x:
    return Action::RandomizeColumn;
  case SDLK_b:
    return Action::TrackSettings;
  case SDLK_y:
    return Action::Master;
  case SDLK_SPACE:
    return Action::Play;
  case SDLK_p:
    return Action::Session;
  case SDLK_q:
    return Action::PrevTrack;
  case SDLK_e:
    return Action::NextTrack;
  case SDLK_m:
    return Action::Mixer;
  case SDLK_TAB:
    return Action::Dashboard;
  case SDLK_BACKQUOTE:
    return Action::Back;
  case SDLK_z:
    return Action::Undo;
  case SDLK_r:
    return Action::Redo;
  case SDLK_s:
    return Action::Save;
  case SDLK_l:
    return Action::Load;
  case SDLK_BACKSPACE:
    return Action::Clear;
  case SDLK_c:
    return Action::Copy;
  case SDLK_v:
    return Action::Paste;
  case SDLK_EQUALS:
    return Action::TempoUp;
  case SDLK_MINUS:
    return Action::TempoDown;
  default:
    return {};
  }
}
#ifdef AMALTHEA_BRICK
struct Brick {
  // The F1 / F2 keys' LEDs (the TrimUI LED animation driver). TRACKER: both dimly lit in a
  // cool tone (they move between tracks); PERFORMANCE: F1 alone, bright yellow (it leaves the
  // mode); elsewhere (the other appliances, the appliance menu): off.
  enum class Keys { Off, Tracker, Performance };
  static void writeLed(const char*name,const char*value){FILE*f=fopen((std::string("/sys/class/led_anim/")+name).c_str(),"w");if(f){fputs(value,f);fclose(f);}}
  static void led(const char*key,const char*rgb){
    std::string k(key);
    if(!rgb){writeLed(("effect_"+k).c_str(),"0");return;}
    writeLed(("effect_rgb_hex_"+k).c_str(),rgb);writeLed(("effect_cycles_"+k).c_str(),"-1");writeLed(("effect_"+k).c_str(),"4");
  }
  Keys keys=Keys::Off;bool keysSet=false;
  void lightKeys(Keys k){
    if(keysSet&&k==keys)return;
    keys=k;keysSet=true;
    if(k==Keys::Performance){led("f1","FFFF00 ");led("f2",nullptr);}
    else if(k==Keys::Tracker){led("f1","1E4A6E ");led("f2","1E4A6E ");}
    else {led("f1",nullptr);led("f2",nullptr);}
  }
  int fb = -1, input = -1, disp = -1, previousBrightness = -1;
  uint8_t *memory = nullptr;
  fb_fix_screeninfo fix{};
  fb_var_screeninfo var{};
  snd_pcm_t *pcm = nullptr;
  snd_ctl_t *control = nullptr;
  int volumeInput=-1, powerInput=-1, previousHeadphone=-1;
  long mixerValue(const char *name,long value=-1) {
    if(!control) return -1;
    snd_ctl_elem_value_t *v; snd_ctl_elem_value_alloca(&v);
    snd_ctl_elem_value_set_interface(v,SND_CTL_ELEM_IFACE_MIXER);
    snd_ctl_elem_value_set_name(v,name);
    if(value>=0) { snd_ctl_elem_value_set_integer(v,0,value); if(snd_ctl_elem_write(control,v)<0)return -1; }
    if(snd_ctl_elem_read(control,v)<0)return -1;
    return snd_ctl_elem_value_get_integer(v,0);
  }
  int volumePercent() {
    long raw=mixerValue("digital volume");
    if(raw<0)return -1;
    if(raw>=63)return 0;
    return std::clamp(int(std::lround((38-raw)*20./38))*5,0,100);
  }
  std::atomic<bool> audioRun{true};
  std::atomic<uint64_t> maxRenderUs{0};
  // Shared USB audio output: every appliance renders through this thread.
  UsbAudioOut usbOut;
  std::thread thread;
  static constexpr const char *governorPath="/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor";
  std::string previousGovernor;
  bool governorChanged=false;
  ~Brick() {
    if(keysSet&&keys!=Keys::Off)lightKeys(Keys::Off);
    if(control) {
      if(previousHeadphone>=0)mixerValue("Headphone Volume",previousHeadphone);
      snd_ctl_close(control);
    }
    if(volumeInput>=0)close(volumeInput);
    if(powerInput>=0){ioctl(powerInput,EVIOCGRAB,0);close(powerInput);}
    if (disp >= 0) {
      if (previousBrightness >= 0) {
        unsigned long args[4] = {0, (unsigned long)previousBrightness, 0, 0};
        ioctl(disp, 0x102, args);
      }
      close(disp);
    }
    audioRun = false;
    if (thread.joinable())
      thread.join();
    if(governorChanged) {
      std::ofstream policy(governorPath);
      policy << previousGovernor;
    }
    if (pcm)
      snd_pcm_close(pcm);
    if (memory)
      munmap(memory, fix.smem_len);
    if (fb >= 0)
      close(fb);
    if (input >= 0)
      close(input);
  }
  bool setBrightnessPercent(int percent){
    if(disp<0)return false;unsigned long args[4]{};args[1]=4+172*std::clamp(percent,0,100)/100;return ioctl(disp,0x102,args)>=0;
  }
  bool init(Audio &audio, FxAudio &effects, int brightness) {
    // Short bursts of synthesis must not wait for the ondemand frequency ramp.
    // This uses the stock maximum and keeps the kernel's thermal limits active.
    std::ifstream(governorPath) >> previousGovernor;
    if(!previousGovernor.empty()) {
      std::ofstream policy(governorPath);
      policy << "performance";policy.flush();governorChanged=bool(policy);
      fprintf(stderr,"CPU governor: %s -> %s\n",previousGovernor.c_str(),governorChanged?"performance":"unchanged");
    }
    fb = open("/dev/fb0", O_RDWR);
    if (fb < 0 || ioctl(fb, FBIOGET_FSCREENINFO, &fix) ||
        ioctl(fb, FBIOGET_VSCREENINFO, &var))
      return false;
    if (var.xres != 1024 || var.yres != 768 || var.bits_per_pixel != 32 ||
        var.red.offset != 16 || var.green.offset != 8 || var.blue.offset != 0)
      return false;
    memory = (uint8_t *)mmap(nullptr, fix.smem_len, PROT_READ | PROT_WRITE,
                             MAP_SHARED, fb, 0);
    if (memory == MAP_FAILED) {
      memory = nullptr;
      return false;
    }
    disp = open("/dev/disp", O_RDWR);
    if (disp >= 0) {
      unsigned long args[4]{};
      previousBrightness = ioctl(disp, 0x103, args);
      if (previousBrightness >= 0) {
        args[1] = (std::clamp(brightness, 0, 100) * 172) / 100 + 4;
        int result = ioctl(disp, 0x102, args);
        fprintf(stderr, "brightness: previous=%d requested=%lu result=%d\n",
                previousBrightness, args[1], result);
      }
    }
    // Own only the power-key device. Vendor volume/charging/input services keep running.
    powerInput=open("/dev/input/event1",O_RDONLY|O_NONBLOCK);
    char powerName[128]{};
    if(powerInput<0 || ioctl(powerInput,EVIOCGNAME(sizeof powerName),powerName)<0 ||
       !strstr(powerName,"pek") || ioctl(powerInput,EVIOCGRAB,1)<0) {
      fprintf(stderr,"Cannot claim power key; leaving stock power behavior active\n");
      if(powerInput>=0)close(powerInput);powerInput=-1;
    } else fprintf(stderr,"Power key claimed: %s\n",powerName);
    input = open("/dev/input/event3", O_RDONLY | O_NONBLOCK);
    if (input < 0)
      return false;
    volumeInput=open("/dev/input/event0",O_RDONLY|O_NONBLOCK);
    if(snd_ctl_open(&control,"hw:0",0)>=0) {
      previousHeadphone=mixerValue("Headphone Volume");
      // The A133 codec's headphone control is attenuation: raw0 is loudest.
      // The stock driver's ascending dB TLV is misleading; do not use raw7.
      mixerValue("Headphone Volume",0);
      fprintf(stderr,"headphone attenuation: %d -> 0 (maximum output)\n",previousHeadphone);
    }
    int err = snd_pcm_open(&pcm, "hw:0,0", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
      fprintf(stderr, "ALSA open: %s\n", snd_strerror(err));
      return false;
    }
    err = snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE,
                             SND_PCM_ACCESS_RW_INTERLEAVED, 2, 44100, 0, 80000);
    if (err < 0) {
      fprintf(stderr, "ALSA setup: %s\n", snd_strerror(err));
      return false;
    }
    thread = std::thread([&audio, &effects, this]() {
      float data[256 * 2];
      int16_t pcmData[256 * 2];
      sched_param param{};
      param.sched_priority = 12;
      int rt = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param);
      fprintf(stderr, "audio realtime scheduling: %s\n",
              rt ? "unavailable" : "enabled");
      while (audioRun) {
        bool fxMode=effects.active();
        auto renderStart=std::chrono::steady_clock::now();
        if(effects.active())effects.render(data,256);else audio.render(data,256);
        auto renderUs=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-renderStart).count();
        maxRenderUs.store(std::max(maxRenderUs.load(),uint64_t(renderUs)));
        if(fxMode)audio.stats.fxRenderMaxUs.store(std::max(audio.stats.fxRenderMaxUs.load(),uint64_t(renderUs)));
        // Stereo mix to USB (pre codec volume, like a line output). Never blocks.
        usbOut.push(data,256);
        // The Brick's codec plays its first channel on the jack's right side: the frames go
        // to it with left and right exchanged, so the jack matches the mix (and USB out).
        for (int i = 0; i < 256; i++) {
          pcmData[2 * i] = int16_t(data[2 * i + 1] * 32767);
          pcmData[2 * i + 1] = int16_t(data[2 * i] * 32767);
        }
        int pos = 0;
        while (pos < 256 && audioRun) {
          auto n = snd_pcm_writei(pcm, pcmData + pos * 2, 256 - pos);
          if (n < 0) {
            audio.stats.overruns.fetch_add(1);
            if (snd_pcm_recover(pcm, int(n), 1) < 0) {
              audioRun = false;
              running = 0;
              break;
            }
          } else
            pos += n;
        }
      }
      snd_pcm_drop(pcm);
    });
    return true;
  }
  void present(const Canvas &c) {
    if (ioctl(fb, FBIOGET_VSCREENINFO, &var))
      return;
    size_t off = size_t(var.yoffset) * fix.line_length + var.xoffset * 4;
    if (off + size_t(c.h - 1) * fix.line_length + c.w * 4 > fix.smem_len)
      return;
    // Use ordered 32-bit pixel stores for this device-memory mapping, and flush
    // the completed frame even when playback is idle.
    for (int y = 0; y < c.h; y++) {
      volatile uint32_t *destination=reinterpret_cast<volatile uint32_t *>(memory+off+y*fix.line_length);
      const uint32_t *source=c.pixels.data()+y*c.w;
      for(int x=0;x<c.w;x++)destination[x]=source[x];
    }
    std::atomic_thread_fence(std::memory_order_seq_cst);
    size_t first=off & ~size_t(4095),end=(off+size_t(c.h)*fix.line_length+4095)&~size_t(4095);
    msync(memory+first,end-first,MS_SYNC);
  }
};
#endif
int main(int argc, char **argv) {
  std::string screenshot, render, projectPath = "session.amalthea";
#ifndef AMALTHEA_BRICK
  if (const char *userHome = getenv("HOME")) {
    std::filesystem::path dir = std::filesystem::path(userHome) /
                                "Library/Application Support/Amalthea";
    std::filesystem::create_directories(dir);
    projectPath = (dir / "session.amalthea").string();
  }
#endif
  int w = 1024, h = 768, seconds = 0, rows = 16, brightness = 10;
  bool headless = false, help = false, autoplay = false, stress = false, recall = false;
  size_t budget = 128;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto val = [&]() {
      if (i + 1 >= argc) {
        fprintf(stderr, "Missing value for %s\n", a.c_str());
        exit(2);
      }
      return std::string(argv[++i]);
    };
    if (a == "--load")
      recall = true;
    else if (a == "--screenshot")
      screenshot = val();
    else if (a == "--render")
      render = val();
    else if (a == "--project")
      projectPath = val();
    else if (a == "--rg35xx") {
      w = 640;
      h = 480;
    } else if (a == "--brightness")
      brightness = std::stoi(val());
    else if (a == "--rows")
      rows = std::stoi(val());
    else if (a == "--seconds")
      seconds = std::stoi(val());
    else if (a == "--voices")
      budget = std::stoul(val());
    else if (a == "--stress")
      stress = true;
    else if (a == "--headless")
      headless = true;
    else if (a == "--play")
      autoplay = true;
    else {
      fprintf(stderr, "Unknown option: %s\n", a.c_str());
      return 2;
    }
  }
  if (rows != 8 && rows != 16) {
    fprintf(stderr, "--rows must be 8 or 16\n");
    return 2;
  }
  signal(SIGINT, stop);
  signal(SIGTERM, stop);
  Editor editor;
  editor.project = stress ? demoProject() : blankProject();
  if(!stress){
    // Each boot draws from fresh entropy: generators no longer replay the same
    // sequence, and the blank session's instruments start on drawn patches.
    std::random_device device;
    editor.entropy=device()^uint32_t(std::chrono::steady_clock::now().time_since_epoch().count());
    if(!editor.entropy)editor.entropy=0x6d2b79f5u;
    editor.project.seed^=randomNext(editor.entropy);if(!editor.project.seed)editor.project.seed=1;
    editor.randomizeBlankPatches();
  }
  editor.status="BLANK SESSION - HOLD SESSION TO SAVE";
  editor.visibleRows = rows;
  const auto devicePath=(std::filesystem::path(projectPath).parent_path()/"device-settings.amalthea").string();
  std::string err;
  editor.device.brightness=std::clamp(brightness*10,0,100);
  if(std::filesystem::exists(devicePath))loadDeviceSettings(editor.device,devicePath,err);
  if (recall && std::ifstream(projectPath).good()) {
    if (!loadProject(editor.project, projectPath, err))
      editor.status = err + " - ORIGINAL PRESERVED";
    else
      editor.status = "SESSION LOADED";
        editor.dirty=false;
  }
  if (stress)
    for (auto &t : editor.project.tracks)
      t.mute = false;
  Audio audio(budget);FxAudio effects(audio.stats);AudioRouter router{audio,effects};
  Canvas canvas(w, h);
  editor.playing = autoplay;
  audio.submit(editor.project, editor.playing,-1,nullptr,&editor.device);
  if (!render.empty()) {
    audio.submit(editor.project, true,-1,nullptr,&editor.device);
    size_t frames = 44100 * size_t(seconds > 0 ? seconds : 8);
    std::vector<float> data(frames * 2);
    auto start = std::chrono::steady_clock::now();
    audio.render(data.data(), int(frames));
    double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
            .count();
    writeWav(render, data.data(), frames);
    printf("rendered %zu frames in %.6f seconds; steals=%llu\n", frames,
           elapsed, (unsigned long long)audio.stats.steals.load());
  }
  if (!screenshot.empty()) {
    drawUI(canvas, editor, audio.stats);
    writePPM(canvas, screenshot);
  }
  if (headless)
    return 0;
  editor.page=Page::Appliances;editor.playing=false;editor.splash=true;
  audio.submit(editor.project,false,-1,nullptr,&editor.device);
  SDL_Window *window = nullptr;
  SDL_Renderer *renderer = nullptr;
  SDL_Texture *texture = nullptr;
  SDL_AudioDeviceID device = 0;
#ifdef AMALTHEA_BRICK
  Brick brick;
  if (!brick.init(audio,effects, editor.device.brightness)) {
    fprintf(stderr, "Brick initialization failed\n");
    return 1;
  }
  drawUI(canvas,editor,audio.stats);brick.present(canvas);
  std::map<int, Action> physical = {
      {304, Action::TrackSettings},  {305, Action::ColumnSettings},
      {307, Action::Master},      {308, Action::RandomizeColumn},
      {310, Action::ShiftDown}, {311, Action::Shift2Down},
      {314, Action::Session},     {315, Action::Enter},
      {316, Action::Play}, {317, Action::PrevTrack}, {318, Action::NextTrack}};
  // Optional local override file: evdev-code action-name. No dependency on SDL
  // button indices.
  std::map<std::string, Action> names = {
      {"enter", Action::Enter},        {"column", Action::RandomizeColumn},
      {"column-settings", Action::ColumnSettings}, {"track", Action::TrackSettings}, {"effects", Action::Master}, {"session", Action::Session},   {"cancel", Action::Cancel},
      {"play", Action::Play},          {"mixer", Action::Mixer},
      {"dashboard", Action::Dashboard}, {"back", Action::Back}, {"home", Action::Home}, {"rows", Action::Rows},          {"undo", Action::Undo},
      {"previous", Action::PrevTrack}, {"next", Action::NextTrack}};
  {
    std::ifstream map("/mnt/UDISK/tracker-linux/input.map");
    int code;
    std::string name;
    while (map >> code >> name)
      if (names.count(name))
        physical[code] = names[name];
  }
#else
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) {
    fprintf(stderr, "SDL: %s\n", SDL_GetError());
    return 1;
  }
  window = SDL_CreateWindow("Amalthea - Brick simulator",
                            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                            w + 240, h, SDL_WINDOW_RESIZABLE);
  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  if (!window || !renderer) {
    fprintf(stderr, "SDL display: %s\n", SDL_GetError());
    return 1;
  }
  texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                              SDL_TEXTUREACCESS_STREAMING, w + 240, h);
  SDL_RenderSetLogicalSize(renderer, w + 240, h);
  SDL_AudioSpec want{}, got{};
  want.freq = 44100;
  want.format = AUDIO_F32SYS;
  want.channels = 2;
  want.samples = 256;
  want.callback = callback;
  want.userdata = &router;
  device = SDL_OpenAudioDevice(nullptr, 0, &want, &got, 0);
  if (!device) {
    fprintf(stderr, "SDL audio: %s\n", SDL_GetError());
    return 1;
  }
  SDL_PauseAudioDevice(device, 0);
#endif
  bool uiDirty = true, syncNeeded = false, savePending = false;
  auto start=std::chrono::steady_clock::now(), lastDraw=start, saveAt=start, noticeUntil=start;
  SessionBank sessionBank(projectPath);
  const auto fxBankRoot=(std::filesystem::path(projectPath).parent_path()/"fx-session-slots").string();
  const auto lifebloodBankRoot=(std::filesystem::path(projectPath).parent_path()/"lifeblood-session-slots").string();
  editor.sessionState=sessionBank.scan();
  int saveSlot=0;
  bool savingPattern=false,exitAfterSave=false,savingFx=false,savingLifeblood=false;
  SessionButton sessionButton;
  RandomizerButton randomizerButton;
  PowerButton powerButton;
  auto shutdownAt=start;
  bool shutdownLaunched=false;
#ifdef AMALTHEA_BRICK
  pid_t shutdownChild=-1;
#endif
  auto millis=[&]() { return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count()); };
  int held = -1;
  bool triggers[2]{};
  auto repeat = start;
  auto bannerUntil = start, effectUntil = start, volumeUntil = start;
#ifdef AMALTHEA_BRICK
  editor.volume = std::max(0,brick.volumePercent());
  auto volumePoll = start;
#endif
  auto apply = [&](Action a) {
    uiDirty = true;
    if(editor.splash)editor.splash=false;
    if (help) {
      help = false;
      if (a == Action::Cancel)
        return;
    }
    if(a==Action::Home||a==Action::Dashboard||a==Action::Back){
      editor.action(a);held=-1;
      sessionButton.down=randomizerButton.down=powerButton.down=false;
      syncNeeded=true;return;
    }
    if(editor.powerPrompt || editor.poweringOff || a==Action::PowerPrompt || a==Action::PowerConfirm) {
      bool wasOff=editor.poweringOff;
      editor.action(a);held=-1;sessionButton.down=false;randomizerButton.down=false;
      if(!wasOff && editor.poweringOff)shutdownAt=std::chrono::steady_clock::now()+std::chrono::milliseconds(250);
      syncNeeded=true;return;
    }
    if(editor.exitStage!=Editor::ExitStage::None||editor.page==Page::Appliances ){
      held=-1;sessionButton.down=randomizerButton.down=false;
      if(savePending){if(a==Action::ShiftUp||a==Action::Shift2Up)editor.action(a);return;}
      editor.action(a);
      if(editor.exitSaveRequested){editor.exitSaveRequested=false;exitAfterSave=savePending=true;savingFx=editor.inFx();savingLifeblood=editor.inLifeblood();
        saveSlot=savingLifeblood?(editor.lbActiveSession>=0?editor.lbActiveSession:editor.lbSessionSlot):savingFx?(editor.fxActiveSession>=0?editor.fxActiveSession:editor.fxSessionSlot):(editor.activeSession>=0?editor.activeSession:editor.sessionSlot);
        saveAt=std::chrono::steady_clock::now()+std::chrono::milliseconds(180);}
      syncNeeded=true;return;
    }
    if(savePending){if(a==Action::ShiftUp||a==Action::Shift2Up)editor.action(a);return;}
    // Keyboard shortcuts resolve the current page; timed physical gestures carry
    // their bank identity from button-down, even if another control changes pages.
    // LIFEBLOOD banks: the editor handles snapshot gestures; session file work follows below.
    bool lifebloodBank=editor.inLifeblood()&&(a==Action::Save||a==Action::Load||a==Action::SavePattern||a==Action::LoadPattern||a==Action::SaveSession||a==Action::LoadSession);
    if(lifebloodBank&&(a==Action::Save||a==Action::Load)){
      bool snapshots=editor.page==Page::LifebloodSnapshots;
      a=a==Action::Save?(snapshots?Action::SavePattern:Action::SaveSession):(snapshots?Action::LoadPattern:Action::LoadSession);
    }
    if(!lifebloodBank&&(a==Action::Save||a==Action::Load)){
      bool pattern=editor.page==Page::Patterns||editor.page==Page::PatternSettings;
      a=a==Action::Save?(pattern?Action::SavePattern:Action::SaveSession):(pattern?Action::LoadPattern:Action::LoadSession);
    }
    if(!lifebloodBank&&(a==Action::SavePattern||a==Action::LoadPattern)){
      // Playing: the pattern waits for the quantized bar (SESSION > SWITCH QUANTIZE).
      bool ok=a==Action::SavePattern?editor.savePattern(editor.patternSlot):editor.queuePattern(editor.patternSlot);
      bool queued=a==Action::LoadPattern&&ok&&editor.queuedPattern>=0;
      editor.say(ok?"PATTERN "+std::to_string(editor.patternSlot+1)+(a==Action::SavePattern?" SAVED":queued?" NEXT":" LOADED"):"EMPTY PATTERN SLOT");
      noticeUntil=std::chrono::steady_clock::now()+std::chrono::milliseconds(1400);syncNeeded=true;
      if(a==Action::LoadPattern||!ok)return;
      savingPattern=true;a=Action::SaveSession;
    }
    if (!lifebloodBank&&a == Action::SaveSession) {
      if(savePending)return;
      savingFx=editor.inFx();savingLifeblood=false;
      saveSlot=savingFx?editor.fxSessionSlot:savingPattern?std::max(0,editor.activeSession):editor.sessionSlot;
      editor.say(savingPattern?"SAVING PATTERN "+std::to_string(editor.patternSlot+1):"SAVING SESSION "+std::to_string(saveSlot+1));
      savePending=true;
      // Present the message before the atomic write, following the Bigorna-derived UI.
      saveAt=std::chrono::steady_clock::now()+std::chrono::milliseconds(180);
      noticeUntil=saveAt+std::chrono::seconds(2);
      return;
    }
    if (!lifebloodBank&&a == Action::LoadSession) {
      if(editor.inFx()){
        if(loadFxRack(*editor.fxRack,fxSessionPath(fxBankRoot,editor.fxSessionSlot),err)){
          editor.fxActiveSession=editor.fxSessionSlot;editor.say("FX RACK SESSION "+std::to_string(editor.fxSessionSlot+1)+" LOADED");
        }else editor.say(err);
        noticeUntil=std::chrono::steady_clock::now()+std::chrono::milliseconds(1400);syncNeeded=true;return;
      }
      if (sessionBank.load(editor.sessionSlot,editor,err)) {
        editor.activeSession=editor.sessionSlot;
        editor.status = "SESSION "+std::to_string(editor.sessionSlot+1)+" LOADED";
      } else
        editor.status = err;
      editor.say(editor.status);
      noticeUntil=std::chrono::steady_clock::now()+std::chrono::milliseconds(1600);
      syncNeeded = true;
      return;
    }
    const auto oldDevice=editor.device;
    auto oldTrack = editor.track;
    auto oldAnnouncement = editor.effectAnnouncement;
    auto oldNotice=editor.noticeSerial;
    editor.action(a);
    if(editor.exitSaveRequested){
      editor.exitSaveRequested=false;exitAfterSave=true;savePending=true;savingFx=editor.inFx();savingLifeblood=editor.inLifeblood();
      saveSlot=savingLifeblood?(editor.lbActiveSession>=0?editor.lbActiveSession:editor.lbSessionSlot):savingFx?(editor.fxActiveSession>=0?editor.fxActiveSession:editor.fxSessionSlot):(editor.activeSession>=0?editor.activeSession:editor.sessionSlot);
      saveAt=std::chrono::steady_clock::now()+std::chrono::milliseconds(180);
    }
    if(editor.exitStage!=Editor::ExitStage::None){held=-1;sessionButton.down=randomizerButton.down=false;}
    if(editor.device!=oldDevice){
#ifdef AMALTHEA_BRICK
      if(editor.device.brightness!=oldDevice.brightness)brick.setBrightnessPercent(editor.device.brightness);
#endif
      if(!saveDeviceSettings(editor.device,devicePath,err)){editor.say(err);noticeUntil=std::chrono::steady_clock::now()+std::chrono::seconds(2);}
    }
    if(editor.inFx()&&editor.page==Page::FxSessions)editor.fxSessionState=scanFxSessions(fxBankRoot);
    else if(a==Action::Session)editor.sessionState=sessionBank.scan();
    if(editor.inLifeblood()&&editor.page==Page::LifebloodSessions)editor.lbSessionState=scanLifeblood(lifebloodBankRoot);
    if(editor.lbSessionRequest&&editor.lifeblood){
      int request=editor.lbSessionRequest;editor.lbSessionRequest=0;
      if(request==1){
        int slot=editor.lbSessionSlot;
        if(loadLifeblood(*editor.lifeblood,lifebloodPath(lifebloodBankRoot,slot),err)){
          editor.lifeblood->network.generation=++editor.lbGeneration;editor.lbActiveSession=slot;editor.lbVertex=LifebloodIn;
          editor.say("LIFEBLOOD SESSION "+std::to_string(slot+1)+" LOADED");
        }else editor.say(err);
        noticeUntil=std::chrono::steady_clock::now()+std::chrono::milliseconds(1400);
      }else if(!savePending){
        // 2: save the selected slot; 3: keep a stored snapshot in the active session.
        savingLifeblood=savePending=true;savingFx=false;saveSlot=request==3?editor.lbActiveSession:editor.lbSessionSlot;
        if(request==2)editor.say("SAVING LIFEBLOOD SESSION "+std::to_string(saveSlot+1));
        saveAt=std::chrono::steady_clock::now()+std::chrono::milliseconds(180);noticeUntil=saveAt+std::chrono::seconds(2);
      }
    }
    if(editor.fxSessionRequest){
      int request=editor.fxSessionRequest;editor.fxSessionRequest=0;
      if(request==1){if(loadFxRack(*editor.fxRack,fxSessionPath(fxBankRoot,editor.fxSessionSlot),err)){editor.fxActiveSession=editor.fxSessionSlot;editor.say("FX RACK SESSION LOADED");}else editor.say(err);noticeUntil=std::chrono::steady_clock::now()+std::chrono::milliseconds(1400);}
      else {savingFx=savePending=true;saveSlot=editor.fxSessionSlot;editor.say("SAVING FX RACK SESSION "+std::to_string(saveSlot+1));saveAt=std::chrono::steady_clock::now()+std::chrono::milliseconds(180);noticeUntil=saveAt+std::chrono::seconds(2);}
    }
    if (editor.effectAnnouncement != oldAnnouncement)
      effectUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds(900);
    if (editor.track != oldTrack) {
      editor.trackBanner = true;
      bannerUntil =
          std::chrono::steady_clock::now() + std::chrono::milliseconds(650);
    }
    syncNeeded = true;
    if(editor.noticeSerial!=oldNotice){noticeUntil=std::chrono::steady_clock::now()+std::chrono::milliseconds(editor.noticeMs);editor.noticeMs=1200;}
  };
  uint64_t auditionSeen=editor.auditionSerial;
#ifdef AMALTHEA_BRICK
  // Battery charge from the AXP2202 fuel gauge, read on the UI thread every 20 s.
  auto pollBattery=[&]{
    int percent=-1;char status[32]{};
    if(FILE*f=fopen("/sys/class/power_supply/axp2202-battery/capacity","r")){if(fscanf(f,"%d",&percent)!=1)percent=-1;fclose(f);}
    if(FILE*f=fopen("/sys/class/power_supply/axp2202-battery/status","r")){if(fscanf(f,"%31s",status)!=1)status[0]=0;fclose(f);}
    bool charging=!strcmp(status,"Charging")||!strcmp(status,"Full");
    if(percent!=editor.battery||charging!=editor.charging){editor.battery=percent;editor.charging=charging;uiDirty=true;}
  };
  pollBattery();
  auto batteryAt=std::chrono::steady_clock::now()+std::chrono::seconds(20);
#endif
  while (running) {
    auto now = std::chrono::steady_clock::now();
#ifdef AMALTHEA_BRICK
    if(now>=batteryAt){pollBattery();batteryAt=now+std::chrono::seconds(20);}
#endif
    if(editor.inFx()){
      int count=audio.stats.usbChannels.load();if(count<1||count>6)count=6;
      if(count!=editor.fxVisibleChannels){editor.fxVisibleChannels=count;editor.fxChannel%=count;uiDirty=true;}
    }
    if(editor.splash&&now-start>=std::chrono::milliseconds(1400)){editor.splash=false;uiDirty=true;}
    // PERFORMANCE: the chain (with its LFOs) to the audio every 20 ms while the mode is on,
    // and once more as it is left (the chains ring out). F1 is lit while it leaves the mode.
    {
      const bool on=editor.page==Page::Performance&&editor.performance;
      static bool wasOn=false;static auto perfAt=now;static uint64_t perfSerial=0;
      if(on||wasOn){
        bool due=now>=perfAt||editor.performanceSerial!=perfSerial||on!=wasOn;
        if(due&&editor.performance){
          double dt=std::chrono::duration<double>(now-perfAt).count();if(!(dt>0&&dt<.25))dt=.02;
          auto frame=perfFrame(*editor.performance,on?dt:0);if(!on)frame.on=false;
          audio.perform(frame);perfAt=now+std::chrono::milliseconds(20);perfSerial=editor.performanceSerial;
        }
      }
#ifdef AMALTHEA_BRICK
      brick.lightKeys(on?Brick::Keys::Performance:
        !editor.splash&&editor.page!=Page::Appliances&&!editor.inAppliance()?Brick::Keys::Tracker:Brick::Keys::Off);
#endif
      wasOn=on;
    }
    if(auto gesture=powerButton.tick(millis());gesture!=Action::Reserved)apply(gesture);
    if(auto gesture=sessionButton.tick(millis());gesture!=Action::Reserved)apply(gesture);
    if(auto gesture=randomizerButton.tick(millis());gesture!=Action::Reserved)apply(gesture);
    if(!editor.notice.empty() && now>=noticeUntil && !savePending) {editor.notice.clear();uiDirty=true;}
    if (editor.effectBanner && now >= effectUntil) { editor.effectBanner=false;uiDirty=true; }
    if (editor.volumeBanner && now >= volumeUntil) { editor.volumeBanner=false;uiDirty=true; }
    if (editor.trackBanner && now >= bannerUntil) {
      editor.trackBanner = false;
      uiDirty = true;
    }
    if (seconds > 0 && now - start > std::chrono::seconds(seconds))
      break;
#ifdef AMALTHEA_BRICK
    input_event ev{};
    while(read(brick.powerInput,&ev,sizeof ev)==sizeof ev) {
      if(ev.type==EV_KEY && ev.code==KEY_POWER) {
        if(ev.value==1)powerButton.press(millis());
        if(ev.value==0){auto gesture=powerButton.release(millis());if(gesture!=Action::Reserved)apply(gesture);}
      }
    }
    bool volumeKey=false;
    while(read(brick.volumeInput,&ev,sizeof ev)==sizeof ev)
      if(ev.type==EV_KEY && ev.value && (ev.code==KEY_VOLUMEUP || ev.code==KEY_VOLUMEDOWN))volumeKey=true;
    if(volumeKey || now>=volumePoll) {
      int actual=brick.volumePercent();
      if(volumeKey || (actual>=0 && actual!=editor.volume)) {
        if(actual>=0)editor.volume=actual;
        editor.volumeBanner=true;volumeUntil=now+std::chrono::milliseconds(1500);uiDirty=true;
      }
      volumePoll=now+std::chrono::milliseconds(80);
    }
    while (read(brick.input, &ev, sizeof ev) == sizeof ev) {
      if(ev.type==EV_KEY && physical.count(ev.code) && physical[ev.code]==Action::RandomizeColumn) {
        if(ev.value==1)randomizerButton.press(millis());
        if(ev.value==0){auto gesture=randomizerButton.release(millis());if(gesture!=Action::Reserved)apply(gesture);}
        continue;
      }
      if(ev.type==EV_KEY && physical.count(ev.code) && physical[ev.code]==Action::Session) {
        if(ev.value==1)sessionButton.press(millis(),editor);
        if(ev.value==0) {auto gesture=sessionButton.release(millis());if(gesture!=Action::Reserved)apply(gesture);}
        continue;
      }
      if (ev.type == EV_KEY && (ev.code == 310 || ev.code == 311)) {
        apply(ev.code == 310
                  ? (ev.value ? Action::ShiftDown : Action::ShiftUp)
                  : (ev.value ? Action::Shift2Down : Action::Shift2Up));
        continue;
      }
      if (ev.type == EV_KEY && ev.value == 1) {
        fprintf(stderr, "input key %d\n", ev.code);
        if (physical.count(ev.code))
          apply(physical[ev.code]);
      }
      if (ev.type == EV_ABS && (ev.code == ABS_Z || ev.code == ABS_RZ)) {
        int index = ev.code == ABS_Z ? 0 : 1;
        bool pressed = ev.value > 127;
        if (pressed && !triggers[index])
          apply(index == 0 ? (editor.shift ? Action::Dashboard : Action::Back) : Action::Home); // L2 Back, Shift+L2 Dashboard
        triggers[index] = pressed;
      }
      if (ev.type == EV_ABS && (ev.code == ABS_HAT0X || ev.code == ABS_HAT0Y)) {
        held = ev.value == 0
                   ? -1
                   : int(ev.code == ABS_HAT0X
                             ? (ev.value < 0 ? Action::Left : Action::Right)
                             : (ev.value < 0 ? Action::Up : Action::Down));
        if (held >= 0) {
          apply(Action(held));
          // Octave transposition is one gesture per physical press, not key repeat.
          if(editor.shift && editor.page==Page::Pattern && !editor.mixer && ev.code==ABS_HAT0Y)held=-1;
          repeat = now + std::chrono::milliseconds(350);
        }
      }
    }
    if (held >= 0 && now >= repeat) {
      apply(Action(held));
      repeat = now + std::chrono::milliseconds(90);
    }
#else
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      if (ev.type == SDL_QUIT)
        running = 0;
      if (ev.type == SDL_KEYUP && (ev.key.keysym.sym == SDLK_LSHIFT ||
                                   ev.key.keysym.sym == SDLK_RSHIFT))
        apply(ev.key.keysym.sym == SDLK_LSHIFT ? Action::ShiftUp
                                               : Action::Shift2Up);
      if(ev.type==SDL_KEYUP && (ev.key.keysym.sym==SDLK_F12 || ev.key.keysym.sym==SDLK_POWER)) {
        auto gesture=powerButton.release(millis());if(gesture!=Action::Reserved)apply(gesture);
      }
      if(ev.type==SDL_KEYUP && ev.key.keysym.sym==SDLK_x){auto gesture=randomizerButton.release(millis());if(gesture!=Action::Reserved)apply(gesture);}
      if(ev.type==SDL_KEYUP && ev.key.keysym.sym==SDLK_p) {auto gesture=sessionButton.release(millis());if(gesture!=Action::Reserved)apply(gesture);}
      if(ev.type==SDL_WINDOWEVENT && ev.window.event==SDL_WINDOWEVENT_FOCUS_LOST){sessionButton.down=false;powerButton.down=false;randomizerButton.down=false;}
      if (ev.type == SDL_MOUSEBUTTONUP) {
        auto randomGesture=randomizerButton.release(millis());if(randomGesture!=Action::Reserved)apply(randomGesture);
        auto gesture=sessionButton.release(millis());if(gesture!=Action::Reserved)apply(gesture);
        apply(Action::ShiftUp);
        apply(Action::Shift2Up);
      }
      if (ev.type == SDL_KEYDOWN) {
        auto k = ev.key.keysym.sym;
        if(k==SDLK_F12 || k==SDLK_POWER){if(!ev.key.repeat)powerButton.press(millis());}
        else if(ev.key.repeat && editor.shift && editor.page==Page::Pattern && !editor.mixer && (k==SDLK_UP || k==SDLK_DOWN))continue;
        else if(k==SDLK_a){if(!ev.key.repeat)apply(Action::ColumnSettings);}
        else if(k==SDLK_x){if(!ev.key.repeat)randomizerButton.press(millis());}
        else if(k==SDLK_p) {if(!ev.key.repeat)sessionButton.press(millis(),editor);}
        else if (k == SDLK_F1 || k == SDLK_F2) {
          uiDirty = true;
          w = k == SDLK_F1 ? 1024 : 640;
          h = w * 3 / 4;
          canvas = Canvas(w, h);
          SDL_DestroyTexture(texture);
          texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                      SDL_TEXTUREACCESS_STREAMING, w + 240, h);
          SDL_SetWindowSize(window, w + 240, h);
          SDL_RenderSetLogicalSize(renderer, w + 240, h);
          SDL_SetWindowTitle(window, w == 1024
                                         ? "Amalthea - Brick simulator"
                                         : "Amalthea - RG35XX H simulator");
        } else if (k == SDLK_h) {
          help = !help;
          uiDirty = true;
        } else if (auto a = key(k))
          apply(*a);
      }
      if (ev.type == SDL_MOUSEBUTTONDOWN) {
        int ww, hh;
        SDL_GetWindowSize(window, &ww, &hh);
        float sc = std::min(float(ww) / (w + 240), float(hh) / h);
        int mx = int((ev.button.x - (ww - (w + 240) * sc) / 2) / sc),
            my = int((ev.button.y - (hh - h * sc) / 2) / sc);
        for (auto b : buttons(w, h))
          if (mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h)
            {if(b.action==Action::Session)sessionButton.press(millis(),editor);else if(b.action==Action::RandomizeColumn)randomizerButton.press(millis());else apply(b.action);}
      }
    }
#endif
    if(editor.inLifeblood()){
      // FM1 inputs: a stereo firmware is one IN; separate tracks (6, or 3) are track inputs.
      int usb=audio.stats.usbState.load(),channels=audio.stats.usbChannels.load();
      editor.lbInputs=usb==4||(usb==1&&channels==1)?0:usb==1?std::min(channels,LifebloodTracks):-1;
      editor.lbHeard=audio.stats.lifebloodHeard.load();
      editor.lbClockBpm=audio.stats.usbClock.load()?int(audio.stats.usbBpm.load()+.5f):0;
      // Animation and angiogenesis run on the UI thread; the audio only sees networks.
      static auto lastTick=now;float dt=std::chrono::duration<float>(now-lastTick).count();lastTick=now;
      if(editor.lifebloodTick(std::min(dt,.1f)))syncNeeded=true;
    }
    if(syncNeeded&&editor.inLifeblood()&&editor.lifeblood){
      auto&s=*editor.lifeblood;
      if(effects.submit(s.network,s.tempo,s.mix,editor.lbBypass,editor.device,s.master)){effects.activate(true);syncNeeded=false;}
    }
    if(syncNeeded&&editor.inFx()&&editor.fxRack){
      if(effects.submit(*editor.fxRack,editor.device)){effects.activate(true);syncNeeded=false;}
    }
    // The audio swapped the queued pattern in at its bar: the editor takes it.
    if(editor.queuedPattern>=0&&editor.queueSubmitted&&audio.stats.patternGeneration.load(std::memory_order_acquire)==editor.queuedGeneration){
      int slot=editor.queuedPattern;editor.adoptQueuedPattern();
      editor.say("PATTERN "+std::to_string(slot+1)+" PLAYING");noticeUntil=std::chrono::steady_clock::now()+std::chrono::milliseconds(1200);
      uiDirty=syncNeeded=true;
    }
    if(syncNeeded&&!editor.inAppliance()&&editor.queuedPattern>=0&&!editor.queueSubmitted){
      editor.queueSubmitted=audio.submit(*editor.queuedProject,true,-1,nullptr,&editor.device,false,
        PatternSync{editor.queuedGeneration,true,false,editor.sessionDefaults.quantize});
    }
    if(syncNeeded&&!editor.inAppliance()&&(editor.queuedPattern<0||editor.queueSubmitted)){
      effects.activate(false);
      auto preview=editor.previewProject();auto event=editor.previewStep();
      if(audio.submit(preview,editor.playing,editor.auditionSerial!=auditionSeen?editor.track:-1,
          editor.auditionSerial!=auditionSeen?&event:nullptr,&editor.device,!editor.editing,
          PatternSync{editor.patternGeneration,false,editor.queuedPattern>=0,editor.sessionDefaults.quantize})){
        syncNeeded=false;auditionSeen=editor.auditionSerial;
      }
    }
    if(savePending && now>=saveAt) {
      bool saved=savingLifeblood?saveLifeblood(*editor.lifeblood,lifebloodPath(lifebloodBankRoot,saveSlot),err):savingFx?saveFxRack(*editor.fxRack,fxSessionPath(fxBankRoot,saveSlot),err):sessionBank.save(saveSlot,editor,err);
      if(saved) {
        editor.dirty=false;if(savingLifeblood){editor.lbActiveSession=saveSlot;editor.lbSessionState=scanLifeblood(lifebloodBankRoot);}else if(savingFx){editor.fxActiveSession=saveSlot;editor.fxSessionState=scanFxSessions(fxBankRoot);}else {editor.activeSession=saveSlot;editor.sessionState=sessionBank.scan();}
        editor.status=savingLifeblood?(editor.lifeblood->activeSnapshot>=0&&editor.page==Page::LifebloodSnapshots?"SNAPSHOT "+std::to_string(editor.lifeblood->activeSnapshot+1)+" STORED IN SESSION "+std::to_string(saveSlot+1):"LIFEBLOOD SESSION "+std::to_string(saveSlot+1)+" SAVED"):savingPattern?"PATTERN "+std::to_string(editor.patternSlot+1)+" SAVED":"SESSION "+std::to_string(saveSlot+1)+" SAVED";
      } else editor.status="SAVE FAILED: "+err;
      editor.say(editor.status);
      savePending=false;savingPattern=false;savingFx=false;savingLifeblood=false;
      if(exitAfterSave){
        exitAfterSave=false;
        if(saved)editor.leaveAppliance();
        else {editor.exitStage=Editor::ExitStage::None;editor.say("SAVE FAILED - "+err);}
        syncNeeded=true;
      }
      noticeUntil=now+std::chrono::milliseconds(1400);uiDirty=true;
    }
    if (uiDirty ||
        ((editor.inAppliance()||editor.page==Page::Mixer||editor.page==Page::Performance || (editor.playing && editor.page==Page::Pattern)) && now - lastDraw >= std::chrono::milliseconds(33))) {
      uiDirty = false;
      drawUI(canvas, editor, audio.stats, help);
#ifdef AMALTHEA_BRICK
      brick.present(canvas);
#else
      Canvas sim(w + 240, h);
      sim.rect(0, 0, sim.w, sim.h, 0x24322e);
      for (int y = 0; y < h; y++)
        std::copy_n(canvas.pixels.data() + y * w, w,
                    sim.pixels.data() + y * sim.w);
      for (auto b : buttons(w, h)) {
        sim.rect(b.x, b.y, b.w, b.h, 0x3e554b);
        sim.text(b.x + 5, b.y + 4, b.label, std::max(14, 20 * h / 768),
                 0xeaf1ed);
      }
      sim.text(w + 10, 610 * h / 768, "H HELP", 22, 0x82e6af);
      sim.text(w + 10, 655 * h / 768, "F1 BRICK", 20, 0xeaf1ed);
      sim.text(w + 10, 700 * h / 768, "F2 RG35XX", 20, 0xeaf1ed);
      SDL_UpdateTexture(texture, nullptr, sim.pixels.data(), sim.w * 4);
      SDL_RenderClear(renderer);
      SDL_RenderCopy(renderer, texture, nullptr, nullptr);
      SDL_RenderPresent(renderer);
#endif
      lastDraw = now;
    }
    if(editor.poweringOff && !shutdownLaunched && !savePending && now>=shutdownAt) {
      shutdownLaunched=true;
#ifdef AMALTHEA_BRICK
      sync();
      char command[]="/sbin/poweroff";char *args[]={command,nullptr};
      int result=posix_spawn(&shutdownChild,command,nullptr,nullptr,args,environ);
      if(result) {
        editor.poweringOff=editor.powerPrompt=false;
        editor.say("POWER OFF FAILED");noticeUntil=now+std::chrono::seconds(3);uiDirty=true;
        shutdownLaunched=false;shutdownChild=-1;
      }
#else
      running=0; // The simulator closes itself; it never shuts down the laptop.
#endif
    }
#ifdef AMALTHEA_BRICK
    if(shutdownChild>0) {
      int status=0;
      if(waitpid(shutdownChild,&status,WNOHANG)==shutdownChild) {
        shutdownChild=-1;
        if(!WIFEXITED(status) || WEXITSTATUS(status)!=0) {
          editor.poweringOff=editor.powerPrompt=false;shutdownLaunched=false;
          editor.say("POWER OFF FAILED");noticeUntil=now+std::chrono::seconds(3);uiDirty=true;
        }
      }
    }
#endif
    std::this_thread::sleep_for(std::chrono::milliseconds(4));
  }
  if(savePending) {
    bool saved=savingLifeblood?saveLifeblood(*editor.lifeblood,lifebloodPath(lifebloodBankRoot,saveSlot),err):savingFx?saveFxRack(*editor.fxRack,fxSessionPath(fxBankRoot,saveSlot),err):sessionBank.save(saveSlot,editor,err);
    if(!saved)fprintf(stderr,"%s\n",err.c_str());
  }
#ifndef AMALTHEA_BRICK
  SDL_CloseAudioDevice(device);
  SDL_DestroyTexture(texture);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
#endif
#ifdef AMALTHEA_BRICK
  fprintf(stderr,"maximum 256-frame render=%llu us\n",(unsigned long long)brick.maxRenderUs.load());
#endif
  fprintf(stderr, "audio blocks=%llu xruns=%llu steals=%llu\n",
          (unsigned long long)audio.stats.blocks.load(),
          (unsigned long long)audio.stats.overruns.load(),
          (unsigned long long)audio.stats.steals.load());
  fprintf(stderr,"note auditions=%llu last=%d\n",(unsigned long long)audio.stats.auditions.load(),audio.stats.auditionNote.load());
  for(int t=0;t<Tracks;t++) fprintf(stderr,"track %02d triggers=%llu\n",t+1,(unsigned long long)audio.stats.triggers[t].load());
  return 0;
}
