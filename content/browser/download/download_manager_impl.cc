// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/download/download_manager_impl.h"

#include <iterator>

#include "base/bind.h"
#include "base/callback.h"
#include "base/debug/alias.h"
#include "base/file_util.h"
#include "base/i18n/case_conversion.h"
#include "base/logging.h"
#include "base/message_loop.h"
#include "base/stl_util.h"
#include "base/stringprintf.h"
#include "base/supports_user_data.h"
#include "base/synchronization/lock.h"
#include "base/sys_string_conversions.h"
#include "build/build_config.h"
#include "content/browser/download/byte_stream.h"
#include "content/browser/download/download_create_info.h"
#include "content/browser/download/download_file_factory.h"
#include "content/browser/download/download_item_factory.h"
#include "content/browser/download/download_item_impl.h"
#include "content/browser/download/download_stats.h"
#include "content/browser/renderer_host/render_view_host_impl.h"
#include "content/browser/renderer_host/resource_dispatcher_host_impl.h"
#include "content/browser/web_contents/web_contents_impl.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/download_interrupt_reasons.h"
#include "content/public/browser/download_manager_delegate.h"
#include "content/public/browser/download_persistent_store_info.h"
#include "content/public/browser/download_url_parameters.h"
#include "content/public/browser/notification_service.h"
#include "content/public/browser/notification_types.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/resource_context.h"
#include "content/public/browser/web_contents_delegate.h"
#include "net/base/load_flags.h"
#include "net/base/upload_data.h"
#include "net/url_request/url_request_context.h"
#include "webkit/glue/webkit_glue.h"

namespace content {
namespace {

void BeginDownload(scoped_ptr<DownloadUrlParameters> params) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::IO));
  // ResourceDispatcherHost{Base} is-not-a URLRequest::Delegate, and
  // DownloadUrlParameters can-not include resource_dispatcher_host_impl.h, so
  // we must down cast. RDHI is the only subclass of RDH as of 2012 May 4.
  scoped_ptr<net::URLRequest> request(
      params->resource_context()->GetRequestContext()->CreateRequest(
          params->url(), NULL));
  request->set_referrer(params->referrer().url.spec());
  webkit_glue::ConfigureURLRequestForReferrerPolicy(
      request.get(), params->referrer().policy);
  request->set_load_flags(request->load_flags() | params->load_flags());
  request->set_method(params->method());
  if (!params->post_body().empty())
    request->AppendBytesToUpload(params->post_body().data(),
                                 params->post_body().size());
  if (params->post_id() >= 0) {
    // The POST in this case does not have an actual body, and only works
    // when retrieving data from cache. This is done because we don't want
    // to do a re-POST without user consent, and currently don't have a good
    // plan on how to display the UI for that.
    DCHECK(params->prefer_cache());
    DCHECK(params->method() == "POST");
    scoped_refptr<net::UploadData> upload_data = new net::UploadData();
    upload_data->set_identifier(params->post_id());
    request->set_upload(upload_data);
  }
  for (DownloadUrlParameters::RequestHeadersType::const_iterator iter
           = params->request_headers_begin();
       iter != params->request_headers_end();
       ++iter) {
    request->SetExtraRequestHeaderByName(
        iter->first, iter->second, false/*overwrite*/);
  }
  params->resource_dispatcher_host()->BeginDownload(
      request.Pass(),
      params->content_initiated(),
      params->resource_context(),
      params->render_process_host_id(),
      params->render_view_host_routing_id(),
      params->prefer_cache(),
      params->GetSaveInfo(),
      params->callback());
}

class MapValueIteratorAdapter {
 public:
  explicit MapValueIteratorAdapter(
      base::hash_map<int64, DownloadItem*>::const_iterator iter)
    : iter_(iter) {
  }
  ~MapValueIteratorAdapter() {}

  DownloadItem* operator*() { return iter_->second; }

  MapValueIteratorAdapter& operator++() {
    ++iter_;
    return *this;
  }

  bool operator!=(const MapValueIteratorAdapter& that) const {
    return iter_ != that.iter_;
  }

 private:
  base::hash_map<int64, DownloadItem*>::const_iterator iter_;
  // Allow copy and assign.
};

void EnsureNoPendingDownloadJobsOnFile(bool* result) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));
  *result = (DownloadFile::GetNumberOfDownloadFiles() == 0);
  BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE, MessageLoop::QuitClosure());
}

