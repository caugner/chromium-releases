// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMEOS_ASH_COMPONENTS_BOCA_SESSION_API_REMOVE_STUDENT_REQUEST_H_
#define CHROMEOS_ASH_COMPONENTS_BOCA_SESSION_API_REMOVE_STUDENT_REQUEST_H_

#include <memory>
#include <string>

#include "base/time/time.h"
#include "base/types/expected.h"
#include "chromeos/ash/components/boca/proto/bundle.pb.h"
#include "chromeos/ash/components/boca/proto/session.pb.h"
#include "google_apis/common/base_requests.h"

namespace google_apis {
class RequestSender;
enum ApiErrorCode;
}  // namespace google_apis

namespace ash::boca {

//=================RemoveStudentRequest================

using UploadTokenCallback = base::OnceCallback<void(
    base::expected<bool, google_apis::ApiErrorCode> result)>;

class RemoveStudentRequest : public google_apis::UrlFetchRequestBase {
 public:
  RemoveStudentRequest(google_apis::RequestSender* sender,
                       std::string gaia_id,
                       std::string session_id,
                       UploadTokenCallback callback);
  RemoveStudentRequest(const RemoveStudentRequest&) = delete;
  RemoveStudentRequest& operator=(const RemoveStudentRequest&) = delete;
  ~RemoveStudentRequest() override;

  // For testing.
  void OverrideURLForTesting(std::string url);

  UploadTokenCallback callback() { return std::move(callback_); }

  std::string gaia_id() { return gaia_id_; }
  std::string session_id() { return session_id_; }
  void set_student_ids(std::vector<std::string> student_ids) {
    student_ids_ = std::move(student_ids);
  }

 protected:
  // UrlFetchRequestBase:
  google_apis::HttpRequestMethod GetRequestType() const override;
  GURL GetURL() const override;
  google_apis::ApiErrorCode MapReasonToError(
      google_apis::ApiErrorCode code,
      const std::string& reason) override;
  bool IsSuccessfulErrorCode(google_apis::ApiErrorCode error) override;
  bool GetContentData(std::string* upload_content_type,
                      std::string* upload_content) override;
  void ProcessURLFetchResults(
      const network::mojom::URLResponseHead* response_head,
      const base::FilePath response_file,
      std::string response_body) override;
  void RunCallbackOnPrematureFailure(google_apis::ApiErrorCode code) override;

 private:
  void OnDataParsed(bool success);

  std::string gaia_id_;
  std::string session_id_;
  std::vector<std::string> student_ids_;
  std::string url_base_;

  UploadTokenCallback callback_;

  base::WeakPtrFactory<RemoveStudentRequest> weak_ptr_factory_{this};
};
}  // namespace ash::boca

#endif  // CHROMEOS_ASH_COMPONENTS_BOCA_SESSION_API_REMOVE_STUDENT_REQUEST_H_
