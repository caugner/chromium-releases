// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/devtools/devtools_embedder_message_dispatcher.h"

#include "base/bind.h"
#include "base/json/json_reader.h"
#include "base/values.h"

namespace {

static const char kFrontendHostMethod[] = "method";
static const char kFrontendHostParams[] = "params";

bool GetValue(const base::ListValue& list, int pos, std::string& value) {
  return list.GetString(pos, &value);
}

bool GetValue(const base::ListValue& list, int pos, int& value) {
  return list.GetInteger(pos, &value);
}

bool GetValue(const base::ListValue& list, int pos, bool& value) {
  return list.GetBoolean(pos, &value);
}

template <typename T>
struct StorageTraits {
  typedef T StorageType;
};

template <typename T>
struct StorageTraits<const T&> {
  typedef T StorageType;
};

template <class A>
class Argument {
 public:
  typedef typename StorageTraits<A>::StorageType ValueType;

  Argument(const base::ListValue& list, int pos) {
    valid_ = GetValue(list, pos, value_);
  }

  ValueType value() const { return value_; }
  bool valid() const { return valid_; }

 private:
  ValueType value_;
  bool valid_;
};

bool ParseAndHandle0(const base::Callback<void(void)>& handler,
                     const base::ListValue& list) {
  handler.Run();
  return true;
}

template <class A1>
bool ParseAndHandle1(const base::Callback<void(A1)>& handler,
                     const base::ListValue& list) {
  if (list.GetSize() != 1)
    return false;
  Argument<A1> arg1(list, 0);
  if (!arg1.valid())
    return false;
  handler.Run(arg1.value());
  return true;
}

template <class A1, class A2>
bool ParseAndHandle2(const base::Callback<void(A1, A2)>& handler,
                     const base::ListValue& list) {
  if (list.GetSize() != 2)
    return false;
  Argument<A1> arg1(list, 0);
  if (!arg1.valid())
    return false;
  Argument<A2> arg2(list, 1);
  if (!arg2.valid())
    return false;
  handler.Run(arg1.value(), arg2.value());
  return true;
}

template <class A1, class A2, class A3>
bool ParseAndHandle3(const base::Callback<void(A1, A2, A3)>& handler,
                     const base::ListValue& list) {
  if (list.GetSize() != 3)
    return false;
  Argument<A1> arg1(list, 0);
  if (!arg1.valid())
    return false;
  Argument<A2> arg2(list, 1);
  if (!arg2.valid())
    return false;
  Argument<A3> arg3(list, 2);
  if (!arg3.valid())
    return false;
  handler.Run(arg1.value(), arg2.value(), arg3.value());
  return true;
}

typedef base::Callback<bool(const base::ListValue&)> ListValueParser;

ListValueParser BindToListParser(const base::Callback<void()>& handler) {
  return base::Bind(&ParseAndHandle0, handler);
}

template <class A1>
ListValueParser BindToListParser(const base::Callback<void(A1)>& handler) {
  return base::Bind(&ParseAndHandle1<A1>, handler);
}

template <class A1, class A2>
ListValueParser BindToListParser(const base::Callback<void(A1,A2)>& handler) {
  return base::Bind(&ParseAndHandle2<A1, A2>, handler);
}

template <class A1, class A2, class A3>
ListValueParser BindToListParser(
    const base::Callback<void(A1,A2,A3)>& handler) {
  return base::Bind(&ParseAndHandle3<A1, A2, A3>, handler);
}

}  // namespace

DevToolsEmbedderMessageDispatcher::DevToolsEmbedderMessageDispatcher(
    Delegate* delegate) {
  RegisterHandler("bringToFront",
      BindToListParser(base::Bind(&Delegate::ActivateWindow,
                                  base::Unretained(delegate))));
  RegisterHandler("closeWindow",
      BindToListParser(base::Bind(&Delegate::CloseWindow,
                                  base::Unretained(delegate))));
  RegisterHandler("moveWindowBy",
      BindToListParser(base::Bind(&Delegate::MoveWindow,
                                  base::Unretained(delegate))));
  RegisterHandler("requestSetDockSide",
      BindToListParser(base::Bind(&Delegate::SetDockSide,
                                  base::Unretained(delegate))));
  RegisterHandler("openInNewTab",
      BindToListParser(base::Bind(&Delegate::OpenInNewTab,
                                  base::Unretained(delegate))));
  RegisterHandler("save",
      BindToListParser(base::Bind(&Delegate::SaveToFile,
                                  base::Unretained(delegate))));
  RegisterHandler("append",
      BindToListParser(base::Bind(&Delegate::AppendToFile,
                                  base::Unretained(delegate))));
  RegisterHandler("requestFileSystems",
      BindToListParser(base::Bind(&Delegate::RequestFileSystems,
                                  base::Unretained(delegate))));
  RegisterHandler("addFileSystem",
      BindToListParser(base::Bind(&Delegate::AddFileSystem,
                                  base::Unretained(delegate))));
  RegisterHandler("removeFileSystem",
      BindToListParser(base::Bind(&Delegate::RemoveFileSystem,
                                  base::Unretained(delegate))));
  RegisterHandler("indexPath",
      BindToListParser(base::Bind(&Delegate::IndexPath,
                                  base::Unretained(delegate))));
  RegisterHandler("stopIndexing",
      BindToListParser(base::Bind(&Delegate::StopIndexing,
                                  base::Unretained(delegate))));
  RegisterHandler("searchInPath",
      BindToListParser(base::Bind(&Delegate::SearchInPath,
                                  base::Unretained(delegate))));
}

DevToolsEmbedderMessageDispatcher::~DevToolsEmbedderMessageDispatcher() {}

void DevToolsEmbedderMessageDispatcher::Dispatch(const std::string& message) {
  std::string method;
  base::ListValue empty_params;
  base::ListValue* params = &empty_params;

  base::DictionaryValue* dict;
  scoped_ptr<base::Value> parsed_message(base::JSONReader::Read(message));
  if (!parsed_message ||
      !parsed_message->GetAsDictionary(&dict) ||
      !dict->GetString(kFrontendHostMethod, &method) ||
      (dict->HasKey(kFrontendHostParams) &&
          !dict->GetList(kFrontendHostParams, &params))) {
    LOG(ERROR) << "Cannot parse frontend host message: " << message;
    return;
  }

  HandlerMap::iterator it = handlers_.find(method);
  if (it == handlers_.end()) {
    LOG(ERROR) << "Unsupported frontend host method: " << message;
    return;
  }

  if (!it->second.Run(*params))
    LOG(ERROR) << "Invalid frontend host message parameters: " << message;
}

void DevToolsEmbedderMessageDispatcher::RegisterHandler(
    const std::string& method, const Handler& handler) {
  handlers_[method] = handler;
}