class DownloadItemFactoryImpl : public DownloadItemFactory {
 public:
    DownloadItemFactoryImpl() {}
    virtual ~DownloadItemFactoryImpl() {}

    virtual DownloadItemImpl* CreatePersistedItem(
        DownloadItemImplDelegate* delegate,
        DownloadId download_id,
        const DownloadPersistentStoreInfo& info,
        const net::BoundNetLog& bound_net_log) OVERRIDE {
      return new DownloadItemImpl(delegate, download_id, info, bound_net_log);
    }

    virtual DownloadItemImpl* CreateActiveItem(
        DownloadItemImplDelegate* delegate,
        const DownloadCreateInfo& info,
        scoped_ptr<DownloadRequestHandleInterface> request_handle,
        const net::BoundNetLog& bound_net_log) OVERRIDE {
      return new DownloadItemImpl(delegate, info, request_handle.Pass(),
                                  bound_net_log);
    }

    virtual DownloadItemImpl* CreateSavePageItem(
        DownloadItemImplDelegate* delegate,
        const FilePath& path,
        const GURL& url,
        DownloadId download_id,
        const std::string& mime_type,
        const net::BoundNetLog& bound_net_log) OVERRIDE {
      return new DownloadItemImpl(delegate, path, url, download_id,
                                  mime_type, bound_net_log);
    }
};

}  // namespace

DownloadManagerImpl::DownloadManagerImpl(
    net::NetLog* net_log)
    : item_factory_(new DownloadItemFactoryImpl()),
      file_factory_(new DownloadFileFactory()),
      history_size_(0),
      shutdown_needed_(false),
      browser_context_(NULL),
      delegate_(NULL),
      net_log_(net_log) {
}

DownloadManagerImpl::~DownloadManagerImpl() {
  DCHECK(!shutdown_needed_);
}

DownloadId DownloadManagerImpl::GetNextId() {
  DownloadId id;
  if (delegate_)
   id = delegate_->GetNextId();
  if (!id.IsValid()) {
    static int next_id;
    id = DownloadId(browser_context_, ++next_id);
  }

  return id;
}

void DownloadManagerImpl::DetermineDownloadTarget(
    DownloadItemImpl* item, const DownloadTargetCallback& callback) {
  // Note that this next call relies on
  // DownloadItemImplDelegate::DownloadTargetCallback and
  // DownloadManagerDelegate::DownloadTargetCallback having the same
  // type.  If the types ever diverge, gasket code will need to
  // be written here.
  if (!delegate_ || !delegate_->DetermineDownloadTarget(item, callback)) {
    FilePath target_path = item->GetForcedFilePath();
    // TODO(asanka): Determine a useful path if |target_path| is empty.
    callback.Run(target_path,
                 DownloadItem::TARGET_DISPOSITION_OVERWRITE,
                 DOWNLOAD_DANGER_TYPE_NOT_DANGEROUS,
                 target_path);
  }
}

void DownloadManagerImpl::ReadyForDownloadCompletion(
    DownloadItemImpl* item, const base::Closure& complete_callback) {
  if (!delegate_ ||
      delegate_->ShouldCompleteDownload(item, complete_callback)) {
    complete_callback.Run();
  }
  // Otherwise, the delegate has accepted responsibility to run the
  // callback when the download is ready for completion.
}

bool DownloadManagerImpl::ShouldOpenFileBasedOnExtension(const FilePath& path) {
  if (!delegate_)
    return false;

  return delegate_->ShouldOpenFileBasedOnExtension(path);
}

bool DownloadManagerImpl::ShouldOpenDownload(DownloadItemImpl* item) {
  if (!delegate_)
    return true;

  return delegate_->ShouldOpenDownload(item);
}

void DownloadManagerImpl::SetDelegate(DownloadManagerDelegate* delegate) {
  delegate_ = delegate;
}

DownloadManagerDelegate* DownloadManagerImpl::GetDelegate() const {
  return delegate_;
}

