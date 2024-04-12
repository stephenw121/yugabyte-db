package com.yugabyte.troubleshoot.ts.service;

import com.yugabyte.troubleshoot.ts.logs.LogsUtil;
import com.yugabyte.troubleshoot.ts.models.Anomaly;
import com.yugabyte.troubleshoot.ts.models.AnomalyMetadata;
import com.yugabyte.troubleshoot.ts.models.UniverseMetadata;
import com.yugabyte.troubleshoot.ts.service.anomaly.AnomalyDetector;
import com.yugabyte.troubleshoot.ts.service.anomaly.AnomalyMetadataProvider;
import io.ebean.Database;
import java.time.Duration;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import java.util.concurrent.Future;
import lombok.extern.slf4j.Slf4j;
import org.springframework.scheduling.concurrent.ThreadPoolTaskExecutor;
import org.springframework.stereotype.Service;

@Slf4j
@Service
public class TroubleshootingService {

  private static final long MAX_STEP_SECONDS = Duration.ofMinutes(2).toSeconds();

  private final UniverseMetadataService universeMetadataService;
  private final RuntimeConfigService runtimeConfigService;
  private final List<AnomalyDetector> anomalyDetectors;
  private final AnomalyMetadataProvider anomalyMetadataProvider;
  private final ThreadPoolTaskExecutor anomalyDetectionExecutor;

  public TroubleshootingService(
      Database database,
      UniverseMetadataService universeMetadataService,
      RuntimeConfigService runtimeConfigService,
      List<AnomalyDetector> anomalyDetectors,
      AnomalyMetadataProvider anomalyMetadataProvider,
      ThreadPoolTaskExecutor anomalyDetectionExecutor) {
    this.universeMetadataService = universeMetadataService;
    this.runtimeConfigService = runtimeConfigService;
    this.anomalyDetectors = anomalyDetectors;
    this.anomalyMetadataProvider = anomalyMetadataProvider;
    this.anomalyDetectionExecutor = anomalyDetectionExecutor;
  }

  public List<AnomalyMetadata> getAnomaliesMetadata() {
    return anomalyMetadataProvider.getMetadataList();
  }

  public List<Anomaly> findAnomalies(UUID universeUuid, Instant startTime, Instant endTime) {
    List<Future<AnomalyDetector.AnomalyDetectionResult>> futures = new ArrayList<>();
    // We don't want graph resolution to be too bad even if we check 2 weeks period.
    long step =
        Math.min(
            (endTime.getEpochSecond()
                - startTime.getEpochSecond() / GraphService.GRAPH_POINTS_DEFAULT),
            MAX_STEP_SECONDS);
    UniverseMetadata universeMetadata = universeMetadataService.get(universeUuid);
    AnomalyDetector.AnomalyDetectionContext context =
        AnomalyDetector.AnomalyDetectionContext.builder()
            .universeMetadata(universeMetadata)
            .startTime(startTime)
            .endTime(endTime)
            .stepSeconds(step)
            .config(runtimeConfigService.getUniverseConfig(universeMetadata))
            .build();
    for (AnomalyDetector detector : anomalyDetectors) {
      futures.add(
          anomalyDetectionExecutor.submit(
              LogsUtil.wrapCallable(() -> detector.findAnomalies(context))));
    }
    List<Anomaly> result = new ArrayList<>();
    for (Future<AnomalyDetector.AnomalyDetectionResult> future : futures) {
      try {
        AnomalyDetector.AnomalyDetectionResult detectionResult = future.get();
        if (detectionResult.isSuccess()) {
          result.addAll(detectionResult.getAnomalies());
        } else {
          log.warn("Failure during anomaly detection: {}", detectionResult.getErrorMessages());
        }
      } catch (Exception e) {
        log.warn("Failure during anomaly detection", e);
      }
    }
    return result;
  }
}
