/*
* This file is part of Adblock Plus <https://adblockplus.org/>,
* Copyright (C) 2006-present eyeo GmbH
*
* Adblock Plus is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 3 as
* published by the Free Software Foundation.
*
* Adblock Plus is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with Adblock Plus.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <AdblockPlus/Platform.h>
#include <AdblockPlus/JsEngine.h>
#include <AdblockPlus/FilterEngine.h>
#include <AdblockPlus/DefaultLogSystem.h>
#include "DefaultTimer.h"
#include "DefaultWebRequest.h"
#include "DefaultFileSystem.h"

using namespace AdblockPlus;

namespace
{
  void DummyScheduler(const AdblockPlus::SchedulerTask& task)
  {
    std::thread(task).detach();
  }
}

TimerPtr AdblockPlus::CreateDefaultTimer()
{
  return TimerPtr(new DefaultTimer());
}

FileSystemPtr AdblockPlus::CreateDefaultFileSystem(const Scheduler& scheduler, const std::string& basePath)
{
  return FileSystemPtr(new DefaultFileSystem(scheduler, std::unique_ptr<DefaultFileSystemSync>(new DefaultFileSystemSync(basePath))));
}

WebRequestPtr AdblockPlus::CreateDefaultWebRequest(const Scheduler& scheduler, WebRequestSyncPtr syncImpl)
{
  if (!syncImpl)
    syncImpl.reset(new DefaultWebRequestSync());
  return WebRequestPtr(new DefaultWebRequest(scheduler, std::move(syncImpl)));
}

LogSystemPtr AdblockPlus::CreateDefaultLogSystem()
{
  return LogSystemPtr(new DefaultLogSystem());
}

Platform::Platform(CreationParameters&& creationParameters)
{
  logSystem = creationParameters.logSystem ? std::move(creationParameters.logSystem) : CreateDefaultLogSystem();
  timer = creationParameters.timer ? std::move(creationParameters.timer) : CreateDefaultTimer();
  fileSystem = creationParameters.fileSystem ? std::move(creationParameters.fileSystem) : CreateDefaultFileSystem(::DummyScheduler);
  webRequest = creationParameters.webRequest ? std::move(creationParameters.webRequest) : CreateDefaultWebRequest(::DummyScheduler);
}

Platform::~Platform()
{
}

void Platform::SetUpJsEngine(const AppInfo& appInfo, std::unique_ptr<IV8IsolateProvider> isolate)
{
  std::lock_guard<std::mutex> lock(modulesMutex);
  if (jsEngine)
    return;
  jsEngine = JsEngine::New(appInfo, *this, std::move(isolate));
}

JsEngine& Platform::GetJsEngine()
{
  SetUpJsEngine();
  return *jsEngine;
}

void Platform::CreateFilterEngineAsync(const FilterEngine::CreationParameters& parameters,
  const OnFilterEngineCreatedCallback& onCreated)
{
  std::shared_ptr<std::promise<FilterEnginePtr>> filterEnginePromise;
  {
    std::lock_guard<std::mutex> lock(modulesMutex);
    if (filterEngine.valid())
      return;
    filterEnginePromise = std::make_shared<std::promise<FilterEnginePtr>>();
    filterEngine = filterEnginePromise->get_future();
  }

  GetJsEngine(); // ensures that JsEngine is instantiated
  FilterEngine::CreateAsync(jsEngine, [this, onCreated, filterEnginePromise](const FilterEnginePtr& filterEngine)
  {
    filterEnginePromise->set_value(filterEngine);
    if (onCreated)
      onCreated(*filterEngine);
  }, parameters);
}

FilterEngine& Platform::GetFilterEngine()
{
  CreateFilterEngineAsync();
  return *std::shared_future<FilterEnginePtr>(filterEngine).get();
}

ITimer& Platform::GetTimer()
{
  return *timer;
}

IFileSystem& Platform::GetFileSystem()
{
  return *fileSystem;
}

IWebRequest& Platform::GetWebRequest()
{
  return *webRequest;
}

LogSystem& Platform::GetLogSystem()
{
  return *logSystem;
}