void DownloadManagerImpl::Shutdown() {
  VLOG(20) << __FUNCTION__ << "()"
           << " shutdown_needed_ = " << shutdown_needed_;
  if (!shutdown_needed_)
    return;
  shutdown_needed_ = false;

  FOR_EACH_OBSERVER(Observer, observers_, ManagerGoingDown(this));
  // TODO(benjhayden): Consider clearing observers_.

  AssertContainersConsistent();

  // Go through all downloads in downloads_.  Dangerous ones we need to
  // remove on disk, and in progress ones we need to cancel.
  for (DownloadMap::iterator it = downloads_.begin(); it != downloads_.end();) {
    DownloadItemImpl* download = it->second;

    // Save iterator from potential erases in this set done by called code.
    // Iterators after an erasure point are still valid for lists and
    // associative containers such as sets.
    it++;

    if (download->GetSafetyState() == DownloadItem::DANGEROUS &&
        download->IsPartialDownload()) {
      // The user hasn't accepted it, so we need to remove it
      // from the disk.  This may or may not result in it being
      // removed from the DownloadManager queues and deleted
      // (specifically, DownloadManager::DownloadRemoved only
      // removes and deletes it if it's known to the history service)
      // so the only thing we know after calling this function is that
      // the download was deleted if-and-only-if it was removed
      // from all queues.
      download->Delete(DownloadItem::DELETE_DUE_TO_BROWSER_SHUTDOWN);
    } else if (download->IsPartialDownload()) {
      download->Cancel(false);
      if (delegate_)
        delegate_->UpdateItemInPersistentStore(download);
    }
  }

  // At this point, all dangerous downloads have had their files removed
  // and all in progress downloads have been cancelled.  We can now delete
  // anything left.

  // We delete the downloads before clearing the active_downloads_ map
  // so that downloads in the COMPLETING_INTERNAL state (which will have
  // ignored the Cancel() above) will still show up in active_downloads_
  // in order to satisfy the invariants enforced in AssertStateConsistent().
  STLDeleteValues(&downloads_);
  active_downloads_.clear();
  downloads_.clear();

  // We'll have nothing more to report to the observers after this point.
  observers_.Clear();

  if (delegate_)
    delegate_->Shutdown();
  delegate_ = NULL;
}

bool DownloadManagerImpl::Init(BrowserContext* browser_context) {
  DCHECK(browser_context);
  DCHECK(!shutdown_needed_)  << "DownloadManager already initialized.";
  shutdown_needed_ = true;

  browser_context_ = browser_context;

  return true;
}

DownloadItem* DownloadManagerImpl::StartDownload(
    scoped_ptr<DownloadCreateInfo> info,
    scoped_ptr<ByteStreamReader> stream) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  net::BoundNetLog bound_net_log =
      net::BoundNetLog::Make(net_log_, net::NetLog::SOURCE_DOWNLOAD);

  FilePath default_download_directory;
  if (delegate_) {
    FilePath website_save_directory;      // Unused
    bool skip_dir_check = false;          // Unused
    delegate_->GetSaveDir(GetBrowserContext(), &website_save_directory,
                          &default_download_directory, &skip_dir_check);
  }

  // We create the DownloadItem before the DownloadFile because the
  // DownloadItem already needs to handle a state in which there is
  // no associated DownloadFile (history downloads, !IN_PROGRESS downloads)
  DownloadItemImpl* download =
      CreateDownloadItem(info.get(), bound_net_log);
  scoped_ptr<DownloadFile> download_file(
      file_factory_->CreateFile(
          info->save_info.Pass(), default_download_directory,
          info->url(), info->referrer_url,
          info->received_bytes, delegate_->GenerateFileHash(),
          stream.Pass(), bound_net_log,
          download->DestinationObserverAsWeakPtr()));
  download->Start(download_file.Pass());

  // Delay notification until after Start() so that download_file is bound
  // to download and all the usual setters (e.g. Cancel) work.
  FOR_EACH_OBSERVER(Observer, observers_, OnDownloadCreated(this, download));

  return download;
}

void DownloadManagerImpl::CheckForHistoryFilesRemoval() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  for (DownloadMap::iterator it = downloads_.begin();
       it != downloads_.end(); ++it) {
    DownloadItemImpl* item = it->second;
    if (item->IsPersisted())
      CheckForFileRemoval(item);
  }
}

void DownloadManagerImpl::CheckForFileRemoval(DownloadItemImpl* download_item) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (download_item->IsComplete() &&
      !download_item->GetFileExternallyRemoved()) {
    BrowserThread::PostTask(
        BrowserThread::FILE, FROM_HERE,
        base::Bind(&DownloadManagerImpl::CheckForFileRemovalOnFileThread,
                   this, download_item->GetId(),
                   download_item->GetTargetFilePath()));
  }
}

