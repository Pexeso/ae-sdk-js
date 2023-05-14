// Copyright 2023 Pexeso Inc. All rights reserved.

#include "searchworker.h"

#include <pex/ae/sdk/lock.h>

#include "defer.h"

SearchWorker::~SearchWorker() {
  AE_CheckSearchResult_Delete(&result_);
}

void SearchWorker::Execute() {
  ExecuteStartSearch();
  if (!IsRejected()) {
    ExecuteCheckSearch();
  }
}

void SearchWorker::ExecuteStartSearch() {
  Defer defer;

  AE_Lock();
  defer.Add(AE_Unlock);

  auto status = AE_Status_New();
  if (!status) {
    return Reject(1, "out of memory");  // TODO: handle error
  }
  defer.Add(std::bind(AE_Status_Delete, &status));

  auto request = AE_StartSearchRequest_New();
  if (!request) {
    return Reject(1, "out of memory");  // TODO: handle error
  }
  defer.Add(std::bind(AE_StartSearchRequest_Delete, &request));

  auto result = AE_StartSearchResult_New();
  if (!result) {
    return Reject(1, "out of memory");  // TODO: handle error
  }
  defer.Add(std::bind(AE_StartSearchResult_Delete, &result));

  auto buffer = AE_Buffer_New();
  if (!buffer) {
    return Reject(1, "out of memory");  // TODO: handle error
  }
  defer.Add(std::bind(AE_Buffer_Delete, &buffer));

  AE_Buffer_Set(buffer, ft_->bytes().data(), ft_->bytes().size());

  AE_StartSearchRequest_SetFingerprint(request, buffer, status);
  if (!AE_Status_OK(status)) {
    return Reject(status);
  }

  AE_StartSearch(client_, request, result, status);
  if (!AE_Status_OK(status)) {
    return Reject(status);
  }

  lookup_id_ = AE_StartSearchResult_GetLookupID(result);
}

void SearchWorker::ExecuteCheckSearch() {
  Defer defer;

  AE_Lock();
  defer.Add(AE_Unlock);

  auto status = AE_Status_New();
  if (!status) {
    return Reject(1, "out of memory");  // TODO: handle error
  }
  defer.Add(std::bind(AE_Status_Delete, &status));

  auto request = AE_CheckSearchRequest_New();
  if (!request) {
    return Reject(1, "out of memory");  // TODO: handle error
  }
  defer.Add(std::bind(AE_CheckSearchRequest_Delete, &request));

  AE_CheckSearchRequest_SetLookupID(request, lookup_id_.c_str());

  result_ = AE_CheckSearchResult_New();
  if (!result_) {
    return Reject(1, "out of memory");  // TODO: handle error
  }

  AE_CheckSearch(client_, request, result_, status);
  if (!AE_Status_OK(status)) {
    return Reject(status);
  }
}

Napi::Value SearchWorker::Resolve() {
  auto js_result = Napi::Object::New(Env());
  js_result.Set("lookup_id", lookup_id_);

  auto js_matches = Napi::Array::New(Env());

  if (AE_CheckSearchResult_MatchCount(result_) == 0) {
    js_result.Set("matches", js_matches);
    js_result.Freeze();
    return js_result;
  }

  Defer defer;

  auto status = AE_Status_New();
  if (!status) {
    Reject(1, "out of memory");
    return Env().Undefined();
  }
  defer.Add(std::bind(AE_Status_Delete, &status));

  auto match = AE_SearchMatch_New();
  if (!match) {
    Reject(1, "out of memory");
    return Env().Undefined();
  }
  defer.Add(std::bind(AE_SearchMatch_Delete, &match));

  auto asset = AE_Asset_New();
  if (!asset) {
    Reject(1, "out of memory");
    return Env().Undefined();
  }
  defer.Add(std::bind(AE_Asset_Delete, &asset));

  int matches_pos = 0;
  while (AE_CheckSearchResult_NextMatch(result_, match, &matches_pos)) {
    auto js_segments = Napi::Array::New(Env());

    int64_t query_start;
    int64_t query_end;
    int64_t asset_start;
    int64_t asset_end;
    int type;

    int segments_pos = 0;
    while (AE_SearchMatch_NextSegment(match, &query_start, &query_end, &asset_start, &asset_end,
                                      &type, &segments_pos)) {
      auto js_segment = Napi::Object::New(Env());
      js_segment["query_start"] = query_start;
      js_segment["query_end"] = query_end;
      js_segment["asset_start"] = asset_start;
      js_segment["asset_end"] = asset_end;
      js_segment["type"] = type;
      js_segment.Freeze();
      js_segments[segments_pos - 1] = js_segment;
    }

    AE_SearchMatch_GetAsset(match, asset, status);
    if (!AE_Status_OK(status)) {
      Reject(status);
      return Env().Undefined();
    }

    auto js_asset = Napi::Object::New(Env());
    js_asset["title"] = AE_Asset_GetTitle(asset);
    js_asset["artist"] = AE_Asset_GetArtist(asset);
    js_asset["label"] = AE_Asset_GetLabel(asset);
    js_asset["duration"] = AE_Asset_GetDuration(asset);
    js_asset.Freeze();

    auto js_match = Napi::Object::New(Env());
    js_match["asset"] = js_asset;
    js_match["segments"] = js_segments;
    js_match.Freeze();
    js_matches[matches_pos - 1] = js_match;
  }

  js_result["matches"] = js_matches;
  js_result.Freeze();
  return js_result;
}
