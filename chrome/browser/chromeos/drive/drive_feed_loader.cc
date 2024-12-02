// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/drive/drive_feed_loader.h"

#include <set>

#include "base/command_line.h"
#include "base/file_util.h"
#include "base/format_macros.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/metrics/histogram.h"
#include "base/stringprintf.h"
#include "base/threading/sequenced_worker_pool.h"
#include "base/values.h"
#include "chrome/browser/chromeos/drive/drive_cache.h"
#include "chrome/browser/chromeos/drive/drive_feed_loader_observer.h"
#include "chrome/browser/chromeos/drive/drive_feed_processor.h"
#include "chrome/browser/chromeos/drive/drive_file_system_util.h"
#include "chrome/browser/chromeos/drive/drive_webapps_registry.h"
#include "chrome/browser/google_apis/drive_api_parser.h"
#include "chrome/browser/google_apis/drive_service_interface.h"
#include "chrome/browser/google_apis/gdata_util.h"
#include "chrome/common/chrome_switches.h"
#include "content/public/browser/browser_thread.h"

using content::BrowserThread;

namespace drive {

namespace {

const FilePath::CharType kAccountMetadataFile[] =
    FILE_PATH_LITERAL("account_metadata.json");
const FilePath::CharType kFilesystemProtoFile[] =
    FILE_PATH_LITERAL("file_system.pb");
const FilePath::CharType kResourceMetadataDBFile[] =
    FILE_PATH_LITERAL("resource_metadata.db");

// Update the fetch progress UI per every this number of feeds.
const int kFetchUiUpdateStep = 10;

// Schedule for dumping root file system proto buffers to disk depending its
// total protobuffer size in MB.
typedef struct {
  double size;
  int timeout;
} SerializationTimetable;

SerializationTimetable kSerializeTimetable[] = {
#ifndef NDEBUG
    {0.5, 0},    // Less than 0.5MB, dump immediately.
    {-1,  1},    // Any size, dump if older than 1 minute.
#else
    {0.5, 0},    // Less than 0.5MB, dump immediately.
    {1.0, 15},   // Less than 1.0MB, dump after 15 minutes.
    {2.0, 30},
    {4.0, 60},
    {-1,  120},  // Any size, dump if older than 120 minutes.
#endif
};

// Loads the file at |path| into the string |serialized_proto| on a blocking
// thread.
void LoadProtoOnBlockingPool(const FilePath& path,
                             LoadRootFeedParams* params) {
  base::PlatformFileInfo info;
  if (!file_util::GetFileInfo(path, &info)) {
    params->load_error = DRIVE_FILE_ERROR_NOT_FOUND;
    return;
  }
  params->last_modified = info.last_modified;
  if (!file_util::ReadFileToString(path, &params->proto)) {
    LOG(WARNING) << "Proto file not found at " << path.value();
    params->load_error = DRIVE_FILE_ERROR_NOT_FOUND;
    return;
  }
  params->load_error = DRIVE_FILE_OK;
}

// Saves json file content content in |feed| to |file_pathname| on blocking
// pool. Used for debugging.
void SaveFeedOnBlockingPoolForDebugging(
    const FilePath& file_path,
    scoped_ptr<base::Value> feed) {
  std::string json;
  base::JSONWriter::WriteWithOptions(feed.get(),
                                     base::JSONWriter::OPTIONS_PRETTY_PRINT,
                                     &json);

  int file_size = static_cast<int>(json.length());
  if (file_util::WriteFile(file_path, json.data(), file_size) != file_size) {
    LOG(WARNING) << "Drive metadata file can't be stored at "
                 << file_path.value();
    if (!file_util::Delete(file_path, true)) {
      LOG(WARNING) << "Drive metadata file can't be deleted at "
                   << file_path.value();
      return;
    }
  }
}

// Returns true if file system is due to be serialized on disk based on it
// |serialized_size| and |last_serialized| timestamp.
bool ShouldSerializeFileSystemNow(size_t serialized_size,
                                  const base::Time& last_serialized) {
  const double size_in_mb = serialized_size / 1048576.0;
  const int last_proto_dump_in_min =
      (base::Time::Now() - last_serialized).InMinutes();
  for (size_t i = 0; i < arraysize(kSerializeTimetable); i++) {
    if ((size_in_mb < kSerializeTimetable[i].size ||
         kSerializeTimetable[i].size == -1) &&
        last_proto_dump_in_min >= kSerializeTimetable[i].timeout) {
      return true;
    }
  }
  return false;
}

// Saves the string |serialized_proto| to a file at |path| on a blocking thread.
void SaveProtoOnBlockingPool(const FilePath& path,
                             scoped_ptr<std::string> serialized_proto) {
  const int file_size = static_cast<int>(serialized_proto->length());
  if (file_util::WriteFile(path, serialized_proto->data(), file_size) !=
      file_size) {
    LOG(WARNING) << "Drive proto file can't be stored at "
                 << path.value();
    if (!file_util::Delete(path, true)) {
      LOG(WARNING) << "Drive proto file can't be deleted at "
                   << path.value();
    }
  }
}

bool UseLevelDB() {
  // TODO(achuith): Re-enable this.
  return false;
}

// Run params->feed_load_callback with |error|.
void RunFeedLoadCallback(scoped_ptr<LoadFeedParams> params,
                         DriveFileError error) {
  // Need a reference before calling Pass().
  const LoadDocumentFeedCallback& feed_load_callback =
      params->feed_load_callback;
  feed_load_callback.Run(params.Pass(), error);
}

// Parses a google_apis::DocumentFeed from |data|.
void ParseFeedOnBlockingPool(
    scoped_ptr<base::Value> data,
    scoped_ptr<google_apis::DocumentFeed>* out_current_feed) {
  DCHECK(out_current_feed);
  out_current_feed->reset(
      google_apis::DocumentFeed::ExtractAndParse(*data).release());
}

}  // namespace

LoadFeedParams::LoadFeedParams(
    const LoadDocumentFeedCallback& feed_load_callback)
    : start_changestamp(0),
      root_feed_changestamp(0),
      load_subsequent_feeds(true),
      feed_load_callback(feed_load_callback) {
  DCHECK(!feed_load_callback.is_null());
}

LoadFeedParams::~LoadFeedParams() {
}

LoadRootFeedParams::LoadRootFeedParams(
    const FileOperationCallback& callback)
    : load_error(DRIVE_FILE_OK),
      load_start_time(base::Time::Now()),
      callback(callback) {
}

LoadRootFeedParams::~LoadRootFeedParams() {
}

// Defines set of parameters sent to callback OnNotifyDocumentFeedFetched().
// This is a trick to update the number of fetched documents frequently on
// UI. Due to performance reason, we need to fetch a number of files at
// a time. However, it'll take long time, and a user has no way to know
// the current update state. In order to make users comfortable,
// we increment the number of fetched documents with more frequent but smaller
// steps than actual fetching.
struct GetDocumentsUiState {
  explicit GetDocumentsUiState(base::TimeTicks start_time)
      : num_fetched_documents(0),
        num_showing_documents(0),
        start_time(start_time),
        weak_ptr_factory(this) {
  }

