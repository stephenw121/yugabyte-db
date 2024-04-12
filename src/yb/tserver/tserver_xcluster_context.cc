// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#include "yb/tserver/tserver_xcluster_context.h"

#include "yb/util/result.h"
#include "yb/tserver/xcluster_safe_time_map.h"

namespace yb::tserver {

Result<std::optional<HybridTime>> TserverXClusterContext::GetSafeTime(
    const NamespaceId& namespace_id) const {
  return safe_time_map_.GetSafeTime(namespace_id);
}

bool TserverXClusterContext::IsReadOnlyMode(const NamespaceId namespace_id) const {
  // Namespaces that are part of the safe time belong to an inbound transactional xCluster
  // replication.
  return safe_time_map_.HasNamespace(namespace_id);
}

void TserverXClusterContext::UpdateSafeTime(const XClusterNamespaceToSafeTimePBMap& safe_time_map) {
  safe_time_map_.Update(safe_time_map);
}

bool TserverXClusterContext::SafeTimeComputationRequired() const {
  // If we have any namespaces with safe times, then we need to compute safe time.
  return !safe_time_map_.empty();
}

bool TserverXClusterContext::SafeTimeComputationRequired(const NamespaceId namespace_id) const {
  return safe_time_map_.HasNamespace(namespace_id);
}

}  // namespace yb::tserver
