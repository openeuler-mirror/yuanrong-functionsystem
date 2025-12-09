/*
 * Copyright (c) 2024 Huawei Technologies Co., Ltd
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

package model

// Resource -
type Resource struct {
	Requests map[string]string `json:"requests"`
	Limits   map[string]string `json:"limits"`
}

type IdleRecyclePolicy struct {
	Reserved int `json:"reserved"`
	Scaled   int `json:"scaled"`
}

// Pool -
type Pool struct {
	Id                          string            `json:"id"`
	Group                       string            `json:"group"`
	Size                        int               `json:"size"`
	MaxSize                     int               `json:"max_size"`
	ReadyCount                  int               `json:"ready_count"`
	Status                      int               `json:"status"`
	Msg                         string            `json:"msg"`
	Image                       string            `json:"image"`
	InitImage                   string            `json:"init_image"`
	Reuse                       bool              `json:"reuse"`
	Labels                      map[string]string `json:"labels"`
	Environment                 map[string]string `json:"environment"`
	Resources                   Resource          `json:"resources"`
	Volumes                     string            `json:"volumes"`
	VolumeMounts                string            `json:"volume_mounts"`
	Affinities                  string            `json:"affinities"`
	RuntimeClassName            string            `json:"runtime_class_name"`
	NodeSelector                map[string]string `json:"node_selector"`
	Tolerations                 string            `json:"tolerations"`
	HorizontalPodAutoscalerSpec string            `json:"horizontal_pod_autoscaler_spec"`
	TopologySpreadConstraints   string            `json:"topology_spread_constraints"`
	PodPendingDurationThreshold int               `json:"pod_pending_duration_threshold"`
	IdleRecycleTime             IdleRecyclePolicy `json:"idle_recycle_time"`
	Scalable                    bool              `json:"scalable"`
}

// PodPoolCreateRequest -
type PodPoolCreateRequest struct {
	Pools []Pool `json:"pools"`
}

// PodPoolCreateResponse -
type PodPoolCreateResponse struct {
	FailedPools []string `json:"failed_pools"`
}

// PodPoolUpdateRequest -
type PodPoolUpdateRequest struct {
	ID                          string `json:"id"`
	Size                        int    `json:"size"`
	MaxSize                     int    `json:"max_size"`
	HorizontalPodAutoscalerSpec string `json:"horizontal_pod_autoscaler_spec"`
}

// PodPoolGetRequest -
type PodPoolGetRequest struct {
	ID     string
	Group  string
	Limit  int
	Offset int
}

// PodPoolGetResponse -
type PodPoolGetResponse struct {
	Count    int    `json:"count"`
	PodPools []Pool `json:"pools"`
}
