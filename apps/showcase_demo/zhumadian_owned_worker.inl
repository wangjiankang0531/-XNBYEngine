// Retry only transient Windows file-sharing failures. Each attempt owns its
// complete process tree; closing the job never affects another app's worker.
#ifdef _WIN32
DWORD runOwnedNativeChunkWorker(
    const fs::path& python, std::wstring command, const fs::path& repo, int tile, int depth) {
  DWORD code = 1;
  const fs::path logs = repo / "work/native_worker_logs";
  fs::create_directories(logs);
  const fs::path logFile = logs / (std::to_string(GetCurrentProcessId()) + "_tile_" + std::to_string(tile) + "_depth_" +
                                   std::to_string(depth) + ".log");
  for (int attempt = 0; attempt < 8; ++attempt) {
    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};
    limit.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limit, sizeof(limit))) {
      if (job)
        CloseHandle(job);
      throw std::runtime_error("Cannot own native chunk decoder process tree");
    }
    STARTUPINFOW start{};
    start.cb = sizeof(start);
    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE log = CreateFileW(logFile.c_str(),
                             GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &security,
                             CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL,
                             nullptr);
    HANDLE nullInput = CreateFileW(L"NUL",
                                   GENERIC_READ,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   &security,
                                   OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL,
                                   nullptr);
    if (log == INVALID_HANDLE_VALUE || nullInput == INVALID_HANDLE_VALUE) {
      if (log != INVALID_HANDLE_VALUE)
        CloseHandle(log);
      if (nullInput != INVALID_HANDLE_VALUE)
        CloseHandle(nullInput);
      CloseHandle(job);
      throw std::runtime_error("Cannot capture owned native decoder diagnostics");
    }
    start.dwFlags = STARTF_USESTDHANDLES;
    start.hStdOutput = start.hStdError = log;
    start.hStdInput = nullInput;
    PROCESS_INFORMATION child{};
    auto arguments = command;
    if (!CreateProcessW(python.c_str(),
                        arguments.data(),
                        nullptr,
                        nullptr,
                        TRUE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED,
                        nullptr,
                        repo.c_str(),
                        &start,
                        &child)) {
      const DWORD error = GetLastError();
      CloseHandle(log);
      CloseHandle(nullInput);
      CloseHandle(job);
      if ((error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION) && attempt < 7) {
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        continue;
      }
      throw std::runtime_error("Cannot start native chunk decoder; Windows error " + std::to_string(error));
    }
    CloseHandle(log);
    CloseHandle(nullInput);
    if (!AssignProcessToJobObject(job, child.hProcess)) {
      TerminateProcess(child.hProcess, 1);
      CloseHandle(child.hThread);
      CloseHandle(child.hProcess);
      CloseHandle(job);
      throw std::runtime_error("Cannot attach owned decoder to its job");
    }
    ResumeThread(child.hThread);
    CloseHandle(child.hThread);
    const DWORD wait = WaitForSingleObject(child.hProcess, 180000);
    code = 1;
    if (wait == WAIT_OBJECT_0)
      GetExitCodeProcess(child.hProcess, &code);
    else {
      TerminateJobObject(job, 1);
      WaitForSingleObject(child.hProcess, 5000);
    }
    CloseHandle(child.hProcess);
    CloseHandle(job);
    if (wait != WAIT_OBJECT_0)
      throw std::runtime_error("Native chunk decoder timeout");
    if (code == 0)
      return code;
    bool sharing = code == 0xc0000043;
    if (code == 1) {
      try {
        const auto file = repo / "work/zhumadian_native_chunk_cache" /
                          ("tile_" + std::to_string(tile) + "_depth_" + std::to_string(depth)) / "failure.json";
        nlohmann::json failure;
        std::ifstream input(file);
        if (input) {
          input >> failure;
          const auto reason = failure.value("reason", "");
          sharing = reason.find("DLL load failed") != std::string::npos &&
                    (reason.find("另一个程序") != std::string::npos || reason.find("being used") != std::string::npos ||
                     reason.find("sharing") != std::string::npos);
        }
        if (!sharing) {
          // Imports can fail before the converter creates failure.json. Keep
          // their real diagnostics and retry only the recognized sharing case.
          std::ifstream trace(logFile);
          const std::string text((std::istreambuf_iterator<char>(trace)), std::istreambuf_iterator<char>());
          sharing = text.find("DLL load failed") != std::string::npos &&
                    (text.find("另一个程序") != std::string::npos || text.find("being used") != std::string::npos ||
                     text.find("[WinError 32]") != std::string::npos || text.find("sharing") != std::string::npos);
        }
      } catch (...) {
      }
    }
    if (!sharing || attempt == 7)
      return code;
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
  }
  return code;
}
#endif
