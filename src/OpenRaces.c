/*
 * OpenRaces.asi - Need for Speed: Most Wanted (2012), NFS13.exe v1.4.0.0
 *
 *  - Opponent randomizer: every AI racer gets a random car (and paint) from the
 *    player's car class. Most Wanted races are left alone.
 *  - Open races: every car can run every event (map + free roam). Parts are only
 *    awarded when the event is run in one of its original cars; EasyDrive, part
 *    unlocks and the "NEW" badge keep using the original per-car event lists.
 *
 * Key hook points (game addresses, v1.4.0.0):
 *   0x00595B60  SetSlotVehicle(slot, id, livery, flag)   - opponent car assignment
 *   0x0057BC70  vehicle bundle loader                    - version check only
 *   0x006CC9A1  racer setup (racer record car id)        - random car chosen here, before the racer is configured
 *   0x0075C3D1 / 0x0075C497  race car preloader lists    - original opponents replaced, so they are not loaded too
 *   race intro sequences (AddRaceCarSequenceItem data)   - opponents named by car id are pointed at the new cars
 *   0x006E65E0  post-event awards                        - part rewards filter
 *   0x006ECD70  car's own event list (max 10)            - replaced, original lists
 *   0x006ECA10  event/car membership                     - EasyDrive Races caller gets original lists
 *   0x00702710  car event state ("NEW" flag)             - flag recomputed
 *   0x0074A600  free-roam event markers (20 slots)       - filled with the nearest events
 *   0x00755220  marker update                            - refresh every 100 m
 */
typedef unsigned long DWORD; typedef int BOOL; typedef void* HANDLE; typedef unsigned long SIZE_T;
typedef unsigned char BYTE;
#define API __declspec(dllimport) __stdcall
API DWORD GetModuleFileNameA(HANDLE,char*,DWORD);
API HANDLE CreateThread(void*,SIZE_T,DWORD(__stdcall*)(void*),void*,DWORD,DWORD*);
API void Sleep(DWORD); API DWORD GetTickCount(void);
API HANDLE CreateFileA(const char*,DWORD,DWORD,void*,DWORD,DWORD,HANDLE);
API BOOL WriteFile(HANDLE,const void*,DWORD,DWORD*,void*); API BOOL FlushFileBuffers(HANDLE);
API BOOL VirtualProtect(void*,SIZE_T,DWORD,DWORD*); API void* VirtualAlloc(void*,SIZE_T,DWORD,DWORD);
API BOOL FlushInstructionCache(HANDLE,const void*,SIZE_T); API HANDLE GetCurrentProcess(void);
API BOOL ReadProcessMemory(HANDLE,const void*,void*,SIZE_T,SIZE_T*); API BOOL WriteProcessMemory(HANDLE,void*,const void*,SIZE_T,SIZE_T*);
API unsigned GetPrivateProfileIntA(const char*,const char*,int,const char*);
API BOOL QueryPerformanceCounter(long long*);
API void* AddVectoredExceptionHandler(unsigned long,long(__stdcall*)(void*));
typedef struct { void* BaseAddress; void* AllocationBase; DWORD AllocationProtect; SIZE_T RegionSize; DWORD State; DWORD Protect; DWORD Type; } MBI;
API SIZE_T VirtualQuery(const void*,MBI*,SIZE_T);
__declspec(dllimport) int __cdecl wsprintfA(char*,const char*,...);

int _fltused=0;
#include "cars.h"
#define NCARS ((int)(sizeof(kCars)/sizeof(kCars[0])))

#define LOAD_ADDR 0x0057BC70
static const BYTE kExpected[16]={0x81,0xEC,0x38,0x03,0x00,0x00,0x8B,0x94,0x24,0x44,0x03,0x00,0x00,0x53,0x55,0x56};

static HANDLE g_self, g_log=(HANDLE)-1;
static char g_dir[520];
static int g_enabled=1, g_noDupes=1, g_avoidPlayer=1, g_logOn=1, g_skipMW=1, g_paint=1;
/* Generic paint colours the game itself puts on AI racers of many different cars. */
static const int kPaints[]={1197606,1197607,1197608,1197609,1197610,1197611,1197612,1197613,1197614,1197615,1197616,1197617,
                            1454600,1454601,1454602,1454603,1454604,1454605,1454606,1454607,1454608};
#define NPAINTS ((int)(sizeof(kPaints)/sizeof(kPaints[0])))
static int g_playerId=0;
static DWORD g_t0=0;
static DWORD g_seqFixUntil=0, g_seqNext=0; static int g_seqFixOn=1;
static int g_used[64]; static int g_nUsed=0; static DWORD g_lastPick=0;
static unsigned g_rng=0x9E3779B9u;

typedef int (__fastcall *LoadFn)(void*,void*,int,int,int,int,int);
typedef int (__fastcall *SetSlotFn)(void*,void*,int,int,int,int);
typedef BYTE (__fastcall *Fn1)(void*,void*,int);
static SetSlotFn g_origSet;

#define SET_ADDR 0x00595B60
static const BYTE kExpSet[16]={0x83,0xEC,0x10,0x53,0x55,0x56,0x8B,0x74,0x24,0x20,0x8B,0xEE,0x69,0xED,0x40,0x1C};

static int slen(const char* s){int n=0;while(s[n])n++;return n;}
static void logf_(const char* s){
  if(!g_logOn||g_log==(HANDLE)-1)return; DWORD w;
  WriteFile(g_log,s,slen(s),&w,0); WriteFile(g_log,"\r\n",2,&w,0); FlushFileBuffers(g_log);
}
static unsigned rnd(void){ g_rng^=g_rng<<13; g_rng^=g_rng>>17; g_rng^=g_rng<<5; return g_rng; }
static const Car* find(int id){ for(int i=0;i<NCARS;i++) if(kCars[i].id==id) return &kCars[i]; return 0; }
static int used(int id){ for(int i=0;i<g_nUsed;i++) if(g_used[i]==id) return 1; return 0; }

static int pick(int cls){
  int pool[NCARS], n=0;
  for(int pass=0;pass<3&&n==0;pass++){
    for(int i=0;i<NCARS;i++){
      const Car* c=&kCars[i]; if(c->cls!=cls) continue;
      if(pass<1 && g_noDupes && used(c->id)) continue;
      if(pass<2 && g_avoidPlayer && c->id==g_playerId) continue;
      pool[n++]=c->id;
    }
  }
  return n? pool[rnd()%n] : 0;
}

