// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

#include "azure/storage/files/shares/share_client.hpp"

#include <azure/core/credentials/credentials.hpp>
#include <azure/core/http/policies/policy.hpp>
#include <azure/storage/common/constants.hpp>
#include <azure/storage/common/crypt.hpp>
#include <azure/storage/common/shared_key_policy.hpp>
#include <azure/storage/common/storage_common.hpp>
#include <azure/storage/common/storage_per_retry_policy.hpp>
#include <azure/storage/common/storage_service_version_policy.hpp>

#include "azure/storage/files/shares/share_directory_client.hpp"
#include "azure/storage/files/shares/share_file_client.hpp"
#include "azure/storage/files/shares/version.hpp"

namespace Azure { namespace Storage { namespace Files { namespace Shares {

  ShareClient ShareClient::CreateFromConnectionString(
      const std::string& connectionString,
      const std::string& shareName,
      const ShareClientOptions& options)
  {
    auto parsedConnectionString = _internal::ParseConnectionString(connectionString);
    auto shareUrl = std::move(parsedConnectionString.FileServiceUrl);
    shareUrl.AppendPath(_internal::UrlEncodePath(shareName));

    if (parsedConnectionString.KeyCredential)
    {
      return ShareClient(shareUrl.GetAbsoluteUrl(), parsedConnectionString.KeyCredential, options);
    }
    else
    {
      return ShareClient(shareUrl.GetAbsoluteUrl(), options);
    }
  }

  ShareClient::ShareClient(
      const std::string& shareUrl,
      std::shared_ptr<StorageSharedKeyCredential> credential,
      const ShareClientOptions& options)
      : m_shareUrl(shareUrl)
  {
    ShareClientOptions newOptions = options;
    newOptions.PerRetryPolicies.emplace_back(
        std::make_unique<_internal::SharedKeyPolicy>(credential));

    std::vector<std::unique_ptr<Azure::Core::Http::Policies::HttpPolicy>> perRetryPolicies;
    std::vector<std::unique_ptr<Azure::Core::Http::Policies::HttpPolicy>> perOperationPolicies;
    perRetryPolicies.emplace_back(std::make_unique<_internal::StoragePerRetryPolicy>());
    perOperationPolicies.emplace_back(
        std::make_unique<_internal::StorageServiceVersionPolicy>(newOptions.ApiVersion));
    m_pipeline = std::make_shared<Azure::Core::Http::_internal::HttpPipeline>(
        newOptions,
        _internal::FileServicePackageName,
        PackageVersion::VersionString(),
        std::move(perRetryPolicies),
        std::move(perOperationPolicies));
  }

  ShareClient::ShareClient(const std::string& shareUrl, const ShareClientOptions& options)
      : m_shareUrl(shareUrl)
  {
    std::vector<std::unique_ptr<Azure::Core::Http::Policies::HttpPolicy>> perRetryPolicies;
    std::vector<std::unique_ptr<Azure::Core::Http::Policies::HttpPolicy>> perOperationPolicies;
    perRetryPolicies.emplace_back(std::make_unique<_internal::StoragePerRetryPolicy>());
    perOperationPolicies.emplace_back(
        std::make_unique<_internal::StorageServiceVersionPolicy>(options.ApiVersion));
    m_pipeline = std::make_shared<Azure::Core::Http::_internal::HttpPipeline>(
        options,
        _internal::FileServicePackageName,
        PackageVersion::VersionString(),
        std::move(perRetryPolicies),
        std::move(perOperationPolicies));
  }

  ShareDirectoryClient ShareClient::GetRootDirectoryClient() const
  {
    return ShareDirectoryClient(m_shareUrl, m_pipeline);
  }

  ShareClient ShareClient::WithSnapshot(const std::string& snapshot) const
  {
    ShareClient newClient(*this);
    if (snapshot.empty())
    {
      newClient.m_shareUrl.RemoveQueryParameter(_detail::ShareSnapshotQueryParameter);
    }
    else
    {
      newClient.m_shareUrl.AppendQueryParameter(
          _detail::ShareSnapshotQueryParameter, _internal::UrlEncodeQueryParameter(snapshot));
    }
    return newClient;
  }

