// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/dbus/shill_service_client_stub.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/command_line.h"
#include "base/message_loop/message_loop.h"
#include "base/stl_util.h"
#include "base/strings/string_util.h"
#include "base/values.h"
#include "chromeos/chromeos_switches.h"
#include "chromeos/dbus/dbus_thread_manager.h"
#include "chromeos/dbus/shill_manager_client.h"
#include "chromeos/dbus/shill_property_changed_observer.h"
#include "dbus/bus.h"
#include "dbus/message.h"
#include "dbus/object_path.h"
#include "third_party/cros_system_api/dbus/service_constants.h"

namespace chromeos {

namespace {

void ErrorFunction(const std::string& error_name,
                   const std::string& error_message) {
  LOG(ERROR) << "Shill Error: " << error_name << " : " << error_message;
}

void PassStubListValue(const ShillServiceClient::ListValueCallback& callback,
                       base::ListValue* value) {
  callback.Run(*value);
}

void PassStubServiceProperties(
    const ShillServiceClient::DictionaryValueCallback& callback,
    DBusMethodCallStatus call_status,
    const base::DictionaryValue* properties) {
  callback.Run(call_status, *properties);
}

}  // namespace

ShillServiceClientStub::ShillServiceClientStub() : weak_ptr_factory_(this) {
}

ShillServiceClientStub::~ShillServiceClientStub() {
  STLDeleteContainerPairSecondPointers(
      observer_list_.begin(), observer_list_.end());
}


// ShillServiceClient overrides.

void ShillServiceClientStub::Init(dbus::Bus* bus) {
}

void ShillServiceClientStub::AddPropertyChangedObserver(
    const dbus::ObjectPath& service_path,
    ShillPropertyChangedObserver* observer) {
  GetObserverList(service_path).AddObserver(observer);
}

void ShillServiceClientStub::RemovePropertyChangedObserver(
    const dbus::ObjectPath& service_path,
    ShillPropertyChangedObserver* observer) {
  GetObserverList(service_path).RemoveObserver(observer);
}

void ShillServiceClientStub::GetProperties(
    const dbus::ObjectPath& service_path,
    const DictionaryValueCallback& callback) {
  base::DictionaryValue* nested_dict = NULL;
  scoped_ptr<base::DictionaryValue> result_properties;
  DBusMethodCallStatus call_status;
  stub_services_.GetDictionaryWithoutPathExpansion(service_path.value(),
                                                   &nested_dict);
  if (nested_dict) {
    result_properties.reset(nested_dict->DeepCopy());
    // Remove credentials that Shill wouldn't send.
    result_properties->RemoveWithoutPathExpansion(flimflam::kPassphraseProperty,
                                                  NULL);
    call_status = DBUS_METHOD_CALL_SUCCESS;
  } else {
    LOG(ERROR) << "Properties not found for: " << service_path.value();
    result_properties.reset(new base::DictionaryValue);
    call_status = DBUS_METHOD_CALL_FAILURE;
  }

  base::MessageLoop::current()->PostTask(
      FROM_HERE,
      base::Bind(&PassStubServiceProperties,
                 callback,
                 call_status,
                 base::Owned(result_properties.release())));
}

void ShillServiceClientStub::SetProperty(const dbus::ObjectPath& service_path,
                                         const std::string& name,
                                         const base::Value& value,
                                         const base::Closure& callback,
                                         const ErrorCallback& error_callback) {
  if (!SetServiceProperty(service_path.value(), name, value)) {
    LOG(ERROR) << "Service not found: " << service_path.value();
    error_callback.Run("Error.InvalidService", "Invalid Service");
    return;
  }
  base::MessageLoop::current()->PostTask(FROM_HERE, callback);
}

void ShillServiceClientStub::SetProperties(
    const dbus::ObjectPath& service_path,
    const base::DictionaryValue& properties,
    const base::Closure& callback,
    const ErrorCallback& error_callback) {
  for (base::DictionaryValue::Iterator iter(properties);
       !iter.IsAtEnd(); iter.Advance()) {
    if (!SetServiceProperty(service_path.value(), iter.key(), iter.value())) {
      LOG(ERROR) << "Service not found: " << service_path.value();
      error_callback.Run("Error.InvalidService", "Invalid Service");
      return;
    }
  }
  base::MessageLoop::current()->PostTask(FROM_HERE, callback);
}

void ShillServiceClientStub::ClearProperty(
    const dbus::ObjectPath& service_path,
    const std::string& name,
    const base::Closure& callback,
    const ErrorCallback& error_callback) {
  base::DictionaryValue* dict = NULL;
  if (!stub_services_.GetDictionaryWithoutPathExpansion(
      service_path.value(), &dict)) {
    error_callback.Run("Error.InvalidService", "Invalid Service");
    return;
  }
  dict->RemoveWithoutPathExpansion(name, NULL);
  // Note: Shill does not send notifications when properties are cleared.
  base::MessageLoop::current()->PostTask(FROM_HERE, callback);
}

void ShillServiceClientStub::ClearProperties(
    const dbus::ObjectPath& service_path,
    const std::vector<std::string>& names,
    const ListValueCallback& callback,
    const ErrorCallback& error_callback) {
  base::DictionaryValue* dict = NULL;
  if (!stub_services_.GetDictionaryWithoutPathExpansion(
      service_path.value(), &dict)) {
    error_callback.Run("Error.InvalidService", "Invalid Service");
    return;
  }
  scoped_ptr<base::ListValue> results(new base::ListValue);
  for (std::vector<std::string>::const_iterator iter = names.begin();
      iter != names.end(); ++iter) {
    dict->RemoveWithoutPathExpansion(*iter, NULL);
    // Note: Shill does not send notifications when properties are cleared.
    results->AppendBoolean(true);
  }
  base::MessageLoop::current()->PostTask(
      FROM_HERE,
      base::Bind(&PassStubListValue,
                 callback, base::Owned(results.release())));
}

void ShillServiceClientStub::Connect(const dbus::ObjectPath& service_path,
                                     const base::Closure& callback,
                                     const ErrorCallback& error_callback) {
  VLOG(1) << "ShillServiceClientStub::Connect: " << service_path.value();
  base::DictionaryValue* service_properties = NULL;
  if (!stub_services_.GetDictionary(
          service_path.value(), &service_properties)) {
    LOG(ERROR) << "Service not found: " << service_path.value();
    error_callback.Run("Error.InvalidService", "Invalid Service");
    return;
  }

  // Set any other services of the same Type to 'offline' first, before setting
  // State to Association which will trigger sorting Manager.Services and
  // sending an update.
  SetOtherServicesOffline(service_path.value());

  // Set Associating.
  base::StringValue associating_value(flimflam::kStateAssociation);
  SetServiceProperty(service_path.value(),
                     flimflam::kStateProperty,
                     associating_value);

  // Stay Associating until the state is changed again after a delay.
  base::TimeDelta delay;
  if (CommandLine::ForCurrentProcess()->HasSwitch(
          chromeos::switches::kEnableStubInteractive)) {
    const int kConnectDelaySeconds = 5;
    delay = base::TimeDelta::FromSeconds(kConnectDelaySeconds);
  }
  base::MessageLoop::current()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&ShillServiceClientStub::ContinueConnect,
                 weak_ptr_factory_.GetWeakPtr(),
                 service_path.value()),
      delay);

  callback.Run();
}