void DownloadManagerImpl::CheckForFileRemovalOnFileThread(
    int32 download_id, const FilePath& path) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::FILE));
  if (!file_util::PathExists(path)) {
    BrowserThread::PostTask(
        BrowserThread::UI, FROM_HERE,
        base::Bind(&DownloadManagerImpl::OnFileRemovalDetected,
                   this,
                   download_id));
  }
}

void DownloadManagerImpl::OnFileRemovalDetected(int32 download_id) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  if (ContainsKey(downloads_, download_id))
    downloads_[download_id]->OnDownloadedFileRemoved();
}

BrowserContext* DownloadManagerImpl::GetBrowserContext() const {
  return browser_context_;
}

DownloadItemImpl* DownloadManagerImpl::CreateDownloadItem(
    DownloadCreateInfo* info, const net::BoundNetLog& bound_net_log) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (!info->download_id.IsValid())
    info->download_id = GetNextId();
  DownloadItemImpl* download = item_factory_->CreateActiveItem(
      this, *info,
      scoped_ptr<DownloadRequestHandleInterface>(
          new DownloadRequestHandle(info->request_handle)).Pass(),
      bound_net_log);

  DCHECK(!ContainsKey(downloads_, download->GetId()));
  downloads_[download->GetId()] = download;
  DCHECK(!ContainsKey(active_downloads_, download->GetId()));
  active_downloads_[download->GetId()] = download;

  return download;
}

DownloadItemImpl* DownloadManagerImpl::CreateSavePackageDownloadItem(
    const FilePath& main_file_path,
    const GURL& page_url,
    const std::string& mime_type,
    DownloadItem::Observer* observer) {
  net::BoundNetLog bound_net_log =
      net::BoundNetLog::Make(net_log_, net::NetLog::SOURCE_DOWNLOAD);
  DownloadItemImpl* download = item_factory_->CreateSavePageItem(
      this,
      main_file_path,
      page_url,
      GetNextId(),
      mime_type,
      bound_net_log);

  download->AddObserver(observer);

  DCHECK(!ContainsKey(downloads_, download->GetId()));
  downloads_[download->GetId()] = download;

  FOR_EACH_OBSERVER(Observer, observers_, OnDownloadCreated(this, download));

  // Will notify the observer in the callback.
  if (delegate_)
    delegate_->AddItemToPersistentStore(download);

  return download;
}

void DownloadManagerImpl::AssertStateConsistent(
    DownloadItemImpl* download) const {
  CHECK(ContainsKey(downloads_, download->GetId()));

  int64 state = download->GetState();
  base::debug::Alias(&state);
  if (ContainsKey(active_downloads_, download->GetId())) {
    if (download->IsPersisted())
      CHECK_EQ(DownloadItem::IN_PROGRESS, download->GetState());
    if (DownloadItem::IN_PROGRESS != download->GetState())
      CHECK_EQ(DownloadItem::kUninitializedHandle, download->GetDbHandle());
  }
  if (DownloadItem::IN_PROGRESS == download->GetState())
    CHECK(ContainsKey(active_downloads_, download->GetId()));
}

void DownloadManagerImpl::DownloadCompleted(DownloadItemImpl* download) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(download);
  if (delegate_)
    delegate_->UpdateItemInPersistentStore(download);
  active_downloads_.erase(download->GetId());
  AssertStateConsistent(download);
}

void DownloadManagerImpl::CancelDownload(int32 download_id) {
  // A cancel at the right time could remove the download from the
  // |active_downloads_| map before we get here.
  if (ContainsKey(active_downloads_, download_id))
    active_downloads_[download_id]->Cancel(true);
}

void DownloadManagerImpl::UpdatePersistence(DownloadItemImpl* download) {
  if (delegate_)
    delegate_->UpdateItemInPersistentStore(download);
}

void DownloadManagerImpl::DownloadStopped(DownloadItemImpl* download) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  VLOG(20) << __FUNCTION__ << "()"
           << " download = " << download->DebugString(true);

  RemoveFromActiveList(download);
  // This function is called from the DownloadItem, so DI state
  // should already have been updated.
  AssertStateConsistent(download);
}

void DownloadManagerImpl::RemoveFromActiveList(DownloadItemImpl* download) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(download);

  // Clean up will happen when the history system create callback runs if we
  // don't have a valid db_handle yet.
  if (download->IsPersisted()) {
    active_downloads_.erase(download->GetId());
    if (delegate_)
      delegate_->UpdateItemInPersistentStore(download);
  }
}