  Azure::Response<Models::CreateShareResult> ShareClient::Create(
      const CreateShareOptions& options,
      const Azure::Core::Context& context) const
  {
    auto protocolLayerOptions = _detail::ShareRestClient::Share::CreateOptions();
    protocolLayerOptions.Metadata = options.Metadata;
    protocolLayerOptions.ShareQuota = options.ShareQuotaInGiB;
    protocolLayerOptions.XMsAccessTier = options.AccessTier;
    auto result = _detail::ShareRestClient::Share::Create(
        m_shareUrl, *m_pipeline, context, protocolLayerOptions);
    Models::CreateShareResult ret;
    ret.Created = true;
    ret.ETag = std::move(result->ETag);
    ret.LastModified = std::move(result->LastModified);
    return Azure::Response<Models::CreateShareResult>(std::move(ret), result.ExtractRawResponse());
  }

  Azure::Response<Models::CreateShareResult> ShareClient::CreateIfNotExists(
      const CreateShareOptions& options,
      const Azure::Core::Context& context) const
  {
    try
    {
      return Create(options, context);
    }
    catch (StorageException& e)
    {
      if (e.ErrorCode == _detail::ShareAlreadyExists)
      {
        Models::CreateShareResult ret;
        ret.Created = false;
        return Azure::Response<Models::CreateShareResult>(std::move(ret), std::move(e.RawResponse));
      }
      throw;
    }
  }

  Azure::Response<Models::DeleteShareResult> ShareClient::Delete(
      const DeleteShareOptions& options,
      const Azure::Core::Context& context) const
  {
    auto protocolLayerOptions = _detail::ShareRestClient::Share::DeleteOptions();
    if (options.DeleteSnapshots.HasValue() && options.DeleteSnapshots.GetValue())
    {
      protocolLayerOptions.XMsDeleteSnapshots = Models::DeleteSnapshotsOption::Include;
    }
    auto result = _detail::ShareRestClient::Share::Delete(
        m_shareUrl, *m_pipeline, context, protocolLayerOptions);
    Models::DeleteShareResult ret;
    ret.Deleted = true;
    return Azure::Response<Models::DeleteShareResult>(std::move(ret), result.ExtractRawResponse());
  }

  Azure::Response<Models::DeleteShareResult> ShareClient::DeleteIfExists(
      const DeleteShareOptions& options,
      const Azure::Core::Context& context) const
  {
    try
    {
      return Delete(options, context);
    }
    catch (StorageException& e)
    {
      if (e.ErrorCode == _detail::ShareNotFound)
      {
        Models::DeleteShareResult ret;
        ret.Deleted = false;
        return Azure::Response<Models::DeleteShareResult>(std::move(ret), std::move(e.RawResponse));
      }
      throw;
    }
  }

  Azure::Response<Models::CreateShareSnapshotResult> ShareClient::CreateSnapshot(
      const CreateShareSnapshotOptions& options,
      const Azure::Core::Context& context) const
  {
    auto protocolLayerOptions = _detail::ShareRestClient::Share::CreateSnapshotOptions();
    protocolLayerOptions.Metadata = options.Metadata;
    return _detail::ShareRestClient::Share::CreateSnapshot(
        m_shareUrl, *m_pipeline, context, protocolLayerOptions);
  }

  Azure::Response<Models::ShareProperties> ShareClient::GetProperties(
      const GetSharePropertiesOptions& options,
      const Azure::Core::Context& context) const
  {
    (void)options;
    auto protocolLayerOptions = _detail::ShareRestClient::Share::GetPropertiesOptions();
    return _detail::ShareRestClient::Share::GetProperties(
        m_shareUrl, *m_pipeline, context, protocolLayerOptions);
  }

  Azure::Response<Models::SetSharePropertiesResult> ShareClient::SetProperties(
      const SetSharePropertiesOptions& options,
      const Azure::Core::Context& context) const
  {
    auto protocolLayerOptions = _detail::ShareRestClient::Share::SetPropertiesOptions();
    protocolLayerOptions.ShareQuota = options.ShareQuotaInGiB;
    protocolLayerOptions.XMsAccessTier = options.AccessTier;
    return _detail::ShareRestClient::Share::SetProperties(
        m_shareUrl, *m_pipeline, context, protocolLayerOptions);
  }

  Azure::Response<Models::SetShareMetadataResult> ShareClient::SetMetadata(
      Storage::Metadata metadata,
      const SetShareMetadataOptions& options,
      const Azure::Core::Context& context) const
  {
    (void)options;
    auto protocolLayerOptions = _detail::ShareRestClient::Share::SetMetadataOptions();
    protocolLayerOptions.Metadata = metadata;
    return _detail::ShareRestClient::Share::SetMetadata(
        m_shareUrl, *m_pipeline, context, protocolLayerOptions);
  }

