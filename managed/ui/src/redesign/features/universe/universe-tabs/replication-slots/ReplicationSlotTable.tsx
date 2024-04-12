import { FC } from 'react';
import _ from 'lodash';
import { browserHistory } from 'react-router';
import { useTranslation } from 'react-i18next';
import { useQuery, useQueries } from 'react-query';
import { Box, Typography, Link } from '@material-ui/core';
import { TableHeaderColumn } from 'react-bootstrap-table';
import { YBTable } from '../../../../../components/common/YBTable';
import { YBLoading } from '../../../../../components/common/indicators';
import { QUERY_KEY, api } from '../../../../utils/api';
import { fetchCDCLagMetrics } from './utils/helper';
import { formatDuration } from '../../../../../utils/Formatters';
import { ReplicationSlot, ReplicationSlotResponse, SlotState } from './utils/types';
import { StatusBadge, Badge_Types } from '../../../../../components/common/badge/StatusBadge';
import { replicationSlotStyles } from './utils/ReplicationSlotStyles';

//icons
import DatabaseEvent from '../../../../assets/database-event.svg';
import Flash from '../../../../assets/flash.svg';
interface ReplicationTableProps {
  universeUUID: string;
  nodePrefix: string;
}

export const ReplicationSlotTable: FC<ReplicationTableProps> = ({ universeUUID, nodePrefix }) => {
  const classes = replicationSlotStyles();
  const { t } = useTranslation();

  const { data: replicationSlotData, isLoading } = useQuery<ReplicationSlotResponse>(
    [QUERY_KEY.getReplicationSlots, universeUUID],
    () => api.getReplicationSlots(universeUUID)
  );

  const metricsQuery = useQueries(
    (replicationSlotData?.replicationSlots || []).map((d) => ({
      queryKey: d.streamID,
      queryFn: () => fetchCDCLagMetrics(d.streamID, nodePrefix),
      enabled: !!replicationSlotData?.replicationSlots?.length
    }))
  );

  const getCurrentLag = (sID: string) => {
    const streamResult = metricsQuery.find((m) => m.data?.streamID === sID);
    if (!streamResult) return 'n/a';
    const currentLag = Number(_.last(streamResult?.data?.cdcsdk_sent_lag_micros?.data[0]?.y));
    return currentLag > 0 ? formatDuration(currentLag) : '0ms';
  };

  const getStatus = (sID: string, label: string) => {
    const streamResult = metricsQuery.find((m) => m.data?.streamID === sID);
    if (!streamResult) return 'n/a';
    const expiryTime = Number(_.last(streamResult?.data?.cdcsdk_expiry_time_mins?.data[0]?.y));
    return expiryTime > 0 ? _.capitalize(label) : _.capitalize(SlotState.EXPIRED);
  };

  const handleRowClick = (row: ReplicationSlot) => {
    browserHistory.push(`/universes/${universeUUID}/replication-slots/${row.streamID}`);
  };

  if (isLoading) return <YBLoading />;
  if (!!replicationSlotData?.replicationSlots?.length)
    return (
      <Box display="flex" flexDirection="column" width="100%">
        <Typography>
          {replicationSlotData?.replicationSlots?.length} {t('cdc.replicationSlots')}
        </Typography>
        <Box mt={3} width="100%" height="700px">
          <YBTable
            data={replicationSlotData?.replicationSlots}
            height={'690px'}
            tableStyle={{
              padding: '24px',
              borderRadius: '0px 10px 10px 10px',
              border: '0px solid  #DDE1E6',
              background: '#FFF',
              boxShadow: '0px 2px 10px 0px rgba(0, 0, 0, 0.15)',
              overflow: 'scroll'
            }}
            headerStyle={{
              fontSize: '12px',
              fontWeight: 600,
              color: '#0B1117'
            }}
            options={{
              onRowClick: handleRowClick
            }}
          >
            <TableHeaderColumn
              width={'35%'}
              dataField="slotName"
              dataSort
              dataFormat={(cell) => <span>{cell}</span>}
            >
              <span>{t('cdc.name')}</span>
            </TableHeaderColumn>
            <TableHeaderColumn
              width={'20%'}
              dataField="state"
              dataSort
              dataFormat={(cell, row) => (
                <StatusBadge
                  statusType={
                    getStatus(row.streamID, cell) === _.capitalize(SlotState.EXPIRED)
                      ? Badge_Types.EXPIRED
                      : [SlotState.INITIATED, SlotState.ACTIVE].includes(cell)
                      ? Badge_Types.EXPIRED
                      : Badge_Types.DELETED
                  }
                  customLabel={getStatus(row.streamID, cell)}
                />
              )}
            >
              <span>{t('cdc.status')}</span>
            </TableHeaderColumn>
            <TableHeaderColumn
              width={'25%'}
              dataField="databaseName"
              dataSort
              dataFormat={(cell) => <span>{cell}</span>}
            >
              <span>{t('cdc.database')}</span>
            </TableHeaderColumn>
            <TableHeaderColumn
              width={'20%'}
              dataField="streamID"
              dataSort
              dataFormat={(cell) => <span>{getCurrentLag(cell)}</span>}
              isKey
            >
              <span>{t('cdc.currentLag')}</span>
            </TableHeaderColumn>
          </YBTable>
        </Box>
      </Box>
    );
  else
    return (
      <Box className={classes.emptyContainer}>
        <img src={DatabaseEvent} alt="--" />
        <Box mt={2} mb={2}>
          <Typography className={classes.emptyContainerTitle}>
            {t('cdc.emptyContainerTitle')}{' '}
          </Typography>
        </Box>
        <Typography variant="body2" className={classes.emptyContainerSubtitle}>
          {t('cdc.emptyContainerSubtitle')}
        </Typography>
        <Box display="flex" flexDirection="row" mt={2} alignItems={'end'}>
          <img src={Flash} alt="--" /> &nbsp;
          <Link
            component={'button'}
            underline="always"
            onClick={() => {}}
            className={classes.emptyContainerLink}
          >
            {t('cdc.emptyContainerLink')}
          </Link>
        </Box>
      </Box>
    );
};
