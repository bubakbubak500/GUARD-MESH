#include "ui-touch/models/GuardianRpc.h"
#include "ui-touch/services/GuardianRpcLink.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#define CHECK(value) do { if (!(value)) { std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#value); std::abort(); } } while (0)
using guardian::EventQueue;
using Packets = std::vector<std::vector<uint8_t>>;
static Packets fragments(const std::string& text,uint8_t id,size_t payload=16) {
  Packets packets;
  for (size_t offset=0,index=0;offset<text.size();offset+=payload,++index) {
    const size_t count=std::min(payload,text.size()-offset);
    std::vector<uint8_t> packet{
      uint8_t((offset==0?1:0)|(offset+count==text.size()?2:0)),id,uint8_t(index),uint8_t(index>>8)
    };
    packet.insert(packet.end(),text.begin()+offset,text.begin()+offset+count);
    packets.push_back(packet);
  }
  return packets;
}
static bool receive(EventQueue& queue,const Packets& packets,uint32_t now=1) {
  for (const auto& packet:packets) if (!queue.accept(packet.data(),packet.size(),now)) return false;
  return true;
}
static uint8_t requestId;
static bool notify(const uint8_t* bytes,size_t size) {
  CHECK(size>=5 && size<=20); requestId=bytes[1]; return true;
}
int main() {
  EventQueue queue;
  std::string output;
  const std::string unicode="{\"event_id\":1,\"type\":\"alert.received\",\"payload\":{\"note\":\"Příjem 😀\"}}";
  CHECK(receive(queue,fragments(unicode,255))); // all packets fit MTU 23
  CHECK(queue.take(output) && output==unicode && !queue.take(output));
  const auto split=fragments(unicode,0,1); // every UTF-8 continuation crosses fragments
  for (size_t index=0;index<split.size();++index) {
    CHECK(queue.accept(split[index].data(),split[index].size(),2));
    if (index+1<split.size()) CHECK(!queue.take(output));
  }
  CHECK(queue.take(output) && output==unicode);
  CHECK(receive(queue,fragments("{}",255)) && receive(queue,fragments("{}",0)));
  CHECK(queue.take(output) && queue.take(output)); // IDs wrap and are not event IDs

  uint8_t bad[]={2,1,0,0,'}'};
  CHECK(!queue.accept(bad,sizeof bad,10));
  bad[0]=5; CHECK(!queue.accept(bad,sizeof bad,10));
  bad[0]=1; bad[2]=1; CHECK(!queue.accept(bad,sizeof bad,10));
  bad[2]=0; CHECK(queue.accept(bad,sizeof bad,10));
  CHECK(!queue.accept(bad,sizeof bad,11)); // overlapping START
  bad[0]=2; bad[2]=1; bad[1]=2; CHECK(!queue.accept(bad,sizeof bad,11));
  bad[1]=1; bad[2]=2; CHECK(!queue.accept(bad,sizeof bad,11)); // missing fragment
  CHECK(!queue.expired(30009) && queue.expired(30010));
  CHECK(!queue.accept(bad,sizeof bad,30010));
  queue.reset(); CHECK(!queue.expired(30010) && !queue.take(output));
  bad[0]=1; bad[2]=0; CHECK(queue.accept(bad,sizeof bad,0xfffffff0u));
  CHECK(!queue.expired(29983) && queue.expired(29984)); // millis() wrap
  queue.reset();
  CHECK(receive(queue,fragments(std::string(EventQueue::MaxBytes,'x'),1)));
  CHECK(queue.take(output) && output.size()==EventQueue::MaxBytes);
  CHECK(!receive(queue,fragments(std::string(EventQueue::MaxBytes+1,'x'),2)));
  queue.reset();
  for (unsigned i=0;i<EventQueue::MaxEvents;++i) CHECK(receive(queue,fragments("{}",uint8_t(i))));
  CHECK(EventQueue::MaxEvents>=20); // The negotiated Guardian window allows 20 writes.
  CHECK(!receive(queue,fragments("{}",21))); // queue never silently drops an event
  queue.reset();
  CHECK(receive(queue,fragments(std::string(EventQueue::MaxBytes,'x'),1)));
  CHECK(receive(queue,fragments(std::string(EventQueue::MaxBytes,'x'),2)));
  CHECK(queue.take(output)); // consuming frees the global byte budget
  CHECK(receive(queue,fragments("{}",3)));
  CHECK(queue.take(output) && output.size()==EventQueue::MaxBytes);
  CHECK(queue.take(output) && output=="{}");
  CHECK(receive(queue,fragments(std::string(EventQueue::MaxBytes,'x'),1)));
  CHECK(receive(queue,fragments(std::string(EventQueue::MaxBytes,'x'),2)));
  CHECK(!receive(queue,fragments("{}",3))); // 64 KiB includes queued + partial bytes
  queue.reset();

  guardian::rpcConnect(); guardian::rpcSubscribe(true);
  CHECK(guardian::rpcStart("{}",10) && guardian::rpcTick(10,notify));
  const std::string response="{\"id\":\"r1\",\"ok\":true,\"result\":{\"ready\":null}}";
  const auto reply=fragments(response,requestId);
  const auto event=fragments(unicode,requestId); // same ID, two independent assemblers
  for (size_t i=0;i<std::max(reply.size(),event.size());++i) {
    if (i<reply.size()) CHECK(guardian::rpcReceive(reply[i].data(),reply[i].size(),11));
    if (i<event.size()) CHECK(guardian::eventReceive(event[i].data(),event[i].size(),11));
  }
  std::string error;
  CHECK(guardian::rpcTake(output,error) && output==response && error.empty());
  CHECK(guardian::eventTake(output) && output==unicode && guardian::rpcReady());
  const auto partial=fragments(unicode,7);
  CHECK(guardian::eventReceive(partial[0].data(),partial[0].size(),100));
  CHECK(guardian::rpcTick(30099,nullptr) && !guardian::rpcTick(30100,nullptr));
  CHECK(!guardian::rpcTake(output,error)); // Idle Event faults have no RPC owner.
  CHECK(!guardian::eventTake(output) && !guardian::rpcReady());
  guardian::rpcConnect();
  CHECK(guardian::eventReceive(partial[0].data(),partial[0].size(),1));
  guardian::rpcDisconnect(); CHECK(!guardian::eventTake(output));
  guardian::rpcConnect();
  const auto complete=fragments("{}",8)[0];
  CHECK(guardian::eventReceive(complete.data(),complete.size(),1));
  guardian::rpcDisconnect(); CHECK(!guardian::eventTake(output));
  CHECK(!guardian::eventReceive(complete.data(),complete.size(),2)); // offline is rejected
  guardian::rpcConnect(); guardian::rpcSubscribe(true);
  for (unsigned i=0;i<EventQueue::MaxEvents;++i) CHECK(guardian::eventReceive(complete.data(),complete.size(),2));
  CHECK(!guardian::eventReceive(complete.data(),complete.size(),2));
  CHECK(!guardian::eventTake(output) && !guardian::rpcReady() && !guardian::rpcTick(3,nullptr));
  puts("Guardian v3 events: MTU23, split UTF-8, independent IDs, order, timeout/wrap, disconnect, count/byte bounds PASS");
}
