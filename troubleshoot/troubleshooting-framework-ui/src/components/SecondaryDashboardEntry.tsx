import { useState } from 'react';
import { useQuery } from 'react-query';
import { Box, Typography, makeStyles } from '@material-ui/core';
import _ from 'lodash';
import clsx from 'clsx';
import { YBButton } from '@yugabytedb/ui-components';
import { YBErrorIndicator } from '../common/YBErrorIndicator';
import { YBBreadcrumb } from '../common/YBBreadcrumb';
import { SecondaryDashboardData } from './SecondaryDashboardData';
import { QUERY_KEY, TroubleshootAPI } from '../api';
import { Anomaly, AppName, GraphQuery, Universe } from '../helpers/dtos';
import { getGraphRequestParams, getRecommendationMetricsMap } from '../helpers/utils';

import { ReactComponent as LoadingIcon } from '../assets/loading.svg';

const useStyles = makeStyles((theme) => ({
  inProgressIcon: {
    color: '#1A44A5'
  },
  icon: {
    height: '40px',
    width: '40px'
  },
  loadingBox: {
    position: 'fixed',
    left: '50%',
    top: '50%',
    width: '100%',
    height: '100%'
  }
}));

export interface SecondaryDashboardEntryProps {
  universeUuid: string;
  hideHeader?: boolean;
  troubleshootUuid: string;
  // TODO: any should be replaced with YBM Node Response
  universeData: Universe | any;
  appName: AppName;
  timezone?: string;
  onSelectedIssue?: (troubleshootUuid: string | null) => void;
}

export const SecondaryDashboardEntry = ({
  universeUuid,
  troubleshootUuid,
  universeData,
  appName,
  timezone,
  hideHeader = false,
  onSelectedIssue
}: SecondaryDashboardEntryProps) => {
  const classes = useStyles();

  const [userSelectedAnomaly, setUserSelectedAnomaly] = useState<Anomaly | null>(null);
  const [graphRequestParams, setGraphRequestParams] = useState<GraphQuery[] | null>(null);
  const [recommendationMetrics, setRecommendationMetrics] = useState<any>(null);

  const { isLoading, isError, isIdle } = useQuery(
    [QUERY_KEY.fetchAnamolies, universeUuid],
    () => TroubleshootAPI.fetchAnamoliesById(universeUuid, troubleshootUuid),
    {
      onSuccess: (anomalyData: Anomaly) => {
        setUserSelectedAnomaly(anomalyData);
        setGraphRequestParams(getGraphRequestParams(anomalyData));
        setRecommendationMetrics(getRecommendationMetricsMap(anomalyData));
      },
      onError: (error: any) => {
        console.error('Failed to fetch Anomalies', error);
      }
    }
  );

  if (isLoading) {
    return (
      <Box className={classes.loadingBox}>
        <LoadingIcon className={clsx(classes.icon, classes.inProgressIcon)} />
      </Box>
    );
  }

  if (isError || (isIdle && userSelectedAnomaly === undefined)) {
    return <YBErrorIndicator customErrorMessage={'Failed to fetch anomalies list'} />;
  }

  const routeToPrimary = () => {
    onSelectedIssue?.(null);
  };

  return (
    <Box>
      {!hideHeader && (
        <Typography variant="h2" className="content-title">
          {appName === AppName.YBA ? (
            <YBBreadcrumb to={`/universes/${universeUuid}/troubleshoot`}>
              {'Troubleshoot'}
            </YBBreadcrumb>
          ) : (
            <Box>
              <YBButton variant="pill" data-testid="BtnAddIPList" onClick={() => routeToPrimary()}>
                {'Troubleshoot'}
              </YBButton>
            </Box>
          )}
        </Typography>
      )}
      {userSelectedAnomaly && (
        <SecondaryDashboardData
          anomalyData={userSelectedAnomaly}
          universeData={universeData}
          universeUuid={universeUuid}
          appName={appName}
          timezone={timezone}
          graphParams={graphRequestParams}
          recommendationMetrics={recommendationMetrics}
        />
      )}
    </Box>
  );
};