/* Same test the game uses inside SetSlotVehicle to decide "this slot is the local player". */
static int isLocalPlayerSlot(BYTE* self,int slot){
  int pid=*(int*)(self+slot*0x1C40+0x4AD0);
  if(pid==-1) return 0;
  void* net=*(void**)0x010EDAD0; if(!net) return slot==0;
  if(!((Fn1)0x006A11C0)(net,0,pid)) return 0;
  return ((int(__fastcall*)(void*,void*,int))0x0069FF70)(net,0,pid)==0;
}

static int readable(void* p){ MBI m;
  return VirtualQuery(p,&m,sizeof m)&&m.State==0x1000&&!(m.Protect&0x101)&&m.Protect!=1; }
#define MAXSLOT 32
#define MW_AI_PROFILE 0x001601B1
static int g_slotOrig[MAXSLOT], g_slotNew[MAXSLOT], g_slotPaint[MAXSLOT]; static DWORD g_slotTime[MAXSLOT];

/* SetSlotVehicle(slot, vehicleId, liveryId, flag) - __thiscall, ret 0x10 */
/* Pre-randomization: each racer is configured for its car (0x6CA580) from its racer record BEFORE the
   car is assigned to a slot. Pick the random car there, so the racer is set up for the car it will drive. */
typedef struct { BYTE* rec; int orig, now_; DWORD t; void* game; } PreMap;
static PreMap g_pm[32]; static int g_npm=0;
typedef struct { BYTE* rec; int orig; } RecOrig; static RecOrig g_recOrig[256]; static int g_nRecOrig=0;
static PreMap* pmFind(int newId){ DWORD now=GetTickCount();
  for(int i=0;i<g_npm;i++) if(g_pm[i].now_==newId && now-g_pm[i].t<120000) return &g_pm[i]; return 0; }
/* 0x68E000 = racer record refresh (thiscall rec, arg game): re-validates the record's five
   customisation entries (+0x70..+0xA8: parts / livery) against the record's car (+0x30). */
typedef void (__fastcall *RecRefreshFn)(BYTE*,void*,void*);
static void refreshRecord(BYTE* rec,void* game){
  if(!rec || !game || !readable(rec+0xA8) || !readable(game)) return;
  ((RecRefreshFn)0x0068E000)(rec,0,game);
}
static void* volatile g_game=0;
void __cdecl premap(int* idp,BYTE* rec,void* game){
  if(game) g_game=game;
  if(!idp) return;
  int id=*idp; char b[200];
  /* The racer record keeps the car we gave it last time (+0x30 is written below), so remember each record's
     real car the first time we see it and always start from that. */
  if(rec){ int k; for(k=0;k<g_nRecOrig;k++) if(g_recOrig[k].rec==rec) break;
    if(k<g_nRecOrig){ if(id!=g_recOrig[k].orig){ id=g_recOrig[k].orig; *idp=id;
        if(readable(rec+0x30)){ *(int*)(rec+0x30)=id; if(!g_enabled) refreshRecord(rec,game); } } }
    else if(g_nRecOrig<256){ g_recOrig[g_nRecOrig].rec=rec; g_recOrig[g_nRecOrig].orig=id; g_nRecOrig++; } }
  if(!g_enabled || !g_playerId) return;
  const Car* c=find(id); const Car* pl=find(g_playerId);
  if(!c || !pl) return;
  if(rec && readable(rec+0x30) && *(int*)(rec+0x30)!=id) return;
  DWORD now=GetTickCount();
  if(now-g_lastPick>8000){ g_nUsed=0; g_npm=0; } g_lastPick=now;
  int r=pick(pl->cls); if(!r) return;
  if(g_nUsed<64) g_used[g_nUsed++]=r;
  *idp=r; if(rec){ *(int*)(rec+0x30)=r; refreshRecord(rec,game); }
  g_seqFixUntil=now+40000; g_seqNext=0;
  if(g_npm<32){ g_pm[g_npm].rec=rec; g_pm[g_npm].orig=id; g_pm[g_npm].now_=r; g_pm[g_npm].t=now; g_pm[g_npm].game=game; g_npm++; }
  DWORD t=now-g_t0; wsprintfA(b,"[racer setup %d.%ds] %d (%s) -> %d (%s)",t/1000,(t/100)%10,id,c->name,r,find(r)->name); logf_(b);
}
#define PM_ADDR 0x006CC9A1
static const BYTE kExpPm[8]={0x8B,0x17,0x8B,0x4C,0x24,0x24,0x8B,0x00};
volatile unsigned g_pmRet=0x006CC9A9;
void PmStub(void);
__asm__(".globl _PmStub\n_PmStub:\n"
  "  pushl %eax\n  pushl %ecx\n  pushl %edx\n"
  "  pushl 0x94(%esi)\n  pushl 0x57c(%esi)\n  pushl %eax\n  call _premap\n  addl $12,%esp\n"
  "  popl %edx\n  popl %ecx\n  popl %eax\n"
  "  movl (%edi),%edx\n  movl 0x24(%esp),%ecx\n  movl (%eax),%eax\n"
  "  jmp *_g_pmRet\n");