  Azure::Response<Models::ShareAccessPolicy> ShareClient::GetAccessPolicy(
      const GetShareAccessPolicyOptions& options,
      const Azure::Core::Context& context) const
  {
    (void)options;
    auto protocolLayerOptions = _detail::ShareRestClient::Share::GetAccessPolicyOptions();
    return _detail::ShareRestClient::Share::GetAccessPolicy(
        m_shareUrl, *m_pipeline, context, protocolLayerOptions);
  }

  Azure::Response<Models::SetShareAccessPolicyResult> ShareClient::SetAccessPolicy(
      const std::vector<Models::SignedIdentifier>& accessPolicy,
      const SetShareAccessPolicyOptions& options,
      const Azure::Core::Context& context) const
  {
    (void)options;
    auto protocolLayerOptions = _detail::ShareRestClient::Share::SetAccessPolicyOptions();
    protocolLayerOptions.ShareAcl = accessPolicy;
    return _detail::ShareRestClient::Share::SetAccessPolicy(
        m_shareUrl, *m_pipeline, context, protocolLayerOptions);
  }

  Azure::Response<Models::ShareStatistics> ShareClient::GetStatistics(
      const GetShareStatisticsOptions& options,
      const Azure::Core::Context& context) const
  {
    (void)options;
    auto protocolLayerOptions = _detail::ShareRestClient::Share::GetStatisticsOptions();
    return _detail::ShareRestClient::Share::GetStatistics(
        m_shareUrl, *m_pipeline, context, protocolLayerOptions);
  }

  Azure::Response<Models::CreateSharePermissionResult> ShareClient::CreatePermission(
      const std::string& permission,
      const CreateSharePermissionOptions& options,
      const Azure::Core::Context& context) const
  {
    (void)options;
    auto protocolLayerOptions = _detail::ShareRestClient::Share::CreatePermissionOptions();
    protocolLayerOptions.Permission.FilePermission = permission;
    return _detail::ShareRestClient::Share::CreatePermission(
        m_shareUrl, *m_pipeline, context, protocolLayerOptions);
  }

  Azure::Response<std::string> ShareClient::GetPermission(
      const std::string& permissionKey,
      const GetSharePermissionOptions& options,
      const Azure::Core::Context& context) const
  {
    (void)options;
    auto protocolLayerOptions = _detail::ShareRestClient::Share::GetPermissionOptions();
    protocolLayerOptions.FilePermissionKeyRequired = permissionKey;
    auto result = _detail::ShareRestClient::Share::GetPermission(
        m_shareUrl, *m_pipeline, context, protocolLayerOptions);

    return Azure::Response<std::string>(
        result.ExtractValue().FilePermission, result.ExtractRawResponse());
  }

  Azure::Response<Models::ListFilesAndDirectoriesSinglePageResult>
  ShareClient::ListFilesAndDirectoriesSinglePage(
      const ListFilesAndDirectoriesSinglePageOptions& options,
      const Azure::Core::Context& context) const
  {
    auto protocolLayerOptions
        = _detail::ShareRestClient::Directory::ListFilesAndDirectoriesSinglePageOptions();
    protocolLayerOptions.Prefix = options.Prefix;
    protocolLayerOptions.ContinuationToken = options.ContinuationToken;
    protocolLayerOptions.MaxResults = options.PageSizeHint;
    auto result = _detail::ShareRestClient::Directory::ListFilesAndDirectoriesSinglePage(
        m_shareUrl, *m_pipeline, context, protocolLayerOptions);
    Models::ListFilesAndDirectoriesSinglePageResult ret;
    ret.ServiceEndpoint = std::move(result->ServiceEndpoint);
    ret.ShareName = std::move(result->ShareName);
    ret.ShareSnapshot = std::move(result->ShareSnapshot);
    ret.DirectoryPath = std::move(result->DirectoryPath);
    ret.Prefix = std::move(result->Prefix);
    ret.PageSizeHint = result->PageSizeHint;
    ret.ContinuationToken = std::move(result->ContinuationToken);
    ret.DirectoryItems = std::move(result->SinglePage.DirectoryItems);
    ret.FileItems = std::move(result->SinglePage.FileItems);

    return Azure::Response<Models::ListFilesAndDirectoriesSinglePageResult>(
        std::move(ret), result.ExtractRawResponse());
  }

}}}} // namespace Azure::Storage::Files::Shares
