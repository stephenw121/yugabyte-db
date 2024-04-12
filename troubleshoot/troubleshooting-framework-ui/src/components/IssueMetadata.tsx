import { FC } from 'react';
import { Box, Divider, Tooltip } from '@material-ui/core';
import { Link } from 'react-router';
import _ from 'lodash';
import { YBButton, YBLabel } from '@yugabytedb/ui-components';
import { Anomaly, AnomalyCategory, AppName, NodeInfo } from '../helpers/dtos';
import { isNonEmptyString } from '../helpers/objectUtils';
import { YBTimeFormats, formatDatetime } from '../helpers/dateUtils';
import { useHelperStyles } from './styles';

import LightBulbIcon from '../assets/lightbulb.svg';
import WarningIcon from '../assets/warning-solid.svg';

interface IssueMetadataProps {
  data: Anomaly;
  title: string;
  uuid: string;
  universeUuid: string;
  appName: AppName;
  timezone?: string;
  onSelectedIssue?: (troubleshootUuid: string) => void;
}

// eslint-disable-next-line @typescript-eslint/no-var-requires
export const IssueMetadata: FC<IssueMetadataProps> = ({
  data,
  title,
  uuid,
  universeUuid,
  appName,
  timezone,
  onSelectedIssue
}) => {
  const classes = useHelperStyles();
  // const defaultUniverseUUID = 'b19a09a8-aa2c-4a5d-a248-5702dd1839b4';
  const troubleshootUUID = uuid;
  let anomalySummary = data.summary;
  let SQLQuery = '';
  let restSummary = '';
  if (data.category === AnomalyCategory.SQL || data.category === AnomalyCategory.NODE) {
    const splitData = anomalySummary.split("'");
    if (splitData.length > 1) {
      const firstHalf = splitData?.[0];
      const secondHalf = splitData?.[1];
      anomalySummary = firstHalf;
      SQLQuery = secondHalf;
      restSummary = splitData[splitData.length - 1];
    }
  }

  const routeToSecondary = () => {
    onSelectedIssue?.(troubleshootUUID);
  };

  return (
    <Box>
      <Box className={classes.troubleshootBox}>
        <Box className={classes.anomalyTitle}> {title} </Box>
        <Box mt={1}>
          <Divider />
        </Box>

        <Box mt={2} className={classes.flexRow}>
          <YBLabel>{`Observation: `}</YBLabel>
          <span>
            {anomalySummary}
            {isNonEmptyString(SQLQuery) && (
              <span className={classes.queryBox}>{`${SQLQuery}`}</span>
            )}
            {isNonEmptyString(restSummary) && <span>{restSummary}</span>}
          </span>
        </Box>

        <Box mt={2.5} className={classes.flexRow}>
          <YBLabel>{`Start Time: `}</YBLabel>

          {formatDatetime(data.startTime, YBTimeFormats.YB_DATE_TIME_TIMESTAMP, timezone)}
          {!data.endTime && (
            <Tooltip title={'Ongoing Incident'} arrow placement="top">
              <img src={WarningIcon} className={classes.incidentStatus} alt="status" />
            </Tooltip>
          )}
        </Box>

        {data.endTime && (
          <Box mt={2.5} className={classes.flexRow}>
            <YBLabel>{`End Time: `}</YBLabel>
            {formatDatetime(data.endTime, YBTimeFormats.YB_DATE_TIME_TIMESTAMP, timezone)}
          </Box>
        )}

        <Box mt={2.5} className={classes.flexRow}>
          <YBLabel className={classes.metaDataLabel}>{`Affected Nodes: `}</YBLabel>
          <Box className={classes.flexColumn}>
            {data.affectedNodes?.map((affectedNode: NodeInfo, idx: number) => {
              return (
                <Box>
                  <li>{affectedNode.name}</li>
                </Box>
              );
            })}
          </Box>
        </Box>

        <Box mt={2.5} mb={1}>
          <img src={LightBulbIcon} alt="more" className={classes.learnMoreImage} />
          {appName === AppName.YBA ? (
            <Link
              to={`/universes/${universeUuid}/troubleshoot/${troubleshootUUID}`}
              target="_blank"
            >
              <span className={classes.redirectLinkText}>
                {'To troubleshoot, refer to the dashboard.'}
              </span>
            </Link>
          ) : (
            <Box>
              <YBButton
                variant="pill"
                data-testid="PrimaryDashboard-RouteToSecondaryButton"
                onClick={() => routeToSecondary()}
              >
                <span className={classes.redirectLinkText}>
                  {'To troubleshoot, refer to the dashboard.'}
                </span>
              </YBButton>
            </Box>
          )}
        </Box>
      </Box>
    </Box>
  );
};