void ShillServiceClientStub::Disconnect(const dbus::ObjectPath& service_path,
                                        const base::Closure& callback,
                                        const ErrorCallback& error_callback) {
  base::Value* service;
  if (!stub_services_.Get(service_path.value(), &service)) {
    error_callback.Run("Error.InvalidService", "Invalid Service");
    return;
  }
  base::TimeDelta delay;
  if (CommandLine::ForCurrentProcess()->HasSwitch(
          chromeos::switches::kEnableStubInteractive)) {
    const int kConnectDelaySeconds = 2;
    delay = base::TimeDelta::FromSeconds(kConnectDelaySeconds);
  }
  // Set Idle after a delay
  base::StringValue idle_value(flimflam::kStateIdle);
  base::MessageLoop::current()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&ShillServiceClientStub::SetProperty,
                 weak_ptr_factory_.GetWeakPtr(),
                 service_path,
                 flimflam::kStateProperty,
                 idle_value,
                 base::Bind(&base::DoNothing),
                 error_callback),
      delay);
  callback.Run();
}

void ShillServiceClientStub::Remove(const dbus::ObjectPath& service_path,
                                    const base::Closure& callback,
                                    const ErrorCallback& error_callback) {
  base::MessageLoop::current()->PostTask(FROM_HERE, callback);
}