  // The number of fetched documents.
  int num_fetched_documents;

  // The number documents shown on UI.
  int num_showing_documents;

  // When the UI update has started.
  base::TimeTicks start_time;

  // Time elapsed since the feed fetching was started.
  base::TimeDelta feed_fetching_elapsed_time;

  base::WeakPtrFactory<GetDocumentsUiState> weak_ptr_factory;
};

DriveFeedLoader::DriveFeedLoader(
    DriveResourceMetadata* resource_metadata,
    google_apis::DriveServiceInterface* drive_service,
    DriveWebAppsRegistryInterface* webapps_registry,
    DriveCache* cache,
    scoped_refptr<base::SequencedTaskRunner> blocking_task_runner)
    : resource_metadata_(resource_metadata),
      drive_service_(drive_service),
      webapps_registry_(webapps_registry),
      cache_(cache),
      blocking_task_runner_(blocking_task_runner),
      refreshing_(false),
      ALLOW_THIS_IN_INITIALIZER_LIST(weak_ptr_factory_(this)) {
}

DriveFeedLoader::~DriveFeedLoader() {
}

void DriveFeedLoader::AddObserver(DriveFeedLoaderObserver* observer) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  observers_.AddObserver(observer);
}

void DriveFeedLoader::RemoveObserver(DriveFeedLoaderObserver* observer) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  observers_.RemoveObserver(observer);
}