static int __fastcall HookSet(void* self,void* edx,int slot,int id,int livery,int flag){
  char b[256];
  if(slot<0||slot>=MAXSLOT) return g_origSet(self,edx,slot,id,livery,flag);
  if(slot==0 || isLocalPlayerSlot((BYTE*)self,slot)){
    if(id!=g_playerId){ const Car* c=find(id);
      wsprintfA(b,"[player] slot %d: %d (%s) class %s",slot,id,c?c->name:"?",c?kClassNames[c->cls]:"unknown"); logf_(b); }
    g_playerId=id; g_slotOrig[slot]=g_slotNew[slot]=0;
    return g_origSet(self,edx,slot,id,livery,flag);
  }
  /* Most Wanted races: the boss racer uses the Most Wanted AI driver profile (attrib 1442225).
     It is set on the slot (+0x1A34) just before the car is assigned. */
  if(g_skipMW && *(int*)((BYTE*)self+slot*0x1C40+0x3050+0x1A34)==MW_AI_PROFILE){
    g_slotOrig[slot]=g_slotNew[slot]=0;
    { PreMap* pm=pmFind(id); if(pm){ if(pm->rec){ *(int*)(pm->rec+0x30)=pm->orig; refreshRecord(pm->rec,pm->game); } id=pm->orig; pm->now_=0; } }
    wsprintfA(b,"[opponent] slot %d: %d (%s) unchanged [Most Wanted race]",slot,id,find(id)?find(id)->name:"?"); logf_(b);
    return g_origSet(self,edx,slot,id,livery,flag);
  }
  DWORD now=GetTickCount();
  /* The game re-applies a slot's stored car at times; never re-roll a car we already chose. */
  if(id && id==g_slotNew[slot] && now-g_slotTime[slot]<30000) { g_slotTime[slot]=now; return g_origSet(self,edx,slot,id,g_slotPaint[slot],flag); }
  if(id && id==g_slotOrig[slot] && now-g_slotTime[slot]<30000){
    g_slotTime[slot]=now; return g_origSet(self,edx,slot,g_slotNew[slot],g_slotPaint[slot],flag); }
  const Car* orig=find(id); const Car* pl=find(g_playerId);
  if(!g_enabled||!orig||!pl){
    wsprintfA(b,"[opponent] slot %d: %d (%s) unchanged%s",slot,id,orig?orig->name:"not a racer car",
              !g_enabled?" [disabled]":!orig?" [not in list]":" [player car unknown]"); logf_(b);
    return g_origSet(self,edx,slot,id,livery,flag);
  }
  if(now-g_lastPick>8000) g_nUsed=0; g_lastPick=now;
  /* Only race opponents are randomized. Their car is picked at racer setup (premap); anything else that
     comes through here (free-roam jack spot cars, traffic specials) keeps the game's own car. */
  PreMap* pm=pmFind(id);
  if(!pm){
    g_slotOrig[slot]=g_slotNew[slot]=0;
    wsprintfA(b,"[vehicle] slot %d: %d (%s) unchanged [not a race opponent]",slot,id,orig->name); logf_(b);
    return g_origSet(self,edx,slot,id,livery,flag);
  }
  int r=id; id=pm->orig; orig=find(id); if(!orig) orig=find(r);   /* chosen at racer setup already */
  int paint = g_paint ? kPaints[rnd()%NPAINTS] : ((r==id)?livery:0);
  g_slotOrig[slot]=id; g_slotNew[slot]=r; g_slotPaint[slot]=paint; g_slotTime[slot]=now;
  { DWORD t=now-g_t0; wsprintfA(b,"[opponent %d.%ds] slot %d: %d (%s) -> %d (%s), paint %d",t/1000,(t/100)%10,slot,id,orig->name,r,find(r)?find(r)->name:"?",paint); logf_(b); }
  return g_origSet(self,edx,slot,r,paint,flag);
}

/* The race's opponent preloader (0x75C1C0) builds a list of cars to keep loaded from the event's
   ORIGINAL opponent list, then adds the actual racers. With randomized opponents that loads every
   original car as well (up to 11 extra car bundles), which overloads the loader in big races
   (Round Trip: hang or crash during fix-up). Swap each original id for the car we put in its slot. */
#define PRE_ADDR 0x0075C3D1
static const BYTE kExpPre[9]={0x8B,0x10,0x89,0x94,0x8E,0xCC,0x00,0x00,0x00};
volatile unsigned g_preRet=0x0075C3DA, g_preSkip=0x0075C3E0;
int __cdecl mapPreload(int id){
  DWORD now=GetTickCount();
  for(int s=1;s<MAXSLOT;s++) if(id && g_slotOrig[s]==id && g_slotNew[s] && now-g_slotTime[s]<120000) return g_slotNew[s];
  /* Not (yet) assigned by us: drop it. The real racers are added to the same list separately,
     so dropping the event's original cars only avoids loading cars that will not race. */
  if(g_enabled && find(id)) return 0;
  return id;
}
/* Second list source (0x75C497): the racer entities. Their id is still the event's original car. */
#define PRE2_ADDR 0x0075C497
volatile unsigned g_pre2Ret=0x0075C4A0, g_pre2Skip=0x0075C4A6;
/* obj = the racer's vehicle record; its car id is at +0x30 (read by 0x6788D0). Round Trip spawns its
   racers from that record, so it must name the car we actually loaded, not the event's original. */
int __cdecl mapPreload2(int id,BYTE* obj){
  DWORD now=GetTickCount();
  for(int i=0;i<g_npm;i++) if(g_pm[i].rec==obj && now-g_pm[i].t<120000) return id;   /* set up already */
  for(int s=1;s<MAXSLOT;s++) if(id && g_slotOrig[s]==id && g_slotNew[s] && now-g_slotTime[s]<120000){
    int r=g_slotNew[s];
    if(r!=id && obj && readable(obj+0x30) && *(int*)(obj+0x30)==id) *(int*)(obj+0x30)=r;
    return r; }
  return id;
}
void Pre2Stub(void);
__asm__(".globl _Pre2Stub\n_Pre2Stub:\n"
  "  pushl %ecx\n  pushl %eax\n  pushl %edi\n  pushl (%eax)\n  call _mapPreload2\n  addl $8,%esp\n  movl %eax,%edx\n"
  "  popl %eax\n  popl %ecx\n  testl %edx,%edx\n  jz 1f\n  movl %edx,0xcc(%esi,%ecx,4)\n  jmp *_g_pre2Ret\n"
  "1:\n  jmp *_g_pre2Skip\n");
void PreStub(void);
__asm__(".globl _PreStub\n_PreStub:\n"
  "  pushl %ecx\n  pushl %eax\n  pushl (%eax)\n  call _mapPreload\n  addl $4,%esp\n  movl %eax,%edx\n"
  "  popl %eax\n  popl %ecx\n  testl %edx,%edx\n  jz 1f\n  movl %edx,0xcc(%esi,%ecx,4)\n  jmp *_g_preRet\n"
  "1:\n  jmp *_g_preSkip\n");