void ShillServiceClientStub::ActivateCellularModem(
    const dbus::ObjectPath& service_path,
    const std::string& carrier,
    const base::Closure& callback,
    const ErrorCallback& error_callback) {
  base::DictionaryValue* service_properties =
      GetModifiableServiceProperties(service_path.value(), false);
  if (!service_properties) {
    LOG(ERROR) << "Service not found: " << service_path.value();
    error_callback.Run("Error.InvalidService", "Invalid Service");
  }
  SetServiceProperty(service_path.value(),
                     flimflam::kActivationStateProperty,
                     base::StringValue(flimflam::kActivationStateActivating));
  base::TimeDelta delay;
  if (CommandLine::ForCurrentProcess()->HasSwitch(
          chromeos::switches::kEnableStubInteractive)) {
    const int kConnectDelaySeconds = 2;
    delay = base::TimeDelta::FromSeconds(kConnectDelaySeconds);
  }
  // Set Activated after a delay
  base::MessageLoop::current()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&ShillServiceClientStub::SetCellularActivated,
                 weak_ptr_factory_.GetWeakPtr(),
                 service_path,
                 error_callback),
      delay);

  base::MessageLoop::current()->PostTask(FROM_HERE, callback);
}

void ShillServiceClientStub::CompleteCellularActivation(
    const dbus::ObjectPath& service_path,
    const base::Closure& callback,
    const ErrorCallback& error_callback) {
  base::MessageLoop::current()->PostTask(FROM_HERE, callback);
}

void ShillServiceClientStub::GetLoadableProfileEntries(
    const dbus::ObjectPath& service_path,
    const DictionaryValueCallback& callback) {
  // Provide a dictionary with a single { profile_path, service_path } entry
  // if the Profile property is set, or an empty dictionary.
  scoped_ptr<base::DictionaryValue> result_properties(
      new base::DictionaryValue);
  base::DictionaryValue* service_properties =
      GetModifiableServiceProperties(service_path.value(), false);
  if (service_properties) {
    std::string profile_path;
    if (service_properties->GetStringWithoutPathExpansion(
            flimflam::kProfileProperty, &profile_path)) {
      result_properties->SetStringWithoutPathExpansion(
          profile_path, service_path.value());
    }
  } else {
    LOG(WARNING) << "Service not in profile: " << service_path.value();
  }

  DBusMethodCallStatus call_status = DBUS_METHOD_CALL_SUCCESS;
  base::MessageLoop::current()->PostTask(
      FROM_HERE,
      base::Bind(&PassStubServiceProperties,
                 callback,
                 call_status,
                 base::Owned(result_properties.release())));
}

ShillServiceClient::TestInterface* ShillServiceClientStub::GetTestInterface() {
  return this;
}

// ShillServiceClient::TestInterface overrides.

void ShillServiceClientStub::AddService(const std::string& service_path,
                                        const std::string& name,
                                        const std::string& type,
                                        const std::string& state,
                                        bool add_to_visible_list,
                                        bool add_to_watch_list) {
  std::string nstate = state;
  if (CommandLine::ForCurrentProcess()->HasSwitch(
          chromeos::switches::kDefaultStubNetworkStateIdle)) {
    nstate = flimflam::kStateIdle;
  }
  AddServiceWithIPConfig(service_path, name, type, nstate, "",
                         add_to_visible_list, add_to_watch_list);
}

