/*
 * Copyright (c) YugaByte, Inc.
 */

package util

// Environment variable fields
const (
	// GCPCredentialsEnv env variable name for gcp provider/storage config/releases
	GCPCredentialsEnv = "GOOGLE_APPLICATION_CREDENTIALS"
	// GCSCredentialsJSON field name to denote in Json request
	GCSCredentialsJSON = "GCS_CREDENTIALS_JSON"
	// UseGCPIAM field name to denote in Json request
	UseGCPIAM = "USE_GCP_IAM"

	// AWSAccessKeyEnv env variable name for aws provider/storage config/releases
	AWSAccessKeyEnv = "AWS_ACCESS_KEY_ID"
	// AWSSecretAccessKeyEnv env variable name for aws provider/storage config/releases
	AWSSecretAccessKeyEnv = "AWS_SECRET_ACCESS_KEY"
	// IAMInstanceProfile field name to denote in Json request
	IAMInstanceProfile = "IAM_INSTANCE_PROFILE"

	// AzureSubscriptionIDEnv env variable name for azure provider
	AzureSubscriptionIDEnv = "AZURE_SUBSCRIPTION_ID"
	// AzureRGEnv env variable name for azure provider
	AzureRGEnv = "AZURE_RG"
	// AzureTenantIDEnv env variable name for azure provider
	AzureTenantIDEnv = "AZURE_TENANT_ID"
	// AzureClientIDEnv env variable name for azure provider
	AzureClientIDEnv = "AZURE_CLIENT_ID"
	// AzureClientSecretEnv env variable name for azure provider
	AzureClientSecretEnv = "AZURE_CLIENT_SECRET"

	// AzureStorageSasTokenEnv env variable name azure storage config
	AzureStorageSasTokenEnv = "AZURE_STORAGE_SAS_TOKEN"
)

// Minimum YugabyteDB Anywhere versions to support operation
const (

	// YBAAllowUniverseMinVersion specifies minimum version
	// required to use Universe resource via YBA CLI
	YBAAllowUniverseMinVersion = "2.17.1.0-b371"

	// YBAAllowBackupMinVersion specifies minimum version
	// required to use Scheduled Backup resource via YBA CLI
	YBAAllowBackupMinVersion = "2.18.1.0-b20"

	// YBAAllowEditProviderMinVersion specifies minimum version
	// required to Edit a Provider (onprem or cloud) resource
	// via YBA CLI
	YBAAllowNewProviderMinVersion = "2.18.0.0-b65"

	// YBAAllowFailureSubTaskListMinVersion specifies minimum version
	// required to fetch failed subtask message from YugabyteDB Anywhere
	YBAAllowFailureSubTaskListMinVersion = "2.19.0.0-b68"

	MinCLIStableVersion  = "2024.1.0.0-b4"
	MinCLIPreviewVersion = "2.21.0.0-b545"
)

// UniverseStates
const (
	// ReadyUniverseState state
	ReadyUniverseState = "Ready"
	// PausedUniverseState state
	PausedUniverseState = "Paused"
	// PendingUniverseState state
	PendingUniverseState = "Pending"
	// WarningUniverseState state
	WarningUniverseState = "Warning"
	// BadUniverseState state
	BadUniverseState = "Error"
	// UnknownUniverseState state
	UnknownUniverseState = "Loading"
)

// ProviderStates
const (
	// ReadyProviderState state
	ReadyProviderState = "READY"
	// UpdatingProviderState state
	UpdatingProviderState = "UPDATING"
	// ErrorroviderState state
	ErrorProviderState = "ERROR"
	// DeletingProviderState state
	DeletingProviderState = "DELETING"
)

// Allowed states for YugabyteDB Anywhere Tasks
const (
	// CreateTaskStatus task status
	CreatedTaskStatus = "Created"
	// InitializingTaskStatus task status
	InitializingTaskStatus = "Initializing"
	// RunningTaskStatus task status
	RunningTaskStatus = "Running"
	// SuccessTaskStatus task status
	SuccessTaskStatus = "Success"
	// FailureTaskStatus task status
	FailureTaskStatus = "Failure"
	// UnknownTaskStatus task status
	UnknownTaskStatus = "Unknown"
	// AbortTaskStatus task status
	AbortTaskStatus = "Abort"
	// AbortedTaskStatus task status
	AbortedTaskStatus = "Aborted"
)

// Node operations allowed on universe
const (
	// AddNode operation
	AddNode = "ADD"
	// StartNode operation
	StartNode = "START"
	// RebootNode operation
	RebootNode = "REBOOT"
	// StopNode operation
	StopNode = "STOP"
	// RemoveNode operation
	RemoveNode = "REMOVE"
	// ReprovisionNode operation
	ReprovisionNode = "REPROVISION"
	// ReleaseNode operation
	ReleaseNode = "RELEASE"
)

const (
	// StorageCustomerConfigType field name to denote in request bodies
	StorageCustomerConfigType = "STORAGE"
)

// Server Type values
const (
	// MasterServerType for master processes
	MasterServerType = "MASTER"
	// TserverServerType for tserver processes
	TserverServerType = "TSERVER"
	// ControllerServerType for YBC processes
	ControllerServerType = "CONTROLLER"
)

// Operation Type
const (
	// UpgradeOperation type
	UpgradeOperation = "Upgrade"
	// EditOperation type
	EditOperation = "Edit"
)

// Different resource types that are supported in CLI
const (
	// UniverseType resource
	UniverseType = "universe"
	// ProviderType resource
	ProviderType = "provider"
	// StorageConfigurationType resource
	StorageConfigurationType = "storage configuration"
)

// Different cloud provider types
const (
	// util.AWSProviderType type
	AWSProviderType = "aws"
	// AzureProviderType type
	AzureProviderType = "azu"
	// GCPProviderType type
	GCPProviderType = "gcp"
	// K8sProviderType type
	K8sProviderType = "kubernetes"
	// OnpremProviderType type
	OnpremProviderType = "onprem"
)

// Different storage configuration types
const (
	// S3StorageConfigType type
	S3StorageConfigType = "S3"
	// AzureStorageConfigType type
	AzureStorageConfigType = "AZ"
	// GCSStorageConfigType type
	GCSStorageConfigType = "GCS"
	// NFSStorageConfigType type
	NFSStorageConfigType = "NFS"
)

// ClusterTypes for universe
const (
	// PrimaryClusterType for primary cluster
	PrimaryClusterType = "PRIMARY"
	// ReadReplicaClusterType for rrs
	ReadReplicaClusterType = "ASYNC"
)

// CompletedStates returns set of states that mark the task as completed
func CompletedStates() []string {
	return []string{SuccessTaskStatus, FailureTaskStatus, AbortedTaskStatus}
}

// ErrorStates return set of states that mark state as failure
func ErrorStates() []string {
	return []string{FailureTaskStatus, AbortedTaskStatus}
}

// IncompleteStates return set of states for ongoing tasks
func IncompleteStates() []string {
	return []string{CreatedTaskStatus, InitializingTaskStatus, RunningTaskStatus, AbortTaskStatus}
}

// YugabyteDB Anywhere versions >= the minimum listed versions for operations
// that need to be restricted

// YBARestrictBackupVersions are certain YugabyteDB Anywhere versions >= min
// version for backups that would not support the operation
func YBARestrictBackupVersions() []string {
	return []string{"2.19.0.0"}
}

// YBARestrictFailedSubtasksVersions are certain YugabyteDB Anywhere versions >= min
// version for of fetching failed subtask lists that would not support the operation
func YBARestrictFailedSubtasksVersions() []string {
	return []string{"2.19.0.0"}
}