void DriveFeedLoader::ReloadFromServerIfNeeded(
    const FileOperationCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());
  DVLOG(1) << "ReloadFromServerIfNeeded local_changestamp="
           << resource_metadata_->largest_changestamp()
           << ", origin=" << resource_metadata_->origin();

  // Sets the refreshing flag, so that the caller does not send refresh requests
  // in parallel (see DriveFileSystem::CheckForUpdates).
  //
  // Corresponding "refresh_ = false" is reached as follows.
  // - Control flows to OnGetAboutResource / OnGetAccountMetadata, in which,
  //   - if feed is up to date, "refresh_ = false" and return.
  //   - otherwise always call LoadFromServer() with callback function
  //     OnFeedFromServerLoaded. There we do "refresh_ = false".
  refreshing_ = true;

  // First fetch the latest changestamp to see if there were any new changes
  // there at all.
  if (google_apis::util::IsDriveV2ApiEnabled()) {
    drive_service_->GetAccountMetadata(
        base::Bind(&DriveFeedLoader::OnGetAboutResource,
                   weak_ptr_factory_.GetWeakPtr(),
                   callback));
    // Drive v2 needs a separate application list fetch operation.
    // TODO(kochi): Application list rarely changes and is not necessarily
    // refreshed as often as files.
    drive_service_->GetApplicationInfo(
        base::Bind(&DriveFeedLoader::OnGetApplicationList,
                   weak_ptr_factory_.GetWeakPtr()));
    return;
  }

  drive_service_->GetAccountMetadata(
      base::Bind(&DriveFeedLoader::OnGetAccountMetadata,
                 weak_ptr_factory_.GetWeakPtr(),
                 callback));
}

void DriveFeedLoader::OnGetAccountMetadata(
    const FileOperationCallback& callback,
    google_apis::GDataErrorCode status,
    scoped_ptr<base::Value> feed_data) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());
  DCHECK(refreshing_);

  int64 local_changestamp = resource_metadata_->largest_changestamp();

  scoped_ptr<LoadFeedParams> params(new LoadFeedParams(
      base::Bind(&DriveFeedLoader::OnFeedFromServerLoaded,
                 weak_ptr_factory_.GetWeakPtr())));
  params->start_changestamp = local_changestamp > 0 ? local_changestamp + 1 : 0;
  params->load_finished_callback = callback;

  DriveFileError error = util::GDataToDriveFileError(status);
  if (error != DRIVE_FILE_OK) {
    // Get changes starting from the next changestamp from what we have locally.
    LoadFromServer(params.Pass());
    return;
  }

  scoped_ptr<google_apis::AccountMetadataFeed> account_metadata;
  if (feed_data.get()) {
    account_metadata = google_apis::AccountMetadataFeed::CreateFrom(*feed_data);
#ifndef NDEBUG
    // Save account metadata feed for analysis.
    const FilePath path =
        cache_->GetCacheDirectoryPath(DriveCache::CACHE_TYPE_META).Append(
            kAccountMetadataFile);
    google_apis::util::PostBlockingPoolSequencedTask(
        FROM_HERE,
        blocking_task_runner_,
        base::Bind(&SaveFeedOnBlockingPoolForDebugging,
                   path, base::Passed(&feed_data)));
#endif
  }

  if (!account_metadata.get()) {
    LoadFromServer(params.Pass());
    return;
  }

  webapps_registry_->UpdateFromFeed(*account_metadata.get());

  if (local_changestamp >= account_metadata->largest_changestamp()) {
    if (local_changestamp > account_metadata->largest_changestamp()) {
      LOG(WARNING) << "Cached client feed is fresher than server, client = "
                   << local_changestamp
                   << ", server = "
                   << account_metadata->largest_changestamp();
    }

    // No changes detected, tell the client that the loading was successful.
    refreshing_ = false;
    callback.Run(DRIVE_FILE_OK);
    return;
  }

  // Load changes from the server.
  params->root_feed_changestamp = account_metadata->largest_changestamp();
  LoadFromServer(params.Pass());
}