static BYTE g_scanBuf[0x10000+0x40];
typedef struct { BYTE* it; int orig, now_; } ItemOrig; static ItemOrig g_itemOrig[64]; static int g_nItemOrig=0;
static void seqFixScan(void){
  if(!g_seqFixOn) return;
  DWORD now=GetTickCount(); if(!g_seqFixUntil || now>g_seqFixUntil || now<g_seqNext) return; g_seqNext=now+1000;
  HANDLE me=GetCurrentProcess(); BYTE* a=(BYTE*)0x10000; MBI m; char l[300];
  while((unsigned)a<0x7FFE0000 && VirtualQuery(a,&m,sizeof m)){
    BYTE* base=(BYTE*)m.BaseAddress; SIZE_T sz=m.RegionSize;
    if(m.State==0x1000 && (m.Protect&0xCC) && !(m.Protect&0x101) && sz>=0x40){
      for(SIZE_T c=0;c<sz;c+=0x10000){
        SIZE_T n=sz-c; if(n>0x10000+0x40) n=0x10000+0x40; SIZE_T got=0;
        if(!ReadProcessMemory(me,base+c,g_scanBuf,n,&got) || got<0x40) continue;
        SIZE_T lim=got<0x10000+0x40?got:0x10000+0x40; if(c+0x10000<sz && lim>0x10000) lim=0x10000+0x30;
        for(SIZE_T o=8;o+0x30<=lim && o<0x10000+8;o+=4){
          if(*(unsigned*)(g_scanBuf+o)!=0x676F92C5u || *(unsigned*)(g_scanBuf+o+4)!=0x30) continue;
          BYTE* it=base+c+o-8; BYTE* bi=g_scanBuf+o-8; if(*(int*)(bi+0x28)!=1) continue;
          int car=*(int*)(bi+0x2C);
          { int k; for(k=0;k<g_nItemOrig;k++) if(g_itemOrig[k].it==it && g_itemOrig[k].now_==car) { car=g_itemOrig[k].orig; break; } }
          for(int i=0;i<g_npm;i++) if(g_pm[i].orig==car && g_pm[i].now_ && g_pm[i].now_!=car && now-g_pm[i].t<180000){
            int nv=g_pm[i].now_; SIZE_T w=0;
            if(nv==*(int*)(bi+0x2C)) break;
            if(WriteProcessMemory(me,it+0x2C,&nv,4,&w) && w==4){
              { int k; for(k=0;k<g_nItemOrig;k++) if(g_itemOrig[k].it==it) break;
                if(k==g_nItemOrig && g_nItemOrig<64) g_nItemOrig++;
                if(k<64){ g_itemOrig[k].it=it; g_itemOrig[k].orig=car; g_itemOrig[k].now_=nv; } }
              DWORD t=now-g_t0; wsprintfA(l,"[intro fix %d.%ds] race intro item %08X (id %06X): car %d (%s) -> %d (%s)",t/1000,(t/100)%10,(unsigned)it,
                *(unsigned*)(bi+0x14),car,find(car)?find(car)->name:"?",nv,find(nv)?find(nv)->name:"?"); logf_(l); }
            break; } } }
    }
    a=base+sz; if(!sz) break;
  }
}

static int matches(const BYTE* p,const BYTE* e){ for(int i=0;i<16;i++) if(p[i]!=e[i]) return 0; return 1; }
static void* hook(BYTE* p,int n,void* fn){
  BYTE* tr=(BYTE*)VirtualAlloc(0,32,0x3000,0x40);
  for(int i=0;i<n;i++) tr[i]=p[i];
  tr[n]=0xE9; *(int*)(tr+n+1)=(int)(p+n)-(int)(tr+n+5);
  DWORD old; VirtualProtect(p,n,0x40,&old);
  p[0]=0xE9; *(int*)(p+1)=(int)(BYTE*)fn-(int)(p+5); for(int i=5;i<n;i++) p[i]=0x90;
  VirtualProtect(p,n,old,&old); FlushInstructionCache(GetCurrentProcess(),p,n);
  return tr;
}

/* ===================== Every event for every car =====================
 * The EasyDrive event table (GAMEPLAY.BNDL, resource F7_30_0F_00) lists, for each event entry,
 * the cars allowed to run it. In memory: root(0x01177868)+0x14C84/0x14C88 = attrib handle of the table;
 * table+0x10 = record pointer array, +0x14 = u16 count. record+0x0C = entry key, +0x10 = car array,
 * +0x14 = u16 car count. Car slots are 0x2C bytes, car id at +0x0C.
 * We give every entry a longer car list (originals first, then every other car, cloned from the
 * first slot). The original lists are remembered so parts are only awarded to original cars.     */
static int g_allEvents=1;
static unsigned g_callers[64]; static int g_nCallers=0; static int g_newFlagFix=1;
#define MAXREC 256
#define MAXCARS 128
typedef struct { int key; int n; int cars[24]; BYTE* myArr; BYTE* origArr; BYTE* rec; int myN; } OrigRec;
static OrigRec g_orig[MAXREC]; static int g_nOrig=0;
static BYTE* g_pool=0; static int g_poolUsed=0, g_poolSize=0;

static OrigRec* origFor(int key){ for(int i=0;i<g_nOrig;i++) if(g_orig[i].key==key) return &g_orig[i]; return 0; }
static int inOrig(OrigRec* o,int car){ for(int i=0;i<o->n;i++) if(o->cars[i]==car) return 1; return 0; }

static BYTE* eventTable(int* count){
  BYTE* root=*(BYTE**)0x01177868; if(!root||!readable(root+0x14C84)) return 0;
  unsigned h=*(unsigned*)(root+0x14C84), off=*(unsigned*)(root+0x14C88);
  if(!h||!readable((void*)h)) return 0;
  unsigned base=*(unsigned*)(h+0x28); if(!base) return 0;
  BYTE* t=(BYTE*)(base+off); if(!readable(t)) return 0;
  int n=*(unsigned short*)(t+0x14); BYTE** recs=*(BYTE***)(t+0x10);
  if(n<=0||n>MAXREC||!readable(recs)) return 0;
  *count=n; return t;
}