void DownloadManagerImpl::SetDownloadItemFactoryForTesting(
    scoped_ptr<DownloadItemFactory> item_factory) {
  item_factory_ = item_factory.Pass();
}

void DownloadManagerImpl::SetDownloadFileFactoryForTesting(
    scoped_ptr<DownloadFileFactory> file_factory) {
  file_factory_ = file_factory.Pass();
}

DownloadFileFactory* DownloadManagerImpl::GetDownloadFileFactoryForTesting() {
  return file_factory_.get();
}

int DownloadManagerImpl::RemoveDownloadItems(
    const DownloadItemImplVector& pending_deletes) {
  if (pending_deletes.empty())
    return 0;

  // Delete from internal maps.
  for (DownloadItemImplVector::const_iterator it = pending_deletes.begin();
       it != pending_deletes.end();
       ++it) {
    DownloadItemImpl* download = *it;
    DCHECK(download);
    int32 download_id = download->GetId();
    delete download;
    downloads_.erase(download_id);
  }
  NotifyModelChanged();
  return static_cast<int>(pending_deletes.size());
}

void DownloadManagerImpl::DownloadRemoved(DownloadItemImpl* download) {
  if (!download ||
      downloads_.find(download->GetId()) == downloads_.end())
    return;

  // TODO(benjhayden,rdsmith): Remove this.
  if (!download->IsPersisted())
    return;

  // Make history update.
  if (delegate_)
    delegate_->RemoveItemFromPersistentStore(download);

  // Remove from our tables and delete.
  int downloads_count =
      RemoveDownloadItems(DownloadItemImplVector(1, download));
  DCHECK_EQ(1, downloads_count);
}

int DownloadManagerImpl::RemoveDownloadsBetween(base::Time remove_begin,
                                                base::Time remove_end) {
  if (delegate_)
    delegate_->RemoveItemsFromPersistentStoreBetween(remove_begin, remove_end);

  DownloadItemImplVector pending_deletes;
  for (DownloadMap::const_iterator it = downloads_.begin();
      it != downloads_.end();
      ++it) {
    DownloadItemImpl* download = it->second;
    if (download->IsPersisted() &&
        download->GetStartTime() >= remove_begin &&
        (remove_end.is_null() || download->GetStartTime() < remove_end) &&
        (download->IsComplete() || download->IsCancelled())) {
      AssertStateConsistent(download);
      download->NotifyRemoved();
      pending_deletes.push_back(download);
    }
  }
  return RemoveDownloadItems(pending_deletes);
}

int DownloadManagerImpl::RemoveDownloads(base::Time remove_begin) {
  return RemoveDownloadsBetween(remove_begin, base::Time());
}

int DownloadManagerImpl::RemoveAllDownloads() {
  // The null times make the date range unbounded.
  int num_deleted = RemoveDownloadsBetween(base::Time(), base::Time());
  RecordClearAllSize(num_deleted);
  return num_deleted;
}

void DownloadManagerImpl::DownloadUrl(
    scoped_ptr<DownloadUrlParameters> params) {
  if (params->post_id() >= 0) {
    // Check this here so that the traceback is more useful.
    DCHECK(params->prefer_cache());
    DCHECK(params->method() == "POST");
  }
  BrowserThread::PostTask(BrowserThread::IO, FROM_HERE, base::Bind(
      &BeginDownload, base::Passed(params.Pass())));
}

void DownloadManagerImpl::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
  // TODO: It is the responsibility of the observers to query the
  // DownloadManager. Remove the following call from here and update all
  // observers.
  observer->ModelChanged(this);
}

void DownloadManagerImpl::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

// Operations posted to us from the history service ----------------------------