void DriveFeedLoader::OnGetAboutResource(
    const FileOperationCallback& callback,
    google_apis::GDataErrorCode status,
    scoped_ptr<base::Value> feed_data) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());
  DCHECK(refreshing_);

  int64 local_changestamp = resource_metadata_->largest_changestamp();

  scoped_ptr<LoadFeedParams> params(new LoadFeedParams(
      base::Bind(&DriveFeedLoader::OnFeedFromServerLoaded,
                 weak_ptr_factory_.GetWeakPtr())));
  params->start_changestamp = local_changestamp > 0 ? local_changestamp + 1 : 0;
  params->load_finished_callback = callback;

  DriveFileError error = util::GDataToDriveFileError(status);
  if (error != DRIVE_FILE_OK) {
    // Get changes starting from the next changestamp from what we have locally.
    LoadFromServer(params.Pass());
    return;
  }

  scoped_ptr<google_apis::AboutResource> about_resource;
  if (feed_data.get())
    about_resource = google_apis::AboutResource::CreateFrom(*feed_data);

  if (!about_resource.get()) {
    LoadFromServer(params.Pass());
    return;
  }

  int64 largest_changestamp = about_resource->largest_change_id();
  resource_metadata_->InitializeRootEntry(about_resource->root_folder_id());

  if (local_changestamp >= largest_changestamp) {
    if (local_changestamp > largest_changestamp) {
      LOG(WARNING) << "Cached client feed is fresher than server, client = "
                   << local_changestamp
                   << ", server = "
                   << largest_changestamp;
    }

    // No changes detected, tell the client that the loading was successful.
    refreshing_ = false;
    callback.Run(DRIVE_FILE_OK);
    return;
  }

  // Load changes from the server.
  params->root_feed_changestamp = largest_changestamp;
  LoadFromServer(params.Pass());
}

void DriveFeedLoader::OnGetApplicationList(google_apis::GDataErrorCode status,
                                           scoped_ptr<base::Value> json) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  DriveFileError error = util::GDataToDriveFileError(status);
  if (error != DRIVE_FILE_OK)
    return;

  if (json.get()) {
    scoped_ptr<google_apis::AppList> applist(
        google_apis::AppList::CreateFrom(*json));
    if (applist.get())
      webapps_registry_->UpdateFromApplicationList(*applist.get());
  }
}

void DriveFeedLoader::LoadFromServer(scoped_ptr<LoadFeedParams> params) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  const base::TimeTicks start_time = base::TimeTicks::Now();

  // base::Passed() may get evaluated first, so get a pointer to params.
  LoadFeedParams* params_ptr = params.get();
  if (google_apis::util::IsDriveV2ApiEnabled()) {
    drive_service_->GetDocuments(
        params_ptr->feed_to_load,
        params_ptr->start_changestamp,
        std::string(),  // No search query.
        std::string(),  // No directory resource ID.
        base::Bind(&DriveFeedLoader::OnGetChangelist,
                   weak_ptr_factory_.GetWeakPtr(),
                   base::Passed(&params),
                   start_time));
  } else {
    drive_service_->GetDocuments(
        params_ptr->feed_to_load,
        params_ptr->start_changestamp,
        params_ptr->search_query,
        params_ptr->directory_resource_id,
        base::Bind(&DriveFeedLoader::OnGetDocuments,
                   weak_ptr_factory_.GetWeakPtr(),
                   base::Passed(&params),
                   start_time));
  }
}

void DriveFeedLoader::LoadDirectoryFromServer(
    const std::string& directory_resource_id,
    const LoadDocumentFeedCallback& feed_load_callback) {
  DCHECK(!feed_load_callback.is_null());

  scoped_ptr<LoadFeedParams> params(new LoadFeedParams(feed_load_callback));
  params->directory_resource_id = directory_resource_id;
  LoadFromServer(params.Pass());
}

