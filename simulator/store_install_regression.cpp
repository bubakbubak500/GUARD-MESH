// SPDX-License-Identifier: GPL-3.0-or-later
#include "services/StagedFileInstall.h"
#include <FS.h>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
namespace {
using Install=ui::StagedFileInstall;
void check(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
void put(fs::FS& disk,const char* path,const char* text) { auto f=disk.open(path,"w"); f.print(text); f.close(); }
std::string get(fs::FS& disk,const char* path) {
  auto f=disk.open(path,"r"); std::string text;
  for (int c; (c=f.read())>=0;) text+=char(c);
  return text;
}
bool stage(Install& install,const char* path,const char* text) {
  return install.stage(path,reinterpret_cast<const uint8_t*>(text),std::strlen(text));
}
Install::Host host{std::malloc,std::free,nullptr};
fs::FS seeded() {
  fs::FS disk; disk.enableMemory(); disk.mkdir("/apps"); disk.mkdir("/lang");
  put(disk,"/apps/clock.lua","old source"); put(disk,"/apps/clock.json","old manifest");
  put(disk,"/lang/cs.lang","old Czech"); put(disk,"/lang/de.lang","unrelated German");
  return disk;
}
void oldApp(fs::FS& disk) {
  check(get(disk,"/apps/clock.lua")=="old source","Failed install lost old app source");
  check(get(disk,"/apps/clock.json")=="old manifest","Failed install lost old app manifest");
}
}
void runStoreInstallRegression() {
  auto disk=seeded();
  {
    Install install(disk,host);
    check(stage(install,"/apps/clock.lua","new source"),"Could not stage app source");
    oldApp(disk);
    // A subsequent HTTP failure destroys the incomplete install scope.
  }
  oldApp(disk); check(!disk.exists("/apps/clock.lua.tmp"),"Abandoned install leaked staged source");
  {
    Install install(disk,host);
    check(stage(install,"/apps/clock.lua","new source") &&
          stage(install,"/apps/clock.json","new manifest") && install.commit(),"App pair install failed");
    check(!install.commit(),"Install committed twice");
  }
  check(get(disk,"/apps/clock.lua")=="new source" && get(disk,"/apps/clock.json")=="new manifest","App pair mismatch");
  check(!disk.exists("/apps/clock.lua.bak") && !disk.exists("/apps/clock.json.bak"),"Successful install leaked backups");
  // Each possible rename failure: backing up either original, publishing either replacement.
  for (size_t failure=1;failure<=4;++failure) {
    disk=seeded();
    {
      Install install(disk,host);
      check(stage(install,"/apps/clock.lua","new source") && stage(install,"/apps/clock.json","new manifest"),"Staging failed");
      disk.failRenameOnCall(failure);
      check(!install.commit() && install.error()==Install::CommitFailed,"Commit failure not reported");
    }
    oldApp(disk);
    check(!disk.exists("/apps/clock.lua.tmp") && !disk.exists("/apps/clock.json.tmp"),"Rollback left temporary files");
    check(!disk.exists("/apps/clock.lua.bak") && !disk.exists("/apps/clock.json.bak"),"Rollback left backup files");
  }
  disk=seeded();
  {
    Install install(disk,host);
    check(stage(install,"/apps/clock.lua","new source") && stage(install,"/apps/clock.json","new manifest"),"Staging failed");
    disk.failRenameOnCall(3,true);
    check(!install.commit() && install.error()==Install::RecoveryRequired,"Failed rollback was hidden");
  }
  check(get(disk,"/apps/clock.lua.bak")=="old source" && get(disk,"/apps/clock.json.bak")=="old manifest","Failed rollback destroyed backups");
  disk.failRenameOnCall(0);
  {
    Install install(disk,host);
    check(!stage(install,"/apps/clock.lua","retry") && install.error()==Install::RecoveryRequired,"Retry overwrote recovery backup");
  }
  check(get(disk,"/apps/clock.lua.bak")=="old source","Recovery backup mutated by retry");
  for (bool writeFailure : {true,false}) {
    disk=seeded();
    if (writeFailure) disk.limitWrites(4); else disk.limitReads(4);
    {
      Install install(disk,host);
      check(!stage(install,"/lang/cs.lang","new Czech translation"),"Short language I/O accepted");
      check(install.error()==(writeFailure ? Install::WriteFailed : Install::VerifyFailed),"Wrong I/O diagnostic");
      check(!install.commit(),"Incomplete language install committed");
    }
    disk.allowWrites(); disk.allowReads();
    check(get(disk,"/lang/cs.lang")=="old Czech","Language write failure lost original");
    check(get(disk,"/lang/de.lang")=="unrelated German","Language write failure removed another language");
    check(!disk.exists("/lang/cs.lang.tmp"),"Failed language write leaked temporary file");
  }
  disk=seeded();
  {
    Install oom(disk,{[](size_t)->void*{return nullptr;},std::free,nullptr});
    check(!stage(oom,"/lang/cs.lang","new") && oom.error()==Install::NoMemory,"Scratch allocation failure ignored");
  }
  check(get(disk,"/lang/cs.lang")=="old Czech","OOM damaged language");
  {
    Install install(disk,host);
    std::string large(12000,'x');
    check(stage(install,"/lang/cs.lang",large.c_str()) && install.commit(),"Multi-chunk language install failed");
    check(get(disk,"/lang/cs.lang")==large,"Multi-chunk language content differs");
  }
  check(get(disk,"/lang/de.lang")=="unrelated German","Install modified unrelated language");
  disk=seeded();
  {
    Install install(disk,host);
    check(stage(install,"/apps/new.lua","source") && stage(install,"/apps/new.json","manifest"),"New install staging failed");
    disk.failRenameOnCall(2);
    check(!install.commit(),"New pair accepted second rename failure");
  }
  check(!disk.exists("/apps/new.lua") && !disk.exists("/apps/new.json"),"Failed new install left half an app");
}
