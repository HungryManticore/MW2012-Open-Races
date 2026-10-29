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
 *   0x0057BC70  vehicle bundle loader                    - logging only
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
static int g_used[64]; static int g_nUsed=0; static DWORD g_lastPick=0;
static unsigned g_rng=0x9E3779B9u;

typedef int (__fastcall *LoadFn)(void*,void*,int,int,int,int,int);
typedef int (__fastcall *SetSlotFn)(void*,void*,int,int,int,int);
typedef BYTE (__fastcall *Fn1)(void*,void*,int);
static LoadFn g_origLoad; static SetSlotFn g_origSet;

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
    wsprintfA(b,"[opponent] slot %d: %d (%s) unchanged [Most Wanted race]",slot,id,find(id)?find(id)->name:"?"); logf_(b);
    return g_origSet(self,edx,slot,id,livery,flag);
  }
  DWORD now=GetTickCount();
  /* The game re-applies a slot's stored car at times; never re-roll a car we already chose. */
  if(id && id==g_slotNew[slot]) { g_slotTime[slot]=now; return g_origSet(self,edx,slot,id,g_slotPaint[slot],flag); }
  if(id && id==g_slotOrig[slot] && now-g_slotTime[slot]<30000){
    g_slotTime[slot]=now; return g_origSet(self,edx,slot,g_slotNew[slot],g_slotPaint[slot],flag); }
  const Car* orig=find(id); const Car* pl=find(g_playerId);
  if(!g_enabled||!orig||!pl){
    wsprintfA(b,"[opponent] slot %d: %d (%s) unchanged%s",slot,id,orig?orig->name:"not a racer car",
              !g_enabled?" [disabled]":!orig?" [not in list]":" [player car unknown]"); logf_(b);
    return g_origSet(self,edx,slot,id,livery,flag);
  }
  if(now-g_lastPick>8000) g_nUsed=0; g_lastPick=now;
  int r=pick(pl->cls); if(!r) r=id;
  if(g_nUsed<64) g_used[g_nUsed++]=r;
  int paint = g_paint ? kPaints[rnd()%NPAINTS] : ((r==id)?livery:0);
  g_slotOrig[slot]=id; g_slotNew[slot]=r; g_slotPaint[slot]=paint; g_slotTime[slot]=now;
  wsprintfA(b,"[opponent] slot %d: %d (%s) -> %d (%s), paint %d",slot,id,orig->name,r,find(r)->name,paint); logf_(b);
  return g_origSet(self,edx,slot,r,paint,flag);
}

/* Bundle loader - logging only now. */
static int __fastcall HookLoad(void* self,void* edx,int mode,int id,int slot,int a4,int a5){
  char b[128]; wsprintfA(b,"  load: slot %d car %d (%s)",slot,id,mode==1?"player":"AI"); logf_(b);
  return g_origLoad(self,edx,mode,id,slot,a4,a5);
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

#ifdef EVENT_DIAG
typedef BYTE (__fastcall *MemberFn)(void*,void*,int,int,int);
static MemberFn g_origMember;
static BYTE __fastcall HookMember(void* self,void* edx,int key,int car,int flag){
  unsigned ret=(unsigned)__builtin_return_address(0); int seen=0;
  for(int i=0;i<g_nCallers;i++) if(g_callers[i]==ret){seen=1;break;}
  if(!seen && g_nCallers<64){ char b[96]; g_callers[g_nCallers++]=ret; wsprintfA(b,"[diag] membership check called from %08X",ret); logf_(b); }
  return g_origMember(self,edx,key,car,flag);
}
static int g_nCrash=0;
static long __stdcall CrashLog(void* xp){
  unsigned* rec=*(unsigned**)xp; BYTE* ctx=((BYTE**)xp)[1];
  if(rec[0]!=0xC0000005 || g_nCrash>=3) return 0;
  g_nCrash++;
  char b[600]; unsigned esp=*(unsigned*)(ctx+0xC4);
  int n=wsprintfA(b,"[crash] access violation at %08X (reading %08X) eax %08X ecx %08X edx %08X esi %08X edi %08X; stack:",
     rec[3],rec[6],*(unsigned*)(ctx+0xB0),*(unsigned*)(ctx+0xAC),*(unsigned*)(ctx+0xA8),*(unsigned*)(ctx+0xA0),*(unsigned*)(ctx+0x9C));
  int k=0; for(unsigned p=esp;k<16 && p<esp+0x800;p+=4){ if(!readable((void*)p)) break; unsigned v=*(unsigned*)p;
    if(v>0x401000&&v<0xF82000){ n+=wsprintfA(b+n," %08X",v); k++; } }
  logf_(b); return 0;
}
static void installDiag(void){
  BYTE* m=(BYTE*)0x006ECA10; static const BYTE e[9]={0x8B,0x41,0x7C,0x8B,0x89,0x80,0x00,0x00,0x00};
  for(int i=0;i<9;i++) if(m[i]!=e[i]){ logf_("[diag] membership fn mismatch"); return; }
  AddVectoredExceptionHandler(1,CrashLog);
  logf_("[diag] diagnostics installed");
}
#endif

static int install(void){
  BYTE* a=(BYTE*)LOAD_ADDR; BYTE* s=(BYTE*)SET_ADDR;
  if(!readable(a)||!readable(s)||!matches(a,kExpected)||!matches(s,kExpSet)) return 0;
  g_origSet=(SetSlotFn)hook(s,5,(void*)HookSet);
  g_origLoad=(LoadFn)hook(a,6,(void*)HookLoad);
  installEvents();
#ifdef EVENT_DIAG
  installDiag();
#endif
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
  g_skipMW=GetPrivateProfileIntA("Settings","SkipMostWantedRaces",1,ini);
  g_paint=GetPrivateProfileIntA("Settings","RandomizePaint",1,ini);
  g_allEvents=GetPrivateProfileIntA("Settings","AllEventsForAllCars",1,ini);
  g_newFlagFix=GetPrivateProfileIntA("Settings","FixNewEventsFlag",1,ini);
  if(g_logOn) g_log=CreateFileA(lg,0x40000000,1,0,2,0x80,0);
  long long t; QueryPerformanceCounter(&t); g_rng^=(unsigned)t^(unsigned)(t>>32)^GetTickCount(); if(!g_rng)g_rng=1;
  logf_("OpenRaces - waiting for game code to unpack...");
  /* NFS13.exe is decrypted by its DRM stub after DLLs load, so wait for the real bytes. */
  for(int i=0;i<12000;i++){
    if(install()){ wsprintfA(b,"Hooks installed (v7.1). %d cars in the class table.",NCARS); logf_(b);
      /* keep the event table opened up (it is patched once loaded, and again if the game reloads it) */
      while(g_allEvents){ if(!g_origView) patchEvents(); Sleep(250); }
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
