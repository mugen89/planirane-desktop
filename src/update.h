// Updates from GitHub Releases (the public repository in install.h's kRepoUrl).
//
// Which version is newest comes from the redirect of /releases/latest (…/releases/tag/v1.2.3), not from
// api.github.com: the API allows 60 requests an hour per IP address, which one office behind one address
// would share. The setup is downloaded from /releases/download/v1.2.3/ together with its .sha256, checked,
// and started with --update: it closes this program, replaces it and starts the new one. Nothing is lost by
// the restart — the stopwatch lives on the server.
#pragma once
#include "common.h"

namespace kd {

constexpr const wchar_t* kSetupAsset = L"KanbanDesktop-Setup.exe";

std::wstring updateBase();  // kRepoUrl; KANBANDESKTOP_UPDATE_BASE points it elsewhere for tests
int compareVersions(const std::string& a, const std::string& b);  // <0, 0, >0

// done(version, error): version is the newest published one ("" when there is none yet), on the UI thread.
void checkLatestRelease(std::function<void(const std::string& version, const std::wstring& error)> done);
// Downloads and verifies that version's setup; done(path to the setup, error) on the UI thread.
void downloadSetup(const std::string& version, std::function<void(const std::wstring& path, const std::wstring& error)> done);
bool startSetup(const std::wstring& path);  // --update: it closes us, installs, starts the new version

}  // namespace kd
