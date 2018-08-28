/******************************************************************************
 * Copyright 2017 The Apollo Authors. All Rights Reserved.
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
 *****************************************************************************/

#include "modules/prediction/container/obstacles/obstacle_clusters.h"

#include <limits>

#include "modules/prediction/common/road_graph.h"
#include "modules/prediction/common/prediction_map.h"

namespace apollo {
namespace prediction {

using ::apollo::hdmap::LaneInfo;

std::unordered_map<std::string, LaneGraph> ObstacleClusters::lane_graphs_;
std::unordered_map<std::string,
                   std::vector<LaneObstacle>> ObstacleClusters::lane_obstacles_;

void ObstacleClusters::Clear() { lane_graphs_.clear(); }

void ObstacleClusters::Init() { Clear(); }

const LaneGraph& ObstacleClusters::GetLaneGraph(
    const double start_s, const double length,
    std::shared_ptr<const LaneInfo> lane_info_ptr) {
  std::string lane_id = lane_info_ptr->id().id();
  if (lane_graphs_.find(lane_id) != lane_graphs_.end()) {
    LaneGraph* lane_graph = &lane_graphs_[lane_id];
    for (int i = 0; i < lane_graph->lane_sequence_size(); ++i) {
      LaneSequence* lane_seq_ptr = lane_graph->mutable_lane_sequence(i);
      if (lane_seq_ptr->lane_segment_size() == 0) {
        continue;
      }
      LaneSegment* first_lane_seg_ptr = lane_seq_ptr->mutable_lane_segment(0);
      if (first_lane_seg_ptr->lane_id() != lane_id) {
        continue;
      }
      first_lane_seg_ptr->set_start_s(start_s);
    }
    return lane_graphs_[lane_id];
  }
  RoadGraph road_graph(start_s, length, lane_info_ptr);
  LaneGraph lane_graph;
  road_graph.BuildLaneGraph(&lane_graph);
  lane_graphs_[lane_id] = std::move(lane_graph);
  return lane_graphs_[lane_id];
}

bool ObstacleClusters::ForwardNearbyObstacle(
    const LaneSequence& lane_sequence,
    const int obstacle_id,
    const double obstacle_s,
    NearbyObstacle* const nearby_obstacle_ptr) {
  double accumulated_s = 0.0;
  for (const LaneSegment& lane_segment : lane_sequence.lane_segment()) {
    std::string lane_id = lane_segment.lane_id();
    double lane_length = lane_segment.total_length();
    if (lane_obstacles_.find(lane_id) == lane_obstacles_.end() ||
        lane_obstacles_[lane_id].empty()) {
      accumulated_s += lane_length;
      continue;
    }
    for (const LaneObstacle& lane_obstacle : lane_obstacles_[lane_id]) {
      if (lane_obstacle.obstacle_id() == obstacle_id) {
        continue;
      }
      double relative_s = accumulated_s + lane_obstacle.lane_s() - obstacle_s;
      if (relative_s > 0.0) {
        nearby_obstacle_ptr->set_id(lane_obstacle.obstacle_id());
        nearby_obstacle_ptr->set_s(relative_s);
        nearby_obstacle_ptr->set_l(lane_obstacle.lane_l());
        return true;
      }
    }
  }
  return false;
}

bool ObstacleClusters::BackwardNearbyObstacle(
    const LaneSequence& lane_sequence,
    const int obstacle_id,
    const double obstacle_s,
    NearbyObstacle* const nearby_obstacle_ptr) {
  // TODO(kechxu) implement
  if (lane_sequence.lane_segment_size() == 0) {
    AERROR << "Empty lane sequence found.";
    return false;
  }
  const LaneSegment& lane_segment = lane_sequence.lane_segment(0);
  std::string lane_id = lane_segment.lane_id();

  // Search current lane
  if (lane_obstacles_.find(lane_id) != lane_obstacles_.end() &&
      !lane_obstacles_[lane_id].empty()) {
    for (std::size_t i = lane_obstacles_[lane_id].size() - 1; i >= 0; --i) {
      const LaneObstacle& lane_obstacle = lane_obstacles_[lane_id][i];
      if (lane_obstacle.obstacle_id() == obstacle_id) {
        continue;
      }
      double relative_s = lane_obstacle.lane_s() - obstacle_s;
      if (relative_s < 0.0) {
        nearby_obstacle_ptr->set_id(lane_obstacle.obstacle_id());
        nearby_obstacle_ptr->set_s(relative_s);
        nearby_obstacle_ptr->set_l(lane_obstacle.lane_l());
        return true;
      }
    }
  }

  // Search backward lanes
  std::shared_ptr<const LaneInfo> lane_info_ptr =
      PredictionMap::LaneById(lane_id);
  bool found_one_behind = false;
  double relative_s = -std::numeric_limits<double>::infinity();
  for (const auto& predecessor_lane_id :
       lane_info_ptr->lane().predecessor_id()) {
    std::string lane_id = predecessor_lane_id.id();
    if (lane_obstacles_.find(lane_id) == lane_obstacles_.end() ||
        lane_obstacles_[lane_id].empty()) {
      continue;
    }
    std::shared_ptr<const LaneInfo> pred_lane_info_ptr =
      PredictionMap::LaneById(predecessor_lane_id.id());
    const LaneObstacle& backward_obs = lane_obstacles_[lane_id].back();
    double delta_s = backward_obs.lane_s() -
                     (obstacle_s + pred_lane_info_ptr->total_length());
    found_one_behind = true;
    if (delta_s > relative_s) {
      relative_s = delta_s;
      nearby_obstacle_ptr->set_id(backward_obs.obstacle_id());
      nearby_obstacle_ptr->set_s(relative_s);
      nearby_obstacle_ptr->set_l(backward_obs.lane_l());
    }
  }

  return found_one_behind;
}

}  // namespace prediction
}  // namespace apollo
