/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package utils

import (
	"io/ioutil"
	"os"
	"testing"

	"github.com/smartystreets/goconvey/convey"
)

func TestLoadClusterConfig(t *testing.T) {
	convey.Convey("Testing LoadConfig", t, func() {
		// Test case 1: Normal case
		convey.Convey("Given a valid YAML file", func() {
			fileContent := `
cluster_name: default
master_ip:
  - 192.168.1.1
agent_ip:
  - 192.168.1.2
  - 192.168.1.3
master_cmd:
  - yr start --master
agent_cmd:
  - ./program
`
			filePath := "cluster-config-test.yaml"
			err := ioutil.WriteFile(filePath, []byte(fileContent), 0o644)
			convey.So(err, convey.ShouldBeNil)
			convey.Reset(func() {
				os.Remove(filePath)
			})

			config, err := LoadClusterConfig(filePath)
			convey.So(err, convey.ShouldBeNil)
			convey.So(config.ClusterName, convey.ShouldEqual, "default")
			convey.So(config.MasterIp, convey.ShouldResemble, []string{"192.168.1.1"})
			convey.So(config.AgentIp, convey.ShouldResemble, []string{"192.168.1.2", "192.168.1.3"})
			convey.So(config.MasterCmd, convey.ShouldResemble, []string{"yr start --master"})
			convey.So(config.AgentCmd, convey.ShouldResemble, []string{"./program"})
		})

		// Test case 2: Missing required fields - cluster_name
		convey.Convey("Given a YAML file missing cluster_name", func() {
			fileContent := `
master_ip:
 - 192.168.1.1
agent_ip:
 - 192.168.1.2
`
			filePath := "cluster-config-test.yaml"
			err := ioutil.WriteFile(filePath, []byte(fileContent), 0o644)
			convey.So(err, convey.ShouldBeNil)
			convey.Reset(func() {
				os.Remove(filePath)
			})

			_, err = LoadClusterConfig(filePath)
			convey.So(err, convey.ShouldNotBeNil)
			convey.So(err.Error(), convey.ShouldContainSubstring, "field 'cluster_name' is required")
		})

		// Test case 3: Missing required fields - master_ip
		convey.Convey("Given a YAML file missing master_ip", func() {
			fileContent := `
cluster_name: default
agent_ip:
 - 192.168.1.2
`
			filePath := "cluster-config-test.yaml"
			err := ioutil.WriteFile(filePath, []byte(fileContent), 0o644)
			convey.So(err, convey.ShouldBeNil)
			convey.Reset(func() {
				os.Remove(filePath)
			})

			_, err = LoadClusterConfig(filePath)
			convey.So(err, convey.ShouldNotBeNil)
			convey.So(err.Error(), convey.ShouldContainSubstring, "field 'master_ip' is required")
		})

		// Test case 4: Missing required fields - agent_ip
		convey.Convey("Given a YAML file missing agent_ip", func() {
			fileContent := `
cluster_name: default
 - 192.168.1.2
master_ip:
 - 192.168.1.1
`
			filePath := "cluster-config-test.yaml"
			err := ioutil.WriteFile(filePath, []byte(fileContent), 0o644)
			convey.So(err, convey.ShouldBeNil)
			convey.Reset(func() {
				os.Remove(filePath)
			})

			_, err = LoadClusterConfig(filePath)
			convey.So(err, convey.ShouldNotBeNil)
			convey.So(err.Error(), convey.ShouldContainSubstring, "field 'agent_ip' is required")
		})

		// Test case 5: master_cmd with multiple lines
		convey.Convey("Given a YAML file with multiple lines in master_cmd", func() {
			fileContent := `
cluster_name: default
master_ip:
- 192.168.1.1
agent_ip:
- 192.168.1.2
master_cmd:
- yr start --master
- echo "master2"
`
			filePath := "cluster-config-test.yaml"
			err := ioutil.WriteFile(filePath, []byte(fileContent), 0o644)
			convey.So(err, convey.ShouldBeNil)
			convey.Reset(func() {
				os.Remove(filePath)
			})

			_, err = LoadClusterConfig(filePath)
			convey.So(err, convey.ShouldNotBeNil)
			convey.So(err.Error(), convey.ShouldContainSubstring, "field 'master_cmd' can have at most one command")
		})

		// Test case 6: agent_cmd with multiple lines
		convey.Convey("Given a YAML file with multiple lines in agent_cmd", func() {
			fileContent := `
cluster_name: default
master_ip:
- 192.168.1.1
agent_ip:
- 192.168.1.2
agent_cmd:
- yr start --master
- echo "agent2"
`
			filePath := "cluster-config-test.yaml"
			err := ioutil.WriteFile(filePath, []byte(fileContent), 0o644)
			convey.So(err, convey.ShouldBeNil)
			convey.Reset(func() {
				os.Remove(filePath)
			})

			_, err = LoadClusterConfig(filePath)
			convey.So(err, convey.ShouldNotBeNil)
			convey.So(err.Error(), convey.ShouldContainSubstring, "field 'agent_cmd' can have at most one command")
		})

		// Test case 7: invalid MasterIp
		convey.Convey("Given a YAML file with invalid master_ip", func() {
			fileContent := `
cluster_name: default
master_ip:
- 192.168.1.256
agent_ip:
- 192.168.1.2
`
			filePath := "cluster-config-test.yaml"
			err := ioutil.WriteFile(filePath, []byte(fileContent), 0o644)
			convey.So(err, convey.ShouldBeNil)
			convey.Reset(func() {
				os.Remove(filePath)
			})

			_, err = LoadClusterConfig(filePath)
			convey.So(err, convey.ShouldNotBeNil)
			convey.So(err.Error(), convey.ShouldContainSubstring, "invalid IP address in master_ip")
		})

		// Test case 8: invalid AgentIp
		convey.Convey("Given a YAML file with invalid agent_ip", func() {
			fileContent := `
cluster_name: default
master_ip:
- 192.168.1.1
agent_ip:
- 192.168.1.x
`
			filePath := "cluster-config-test.yaml"
			err := ioutil.WriteFile(filePath, []byte(fileContent), 0o644)
			convey.So(err, convey.ShouldBeNil)
			convey.Reset(func() {
				os.Remove(filePath)
			})

			_, err = LoadClusterConfig(filePath)
			convey.So(err, convey.ShouldNotBeNil)
			convey.So(err.Error(), convey.ShouldContainSubstring, "invalid IP address in agent_ip")
		})

		// Test case 9: File not found
		convey.Convey("Given a non-existent YAML file", func() {
			filePath := "nonexistent.yaml"
			_, err := LoadClusterConfig(filePath)
			convey.Reset(func() {
				os.Remove(filePath)
			})
			convey.So(err, convey.ShouldNotBeNil)
			convey.So(err.Error(), convey.ShouldContainSubstring, "failed to read file")
		})
	})
}