// The history service has retrieved all download entries. 'entries' contains
// 'DownloadPersistentStoreInfo's in sorted order (by ascending start_time).
void DownloadManagerImpl::OnPersistentStoreQueryComplete(
    std::vector<DownloadPersistentStoreInfo>* entries) {
  history_size_ = entries->size();
  for (size_t i = 0; i < entries->size(); ++i) {
    int64 db_handle = entries->at(i).db_handle;
    base::debug::Alias(&db_handle);

    net::BoundNetLog bound_net_log =
        net::BoundNetLog::Make(net_log_, net::NetLog::SOURCE_DOWNLOAD);
    DownloadItemImpl* download = item_factory_->CreatePersistedItem(
        this, GetNextId(), entries->at(i), bound_net_log);
    DCHECK(!ContainsKey(downloads_, download->GetId()));
    downloads_[download->GetId()] = download;
    FOR_EACH_OBSERVER(Observer, observers_, OnDownloadCreated(this, download));
    VLOG(20) << __FUNCTION__ << "()" << i << ">"
             << " download = " << download->DebugString(true);
  }
  NotifyModelChanged();
  CheckForHistoryFilesRemoval();
}

void DownloadManagerImpl::AddDownloadItemToHistory(DownloadItemImpl* download,
                                                   int64 db_handle) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK_NE(DownloadItem::kUninitializedHandle, db_handle);
  DCHECK(!download->IsPersisted());
  download->SetDbHandle(db_handle);
  download->SetIsPersisted();

  RecordHistorySize(history_size_);
  // Not counting |download|.
  ++history_size_;

  // Show in the appropriate browser UI.
  // This includes buttons to save or cancel, for a dangerous download.
  ShowDownloadInBrowser(download);

  // Inform interested objects about the new download.
  NotifyModelChanged();
}

void DownloadManagerImpl::OnItemAddedToPersistentStore(int32 download_id,
                                                       int64 db_handle) {
  // It's valid that we don't find a matching item, i.e. on shutdown.
  if (!ContainsKey(downloads_, download_id))
    return;

  DownloadItemImpl* item = downloads_[download_id];
  AddDownloadItemToHistory(item, db_handle);
  if (item->IsSavePackageDownload()) {
    OnSavePageItemAddedToPersistentStore(item);
  } else {
    OnDownloadItemAddedToPersistentStore(item);
  }
}

// Once the new DownloadItem has been committed to the persistent store,
// associate it with its db_handle (TODO(benjhayden) merge db_handle with id),
// show it in the browser (TODO(benjhayden) the ui should observe us instead),
// and notify observers (TODO(benjhayden) observers should be able to see the
// item when it's created so they can observe it directly. Are there any
// clients that actually need to know when the item is added to the history?).
void DownloadManagerImpl::OnDownloadItemAddedToPersistentStore(
    DownloadItemImpl* item) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  VLOG(20) << __FUNCTION__ << "()" << " db_handle = " << item->GetDbHandle()
           << " download_id = " << item->GetId()
           << " download = " << item->DebugString(true);

  // If the download is still in progress, try to complete it.
  //
  // Otherwise, download has been cancelled or interrupted before we've
  // received the DB handle.  We post one final message to the history
  // service so that it can be properly in sync with the DownloadItem's
  // completion status, and also inform any observers so that they get
  // more than just the start notification.
  if (item->IsInProgress()) {
    item->MaybeCompleteDownload();
  } else {
    DCHECK(item->IsCancelled());
    active_downloads_.erase(item->GetId());
    if (delegate_)
      delegate_->UpdateItemInPersistentStore(item);
    item->UpdateObservers();
  }
}

void DownloadManagerImpl::ShowDownloadInBrowser(DownloadItemImpl* download) {
  // The 'contents' may no longer exist if the user closed the contents before
  // we get this start completion event.
  WebContents* content = download->GetWebContents();

  // If the contents no longer exists, we ask the embedder to suggest another
  // contents.
  if (!content && delegate_)
    content = delegate_->GetAlternativeWebContentsToNotifyForDownload();

  if (content && content->GetDelegate())
    content->GetDelegate()->OnStartDownload(content, download);
}

int DownloadManagerImpl::InProgressCount() const {
  // Don't use active_downloads_.count() because Cancel() leaves items in
  // active_downloads_ if they haven't made it into the persistent store yet.
  // Need to actually look at each item's state.
  int count = 0;
  for (DownloadMap::const_iterator it = active_downloads_.begin();
       it != active_downloads_.end(); ++it) {
    DownloadItemImpl* item = it->second;
    if (item->IsInProgress())
      ++count;
  }
  return count;
}

void DownloadManagerImpl::NotifyModelChanged() {
  FOR_EACH_OBSERVER(Observer, observers_, ModelChanged(this));
}

DownloadItem* DownloadManagerImpl::GetDownload(int download_id) {
  return ContainsKey(downloads_, download_id) ? downloads_[download_id] : NULL;
}

