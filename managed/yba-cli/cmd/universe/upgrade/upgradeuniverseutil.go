/*
 * Copyright (c) YugaByte, Inc.
 */

package upgrade

import (
	"fmt"
	"net/http"
	"os"
	"strings"

	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
	ybaclient "github.com/yugabyte/platform-go-client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/cmd/util"
	ybaAuthClient "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/client"
	"github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter"
	universeFormatter "github.com/yugabyte/yugabyte-db/managed/yba-cli/internal/formatter/universe"
)

func waitForUpgradeUniverseTask(
	authAPI *ybaAuthClient.AuthAPIClient, universeName, universeUUID, taskUUID string) {

	var universeData []ybaclient.UniverseResp
	var response *http.Response
	var err error

	msg := fmt.Sprintf("The universe %s (%s) is being upgraded",
		formatter.Colorize(universeName, formatter.GreenColor), universeUUID)

	if viper.GetBool("wait") {
		if taskUUID != "" {
			logrus.Info(fmt.Sprintf("Waiting for universe %s (%s) to be upgraded\n",
				formatter.Colorize(universeName, formatter.GreenColor), universeUUID))
			err = authAPI.WaitForTask(taskUUID, msg)
			if err != nil {
				logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
			}
		}
		logrus.Infof("The universe %s (%s) has been upgraded\n",
			formatter.Colorize(universeName, formatter.GreenColor), universeUUID)

		universeData, response, err = authAPI.ListUniverses().Name(universeName).Execute()
		if err != nil {
			errMessage := util.ErrorFromHTTPResponse(response, err, "Universe", "Upgrade - Fetch Universe")
			logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
		}
		universesCtx := formatter.Context{
			Output: os.Stdout,
			Format: universeFormatter.NewUniverseFormat(viper.GetString("output")),
		}

		universeFormatter.Write(universesCtx, universeData)

	} else {
		logrus.Infoln(msg + "\n")
	}

}

// UpgradeValidations to ensure that the universe being accessed exists
func UpgradeValidations(cmd *cobra.Command, operation string) (
	*ybaAuthClient.AuthAPIClient,
	ybaclient.UniverseResp,
	error,
) {
	authAPI := ybaAuthClient.NewAuthAPIClientAndCustomer()
	universeName, err := cmd.Flags().GetString("name")
	if err != nil {
		logrus.Fatalf(formatter.Colorize(err.Error()+"\n", formatter.RedColor))
	}

	universeListRequest := authAPI.ListUniverses()
	universeListRequest = universeListRequest.Name(universeName)

	r, response, err := universeListRequest.Execute()
	if err != nil {
		errMessage := util.ErrorFromHTTPResponse(
			response, err,
			"Universe",
			fmt.Sprintf("%s - List Universes", operation))
		logrus.Fatalf(formatter.Colorize(errMessage.Error()+"\n", formatter.RedColor))
	}
	if len(r) < 1 {
		err := fmt.Errorf("No universes with name: %s found\n", universeName)
		return nil, ybaclient.UniverseResp{}, err
	}
	return authAPI, r[0], nil
}

func FetchMasterGFlags(masterGFlagsString string) map[string]string {
	masterGFlags := make(map[string]interface{}, 0)
	if len(strings.TrimSpace(masterGFlagsString)) != 0 {
		for _, masterGFlagPair := range strings.Split(masterGFlagsString, ",") {
			kvp := strings.Split(masterGFlagPair, "=")
			if len(kvp) != 2 {
				logrus.Fatalln(
					formatter.Colorize("Incorrect format in master gflag.\n",
						formatter.RedColor))
			}
			masterGFlags[kvp[0]] = kvp[1]
		}
	}
	return *util.StringMap(masterGFlags)
}

func FetchTServerGFlags(
	tserverGFlagsStringList []string,
	noOfClusters int,
) []map[string]string {
	tserverGFlagsList := make([]map[string]string, 0)
	for _, tserverGFlagsString := range tserverGFlagsStringList {
		if len(strings.TrimSpace(tserverGFlagsString)) > 0 {
			tserverGFlags := make(map[string]interface{}, 0)
			for _, tserverGFlagPair := range strings.Split(tserverGFlagsString, ",") {
				kvp := strings.Split(tserverGFlagPair, "=")
				if len(kvp) != 2 {
					logrus.Fatalln(
						formatter.Colorize("Incorrect format in tserver gflag.\n",
							formatter.RedColor))
				}
				tserverGFlags[kvp[0]] = kvp[1]
			}
			tserverGflagsMap := util.StringMap(tserverGFlags)
			tserverGFlagsList = append(tserverGFlagsList, *tserverGflagsMap)
		}
	}
	if len(tserverGFlagsList) == 0 {
		for i := 0; i < noOfClusters; i++ {
			tserverGFlagsList = append(tserverGFlagsList, make(map[string]string, 0))
		}
	}
	tserverGFlagsListLen := len(tserverGFlagsList)
	if tserverGFlagsListLen < noOfClusters {
		for i := 0; i < noOfClusters-tserverGFlagsListLen; i++ {
			tserverGFlagsList = append(tserverGFlagsList, tserverGFlagsList[0])
		}
	}
	return tserverGFlagsList
}