void DriveFeedLoader::SearchFromServer(
    const std::string& search_query,
    const GURL& next_feed,
    const LoadDocumentFeedCallback& feed_load_callback) {
  DCHECK(!feed_load_callback.is_null());

  scoped_ptr<LoadFeedParams> params(new LoadFeedParams(feed_load_callback));
  params->search_query = search_query;
  params->feed_to_load = next_feed;
  params->load_subsequent_feeds = false;
  LoadFromServer(params.Pass());
}

void DriveFeedLoader::OnFeedFromServerLoaded(scoped_ptr<LoadFeedParams> params,
                                             DriveFileError error) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!params->load_finished_callback.is_null());
  DCHECK(refreshing_);

  if (error == DRIVE_FILE_OK) {
    UpdateFromFeed(params->feed_list,
                   params->start_changestamp,
                   params->root_feed_changestamp);
  }
  refreshing_ = false;

  if (error != DRIVE_FILE_OK) {
    params->load_finished_callback.Run(error);
    return;
  }

  // Save file system metadata to disk.
  SaveFileSystem();

  // Tell the client that the loading was successful.
  params->load_finished_callback.Run(DRIVE_FILE_OK);

  FOR_EACH_OBSERVER(DriveFeedLoaderObserver,
                    observers_,
                    OnFeedFromServerLoaded());
}

void DriveFeedLoader::OnGetDocuments(scoped_ptr<LoadFeedParams> params,
                                     base::TimeTicks start_time,
                                     google_apis::GDataErrorCode status,
                                     scoped_ptr<base::Value> data) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (params->feed_list.empty()) {
    UMA_HISTOGRAM_TIMES("Drive.InitialFeedLoadTime",
                        base::TimeTicks::Now() - start_time);
  }

  DriveFileError error = util::GDataToDriveFileError(status);
  if (error == DRIVE_FILE_OK &&
      (!data.get() || data->GetType() != Value::TYPE_DICTIONARY)) {
    error = DRIVE_FILE_ERROR_FAILED;
  }

  if (error != DRIVE_FILE_OK) {
    RunFeedLoadCallback(params.Pass(), error);
    return;
  }

  scoped_ptr<google_apis::DocumentFeed>* current_feed =
      new scoped_ptr<google_apis::DocumentFeed>;
  google_apis::util::PostBlockingPoolSequencedTaskAndReply(
      FROM_HERE,
      blocking_task_runner_,
      base::Bind(&ParseFeedOnBlockingPool,
                 base::Passed(&data),
                 current_feed),
      base::Bind(&DriveFeedLoader::OnParseFeed,
                 weak_ptr_factory_.GetWeakPtr(),
                 base::Passed(&params),
                 start_time,
                 base::Owned(current_feed)));
}

