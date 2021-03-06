// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

#include "azure/storage/files/datalake/datalake_path_client.hpp"

#include <azure/core/http/policies/policy.hpp>
#include <azure/storage/common/constants.hpp>
#include <azure/storage/common/crypt.hpp>
#include <azure/storage/common/shared_key_policy.hpp>
#include <azure/storage/common/storage_common.hpp>
#include <azure/storage/common/storage_per_retry_policy.hpp>
#include <azure/storage/common/storage_service_version_policy.hpp>
#include <azure/storage/common/storage_switch_to_secondary_policy.hpp>

#include "azure/storage/files/datalake/datalake_constants.hpp"
#include "azure/storage/files/datalake/datalake_utilities.hpp"
#include "azure/storage/files/datalake/version.hpp"

namespace Azure { namespace Storage { namespace Files { namespace DataLake {

  namespace {
    Models::LeaseState FromBlobLeaseState(Blobs::Models::LeaseState state)
    {
      if (state == Blobs::Models::LeaseState::Available)
      {
        return Models::LeaseState::Available;
      }
      if (state == Blobs::Models::LeaseState::Breaking)
      {
        return Models::LeaseState::Breaking;
      }
      if (state == Blobs::Models::LeaseState::Broken)
      {
        return Models::LeaseState::Broken;
      }
      if (state == Blobs::Models::LeaseState::Expired)
      {
        return Models::LeaseState::Expired;
      }
      if (state == Blobs::Models::LeaseState::Leased)
      {
        return Models::LeaseState::Leased;
      }
      return Models::LeaseState();
    }

    Models::LeaseStatus FromBlobLeaseStatus(Blobs::Models::LeaseStatus status)
    {
      if (status == Blobs::Models::LeaseStatus::Locked)
      {
        return Models::LeaseStatus::Locked;
      }
      if (status == Blobs::Models::LeaseStatus::Unlocked)
      {
        return Models::LeaseStatus::Unlocked;
      }
      return Models::LeaseStatus();
    }
  } // namespace

  DataLakePathClient DataLakePathClient::CreateFromConnectionString(
      const std::string& connectionString,
      const std::string& fileSystemName,
      const std::string& path,
      const DataLakeClientOptions& options)
  {
    auto parsedConnectionString = _internal::ParseConnectionString(connectionString);
    auto pathUrl = std::move(parsedConnectionString.DataLakeServiceUrl);
    pathUrl.AppendPath(_internal::UrlEncodePath(fileSystemName));
    pathUrl.AppendPath(_internal::UrlEncodePath(path));

    if (parsedConnectionString.KeyCredential)
    {
      return DataLakePathClient(
          pathUrl.GetAbsoluteUrl(), parsedConnectionString.KeyCredential, options);
    }
    else
    {
      return DataLakePathClient(pathUrl.GetAbsoluteUrl(), options);
    }
  }

