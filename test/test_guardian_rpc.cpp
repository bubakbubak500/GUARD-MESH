#include "ui-touch/models/GuardianRpc.h"
#include "ui-touch/services/GuardianJson.h"
#include "ui-touch/services/GuardianDraft.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(value) do { if (!(value)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#value); std::abort(); } } while (0)
#undef assert
#define assert(value) CHECK(value)
using guardian::Rpc;
static uint8_t send(Rpc& r,const std::string& json,uint32_t now=0) {
  assert(r.start(json,now)); uint8_t p[20],id=0; std::string seen;
  while (auto n=r.peek(p)) { id=p[1]; assert(n<=20); seen.append((char*)p+4,n-4); r.sent(n); }
  assert(seen==json); return id;
}
int main() {
  Rpc r; assert(!r.start("{}",0)); r.reset(true); assert(!r.start("{}",0)); r.subscribed=true;
  const auto id=send(r,"{\"id\":\"žluťoučký\"}");
  assert(!r.start("{}",1));
  uint8_t p[]={3,id,0,0,'{','}'}; assert(r.accept(p,sizeof p,1));
  std::string json,error; assert(r.take(json,error) && json=="{}" && error.empty());
  assert(r.ready());
  auto completed=send(r,"{}"); p[1]=completed; assert(r.accept(p,sizeof p,1));
  r.reset(); assert(r.take(json,error) && error=="disconnected");
  r.reset(true); r.subscribed=true;
  const auto id2=send(r,"{}"); p[1]=id2; p[2]=1; assert(!r.accept(p,sizeof p,1));
  p[2]=0; p[0]=5; assert(!r.accept(p,sizeof p,1)); p[0]=1; assert(r.accept(p,sizeof p,2));
  assert(!r.accept(p,sizeof p,3)); assert(r.expired(30002)); r.fail("timeout");
  assert(r.take(json,error) && error=="timeout"); r.reset(true); r.subscribed=true;
  const auto id3=send(r,"{}",0xffffff00u);
  std::string unicode="{\"body\":\"Příjem 😀\"}";
  unsigned i=0;
  for (size_t offset=0;offset<unicode.size();++offset,++i) {
    uint8_t b[]={uint8_t((offset==0?1:0)|(offset+1==unicode.size()?2:0)),id3,uint8_t(i),uint8_t(i>>8),uint8_t(unicode[offset])};
    assert(r.accept(b,sizeof b,10));
  }
  assert(r.take(json,error) && json==unicode);
  JsonDocument doc; assert(guardian::parseObject(doc,json));
  assert(!guardian::parseObject(doc,"{}{}")); assert(!guardian::parseObject(doc,"{\"x\":NaN}"));
  assert(!guardian::parseObject(doc,std::string("{\"x\":\"")+char(0xc0)+char(0x80)+"\"}"));
  r.reset(true); r.subscribed=true; assert(!r.start(std::string(32769,'x'),0));
  send(r,"{}"); r.reset(); assert(r.take(json,error) && error=="disconnected");
  r.reset(true); r.subscribed=true; const auto bigId=send(r,"{}");
  uint8_t chunk[20]; memset(chunk+4,'x',16); chunk[1]=bigId;
  for (unsigned index=0;index<2048;++index) {
    chunk[0]=index==0?1:0; chunk[2]=uint8_t(index); chunk[3]=uint8_t(index>>8);
    assert(r.accept(chunk,20,10));
  }
  chunk[0]=2; chunk[2]=0; chunk[3]=8; assert(!r.accept(chunk,5,10));
  r.reset(true); r.take(json,error); r.subscribed=true; send(r,"{}",0xfffffff0u);
  assert(!r.expired(59983) && r.expired(59984));
  guardian::Draft draft; draft.to="OK7PS"; draft.body="Zkouška 😀"; draft.token=guardian::newToken();
  assert(draft.token.size()==36 && draft.token[14]=='4');
  assert(guardian::validateDraft(draft) && guardian::saveDraft(draft));
  guardian::Draft restored; assert(guardian::loadDraft(restored));
  assert(restored.token==draft.token && restored.body==draft.body);
  restored.body=" \n\t"; assert(!guardian::validateDraft(restored));
  restored=draft; restored.to="OK 7"; assert(!guardian::validateDraft(restored));
  puts("Guardian v2: CCCD gating, chunks, UTF-8, ordering, limits, timeout, draft/token PASS");
}