void DriveFeedLoader::OnParseFeed(
    scoped_ptr<LoadFeedParams> params,
    base::TimeTicks start_time,
    scoped_ptr<google_apis::DocumentFeed>* current_feed) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(current_feed);

  if (!current_feed->get()) {
    RunFeedLoadCallback(params.Pass(), DRIVE_FILE_ERROR_FAILED);
    return;
  }

  GURL next_feed_url;
  const bool has_next_feed_url =
      params->load_subsequent_feeds &&
      (*current_feed)->GetNextFeedURL(&next_feed_url);

  // Add the current feed to the list of collected feeds for this directory.
  params->feed_list.push_back(current_feed->release());

  // Compute and notify the number of entries fetched so far.
  int num_accumulated_entries = 0;
  for (size_t i = 0; i < params->feed_list.size(); ++i)
    num_accumulated_entries += params->feed_list[i]->entries().size();

  // Check if we need to collect more data to complete the directory list.
  if (has_next_feed_url && !next_feed_url.is_empty()) {
    // Post an UI update event to make the UI smoother.
    GetDocumentsUiState* ui_state = params->ui_state.get();
    if (ui_state == NULL) {
      ui_state = new GetDocumentsUiState(base::TimeTicks::Now());
      params->ui_state.reset(ui_state);
    }
    DCHECK(ui_state);

    if ((ui_state->num_fetched_documents - ui_state->num_showing_documents)
        < kFetchUiUpdateStep) {
      // Currently the UI update is stopped. Start UI periodic callback.
      base::MessageLoopProxy::current()->PostTask(
          FROM_HERE,
          base::Bind(&DriveFeedLoader::OnNotifyDocumentFeedFetched,
                     weak_ptr_factory_.GetWeakPtr(),
                     ui_state->weak_ptr_factory.GetWeakPtr()));
    }
    ui_state->num_fetched_documents = num_accumulated_entries;
    ui_state->feed_fetching_elapsed_time = base::TimeTicks::Now() - start_time;

    // |params| will be passed to the callback and thus nulled. Extract the
    // pointer so we can use it bellow.
    LoadFeedParams* params_ptr = params.get();
    // Kick off the remaining part of the feeds.
    drive_service_->GetDocuments(
        next_feed_url,
        params_ptr->start_changestamp,
        params_ptr->search_query,
        params_ptr->directory_resource_id,
        base::Bind(&DriveFeedLoader::OnGetDocuments,
                   weak_ptr_factory_.GetWeakPtr(),
                   base::Passed(&params),
                   start_time));
    return;
  }

  // Notify the observers that all document feeds are fetched.
  FOR_EACH_OBSERVER(DriveFeedLoaderObserver, observers_,
                    OnDocumentFeedFetched(num_accumulated_entries));

  UMA_HISTOGRAM_TIMES("Drive.EntireFeedLoadTime",
                      base::TimeTicks::Now() - start_time);

  // Run the callback so the client can process the retrieved feeds.
  RunFeedLoadCallback(params.Pass(), DRIVE_FILE_OK);
}

void DriveFeedLoader::OnGetChangelist(scoped_ptr<LoadFeedParams> params,
                                      base::TimeTicks start_time,
                                      google_apis::GDataErrorCode status,
                                      scoped_ptr<base::Value> data) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (params->feed_list.empty()) {
    UMA_HISTOGRAM_TIMES("Drive.InitialFeedLoadTime",
                        base::TimeTicks::Now() - start_time);
  }

  DriveFileError error = util::GDataToDriveFileError(status);
  if (error == DRIVE_FILE_OK &&
      (!data.get() || data->GetType() != Value::TYPE_DICTIONARY)) {
    error = DRIVE_FILE_ERROR_FAILED;
  }

  if (error != DRIVE_FILE_OK) {
    RunFeedLoadCallback(params.Pass(), error);
    return;
  }

  GURL next_feed_url;
  scoped_ptr<google_apis::ChangeList> current_feed(
      google_apis::ChangeList::CreateFrom(*data));
  if (!current_feed.get()) {
    RunFeedLoadCallback(params.Pass(), DRIVE_FILE_ERROR_FAILED);
    return;
  }
  const bool has_next_feed = !current_feed->next_page_token().empty();

#ifndef NDEBUG
  // Save initial root feed for analysis.
  std::string file_name =
      base::StringPrintf("DEBUG_changelist_%" PRId64 ".json",
                         params->start_changestamp);
  google_apis::util::PostBlockingPoolSequencedTask(
      FROM_HERE,
      blocking_task_runner_,
      base::Bind(&SaveFeedOnBlockingPoolForDebugging,
                 cache_->GetCacheDirectoryPath(
                     DriveCache::CACHE_TYPE_META).Append(file_name),
                 base::Passed(&data)));
