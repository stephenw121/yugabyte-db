// tslint:disable
/**
 * Yugabyte Cloud
 * YugabyteDB as a Service
 *
 * The version of the OpenAPI document: v1
 * Contact: support@yugabyte.com
 *
 * NOTE: This class is auto generated by OpenAPI Generator (https://openapi-generator.tech).
 * https://openapi-generator.tech
 * Do not edit the class manually.
 */




/**
 * Node level information
 * @export
 * @interface ClusterNodeInfo
 */
export interface ClusterNodeInfo  {
  /**
   * The total amount of RAM (MB) used by all nodes
   * @type {number}
   * @memberof ClusterNodeInfo
   */
  memory_mb: number;
  /**
   * The total size of disk (GB)
   * @type {number}
   * @memberof ClusterNodeInfo
   */
  disk_size_gb: number;
  /**
   * The total size of used disk space (GB)
   * @type {number}
   * @memberof ClusterNodeInfo
   */
  disk_size_used_gb?: number;
  /**
   * The average CPU usage over all nodes
   * @type {number}
   * @memberof ClusterNodeInfo
   */
  cpu_usage?: number;
  /**
   * The number of CPU cores per node
   * @type {number}
   * @memberof ClusterNodeInfo
   */
  num_cores: number;
  /**
   * The total size of provisioned ram (GB)
   * @type {number}
   * @memberof ClusterNodeInfo
   */
  ram_provisioned_gb: number;
}



