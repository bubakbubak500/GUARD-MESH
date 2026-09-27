// SPDX-License-Identifier: GPL-3.0-or-later
#include "services/AppStoreData.h"
#include "services/AppInventory.h"
#include <FS.h>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
namespace {
fs::FS disk;
int allocated=0,released=0,failAt=0;
void check(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
void put(const char* path,const char* body) { auto f=disk.open(path,"w"); f.print(body); f.close(); }
ui::AppStoreData::Host host() {
  return {[]()->fs::FS*{return &disk;},
    [](char* out,size_t cap,const char* path){snprintf(out,cap,"%s",path);},
    [](size_t size)->void*{if (++allocated==failAt) return nullptr; return malloc(size);},
    [](void* ptr){++released;free(ptr);}};
}
}
void runStoreDataRegression() {
  disk=fs::FS{}; disk.enableMemory(); disk.mkdir("/lang");
  allocated=released=0; failAt=2;
  {
    ui::AppStoreData data(host());
    data.parseApps("[]");
    check(data.appCount()==-1,"Catalog OOM stuck at loading");
    failAt=0; check(data.ready(),"Catalog allocation could not retry");
    data.parseApps(R"({"apps":[
      {"id":"clock","name":"Clock {one}","ver":"2","desc":"Quoted \"clock\" and } brace"},
      {"id":"clock","name":"Duplicate","ver":"99"},
      {"id":"../bad","name":"Bad path","ver":"1"},
      {"id":"12345678901234567890","name":"Too long","ver":"1"},
      {"id":"map","name":"Mapa","ver":"3","desc":"Long description with a UTF-8 suffix: 123456789012345678901234567890123456ěščřžýáíé"}
    ]})");
    check(data.appCount()==2,"Catalog brace parsing, duplicate or path handling failed");
    check(!strcmp(data.apps()[0].name,"Clock {one}") && !strcmp(data.apps()[0].desc,"Quoted \"clock\" and } brace"),"Catalog display escapes lost");
    check(!strcmp(data.apps()[1].id,"map"),"Catalog stopped after quoted closing brace");
    data.parseLanguages(R"({"langs":[{"code":"cs","name":"Čeština","ver":"22"},
      {"code":"de","name":"Deutsch"},{"code":"cs","name":"Dup","ver":"9"},
      {"code":"bad","name":"Bad","ver":"12345678"}]})");
    check(data.languageCount()==2 && !strcmp(data.languages()[0].ver,"22") && !strcmp(data.languages()[1].ver,"1"),"Language catalog parsing/default/validation failed");
    put("/lang/cs.lang","# name: Čeština\n# base: cs\n# ver: 22\nKey\tValue\n");
    put("/lang/custom.lang","# name: Custom\n# base: en\nKey\tValue\n");
    put("/lang/.hidden.lang","# name: hidden\n");
    put("/lang/too-long-code.lang","# name: invalid\n");
    disk.mkdir("/lang/directory.lang");
    data.scanLanguages();
    check(data.installedCount()==2,"Language scan included hidden/overlong/directory entries");
    char name[28],base[12],version[8];
    check(data.languageMetadata("cs",name,sizeof name,base,sizeof base,version,sizeof version) &&
          !strcmp(base,"cs") && !strcmp(version,"22"),"Installed language metadata mismatch");
    check(data.languageMetadata("custom",name,sizeof name,base,sizeof base,version,sizeof version) &&
          !strcmp(version,"1"),"Old language version default lost");
    check(!data.languageMetadata("../cs",name,sizeof name,base,sizeof base),"Language metadata accepted traversal");
    disk.remove("/lang/cs.lang"); data.scanLanguages();
    check(data.installedCount()==1 && !strcmp(data.installedLanguages()[0].code,"custom"),"Language scan retained stale rows");
    data.parseApps(nullptr); data.parseLanguages("truncated {");
    check(data.appCount()==-1 && data.languageCount()==-1,"Failed catalog exposed old rows");
    char value[30];
    check(ui::appManifestField(R"({"name":"id","id":"right"})","id",value,sizeof value) && !strcmp(value,"right"),"Manifest value mistaken for field name");
    check(!ui::appManifestField(R"({"name":"id","desc":"wrong"})","id",value,sizeof value),"Absent manifest key accepted from value text");
  }
  check(allocated==4 && released==3,"Store table ownership leaked allocations");
}
