// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <cstring>
#include <windows.h>

// DbgHelp declarations require the Windows types above.
#include <dbghelp.h>
namespace {
LONG WINAPI reportCrash(EXCEPTION_POINTERS *exception) {
  fprintf(stderr, "Simulator native crash: code=0x%08lx address=%p\n",
          exception->ExceptionRecord->ExceptionCode, exception->ExceptionRecord->ExceptionAddress);
  const auto process = GetCurrentProcess();
  char symbolDirectory[MAX_PATH]{};
  GetModuleFileNameA(nullptr, symbolDirectory, sizeof symbolDirectory);
  if (auto *slash = strrchr(symbolDirectory, '\\'))
    *slash = 0;
  SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS);
  if (SymInitialize(process, symbolDirectory, TRUE)) {
    CONTEXT context = *exception->ContextRecord;
    STACKFRAME64 frame{};
    frame.AddrPC = {context.Rip, 0, AddrModeFlat};
    frame.AddrStack = {context.Rsp, 0, AddrModeFlat};
    frame.AddrFrame = {context.Rbp, 0, AddrModeFlat};
    for (unsigned i = 0; i < 40 && frame.AddrPC.Offset; ++i) {
      alignas(SYMBOL_INFO) char symbolBuffer[sizeof(SYMBOL_INFO) + 1024]{};
      auto *symbol = reinterpret_cast<SYMBOL_INFO *>(symbolBuffer);
      symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
      symbol->MaxNameLen = 1024;
      DWORD64 displacement = 0;
      fprintf(stderr, "  #%u 0x%llx", i, static_cast<unsigned long long>(frame.AddrPC.Offset));
      if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol))
        fprintf(stderr, " %s+0x%llx", symbol->Name, static_cast<unsigned long long>(displacement));
      IMAGEHLP_LINE64 line{};
      line.SizeOfStruct = sizeof line;
      DWORD lineDisplacement = 0;
      if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &lineDisplacement, &line))
        fprintf(stderr, " (%s:%lu)", line.FileName, line.LineNumber);
      fputc('\n', stderr);
      if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, &context, nullptr,
                       SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
        break;
    }
    SymCleanup(process);
  }
  fflush(stderr);
  return EXCEPTION_EXECUTE_HANDLER;
}
} // namespace
void installSimulatorCrashDiagnostics() { SetUnhandledExceptionFilter(reportCrash); }