/* returns number of entries patched (0 = nothing to do / not ready) */
static int patchEvents(void){
  int n; BYTE* t=eventTable(&n); if(!t) return 0;
  BYTE** recs=*(BYTE***)(t+0x10);
  /* sanity + collect every car */
  int all[MAXCARS], nall=0;
  for(int i=0;i<n;i++){
    BYTE* r=recs[i]; if(!readable(r)) return 0;
    int key=*(int*)(r+0xC); BYTE* arr=*(BYTE**)(r+0x10); int c=*(unsigned short*)(r+0x14);
    OrigRec* o=origFor(key);
    if(o && (arr==o->myArr || arr==o->origArr)) continue;   /* already ours (or temporarily swapped back) */
    if(c<=0||c>24||!readable(arr)||!readable(arr+c*0x2C-1)) return 0;
    for(int k=0;k<c;k++){ int car=*(int*)(arr+k*0x2C+0xC); if(car<=0||car>0x7FFFFFF) return 0;
      int seen=0; for(int m=0;m<nall;m++) if(all[m]==car){seen=1;break;}
      if(!seen&&nall<MAXCARS) all[nall++]=car; }
  }
  /* original cars from earlier patch rounds count too */
  for(int i=0;i<g_nOrig;i++) for(int k=0;k<g_orig[i].n;k++){ int car=g_orig[i].cars[k], seen=0;
    for(int m=0;m<nall;m++) if(all[m]==car){seen=1;break;} if(!seen&&nall<MAXCARS) all[nall++]=car; }
  int need=n*nall*0x2C;
  if(!g_pool||g_poolSize-g_poolUsed<need){ g_pool=(BYTE*)VirtualAlloc(0,need,0x3000,0x04); g_poolUsed=0; g_poolSize=g_pool?need:0; }
  if(!g_pool) return 0;
  int patched=0;
  for(int i=0;i<n;i++){
    BYTE* r=recs[i]; int key=*(int*)(r+0xC); BYTE* arr=*(BYTE**)(r+0x10); int c=*(unsigned short*)(r+0x14);
    OrigRec* o=origFor(key);
    if(o && (arr==o->myArr || arr==o->origArr)) continue;
    if(!o){ if(g_nOrig>=MAXREC) continue; o=&g_orig[g_nOrig++]; o->key=key; o->n=c;
            for(int k=0;k<c;k++) o->cars[k]=*(int*)(arr+k*0x2C+0xC); }
    BYTE* na=g_pool+g_poolUsed; int nc=0;
    for(int k=0;k<c;k++,nc++) for(int b=0;b<0x2C;b++) na[nc*0x2C+b]=arr[k*0x2C+b];
    for(int m=0;m<nall;m++){ int car=all[m], have=0;
      for(int k=0;k<c;k++) if(*(int*)(arr+k*0x2C+0xC)==car){have=1;break;}
      if(have) continue;
      for(int b=0;b<0x2C;b++) na[nc*0x2C+b]=arr[b];      /* clone first slot */
      *(int*)(na+nc*0x2C+0xC)=car;
      *(unsigned short*)(na+nc*0x2C+0x24)=0;            /* this slot unlocks no parts */
      nc++; }
    g_poolUsed+=nc*0x2C; o->myArr=na; o->origArr=arr; o->rec=r; o->myN=nc;
    { DWORD old; VirtualProtect(r+0x10,8,0x04,&old);   /* make sure the record is writable */
      *(BYTE**)(r+0x10)=na;                              /* superset first, then grow the count */
      *(unsigned short*)(r+0x14)=(unsigned short)nc;
      if(old!=0x04 && old!=0x40) VirtualProtect(r+0x10,8,old,&old); }
    patched++;
  }
  if(patched){ char b[128]; wsprintfA(b,"[events] %d event entries opened to all %d cars",patched,nall); logf_(b); }
  return patched;
}

typedef void (__fastcall *AwardFn)(void*,void*);
typedef int* (__fastcall *LookupFn)(void*,void*,int*,int);
static AwardFn g_origAward;
static void __fastcall HookAward(void* self,void* edx){
  BYTE* me=(BYTE*)self; char b[200];
  int ev=*(int*)(me+0x2CE0), cnt=*(int*)(me+0x38B4);
  if(g_allEvents && ev && cnt>0 && g_nOrig){
    void* lk=*(void**)(me+4); int out[2]={0,0};
    if(lk && readable(lk)){
      ((LookupFn)0x006E5DC0)(lk,0,out,ev);
      if(out[0] && readable((void*)out[0])){
        BYTE* data=(BYTE*)(*(unsigned*)(out[0]+0x28)+out[1]);
        if(readable(data+0x5C)){
          OrigRec* o=origFor(*(int*)(data+0x5C)); const Car* c=find(g_playerId);
          if(o && !inOrig(o,g_playerId)){
            *(int*)(me+0x38B4)=0;
            wsprintfA(b,"[events] event %d finished in %s (not its original car) - %d part reward(s) withheld",ev,c?c->name:"?",cnt); logf_(b);
          } else if(o){
            wsprintfA(b,"[events] event %d finished in %s (original car) - parts awarded",ev,c?c->name:"?"); logf_(b);
          }
        }
      }
    }
  }
  g_origAward(self,edx);
}
/* ---- keep per-car data on the ORIGINAL lists ---- */
typedef BYTE (__fastcall *HasAttrFn)(void*,void*,int);
/* Replacement for 0x6ECD70: "events that belong to car X" (max 10). Used for part unlocks and Easydrive.
   Same logic as the game, but membership comes from the original (unexpanded) lists. */
static int tableMember(BYTE* self,int key,int car){
  unsigned h=*(unsigned*)(self+0x7C), off=*(unsigned*)(self+0x80); if(!h) return 0;
  BYTE* t=(BYTE*)(*(unsigned*)(h+0x28)+off); int n=*(unsigned short*)(t+0x14); BYTE** recs=*(BYTE***)(t+0x10);
  for(int i=0;i<n;i++) if(*(int*)(recs[i]+0xC)==key){
    BYTE* arr=*(BYTE**)(recs[i]+0x10); int c=*(unsigned short*)(recs[i]+0x14);
    for(int k=0;k<c;k++){ int id=*(int*)(arr+k*0x2C+0xC); if(id && id==car) return 1; } return 0; }
  return 0;
}
static void __fastcall MyCarEvents(BYTE* self,void* edx,BYTE* out,int car){
  BYTE* root=*(BYTE**)(self+0xA4); BYTE* edm=*(BYTE**)(root+0x2F3FC); BYTE* lk=*(BYTE**)(edm+4);
  unsigned h=*(unsigned*)(lk+0x11C), off=*(unsigned*)(lk+0x120);
  BYTE* data=(BYTE*)(*(unsigned*)(h+0x28)+off); int n=*(unsigned short*)(data+0x14);
  for(int i=0;i<n;i++){
    BYTE* ev=(*(BYTE***)(data+0x10))[i];
    BYTE* coll=(BYTE*)(*(unsigned*)(*(unsigned*)ev+0x28)+*(unsigned*)(ev+4));
    if(!((HasAttrFn)0x00AC54C0)(coll,0,0x5F3F3)) continue;
    int key=*(int*)(ev+0x5C); OrigRec* o=origFor(key);
    int member = o ? inOrig(o,car) : tableMember(self,key,car);
    if(member && ev[0xEA]){ int c=*(int*)(out+0x28); if(c!=10){ ((BYTE**)out)[c]=ev; *(int*)(out+0x28)=c+1; } }
  }
}
/* Guards: these per-event lookups crash if asked about an event the game has no data for. */
typedef int (__fastcall *Ev3Fn)(void*,void*,void*,int,int);
typedef int (__fastcall *Ev2Fn)(void*,void*,void*,int);
static Ev3Fn g_origDiff, g_origRewardType; static Ev2Fn g_origMinDiff;
static int eventKnown(void* lk,int ev){ int out[2]={0,0}; if(!lk) return 0;
  ((LookupFn)0x006E5DC0)(lk,0,out,ev); return out[0] && readable((void*)out[0]) && *(unsigned*)(out[0]+0x28); }