void ShillServiceClientStub::AddServiceWithIPConfig(
    const std::string& service_path,
    const std::string& name,
    const std::string& type,
    const std::string& state,
    const std::string& ipconfig_path,
    bool add_to_visible_list,
    bool add_to_watch_list) {
  DBusThreadManager::Get()->GetShillManagerClient()->GetTestInterface()->
      AddManagerService(service_path, add_to_visible_list, add_to_watch_list);

  base::DictionaryValue* properties =
      GetModifiableServiceProperties(service_path, true);
  connect_behavior_.erase(service_path);
  properties->SetWithoutPathExpansion(
      flimflam::kSSIDProperty,
      base::Value::CreateStringValue(service_path));
  properties->SetWithoutPathExpansion(
      flimflam::kNameProperty,
      base::Value::CreateStringValue(name));
  properties->SetWithoutPathExpansion(
      flimflam::kTypeProperty,
      base::Value::CreateStringValue(type));
  properties->SetWithoutPathExpansion(
      flimflam::kStateProperty,
      base::Value::CreateStringValue(state));
  if (!ipconfig_path.empty())
    properties->SetWithoutPathExpansion(
        shill::kIPConfigProperty,
        base::Value::CreateStringValue(ipconfig_path));
}

void ShillServiceClientStub::RemoveService(const std::string& service_path) {
  DBusThreadManager::Get()->GetShillManagerClient()->GetTestInterface()->
      RemoveManagerService(service_path);

  stub_services_.RemoveWithoutPathExpansion(service_path, NULL);
  connect_behavior_.erase(service_path);
}

bool ShillServiceClientStub::SetServiceProperty(const std::string& service_path,
                                                const std::string& property,
                                                const base::Value& value) {
  base::DictionaryValue* dict = NULL;
  if (!stub_services_.GetDictionaryWithoutPathExpansion(service_path, &dict))
    return false;

  VLOG(1) << "Service.SetProperty: " << property << " = " << value
          << " For: " << service_path;

  base::DictionaryValue new_properties;
  std::string changed_property;
  bool case_sensitive = true;
  if (StartsWithASCII(property, "Provider.", case_sensitive) ||
      StartsWithASCII(property, "OpenVPN.", case_sensitive) ||
      StartsWithASCII(property, "L2TPIPsec.", case_sensitive)) {
    // These properties are only nested within the Provider dictionary if read
    // from Shill.
    base::DictionaryValue* provider = new base::DictionaryValue;
    provider->SetWithoutPathExpansion(property, value.DeepCopy());
    new_properties.SetWithoutPathExpansion(flimflam::kProviderProperty,
                                           provider);
    changed_property = flimflam::kProviderProperty;
  } else {
    new_properties.SetWithoutPathExpansion(property, value.DeepCopy());
    changed_property = property;
  }

  dict->MergeDictionary(&new_properties);

  if (property == flimflam::kStateProperty) {
    // When State changes the sort order of Services may change.
    DBusThreadManager::Get()->GetShillManagerClient()->GetTestInterface()->
        SortManagerServices();
  }

  base::MessageLoop::current()->PostTask(
      FROM_HERE,
      base::Bind(&ShillServiceClientStub::NotifyObserversPropertyChanged,
                 weak_ptr_factory_.GetWeakPtr(),
                 dbus::ObjectPath(service_path), changed_property));
  return true;
}

const base::DictionaryValue* ShillServiceClientStub::GetServiceProperties(
    const std::string& service_path) const {
  const base::DictionaryValue* properties = NULL;
  stub_services_.GetDictionaryWithoutPathExpansion(service_path, &properties);
  return properties;
}

void ShillServiceClientStub::ClearServices() {
  DBusThreadManager::Get()->GetShillManagerClient()->GetTestInterface()->
      ClearManagerServices();

  stub_services_.Clear();
  connect_behavior_.clear();
}

void ShillServiceClientStub::SetConnectBehavior(const std::string& service_path,
                                const base::Closure& behavior) {
  connect_behavior_[service_path] = behavior;
}

void ShillServiceClientStub::NotifyObserversPropertyChanged(
    const dbus::ObjectPath& service_path,
    const std::string& property) {
  base::DictionaryValue* dict = NULL;
  std::string path = service_path.value();
  if (!stub_services_.GetDictionaryWithoutPathExpansion(path, &dict)) {
    LOG(ERROR) << "Notify for unknown service: " << path;
    return;
  }
  base::Value* value = NULL;
  if (!dict->GetWithoutPathExpansion(property, &value)) {
    LOG(ERROR) << "Notify for unknown property: "
               << path << " : " << property;
    return;
  }
  FOR_EACH_OBSERVER(ShillPropertyChangedObserver,
                    GetObserverList(service_path),
                    OnPropertyChanged(property, *value));
}