#endif

  // Add the current feed to the list of collected feeds for this directory.
  scoped_ptr<google_apis::DocumentFeed> feed =
      google_apis::DocumentFeed::CreateFromChangeList(*current_feed);
  params->feed_list.push_back(feed.release());

  // Compute and notify the number of entries fetched so far.
  int num_accumulated_entries = 0;
  for (size_t i = 0; i < params->feed_list.size(); ++i)
    num_accumulated_entries += params->feed_list[i]->entries().size();

  // Check if we need to collect more data to complete the directory list.
  if (has_next_feed) {
    // Post an UI update event to make the UI smoother.
    GetDocumentsUiState* ui_state = params->ui_state.get();
    if (ui_state == NULL) {
      ui_state = new GetDocumentsUiState(base::TimeTicks::Now());
      params->ui_state.reset(ui_state);
    }
    DCHECK(ui_state);

    if ((ui_state->num_fetched_documents - ui_state->num_showing_documents)
        < kFetchUiUpdateStep) {
      // Currently the UI update is stopped. Start UI periodic callback.
      base::MessageLoopProxy::current()->PostTask(
          FROM_HERE,
          base::Bind(&DriveFeedLoader::OnNotifyDocumentFeedFetched,
                     weak_ptr_factory_.GetWeakPtr(),
                     ui_state->weak_ptr_factory.GetWeakPtr()));
    }
    ui_state->num_fetched_documents = num_accumulated_entries;
    ui_state->feed_fetching_elapsed_time = base::TimeTicks::Now() - start_time;

    // Kick off the remaining part of the feeds.
    // Extract the pointer so we can use it bellow.
    LoadFeedParams* params_ptr = params.get();
    drive_service_->GetDocuments(
        current_feed->next_link(),
        params_ptr->start_changestamp,
        std::string(),  // No search query.
        std::string(),  // No directory resource ID.
        base::Bind(&DriveFeedLoader::OnGetChangelist,
                   weak_ptr_factory_.GetWeakPtr(),
                   base::Passed(&params),
                   start_time));
    return;
  }

  // Notify the observers that all document feeds are fetched.
  FOR_EACH_OBSERVER(DriveFeedLoaderObserver, observers_,
                    OnDocumentFeedFetched(num_accumulated_entries));

  UMA_HISTOGRAM_TIMES("Drive.EntireFeedLoadTime",
                      base::TimeTicks::Now() - start_time);

  // Run the callback so the client can process the retrieved feeds.
  RunFeedLoadCallback(params.Pass(), error);
}

void DriveFeedLoader::OnNotifyDocumentFeedFetched(
    base::WeakPtr<GetDocumentsUiState> ui_state) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (!ui_state) {
    // The ui state instance is already released, which means the fetching
    // is done and we don't need to update any more.
    return;
  }

  base::TimeDelta ui_elapsed_time =
      base::TimeTicks::Now() - ui_state->start_time;

  if (ui_state->num_showing_documents + kFetchUiUpdateStep <=
      ui_state->num_fetched_documents) {
    ui_state->num_showing_documents += kFetchUiUpdateStep;
    FOR_EACH_OBSERVER(DriveFeedLoaderObserver, observers_,
                      OnDocumentFeedFetched(ui_state->num_showing_documents));

    int num_remaining_ui_updates =
        (ui_state->num_fetched_documents - ui_state->num_showing_documents)
        / kFetchUiUpdateStep;
    if (num_remaining_ui_updates > 0) {
      // Heuristically, we use fetched time duration to calculate the next
      // UI update timing.
      base::TimeDelta remaining_duration =
          ui_state->feed_fetching_elapsed_time - ui_elapsed_time;
      base::TimeDelta interval = remaining_duration / num_remaining_ui_updates;
      // If UI update is slow for some reason, the interval can be
      // negative, or very small. This rarely happens but should be handled.
      const int kMinIntervalMs = 10;
      if (interval.InMilliseconds() < kMinIntervalMs)
        interval = base::TimeDelta::FromMilliseconds(kMinIntervalMs);

      base::MessageLoopProxy::current()->PostDelayedTask(
          FROM_HERE,
          base::Bind(&DriveFeedLoader::OnNotifyDocumentFeedFetched,
                     weak_ptr_factory_.GetWeakPtr(),
                     ui_state->weak_ptr_factory.GetWeakPtr()),
          interval);
    }
  }
}