static int __fastcall GuardDiff(void* lk,void* edx,void* root,int ev,int car){ return eventKnown(lk,ev)?g_origDiff(lk,edx,root,ev,car):-1; }
static int __fastcall GuardReward(void* lk,void* edx,void* root,int ev,int car){ return eventKnown(lk,ev)?g_origRewardType(lk,edx,root,ev,car):0; }
static int __fastcall GuardMinDiff(void* lk,void* edx,void* root,int ev){ return eventKnown(lk,ev)?g_origMinDiff(lk,edx,root,ev):-1; }
static void installEventFixes(void){
  static const BYTE pre[7]={0x8B,0x44,0x24,0x08,0x83,0xEC,0x08};
  static const BYTE ce[6]={0x83,0xEC,0x0C,0x55,0x8B,0xE9};
  BYTE *d=(BYTE*)0x006F0980,*m=(BYTE*)0x006F0A20,*r=(BYTE*)0x006F0B60,*c=(BYTE*)0x006ECD70;
  for(int i=0;i<7;i++) if(d[i]!=pre[i]||m[i]!=pre[i]||r[i]!=pre[i]){ logf_("[events] guard targets not found"); return; }
  for(int i=0;i<6;i++) if(c[i]!=ce[i]){ logf_("[events] car-event list builder not found"); return; }
  g_origDiff=(Ev3Fn)hook(d,7,(void*)GuardDiff);
  g_origMinDiff=(Ev2Fn)hook(m,7,(void*)GuardMinDiff);
  g_origRewardType=(Ev3Fn)hook(r,7,(void*)GuardReward);
  hook(c,6,(void*)MyCarEvents);
}

/* Show the original lists while some game code runs (Easydrive / "NEW" flags), then expand again. */
static volatile int g_origView=0;
static void setView(int original){
  for(int i=0;i<g_nOrig;i++){ OrigRec* o=&g_orig[i]; if(!o->rec||!o->myArr) continue;
    if(original){ *(unsigned short*)(o->rec+0x14)=(unsigned short)o->n; *(BYTE**)(o->rec+0x10)=o->origArr; }
    else        { *(BYTE**)(o->rec+0x10)=o->myArr;  *(unsigned short*)(o->rec+0x14)=(unsigned short)o->myN; } }
}
typedef void (__fastcall *Thiscall0)(void*,void*);
static Thiscall0 g_origNewFlags;
/* After the game refreshes the car's event state (map + "NEW" flags) on the expanded lists,
   recompute just the "car has new events" flag (+0x229) using the original lists. */
typedef void* (__fastcall *GetObjFn)(void*,void*);
typedef void* (__fastcall *GetCarFn)(void*,void*,int*);
typedef BYTE (__fastcall *Bool1Fn)(void*,void*,int);
static void __fastcall HookNewFlags(void* self,void* edx){
  g_origNewFlags(self,edx);
  if(!g_nOrig || !g_newFlagFix) return;
  BYTE* me=(BYTE*)self; BYTE* root=*(BYTE**)(me+0xA4); if(!root) return;
  int carbuf[4]={0,0,0,0};
  void* vo=((GetObjFn)0x006BF330)(*(void**)(root+0x2F3DC),0);
  ((GetCarFn)0x006BA320)(vo,0,carbuf);
  int car=carbuf[0];
  BYTE* lk=*(BYTE**)(*(BYTE**)(root+0x2F3FC)+4);
  unsigned h=*(unsigned*)(lk+0x11C), off=*(unsigned*)(lk+0x120);
  BYTE* data=(BYTE*)(*(unsigned*)(h+0x28)+off); int n=*(unsigned short*)(data+0x14);
  BYTE flag=0;
  for(int i=0;i<n && !flag;i++){
    BYTE* ev=(*(BYTE***)(data+0x10))[i];
    BYTE* coll=(BYTE*)(*(unsigned*)(*(unsigned*)ev+0x28)+*(unsigned*)(ev+4));
    if(!((HasAttrFn)0x00AC54C0)(coll,0,0x5F3F3)) continue;
    int evId=*(int*)(ev+0x18);
    if(!((Bool1Fn)0x0068F190)(root+0x29200,0,evId)) continue;
    OrigRec* o=origFor(*(int*)(ev+0x5C)); if(!o||!inOrig(o,car)) continue;
    if(ev[0xEB]) continue;
    if(((Bool1Fn)0x006F9FA0)(me,0,evId)) flag=1;
  }
  me[0x229]=flag;
}
/* membership check 0x6ECA10 (this, key, car, flag): Easydrive UI gets the original answer */
typedef BYTE (__fastcall *Member3Fn)(void*,void*,int,int,int);
static Member3Fn g_origMember2;
static BYTE __fastcall HookMember2(void* self,void* edx,int key,int car,int flag){
  unsigned ret=(unsigned)__builtin_return_address(0);
#ifdef EVENT_DIAG
  { int seen=0; for(int i=0;i<g_nCallers;i++) if(g_callers[i]==ret){seen=1;break;}
    if(!seen && g_nCallers<64){ char b[96]; g_callers[g_nCallers++]=ret; wsprintfA(b,"[diag] membership check called from %08X",ret); logf_(b); } }
#endif
  if(ret==0x00806E15){ OrigRec* o=origFor(key); if(o) return (BYTE)(inOrig(o,car)?1:0); }
  return g_origMember2(self,edx,key,car,flag);
}
static void installViewHooks(void){
  static const BYTE nf[5]={0x83,0xEC,0x2C,0x53,0x55};
  static const BYTE mb[9]={0x8B,0x41,0x7C,0x8B,0x89,0x80,0x00,0x00,0x00};
  BYTE* a=(BYTE*)0x00702710; BYTE* m=(BYTE*)0x006ECA10;
  int ok=1; for(int i=0;i<5;i++) if(a[i]!=nf[i]) ok=0; for(int i=0;i<9;i++) if(m[i]!=mb[i]) ok=0;
  if(!ok){ logf_("[events] view hooks not found"); return; }
  g_origNewFlags=(Thiscall0)hook(a,5,(void*)HookNewFlags);
  g_origMember2=(Member3Fn)hook(m,9,(void*)HookMember2);
}