  DataLakePathClient::DataLakePathClient(
      const std::string& pathUrl,
      std::shared_ptr<StorageSharedKeyCredential> credential,
      const DataLakeClientOptions& options)
      : m_pathUrl(pathUrl), m_blobClient(
                                _detail::GetBlobUrlFromUrl(pathUrl),
                                credential,
                                _detail::GetBlobClientOptions(options))
  {
    DataLakeClientOptions newOptions = options;
    newOptions.PerRetryPolicies.emplace_back(
        std::make_unique<_internal::SharedKeyPolicy>(credential));

    std::vector<std::unique_ptr<Azure::Core::Http::Policies::HttpPolicy>> perRetryPolicies;
    std::vector<std::unique_ptr<Azure::Core::Http::Policies::HttpPolicy>> perOperationPolicies;
    perRetryPolicies.emplace_back(std::make_unique<_internal::StorageSwitchToSecondaryPolicy>(
        m_pathUrl.GetHost(), newOptions.SecondaryHostForRetryReads));
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

  DataLakePathClient::DataLakePathClient(
      const std::string& pathUrl,
      std::shared_ptr<Core::Credentials::TokenCredential> credential,
      const DataLakeClientOptions& options)
      : m_pathUrl(pathUrl), m_blobClient(
                                _detail::GetBlobUrlFromUrl(pathUrl),
                                credential,
                                _detail::GetBlobClientOptions(options))
  {
    std::vector<std::unique_ptr<Azure::Core::Http::Policies::HttpPolicy>> perRetryPolicies;
    std::vector<std::unique_ptr<Azure::Core::Http::Policies::HttpPolicy>> perOperationPolicies;
    perRetryPolicies.emplace_back(std::make_unique<_internal::StorageSwitchToSecondaryPolicy>(
        m_pathUrl.GetHost(), options.SecondaryHostForRetryReads));
    perRetryPolicies.emplace_back(std::make_unique<_internal::StoragePerRetryPolicy>());
    {
      Azure::Core::Credentials::TokenRequestContext tokenContext;
      tokenContext.Scopes.emplace_back(_internal::StorageScope);
      perRetryPolicies.emplace_back(
          std::make_unique<Azure::Core::Http::Policies::BearerTokenAuthenticationPolicy>(
              credential, tokenContext));
    }
    perOperationPolicies.emplace_back(
        std::make_unique<_internal::StorageServiceVersionPolicy>(options.ApiVersion));
    m_pipeline = std::make_shared<Azure::Core::Http::_internal::HttpPipeline>(
        options,
        _internal::FileServicePackageName,
        PackageVersion::VersionString(),
        std::move(perRetryPolicies),
        std::move(perOperationPolicies));
  }

  DataLakePathClient::DataLakePathClient(
      const std::string& pathUrl,
      const DataLakeClientOptions& options)
      : m_pathUrl(pathUrl),
        m_blobClient(_detail::GetBlobUrlFromUrl(pathUrl), _detail::GetBlobClientOptions(options))
  {
    std::vector<std::unique_ptr<Azure::Core::Http::Policies::HttpPolicy>> perRetryPolicies;
    std::vector<std::unique_ptr<Azure::Core::Http::Policies::HttpPolicy>> perOperationPolicies;
    perRetryPolicies.emplace_back(std::make_unique<_internal::StorageSwitchToSecondaryPolicy>(
        m_pathUrl.GetHost(), options.SecondaryHostForRetryReads));
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

  Azure::Response<Models::SetPathAccessControlListResult> DataLakePathClient::SetAccessControlList(
      std::vector<Models::Acl> acls,
      const SetPathAccessControlListOptions& options,
      const Azure::Core::Context& context) const
  {
    _detail::DataLakeRestClient::Path::SetAccessControlOptions protocolLayerOptions;
    protocolLayerOptions.LeaseIdOptional = options.AccessConditions.LeaseId;
    protocolLayerOptions.Owner = options.Owner;
    protocolLayerOptions.Group = options.Group;
    protocolLayerOptions.Acl = Models::Acl::SerializeAcls(acls);
    protocolLayerOptions.IfMatch = options.AccessConditions.IfMatch;
    protocolLayerOptions.IfNoneMatch = options.AccessConditions.IfNoneMatch;
    protocolLayerOptions.IfModifiedSince = options.AccessConditions.IfModifiedSince;
    protocolLayerOptions.IfUnmodifiedSince = options.AccessConditions.IfUnmodifiedSince;
    return _detail::DataLakeRestClient::Path::SetAccessControl(
        m_pathUrl, *m_pipeline, context, protocolLayerOptions);
  }

  Azure::Response<Models::SetPathPermissionsResult> DataLakePathClient::SetPermissions(
      std::string permissions,
      const SetPathPermissionsOptions& options,
      const Azure::Core::Context& context) const
  {
    _detail::DataLakeRestClient::Path::SetAccessControlOptions protocolLayerOptions;
    protocolLayerOptions.LeaseIdOptional = options.AccessConditions.LeaseId;
    protocolLayerOptions.Owner = options.Owner;
    protocolLayerOptions.Group = options.Group;
    protocolLayerOptions.Permissions = permissions;
    protocolLayerOptions.IfMatch = options.AccessConditions.IfMatch;
    protocolLayerOptions.IfNoneMatch = options.AccessConditions.IfNoneMatch;
    protocolLayerOptions.IfModifiedSince = options.AccessConditions.IfModifiedSince;
    protocolLayerOptions.IfUnmodifiedSince = options.AccessConditions.IfUnmodifiedSince;
    return _detail::DataLakeRestClient::Path::SetAccessControl(
        m_pathUrl, *m_pipeline, context, protocolLayerOptions);
  }

  Azure::Response<Models::SetPathHttpHeadersResult> DataLakePathClient::SetHttpHeaders(
      Models::PathHttpHeaders httpHeaders,
      const SetPathHttpHeadersOptions& options,
      const Azure::Core::Context& context) const
  {
    Blobs::SetBlobHttpHeadersOptions blobOptions;
    Blobs::Models::BlobHttpHeaders blobHttpHeaders;
    blobHttpHeaders.CacheControl = httpHeaders.CacheControl;
    blobHttpHeaders.ContentType = httpHeaders.ContentType;
    blobHttpHeaders.ContentDisposition = httpHeaders.ContentDisposition;
    blobHttpHeaders.ContentEncoding = httpHeaders.ContentEncoding;
    blobHttpHeaders.ContentLanguage = httpHeaders.ContentLanguage;
    blobOptions.AccessConditions.IfMatch = options.AccessConditions.IfMatch;
    blobOptions.AccessConditions.IfNoneMatch = options.AccessConditions.IfNoneMatch;
    blobOptions.AccessConditions.IfModifiedSince = options.AccessConditions.IfModifiedSince;
    blobOptions.AccessConditions.IfUnmodifiedSince = options.AccessConditions.IfUnmodifiedSince;
    blobOptions.AccessConditions.LeaseId = options.AccessConditions.LeaseId;
    auto result = m_blobClient.SetHttpHeaders(blobHttpHeaders, blobOptions, context);
    Models::SetPathHttpHeadersResult ret;
    ret.ETag = std::move(result->ETag);
    ret.LastModified = std::move(result->LastModified);
    return Azure::Response<Models::SetPathHttpHeadersResult>(
        std::move(ret), result.ExtractRawResponse());
  }

  Azure::Response<Models::CreatePathResult> DataLakePathClient::Create(
      Models::PathResourceType type,
      const CreatePathOptions& options,
      const Azure::Core::Context& context) const
  {
    _detail::DataLakeRestClient::Path::CreateOptions protocolLayerOptions;
    protocolLayerOptions.Resource = type;
    protocolLayerOptions.LeaseIdOptional = options.AccessConditions.LeaseId;
    protocolLayerOptions.CacheControl = options.HttpHeaders.CacheControl;
    protocolLayerOptions.ContentType = options.HttpHeaders.ContentType;
    protocolLayerOptions.ContentDisposition = options.HttpHeaders.ContentDisposition;
    protocolLayerOptions.ContentEncoding = options.HttpHeaders.ContentEncoding;
    protocolLayerOptions.ContentLanguage = options.HttpHeaders.ContentLanguage;
    protocolLayerOptions.IfMatch = options.AccessConditions.IfMatch;
    protocolLayerOptions.IfNoneMatch = options.AccessConditions.IfNoneMatch;
    protocolLayerOptions.IfModifiedSince = options.AccessConditions.IfModifiedSince;
    protocolLayerOptions.IfUnmodifiedSince = options.AccessConditions.IfUnmodifiedSince;
    protocolLayerOptions.Properties = _detail::SerializeMetadata(options.Metadata);
    protocolLayerOptions.Umask = options.Umask;
    protocolLayerOptions.Permissions = options.Permissions;
    auto result = _detail::DataLakeRestClient::Path::Create(
        m_pathUrl, *m_pipeline, context, protocolLayerOptions);
    Models::CreatePathResult ret;
    ret.ETag = std::move(result->ETag);
    ret.LastModified = std::move(result->LastModified.GetValue());
    ret.FileSize = std::move(result->ContentLength);
    return Azure::Response<Models::CreatePathResult>(std::move(ret), result.ExtractRawResponse());
  }

  Azure::Response<Models::CreatePathResult> DataLakePathClient::CreateIfNotExists(
      Models::PathResourceType type,
      const CreatePathOptions& options,
      const Azure::Core::Context& context) const
  {
    try
    {
      auto createOptions = options;
      createOptions.AccessConditions.IfNoneMatch = Azure::ETag::Any();
      return Create(type, createOptions, context);
    }
    catch (StorageException& e)
    {
      if (e.ErrorCode == _detail::DataLakePathAlreadyExists)
      {
        Models::CreatePathResult ret;
        ret.Created = false;
        return Azure::Response<Models::CreatePathResult>(std::move(ret), std::move(e.RawResponse));
      }
      throw;
    }
  }

  Azure::Response<Models::DeletePathResult> DataLakePathClient::Delete(
      const DeletePathOptions& options,
      const Azure::Core::Context& context) const
  {
    _detail::DataLakeRestClient::Path::DeleteOptions protocolLayerOptions;
    protocolLayerOptions.LeaseIdOptional = options.AccessConditions.LeaseId;
    protocolLayerOptions.IfMatch = options.AccessConditions.IfMatch;
    protocolLayerOptions.IfNoneMatch = options.AccessConditions.IfNoneMatch;
    protocolLayerOptions.IfModifiedSince = options.AccessConditions.IfModifiedSince;
    protocolLayerOptions.IfUnmodifiedSince = options.AccessConditions.IfUnmodifiedSince;
    protocolLayerOptions.RecursiveOptional = options.Recursive;
    auto result = _detail::DataLakeRestClient::Path::Delete(
        m_pathUrl, *m_pipeline, context, protocolLayerOptions);
    Models::DeletePathResult ret;
    ret.Deleted = true;
    return Azure::Response<Models::DeletePathResult>(std::move(ret), result.ExtractRawResponse());
  }

  Azure::Response<Models::DeletePathResult> DataLakePathClient::DeleteIfExists(
      const DeletePathOptions& options,
      const Azure::Core::Context& context) const
  {
    try
    {
      return Delete(options, context);
    }
    catch (StorageException& e)
    {
      if (e.ErrorCode == _detail::DataLakeFilesystemNotFound
          || e.ErrorCode == _detail::DataLakePathNotFound)
      {
        Models::DeletePathResult ret;
        ret.Deleted = false;
        return Azure::Response<Models::DeletePathResult>(std::move(ret), std::move(e.RawResponse));
      }
      throw;
    }
  }

  Azure::Response<Models::PathProperties> DataLakePathClient::GetProperties(
      const GetPathPropertiesOptions& options,
      const Azure::Core::Context& context) const
  {
    Blobs::GetBlobPropertiesOptions blobOptions;
    blobOptions.AccessConditions.IfMatch = options.AccessConditions.IfMatch;
    blobOptions.AccessConditions.IfNoneMatch = options.AccessConditions.IfNoneMatch;
    blobOptions.AccessConditions.IfModifiedSince = options.AccessConditions.IfModifiedSince;
    blobOptions.AccessConditions.IfUnmodifiedSince = options.AccessConditions.IfUnmodifiedSince;
    blobOptions.AccessConditions.LeaseId = options.AccessConditions.LeaseId;
    auto result = m_blobClient.GetProperties(blobOptions, context);
    Models::PathProperties ret;
    ret.ETag = std::move(result->ETag);
    ret.LastModified = std::move(result->LastModified);
    ret.CreatedOn = std::move(result->CreatedOn);
    ret.Metadata = std::move(result->Metadata);
    if (result->LeaseDuration.HasValue())
    {
      ret.LeaseDuration = Models::LeaseDuration(result->LeaseDuration.GetValue().ToString());
    }
    ret.LeaseState = result->LeaseState.HasValue()
        ? FromBlobLeaseState(result->LeaseState.GetValue())
        : ret.LeaseState;
    ret.LeaseStatus = result->LeaseStatus.HasValue()
        ? FromBlobLeaseStatus(result->LeaseStatus.GetValue())
        : ret.LeaseStatus;
    ret.HttpHeaders.CacheControl = std::move(result->HttpHeaders.CacheControl);
    ret.HttpHeaders.ContentDisposition = std::move(result->HttpHeaders.ContentDisposition);
    ret.HttpHeaders.ContentEncoding = std::move(result->HttpHeaders.ContentEncoding);
    ret.HttpHeaders.ContentLanguage = std::move(result->HttpHeaders.ContentLanguage);
    ret.HttpHeaders.ContentType = std::move(result->HttpHeaders.ContentType);
    ret.IsServerEncrypted = result->IsServerEncrypted;
    ret.EncryptionKeySha256 = std::move(result->EncryptionKeySha256);
    ret.CopyId = std::move(result->CopyId);
    ret.CopySource = std::move(result->CopySource);
    ret.CopyStatus = std::move(result->CopyStatus);
    ret.CopyProgress = std::move(result->CopyProgress);
    ret.CopyCompletedOn = std::move(result->CopyCompletedOn);
    ret.ExpiresOn = std::move(result->ExpiresOn);
    ret.LastAccessedOn = std::move(result->LastAccessedOn);
    ret.FileSize = result->BlobSize;
    ret.ArchiveStatus = std::move(result->ArchiveStatus);
    ret.RehydratePriority = std::move(result->RehydratePriority);
    ret.CopyStatusDescription = std::move(result->CopyStatusDescription);
    ret.IsIncrementalCopy = std::move(result->IsIncrementalCopy);
    ret.IncrementalCopyDestinationSnapshot = std::move(result->IncrementalCopyDestinationSnapshot);
    ret.VersionId = std::move(result->VersionId);
    ret.IsCurrentVersion = std::move(result->IsCurrentVersion);
    ret.IsDirectory = _detail::MetadataIncidatesIsDirectory(ret.Metadata);
    return Azure::Response<Models::PathProperties>(std::move(ret), result.ExtractRawResponse());
  }

  Azure::Response<Models::PathAccessControlList> DataLakePathClient::GetAccessControlList(
      const GetPathAccessControlListOptions& options,
      const Azure::Core::Context& context) const
  {
    _detail::DataLakeRestClient::Path::GetPropertiesOptions protocolLayerOptions;
    protocolLayerOptions.Action = _detail::PathGetPropertiesAction::GetAccessControl;
    protocolLayerOptions.LeaseIdOptional = options.AccessConditions.LeaseId;
    protocolLayerOptions.IfMatch = options.AccessConditions.IfMatch;
    protocolLayerOptions.IfNoneMatch = options.AccessConditions.IfNoneMatch;
    protocolLayerOptions.IfModifiedSince = options.AccessConditions.IfModifiedSince;
    protocolLayerOptions.IfUnmodifiedSince = options.AccessConditions.IfUnmodifiedSince;
    auto result = _detail::DataLakeRestClient::Path::GetProperties(
        m_pathUrl, *m_pipeline, _internal::WithReplicaStatus(context), protocolLayerOptions);
    Azure::Nullable<std::vector<Models::Acl>> acl;
    if (result->Acl.HasValue())
    {
      acl = Models::Acl::DeserializeAcls(result->Acl.GetValue());
    }
    Models::PathAccessControlList ret;
    if (!acl.HasValue())
    {
      throw Azure::Core::RequestFailedException(
          "Got null value returned when getting access control.");
    }
    ret.Acls = std::move(acl.GetValue());
    if (result->Owner.HasValue())
    {
      ret.Owner = result->Owner.GetValue();
    }
    if (result->Group.HasValue())
    {
      ret.Group = result->Group.GetValue();
    }
    if (result->Permissions.HasValue())
    {
      ret.Permissions = result->Permissions.GetValue();
    }
    return Azure::Response<Models::PathAccessControlList>(
        std::move(ret), result.ExtractRawResponse());
  }

  Azure::Response<Models::SetPathMetadataResult> DataLakePathClient::SetMetadata(
      Storage::Metadata metadata,
      const SetPathMetadataOptions& options,
      const Azure::Core::Context& context) const
  {
    Blobs::SetBlobMetadataOptions blobOptions;
    blobOptions.AccessConditions.IfMatch = options.AccessConditions.IfMatch;
    blobOptions.AccessConditions.IfNoneMatch = options.AccessConditions.IfNoneMatch;
    blobOptions.AccessConditions.IfModifiedSince = options.AccessConditions.IfModifiedSince;
    blobOptions.AccessConditions.IfUnmodifiedSince = options.AccessConditions.IfUnmodifiedSince;
    blobOptions.AccessConditions.LeaseId = options.AccessConditions.LeaseId;
    auto result = m_blobClient.SetMetadata(std::move(metadata), blobOptions, context);
    Models::SetPathMetadataResult ret;
    ret.ETag = std::move(result->ETag);
    ret.LastModified = std::move(result->LastModified);
    return Azure::Response<Models::SetPathMetadataResult>(
        std::move(ret), result.ExtractRawResponse());
  }

  Azure::Response<Models::SetPathAccessControlListRecursiveSinglePageResult>
  DataLakePathClient::SetAccessControlListRecursiveSinglePageInternal(
      _detail::PathSetAccessControlRecursiveMode mode,
      const std::vector<Models::Acl>& acls,
      const SetPathAccessControlListRecursiveSinglePageOptions& options,
      const Azure::Core::Context& context) const
  {
    _detail::DataLakeRestClient::Path::SetAccessControlRecursiveOptions protocolLayerOptions;
    protocolLayerOptions.Mode = mode;
    protocolLayerOptions.ContinuationToken = options.ContinuationToken;
    protocolLayerOptions.MaxRecords = options.PageSizeHint;
    protocolLayerOptions.ForceFlag = options.ContinueOnFailure;
    protocolLayerOptions.Acl = Models::Acl::SerializeAcls(acls);
    return _detail::DataLakeRestClient::Path::SetAccessControlRecursive(
        m_pathUrl, *m_pipeline, context, protocolLayerOptions);
  }

}}}} // namespace Azure::Storage::Files::DataLake
