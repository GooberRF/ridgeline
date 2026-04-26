#pragma once

#include <wxx_wincore.h>
#include <crash_handler_stub.h>

class CommandLineInfo;

class CrashReportApp : public CWinApp
{
public:
    int Run() override;
    std::string GetArchivedReportFilePath() const;

private:
    void PrepareReport(const CommandLineInfo& cmd_line_info);
    void ArchiveReport(const char* crash_dump_filename, const char* exc_info_filename);
    static int Message(HWND hwnd, const char* text, const char* title, int flags);

    CrashHandlerConfig m_config;
};

inline CrashReportApp* GetCrashReportApp()
{
    return static_cast<CrashReportApp*>(GetApp());
}
