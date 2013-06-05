/*
 * This file is part of Adblock Plus <http://adblockplus.org/>,
 * Copyright (C) 2006-2013 Eyeo GmbH
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

#include <algorithm>
#include <cctype>
#include <functional>
#include <string>

#include <AdblockPlus.h>
#include "JsContext.h"
#include "Thread.h"

using namespace AdblockPlus;

extern std::string jsSources[];

Filter::Filter(JsValuePtr value)
    : JsValue(value)
{
  if (!IsObject())
    throw std::runtime_error("JavaScript value is not an object");
}

Filter::Type Filter::GetType()
{
  std::string className = GetClass();
  if (className == "BlockingFilter")
    return TYPE_BLOCKING;
  else if (className == "WhitelistFilter")
    return TYPE_EXCEPTION;
  else if (className == "ElemHideFilter")
    return TYPE_ELEMHIDE;
  else if (className == "ElemHideException")
    return TYPE_ELEMHIDE_EXCEPTION;
  else if (className == "CommentFilter")
    return TYPE_COMMENT;
  else
    return TYPE_INVALID;
}

bool Filter::IsListed()
{
  JsValuePtr func = jsEngine->Evaluate("API.isListedFilter");
  JsValueList params;
  params.push_back(shared_from_this());
  return func->Call(params)->AsBool();
}

void Filter::AddToList()
{
  JsValuePtr func = jsEngine->Evaluate("API.addFilterToList");
  JsValueList params;
  params.push_back(shared_from_this());
  func->Call(params);
}

void Filter::RemoveFromList()
{
  JsValuePtr func = jsEngine->Evaluate("API.removeFilterFromList");
  JsValueList params;
  params.push_back(shared_from_this());
  func->Call(params);
}

bool Filter::operator==(const Filter& filter) const
{
  return GetProperty("text")->AsString() == filter.GetProperty("text")->AsString();
}

Subscription::Subscription(JsValuePtr value)
    : JsValue(value)
{
  if (!IsObject())
    throw std::runtime_error("JavaScript value is not an object");
}

bool Subscription::IsListed()
{
  JsValuePtr func = jsEngine->Evaluate("API.isListedSubscription");
  JsValueList params;
  params.push_back(shared_from_this());
  return func->Call(params)->AsBool();
}

void Subscription::AddToList()
{
  JsValuePtr func = jsEngine->Evaluate("API.addSubscriptionToList");
  JsValueList params;
  params.push_back(shared_from_this());
  func->Call(params);
}

void Subscription::RemoveFromList()
{
  JsValuePtr func = jsEngine->Evaluate("API.removeSubscriptionFromList");
  JsValueList params;
  params.push_back(shared_from_this());
  func->Call(params);
}

void Subscription::UpdateFilters()
{
  JsValuePtr func = jsEngine->Evaluate("API.updateSubscription");
  JsValueList params;
  params.push_back(shared_from_this());
  func->Call(params);
}

bool Subscription::IsUpdating()
{
  JsValuePtr func = jsEngine->Evaluate("API.isSubscriptionUpdating");
  JsValueList params;
  params.push_back(shared_from_this());
  JsValuePtr result = func->Call(params);
  return result->AsBool();
}

bool Subscription::operator==(const Subscription& subscription) const
{
  return GetProperty("url")->AsString() == subscription.GetProperty("url")->AsString();
}

FilterEngine::FilterEngine(JsEnginePtr jsEngine)
    : jsEngine(jsEngine), initialized(false), firstRun(false), updateCheckId(0)
{
  jsEngine->SetEventCallback("init", std::tr1::bind(&FilterEngine::InitDone,
      this, std::tr1::placeholders::_1));

  {
    // Lock the JS engine while we are loading scripts, no timeouts should fire
    // until we are done.
    const JsContext context(jsEngine);
    for (int i = 0; !jsSources[i].empty(); i += 2)
      jsEngine->Evaluate(jsSources[i + 1], jsSources[i]);
  }

  // TODO: This should really be implemented via a conditional variable
  while (!initialized)
    ::Sleep(10);
}

void FilterEngine::InitDone(JsValueList& params)
{
  jsEngine->RemoveEventCallback("init");
  initialized = true;
  firstRun = params.size() && params[0]->AsBool();
}

bool FilterEngine::IsFirstRun() const
{
  return firstRun;
}

FilterPtr FilterEngine::GetFilter(const std::string& text)
{
  JsValuePtr func = jsEngine->Evaluate("API.getFilterFromText");
  JsValueList params;
  params.push_back(jsEngine->NewValue(text));
  return FilterPtr(new Filter(func->Call(params)));
}

SubscriptionPtr FilterEngine::GetSubscription(const std::string& url)
{
  JsValuePtr func = jsEngine->Evaluate("API.getSubscriptionFromUrl");
  JsValueList params;
  params.push_back(jsEngine->NewValue(url));
  return SubscriptionPtr(new Subscription(func->Call(params)));
}

std::vector<FilterPtr> FilterEngine::GetListedFilters() const
{
  JsValuePtr func = jsEngine->Evaluate("API.getListedFilters");
  JsValueList values = func->Call()->AsList();
  std::vector<FilterPtr> result;
  for (JsValueList::iterator it = values.begin(); it != values.end(); it++)
    result.push_back(FilterPtr(new Filter(*it)));
  return result;
}

std::vector<SubscriptionPtr> FilterEngine::GetListedSubscriptions() const
{
  JsValuePtr func = jsEngine->Evaluate("API.getListedSubscriptions");
  JsValueList values = func->Call()->AsList();
  std::vector<SubscriptionPtr> result;
  for (JsValueList::iterator it = values.begin(); it != values.end(); it++)
    result.push_back(SubscriptionPtr(new Subscription(*it)));
  return result;
}

std::vector<SubscriptionPtr> FilterEngine::FetchAvailableSubscriptions() const
{
  JsValuePtr func = jsEngine->Evaluate("API.getRecommendedSubscriptions");
  JsValueList values = func->Call()->AsList();
  std::vector<SubscriptionPtr> result;
  for (JsValueList::iterator it = values.begin(); it != values.end(); it++)
    result.push_back(SubscriptionPtr(new Subscription(*it)));
  return result;
}

AdblockPlus::FilterPtr FilterEngine::Matches(const std::string& url,
    const std::string& contentType,
    const std::string& documentUrl) const
{
  JsValuePtr func = jsEngine->Evaluate("API.checkFilterMatch");
  JsValueList params;
  params.push_back(jsEngine->NewValue(url));
  params.push_back(jsEngine->NewValue(contentType));
  params.push_back(jsEngine->NewValue(documentUrl));
  JsValuePtr result = func->Call(params);
  if (!result->IsNull())
    return FilterPtr(new Filter(result));
  else
    return FilterPtr();
}

std::vector<std::string> FilterEngine::GetElementHidingSelectors(const std::string& domain) const
{
  JsValuePtr func = jsEngine->Evaluate("API.getElementHidingSelectors");
  JsValueList params;
  params.push_back(jsEngine->NewValue(domain));
  JsValueList result = func->Call(params)->AsList();
  std::vector<std::string> selectors;
  for (JsValueList::iterator it = result.begin(); it != result.end(); ++it)
    selectors.push_back((*it)->AsString());
  return selectors;
}

JsValuePtr FilterEngine::GetPref(const std::string& pref) const
{
  JsValuePtr func = jsEngine->Evaluate("API.getPref");
  JsValueList params;
  params.push_back(jsEngine->NewValue(pref));
  return func->Call(params);
}

void FilterEngine::SetPref(const std::string& pref, JsValuePtr value)
{
  JsValuePtr func = jsEngine->Evaluate("API.setPref");
  JsValueList params;
  params.push_back(jsEngine->NewValue(pref));
  params.push_back(value);
  func->Call(params);
}

void FilterEngine::ForceUpdateCheck(FilterEngine::UpdaterCallback callback)
{
  std::string eventName = "updateCheckDone";
  eventName += ++updateCheckId;

  jsEngine->SetEventCallback(eventName, std::tr1::bind(&FilterEngine::UpdateCheckDone,
      this, eventName, callback, std::tr1::placeholders::_1));

  JsValuePtr func = jsEngine->Evaluate("API.forceUpdateCheck");
  JsValueList params;
  params.push_back(jsEngine->NewValue(eventName));
  func->Call(params);
}

void FilterEngine::UpdateCheckDone(const std::string& eventName, FilterEngine::UpdaterCallback callback, JsValueList& params)
{
  jsEngine->RemoveEventCallback(eventName);

  std::string error(params.size() >= 1 && !params[0]->IsNull() ? params[0]->AsString() : "");
  callback(error);
}