/* ---- Free-roam / minimap markers: the game has room for 20 event markers. ----
   Keep them filled with the 20 events nearest the player and refresh as the player drives. */
#define MARKER_CAP 20
#define REFRESH_DIST 100.0f
typedef void (__fastcall *Thiscall0v)(void*,void*);
typedef int  (__fastcall *Thiscall0i)(void*,void*);
typedef int* (__fastcall *EvLocFn)(void*,void*,int*,int);
typedef float* (__fastcall *PosFn)(void*,void*,int);
static Thiscall0v g_origPlaceMarkers; static Thiscall0i g_origMarkerUpdate;
static float g_lastCenter[3]; static int g_haveCenter=0;
static BYTE** g_sortBuf=0; static int g_sortCap=0;

static BYTE* rootFromMarkers(BYTE* me){ BYTE* a=*(BYTE**)(me+0x18); if(!a) return 0; BYTE* b=*(BYTE**)(a+0x28); if(!b) return 0; return *(BYTE**)(b+4); }
static float* playerPos(BYTE* root){ BYTE* p=*(BYTE**)(root+0x2F480); return p?(float*)(p+0x2CD50):0; }
static float eventDist2(BYTE* lk,int evId,const float* pp){
  int out[2]={0,0}; ((EvLocFn)0x006E56F0)(lk,0,out,evId);
  void* posMgr=*(void**)0x010EDAF0; if(!posMgr) return 3.0e38f;
  float* p=((PosFn)0x0053C020)(posMgr,0,out[0]); if(!p) return 3.0e38f;
  float dx=p[0]-pp[0], dy=p[1]-pp[1], dz=p[2]-pp[2]; return dx*dx+dy*dy+dz*dz;
}
static void __fastcall HookPlaceMarkers(BYTE* me,void* edx){
  unsigned ret=(unsigned)__builtin_return_address(0);
  BYTE* root=rootFromMarkers(me); float* pp=root?playerPos(root):0;
  if(!g_allEvents || !root || !pp){ g_origPlaceMarkers(me,edx); return; }
  BYTE* lk=*(BYTE**)(*(BYTE**)(root+0x2F3FC)+4);
  unsigned h=*(unsigned*)(lk+0x11C), off=*(unsigned*)(lk+0x120);
  BYTE* data=(BYTE*)(*(unsigned*)(h+0x28)+off); int n=*(unsigned short*)(data+0x14);
  BYTE** arr=*(BYTE***)(data+0x10);
  if(n<=0||n>1024){ g_origPlaceMarkers(me,edx); return; }
  if(g_sortCap<n){ g_sortBuf=(BYTE**)VirtualAlloc(0,(n+64)*8,0x3000,0x04); g_sortCap=g_sortBuf?n+64:0; }
  if(!g_sortBuf){ g_origPlaceMarkers(me,edx); return; }
  float* dist=(float*)(g_sortBuf+g_sortCap);            /* second half of the buffer */
  for(int i=0;i<n;i++){ g_sortBuf[i]=arr[i]; dist[i]=eventDist2(lk,*(int*)(arr[i]+0x18),pp); }
  for(int i=1;i<n;i++){ BYTE* e=g_sortBuf[i]; float dv=dist[i]; int j=i-1;      /* insertion sort, nearest first */
    while(j>=0 && dist[j]>dv){ g_sortBuf[j+1]=g_sortBuf[j]; dist[j+1]=dist[j]; j--; } g_sortBuf[j+1]=e; dist[j+1]=dv; }
  if(ret==0x00755362){
    /* refresh: every marker is flagged for removal. Drop the far ones now so near events fit;
       keep the ones still among the nearest events (the game re-confirms them). */
    int cnt=*(int*)(me+0x18C); BYTE keep[64];
    for(int m=0;m<cnt && m<64;m++){ BYTE* ent=me+0x4C+m*0x10; int id=*(int*)ent; keep[m]=0;
      for(int k=0;k<n && k<MARKER_CAP;k++) if(*(int*)(g_sortBuf[k]+0x18)==id){ keep[m]=1; break; }
      if(keep[m]) ent[0xC]=0; }
    ((Thiscall0v)0x00755160)(me,0);                        /* removes the far (still flagged) markers */
    cnt=*(int*)(me+0x18C);
    for(int m=0;m<cnt;m++) me[0x4C+m*0x10+0xC]=1;          /* survivors flagged again; the game unflags the ones it re-finds */
  }
  *(BYTE***)(data+0x10)=g_sortBuf;
  g_origPlaceMarkers(me,edx);
  *(BYTE***)(data+0x10)=arr;
}
static int __fastcall HookMarkerUpdate(BYTE* me,void* edx){
  if(g_allEvents){ BYTE* root=rootFromMarkers(me); float* pp=root?playerPos(root):0;
    if(pp){ float dx=pp[0]-g_lastCenter[0], dy=pp[1]-g_lastCenter[1], dz=pp[2]-g_lastCenter[2];
      if(!g_haveCenter || dx*dx+dy*dy+dz*dz > REFRESH_DIST*REFRESH_DIST){
        g_lastCenter[0]=pp[0]; g_lastCenter[1]=pp[1]; g_lastCenter[2]=pp[2];
        if(g_haveCenter) *(int*)(me+0x1E0)=-1;             /* makes the game rebuild its markers this frame */
        g_haveCenter=1; } } }
  return g_origMarkerUpdate(me,edx);
}
static void installMarkerHooks(void){
  static const BYTE pm[6]={0x55,0x8B,0xEC,0x83,0xE4,0xF0};
  static const BYTE mu[5]={0x83,0xEC,0x0C,0x53,0x56};
  static const BYTE rm[5]={0x83,0xEC,0x08,0x55,0x56};
  BYTE *a=(BYTE*)0x0074A600,*b=(BYTE*)0x00755220,*c=(BYTE*)0x00755160;
  for(int i=0;i<6;i++) if(a[i]!=pm[i]){ logf_("[events] marker functions not found"); return; }
  for(int i=0;i<5;i++) if(b[i]!=mu[i]||c[i]!=rm[i]){ logf_("[events] marker functions not found"); return; }
  g_origPlaceMarkers=(Thiscall0v)hook(a,6,(void*)HookPlaceMarkers);
  g_origMarkerUpdate=(Thiscall0i)hook(b,5,(void*)HookMarkerUpdate);
}