void DownloadManagerImpl::GetAllDownloads(DownloadVector* downloads) {
  for (DownloadMap::iterator it = downloads_.begin();
       it != downloads_.end(); ++it) {
    downloads->push_back(it->second);
  }
}

// Confirm that everything in all maps is also in |downloads_|, and that
// everything in |downloads_| is also in some other map.
void DownloadManagerImpl::AssertContainersConsistent() const {
#if !defined(NDEBUG)
  // Turn everything into sets.
  const DownloadMap* input_maps[] = {&active_downloads_};
  DownloadSet active_set;
  DownloadSet* all_sets[] = {&active_set};
  DCHECK_EQ(ARRAYSIZE_UNSAFE(input_maps), ARRAYSIZE_UNSAFE(all_sets));
  for (size_t i = 0; i < ARRAYSIZE_UNSAFE(input_maps); i++) {
    for (DownloadMap::const_iterator it = input_maps[i]->begin();
         it != input_maps[i]->end(); ++it) {
      all_sets[i]->insert(&*it->second);
    }
  }

  DownloadSet all_downloads;
  for (DownloadMap::const_iterator it = downloads_.begin();
       it != downloads_.end(); ++it) {
    all_downloads.insert(it->second);
  }

  // Check if each set is fully present in downloads, and create a union.
  for (int i = 0; i < static_cast<int>(ARRAYSIZE_UNSAFE(all_sets)); i++) {
    DownloadSet remainder;
    std::insert_iterator<DownloadSet> insert_it(remainder, remainder.begin());
    std::set_difference(all_sets[i]->begin(), all_sets[i]->end(),
                        all_downloads.begin(), all_downloads.end(),
                        insert_it);
    DCHECK(remainder.empty());
  }
#endif
}

// SavePackage will call SavePageDownloadFinished upon completion/cancellation.
// The history callback will call OnSavePageItemAddedToPersistentStore.
// If the download finishes before the history callback,
// OnSavePageItemAddedToPersistentStore calls SavePageDownloadFinished, ensuring
// that the history event is update regardless of the order in which these two
// events complete.
// If something removes the download item from the download manager (Remove,
// Shutdown) the result will be that the SavePage system will not be able to
// properly update the download item (which no longer exists) or the download
// history, but the action will complete properly anyway.  This may lead to the
// history entry being wrong on a reload of chrome (specifically in the case of
// Initiation -> History Callback -> Removal -> Completion), but there's no way
// to solve that without canceling on Remove (which would then update the DB).

void DownloadManagerImpl::OnSavePageItemAddedToPersistentStore(
    DownloadItemImpl* item) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  // Finalize this download if it finished before the history callback.
  if (!item->IsInProgress())
    SavePageDownloadFinished(item);
}

void DownloadManagerImpl::SavePageDownloadFinished(DownloadItem* download) {
  if (download->IsPersisted()) {
    if (delegate_)
      delegate_->UpdateItemInPersistentStore(download);
  }
}

void DownloadManagerImpl::DownloadOpened(DownloadItemImpl* download) {
  if (delegate_)
    delegate_->UpdateItemInPersistentStore(download);
  int num_unopened = 0;
  for (DownloadMap::iterator it = downloads_.begin();
       it != downloads_.end(); ++it) {
    DownloadItemImpl* item = it->second;
    if (item->IsComplete() &&
        !item->GetOpened())
      ++num_unopened;
  }
  RecordOpensOutstanding(num_unopened);
}

void DownloadManagerImpl::DownloadRenamedToIntermediateName(
    DownloadItemImpl* download) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  // download->GetFullPath() is only expected to be meaningful after this
  // callback is received. Therefore we can now add the download to a persistent
  // store. If the rename failed, we processed an interrupt
  // before we receive the DownloadRenamedToIntermediateName() call.
  if (delegate_) {
    delegate_->AddItemToPersistentStore(download);
  } else {
    OnItemAddedToPersistentStore(download->GetId(),
                                 DownloadItem::kUninitializedHandle);
  }
}

void DownloadManagerImpl::DownloadRenamedToFinalName(
    DownloadItemImpl* download) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  // If the rename failed, we processed an interrupt before we get here.
  if (delegate_) {
    delegate_->UpdatePathForItemInPersistentStore(
        download, download->GetFullPath());
  }
}

}  // namespace content