base::DictionaryValue* ShillServiceClientStub::GetModifiableServiceProperties(
    const std::string& service_path, bool create_if_missing) {
  base::DictionaryValue* properties = NULL;
  if (!stub_services_.GetDictionaryWithoutPathExpansion(service_path,
                                                        &properties) &&
      create_if_missing) {
    properties = new base::DictionaryValue;
    stub_services_.Set(service_path, properties);
  }
  return properties;
}

ShillServiceClientStub::PropertyObserverList&
ShillServiceClientStub::GetObserverList(const dbus::ObjectPath& device_path) {
  std::map<dbus::ObjectPath, PropertyObserverList*>::iterator iter =
      observer_list_.find(device_path);
  if (iter != observer_list_.end())
    return *(iter->second);
  PropertyObserverList* observer_list = new PropertyObserverList();
  observer_list_[device_path] = observer_list;
  return *observer_list;
}

void ShillServiceClientStub::SetOtherServicesOffline(
    const std::string& service_path) {
  const base::DictionaryValue* service_properties = GetServiceProperties(
      service_path);
  if (!service_properties) {
    LOG(ERROR) << "Missing service: " << service_path;
    return;
  }
  std::string service_type;
  service_properties->GetString(flimflam::kTypeProperty, &service_type);
  // Set all other services of the same type to offline (Idle).
  for (base::DictionaryValue::Iterator iter(stub_services_);
       !iter.IsAtEnd(); iter.Advance()) {
    std::string path = iter.key();
    if (path == service_path)
      continue;
    base::DictionaryValue* properties;
    if (!stub_services_.GetDictionaryWithoutPathExpansion(path, &properties))
      NOTREACHED();

    std::string type;
    properties->GetString(flimflam::kTypeProperty, &type);
    if (type != service_type)
      continue;
    properties->SetWithoutPathExpansion(
        flimflam::kStateProperty,
        base::Value::CreateStringValue(flimflam::kStateIdle));
  }
}

void ShillServiceClientStub::SetCellularActivated(
    const dbus::ObjectPath& service_path,
    const ErrorCallback& error_callback) {
  SetProperty(service_path,
              flimflam::kActivationStateProperty,
              base::StringValue(flimflam::kActivationStateActivated),
              base::Bind(&base::DoNothing),
              error_callback);
  SetProperty(service_path,
              flimflam::kConnectableProperty,
              base::FundamentalValue(true),
              base::Bind(&base::DoNothing),
              error_callback);
}

void ShillServiceClientStub::ContinueConnect(
    const std::string& service_path) {
  VLOG(1) << "ShillServiceClientStub::ContinueConnect: " << service_path;
  base::DictionaryValue* service_properties = NULL;
  if (!stub_services_.GetDictionary(service_path, &service_properties)) {
    LOG(ERROR) << "Service not found: " << service_path;
    return;
  }

  if (ContainsKey(connect_behavior_, service_path)) {
    const base::Closure& custom_connect_behavior =
        connect_behavior_[service_path];
    custom_connect_behavior.Run();
    return;
  }

  // No custom connect behavior set, continue with the default connect behavior.
  std::string passphrase;
  service_properties->GetStringWithoutPathExpansion(
      flimflam::kPassphraseProperty, &passphrase);
  if (passphrase == "failure") {
    // Simulate a password failure.
    SetServiceProperty(service_path,
                       flimflam::kStateProperty,
                       base::StringValue(flimflam::kStateFailure));
    base::MessageLoop::current()->PostTask(
        FROM_HERE,
        base::Bind(
            base::IgnoreResult(&ShillServiceClientStub::SetServiceProperty),
            weak_ptr_factory_.GetWeakPtr(),
            service_path,
            flimflam::kErrorProperty,
            base::StringValue(flimflam::kErrorBadPassphrase)));
  } else {
    // Set Online.
    SetServiceProperty(service_path,
                       flimflam::kStateProperty,
                       base::StringValue(flimflam::kStateOnline));
  }
}

}  // namespace chromeos