void DriveFeedLoader::LoadFromCache(const FileOperationCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!callback.is_null());
  DCHECK(resource_metadata_->origin() == UNINITIALIZED);

  // Sets the refreshing flag, so that the caller does not send refresh requests
  // in parallel (see DriveFileSystem::LoadFeedIfNeeded).
  //
  // Corresponding unset is in ContinueWithInitializedResourceMetadata, where
  // all the control pathes reach.
  refreshing_ = true;

  LoadRootFeedParams* params = new LoadRootFeedParams(callback);
  FilePath path = cache_->GetCacheDirectoryPath(DriveCache::CACHE_TYPE_META);
  if (UseLevelDB()) {
    path = path.Append(kResourceMetadataDBFile);
    resource_metadata_->InitFromDB(path, blocking_task_runner_,
        base::Bind(
            &DriveFeedLoader::ContinueWithInitializedResourceMetadata,
            weak_ptr_factory_.GetWeakPtr(),
            base::Owned(params)));
  } else {
    path = path.Append(kFilesystemProtoFile);
    BrowserThread::GetBlockingPool()->PostTaskAndReply(FROM_HERE,
        base::Bind(&LoadProtoOnBlockingPool, path, params),
        base::Bind(&DriveFeedLoader::OnProtoLoaded,
                   weak_ptr_factory_.GetWeakPtr(),
                   base::Owned(params)));
  }
}

void DriveFeedLoader::OnProtoLoaded(LoadRootFeedParams* params) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(refreshing_);

  // Update directory structure only if everything is OK and we haven't yet
  // received the feed from the server yet.
  if (params->load_error == DRIVE_FILE_OK) {
    DVLOG(1) << "ParseFromString";
    if (resource_metadata_->ParseFromString(params->proto)) {
      resource_metadata_->set_last_serialized(params->last_modified);
      resource_metadata_->set_serialized_size(params->proto.size());
    } else {
      params->load_error = DRIVE_FILE_ERROR_FAILED;
      LOG(WARNING) << "Parse of cached proto file failed";
    }
  }

  ContinueWithInitializedResourceMetadata(params, params->load_error);
}

void DriveFeedLoader::ContinueWithInitializedResourceMetadata(
    LoadRootFeedParams* params,
    DriveFileError error) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(!params->callback.is_null());
  refreshing_ = false;

  DVLOG(1) << "Time elapsed to load resource metadata from disk="
           << (base::Time::Now() - params->load_start_time).InMilliseconds()
           << " milliseconds";

  params->callback.Run(error);
}

void DriveFeedLoader::SaveFileSystem() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (!ShouldSerializeFileSystemNow(resource_metadata_->serialized_size(),
                                    resource_metadata_->last_serialized())) {
    return;
  }

  if (UseLevelDB()) {
    resource_metadata_->SaveToDB();
  } else {
    const FilePath path =
        cache_->GetCacheDirectoryPath(DriveCache::CACHE_TYPE_META).Append(
            kFilesystemProtoFile);
    scoped_ptr<std::string> serialized_proto(new std::string());
    resource_metadata_->SerializeToString(serialized_proto.get());
    resource_metadata_->set_last_serialized(base::Time::Now());
    resource_metadata_->set_serialized_size(serialized_proto->size());
    google_apis::util::PostBlockingPoolSequencedTask(
        FROM_HERE,
        blocking_task_runner_,
        base::Bind(&SaveProtoOnBlockingPool, path,
                   base::Passed(serialized_proto.Pass())));
  }
}

void DriveFeedLoader::UpdateFromFeed(
    const ScopedVector<google_apis::DocumentFeed>& feed_list,
    int64 start_changestamp,
    int64 root_feed_changestamp) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DVLOG(1) << "Updating directory with a feed";

  std::set<FilePath> changed_dirs;

  DriveFeedProcessor feed_processor(resource_metadata_);
  feed_processor.ApplyFeeds(
      feed_list,
      start_changestamp,
      root_feed_changestamp,
      &changed_dirs);

  // Don't send directory content change notification while performing
  // the initial content retrieval.
  const bool should_notify_directory_changed = (start_changestamp != 0);
  if (should_notify_directory_changed) {
    for (std::set<FilePath>::iterator dir_iter = changed_dirs.begin();
        dir_iter != changed_dirs.end(); ++dir_iter) {
      FOR_EACH_OBSERVER(DriveFeedLoaderObserver, observers_,
                        OnDirectoryChanged(*dir_iter));
    }
  }
}

}  // namespace drive