static const BYTE kExpAW[5]={0x51,0x55,0x56,0x8B,0xF1};
static const BYTE kExpLK[6]={0x53,0x55,0x8B,0x6C,0x24,0x10};
static void installEvents(void){
  if(!g_allEvents) return;
  BYTE* w=(BYTE*)0x006E65E0;
  for(int i=0;i<5;i++) if(w[i]!=kExpAW[i]){ logf_("[events] award function not found - feature disabled"); g_allEvents=0; return; }
  for(int i=0;i<6;i++) if(((BYTE*)0x006E5DC0)[i]!=kExpLK[i]){ logf_("[events] lookup function not found - feature disabled"); g_allEvents=0; return; }
  g_origAward=(AwardFn)hook(w,5,(void*)HookAward);
  installEventFixes();
  installViewHooks();
  installMarkerHooks();
}


static int install(void){
  BYTE* a=(BYTE*)LOAD_ADDR; BYTE* s=(BYTE*)SET_ADDR;
  if(!readable(a)||!readable(s)||!matches(a,kExpected)||!matches(s,kExpSet)) return 0;
  g_origSet=(SetSlotFn)hook(s,5,(void*)HookSet);
  { BYTE* f=(BYTE*)PRE_ADDR; int ok=readable(f); for(int i=0;ok&&i<9;i++) if(f[i]!=kExpPre[i]) ok=0;
    if(ok){ DWORD old; VirtualProtect(f,9,0x40,&old); f[0]=0xE9; *(int*)(f+1)=(int)(BYTE*)PreStub-(int)(f+5);
      for(int i=5;i<9;i++) f[i]=0x90; VirtualProtect(f,9,old,&old); FlushInstructionCache(GetCurrentProcess(),f,9); }
    else logf_("ERROR: preload hook not installed"); }
  { BYTE* f=(BYTE*)PRE2_ADDR; int ok=readable(f); for(int i=0;ok&&i<9;i++) if(f[i]!=kExpPre[i]) ok=0;
    if(ok){ DWORD old; VirtualProtect(f,9,0x40,&old); f[0]=0xE9; *(int*)(f+1)=(int)(BYTE*)Pre2Stub-(int)(f+5);
      for(int i=5;i<9;i++) f[i]=0x90; VirtualProtect(f,9,old,&old); FlushInstructionCache(GetCurrentProcess(),f,9); }
    else logf_("ERROR: preload hook 2 not installed"); }
  { BYTE* f=(BYTE*)PM_ADDR; int ok=readable(f); for(int i=0;ok&&i<8;i++) if(f[i]!=kExpPm[i]) ok=0;
    if(ok){ DWORD old; VirtualProtect(f,8,0x40,&old); f[0]=0xE9; *(int*)(f+1)=(int)(BYTE*)PmStub-(int)(f+5);
      for(int i=5;i<8;i++) f[i]=0x90; VirtualProtect(f,8,old,&old); FlushInstructionCache(GetCurrentProcess(),f,8); }
    else logf_("ERROR: racer setup hook not installed"); }
  installEvents();
  return 1;
}

static void path(char* out,const char* ext){
  int n=0; while(g_dir[n]){out[n]=g_dir[n];n++;}
  while(n&&out[n-1]!='.')n--; for(int i=0;ext[i];i++)out[n++]=ext[i]; out[n]=0;
}

static DWORD __stdcall worker(void* x){
  char ini[520], lg[520], b[128];
  path(ini,"ini"); path(lg,"log");
  g_enabled=GetPrivateProfileIntA("Settings","Enabled",1,ini);
  g_noDupes=GetPrivateProfileIntA("Settings","AvoidDuplicateOpponents",1,ini);
  g_avoidPlayer=GetPrivateProfileIntA("Settings","AvoidPlayerCar",1,ini);
  g_logOn=GetPrivateProfileIntA("Settings","WriteLog",1,ini);
  g_seqFixOn=GetPrivateProfileIntA("Settings","FixRaceIntro",1,ini);
  g_skipMW=GetPrivateProfileIntA("Settings","SkipMostWantedRaces",1,ini);
  g_paint=GetPrivateProfileIntA("Settings","RandomizePaint",1,ini);
  g_allEvents=GetPrivateProfileIntA("Settings","AllEventsForAllCars",1,ini);
  g_newFlagFix=GetPrivateProfileIntA("Settings","FixNewEventsFlag",1,ini);
  if(g_logOn) g_log=CreateFileA(lg,0x40000000,1,0,2,0x80,0);
  long long t; QueryPerformanceCounter(&t); g_rng^=(unsigned)t^(unsigned)(t>>32)^GetTickCount(); if(!g_rng)g_rng=1;
  logf_("OpenRaces - waiting for game code to unpack...");
  /* NFS13.exe is decrypted by its DRM stub after DLLs load, so wait for the real bytes. */
  for(int i=0;i<12000;i++){
    if(install()){ wsprintfA(b,"Hooks installed (v7.7). %d cars in the class table.",NCARS); logf_(b);
      /* keep the event table opened up (it is patched once loaded, and again if the game reloads it) */
      g_t0=GetTickCount();
      for(int k=0;;k++){ if(g_allEvents && !g_origView && k%3==0) patchEvents(); seqFixScan(); Sleep(100); }
      return 0; }
    Sleep(10);
  }
  logf_("ERROR: loader function not found - this build only supports NFS13.exe v1.4.0.0. Nothing was changed.");
  return 0;
}

BOOL __stdcall DllMain(HANDLE h,DWORD r,void* x){
  if(r==1){ g_self=h; GetModuleFileNameA(h,g_dir,512); CreateThread(0,0,worker,0,0,0); }
  return 1;
}
