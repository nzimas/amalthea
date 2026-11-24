#pragma once
// LIFEBLOOD: the FM1's audio through a generated network of the shared inserts.
// Vertex 0 is IN (a stereo FM1), vertex 1 is OUT, vertex 2+k is effect node slot k and
// vertex 9+t is FM1 track t+1 (a firmware sending separate tracks: 6, or 3 for Norilsk).
// Signal flows only from a lower rank to a higher one (IN has rank 0, OUT the
// highest), so every network is acyclic: no edge can ever form a feedback loop.
// Node slots keep their index for life, so the audio keeps each node's processor
// (and its tail) while vessels grow and wither around it.
#include "core.hpp"
#include <array>
#include <bitset>
#include <string>
namespace amalthea {
constexpr int LifebloodMaxNodes=7,LifebloodTracks=6,LifebloodTrackBase=2+LifebloodMaxNodes,LifebloodVertices=LifebloodTrackBase+LifebloodTracks;
constexpr int LifebloodSnapshots=32,LifebloodSessionSlots=64;
constexpr int LifebloodIn=0,LifebloodOut=1,LifebloodFloor=2;
constexpr bool lifebloodNodeVertex(int v){return v>=2&&v<LifebloodTrackBase;}
constexpr bool lifebloodInput(int v){return v==LifebloodIn||v>=LifebloodTrackBase;}
// PATH (after Vascular): ABSTRACT grows textures and spaces; RHYTHMIC grows cuts, gates
// and permutations, every effect clearly wet.
enum LifebloodPath : uint8_t { PathAbstract, PathRhythmic, LifebloodPaths };
const char *lifebloodPathName(int);
// CHARACTER: the recipe X grows networks from. ANY draws one of the others each time.
// CUT: stutters, slicers and repeaters; GATE: rhythmic gates and chops; MAYHEM: cuts into
// destruction; INDUSTRIAL: gated, driven and filtered. Append only (sessions store the number).
enum LifebloodCharacter : uint8_t { CharacterAny, CharacterDrift, CharacterGrit, CharacterSpace, CharacterPulse, CharacterBloom,
 CharacterCut, CharacterGate, CharacterMayhem, CharacterIndustrial, LifebloodCharacters };
// The RHYTHMIC path's recipes (built around cuts and gates); the rest are ABSTRACT.
constexpr bool lifebloodRhythmic(int c){return c==CharacterPulse||(c>=CharacterCut&&c<=CharacterIndustrial);}
constexpr bool lifebloodOnPath(int c,int path){return c>CharacterAny&&c<LifebloodCharacters&&lifebloodRhythmic(c)==(path==PathRhythmic);}
const char *lifebloodCharacterName(int);
// The families recipes reason about (after Vascular's effect characters).
enum class LifebloodFamily : uint8_t { Space, Time, Texture, Color, Drive, Motion, Rhythm };
LifebloodFamily lifebloodFamily(FxType);
int lifebloodHarshness(FxType);   // 0 smooth, 1 edged, 2 abrasive
int lifebloodCost(FxType);        // relative CPU weight (light inserts 1)
struct LifebloodNode {
 bool active=false;
 Effect effect{};InsertSettings settings{};
 float x=.5f,y=.5f;   // position in the field, 0..1 (IN and OUT have fixed places)
 uint8_t rank=1;      // flow order among the active nodes: 1 .. LifebloodMaxNodes
};
struct LifebloodNetwork {
 uint8_t character=CharacterAny;   // the recipe that grew it
 uint8_t inputs=0;                 // 0: one stereo IN; 1..6: that many FM1 track inputs
 // Runtime (not saved): the tracks heard recently. Vessels only ever start from these.
 uint8_t heard=0x3f;
 std::array<LifebloodNode,LifebloodMaxNodes> node{};
 // link[from][to]: edge gain 0..100 (0 = no edge). Only from lower to higher rank.
 std::array<std::array<uint8_t,LifebloodVertices>,LifebloodVertices> link{};
 // Runtime topology identity (not saved): a new value makes the audio crossfade
 // from the old processors to fresh ones.
 uint32_t generation=0;
 int count()const{int n=0;for(auto&k:node)n+=k.active;return n;}
 bool live(int v)const{return v==LifebloodIn?inputs==0:v==LifebloodOut?true:lifebloodNodeVertex(v)?node[v-2].active:v<LifebloodVertices&&v-LifebloodTrackBase<inputs;}
 // An input a vessel may start from: the stereo IN, or a track that is heard (all of
 // them while none is).
 bool source(int v)const{if(v==LifebloodIn)return inputs==0;if(v<LifebloodTrackBase||v-LifebloodTrackBase>=inputs)return false;
  uint8_t all=uint8_t((1u<<inputs)-1);return (heard&all)?bool(heard>>(v-LifebloodTrackBase)&1):true;}
 int rank(int v)const{return lifebloodInput(v)?0:v==LifebloodOut?255:node[v-2].rank;}
 float x(int v)const{return lifebloodInput(v)?.06f:v==LifebloodOut?.94f:node[v-2].x;}
 float y(int v)const{if(v>=LifebloodTrackBase)return .1f+.8f*(v-LifebloodTrackBase+.5f)/std::max<int>(1,inputs);return v<2?.5f:node[v-2].y;}
};
// The blank network: IN straight to OUT, the input passing dry.
LifebloodNetwork blankLifeblood();
struct LifebloodRecipe { uint8_t character=CharacterAny,maxNodes=6,path=PathRhythmic,inputs=0,heard=0x3f; };
// X: a new network grown from the recipe. Deterministic for a given seed.
LifebloodNetwork growLifeblood(const LifebloodRecipe&,uint32_t&seed,const DeviceSettings&);
// A: new vessels between the same nodes (types and parameters kept), after Vascular's
// rewire: nodes in a new random flow order, each hung from IN or an earlier node,
// some merging again, every loose end draining into OUT. Dormant nodes stay unlinked.
void rewireLifeblood(LifebloodNetwork&,uint32_t&seed,unsigned dormant=0);
// Move the network onto `inputs` (0 stereo IN, else that many tracks): vessels from
// the old inputs are spread over the heard tracks at random (some tracks pass straight
// to OUT), so only sounding channels are routed.
void lifebloodSetInputs(LifebloodNetwork&,int inputs,uint8_t heard,uint32_t&seed);
// A heard track with no vessel at all passes straight to OUT. True when one was added.
bool lifebloodConnectHeard(LifebloodNetwork&,uint32_t&seed);
// Y: new parameters for every node, keeping the types and the connections.
void reseedLifeblood(LifebloodNetwork&,uint32_t&seed,int path=PathRhythmic);
// New parameters for one node, inside its family's bounds.
void reseedLifebloodNode(LifebloodNetwork&,int slot,uint32_t&seed,int path=PathRhythmic);
// Every edge goes up in rank between live vertices, OUT is reachable from IN and
// every active node lies on a path from IN to OUT.
bool lifebloodValid(const LifebloodNetwork&);
// Wire any node that lost its inputs or outputs back into the flow.
void lifebloodRepair(LifebloodNetwork&,uint32_t&seed);

// ANGIOGENESIS (B): nodes drift and bounce through the field while, one mutation
// at a time, vessels sprout and wither, nodes bud from a host or are absorbed
// (their inputs joined to their outputs) and single nodes redraw their parameters.
// Runtime state, never saved.
struct Angiogenesis {
 bool active=false;
 std::array<float,LifebloodMaxNodes> vx{},vy{};
 std::array<float,LifebloodMaxNodes> wither{};   // > 0: a pruned node fading out
 float timer=0;int startCost=0;uint32_t seed=0x5eed1e55;uint8_t path=PathRhythmic;
 void start(const LifebloodNetwork&);
 // Advance by dt seconds at GROWTH 0..100 (6..120 mutations a minute), never
 // above maxNodes. Returns true when the network changed.
 bool step(LifebloodNetwork&,float dt,int growth,int maxNodes,const DeviceSettings&);
};

struct LifebloodSession {
 LifebloodNetwork network=blankLifeblood();
 std::array<LifebloodNetwork,LifebloodSnapshots> snapshots{};
 std::bitset<LifebloodSnapshots> stored{};
 int activeSnapshot=-1;
 // Session settings.
 uint16_t tempo=120;               // tempo-synced effects; external MIDI clock overrides
 uint8_t character=CharacterAny;   // recipe X grows from
 uint8_t maxNodes=6;               // 3 .. LifebloodMaxNodes
 uint8_t growth=40;                // angiogenesis speed
 uint8_t mix=100;                  // network wet; the rest is the dry input
 uint8_t path=PathRhythmic;        // ABSTRACT or RHYTHMIC recipes
 uint32_t seed=0x11feb10d;
 Angiogenesis angiogenesis{};      // runtime only
};
// Session settings rows: TEMPO, PATH, CHARACTER, NODES, GROWTH, MIX.
constexpr int LifebloodSettingsFields=6;
std::string lifebloodPath(const std::string&root,int slot);
bool saveLifeblood(const LifebloodSession&,const std::string&path,std::string&error);
bool loadLifeblood(LifebloodSession&,const std::string&path,std::string&error);
std::array<uint8_t,LifebloodSessionSlots> scanLifeblood(const std::string&root);
// Short node label for the field (e.g. "SPACE", "QDLY").
const char *lifebloodLabel(FxType);
}
