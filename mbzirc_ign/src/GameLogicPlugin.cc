/*
 * Copyright (C) 2022 Open Source Robotics Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
*/

#include <ignition/msgs/boolean.pb.h>
#include <ignition/msgs/float.pb.h>
#include <ignition/msgs/stringmsg_v.pb.h>
#include <ignition/plugin/Register.hh>

#include <chrono>
#include <mutex>

#include <ignition/math/AxisAlignedBox.hh>

#include <ignition/gazebo/components/Link.hh>
#include <ignition/gazebo/components/Model.hh>
#include <ignition/gazebo/components/Name.hh>
#include <ignition/gazebo/components/ParentEntity.hh>
#include <ignition/gazebo/components/Pose.hh>
#include <ignition/gazebo/components/Sensor.hh>
#include <ignition/gazebo/EntityComponentManager.hh>
#include <ignition/gazebo/Events.hh>
#include <ignition/gazebo/Util.hh>

#include <ignition/common/Console.hh>
#include <ignition/common/Util.hh>
#include <ignition/transport/Node.hh>

#include <sdf/sdf.hh>

#include "GameLogicPlugin.hh"

IGNITION_ADD_PLUGIN(
    mbzirc::GameLogicPlugin,
    ignition::gazebo::System,
    mbzirc::GameLogicPlugin::ISystemConfigure,
    mbzirc::GameLogicPlugin::ISystemPreUpdate,
    mbzirc::GameLogicPlugin::ISystemPostUpdate)

using namespace ignition;
using namespace gazebo;
using namespace systems;
using namespace mbzirc;

class mbzirc::GameLogicPluginPrivate
{
  /// \brief Target vessel, objects, and report status
  public: class Target
  {
    /// \brief Name of target vessel
    public: std::string vessel;

    /// \brief List of small target objects
    public: std::set<std::string> smallObjects;

    /// \brief List of large target objects
    public: std::set<std::string> largeObjects;

    /// \brief Indicates if vessel has been reported
    public: bool vesselReported = false;

    /// \brief Set of small objects that have been reported.
    public: std::set<std::string> smallObjectsReported;

    /// \brief Set of large objects that have been reported.
    public: std::set<std::string> largeObjectsReported;
  };

  public: enum PenaltyType
          {
            /// \brief Failure to ID target vessel
            TARGET_VESSEL_ID_1 = 0,
            TARGET_VESSEL_ID_2 = 1,
            TARGET_VESSEL_ID_3 = 2,

            /// \brief Failure to ID small object
            SMALL_OBJECT_ID_1 = 3,
            SMALL_OBJECT_ID_2 = 4,
            SMALL_OBJECT_ID_3 = 5,

            /// \brief Failure to ID large object
            LARGE_OBJECT_ID_1 = 6,
            LARGE_OBJECT_ID_2 = 7,
            LARGE_OBJECT_ID_3 = 8,

            /// \brief Failure to retrieve / place small object
            SMALL_OBJECT_RETRIEVE_1 = 9,
            SMALL_OBJECT_RETRIEVE_2 = 10,

            /// \brief Failure to retrieve / place large object
            LARGE_OBJECT_RETRIEVE_1 = 11,
            LARGE_OBJECT_RETRIEVE_2 = 12,

            /// \brief Failure to remain in demonstration area boundary
            BOUNDARY_1 = 13,
            BOUNDARY_2 = 14,
          };

  /// \brief A map of penalty type and time penalties.
  public: const std::map<PenaltyType, int> kTimePenalties = {
          {TARGET_VESSEL_ID_1, 180},
          {TARGET_VESSEL_ID_2, 240},
          {TARGET_VESSEL_ID_3, IGN_INT32_MAX},
          {SMALL_OBJECT_ID_1, 180},
          {SMALL_OBJECT_ID_2, 240},
          {SMALL_OBJECT_ID_3, IGN_INT32_MAX},
          {LARGE_OBJECT_ID_1, 180},
          {LARGE_OBJECT_ID_2, 240},
          {LARGE_OBJECT_ID_3, IGN_INT32_MAX},
          {SMALL_OBJECT_RETRIEVE_1, 120},
          {SMALL_OBJECT_RETRIEVE_2, IGN_INT32_MAX},
          {LARGE_OBJECT_RETRIEVE_1, 120},
          {LARGE_OBJECT_RETRIEVE_2, IGN_INT32_MAX},
          {BOUNDARY_1, 300},
          {BOUNDARY_2, IGN_INT32_MAX}};

  /// \brief Write a simulation timestamp to a logfile.
  /// \param[in] _simTime Current sim time.
  /// \return A file stream that can be used to write additional
  /// information to the logfile.
  public: std::ofstream &Log(const ignition::msgs::Time &_simTime);

  /// \brief Publish the score.
  /// \param[in] _event Unused.
  public: void PublishScore();

  /// \brief Finish game and generate log files
  /// \param[in] _simTime Simulation time.
  public: void Finish(const ignition::msgs::Time &_simTime);

  /// \brief Update the score.yml and summary.yml files. This function also
  /// returns the time point used to calculate the elapsed real time. By
  /// returning this time point, we can make sure that the ::Finish function
  /// uses the same time point.
  /// \param[in] _simTime Current sim time.
  /// \return The time point used to calculate the elapsed real time.
  public: std::chrono::steady_clock::time_point UpdateScoreFiles(
              const ignition::msgs::Time &_simTime);

  /// \brief Log an event to the eventStream.
  /// \param[in] _event The event to log.
  /// \param[in] _data Additional data for the event. Optional
  public: void LogEvent(const std::string &_event,
      const std::string &_data = "");

  /// \brief Battery subscription callback.
  /// \param[in] _msg Battery message.
  /// \param[in] _info Message information.
  public: void OnBatteryMsg(const ignition::msgs::BatteryState &_msg,
    const transport::MessageInfo &_info);

  /// \brief Ignition service callback triggered when the service is called.
  /// \param[in] _req The message containing a flag telling if the game
  /// is to start.
  /// \param[out] _res The response message.
  public: bool OnStartCall(const ignition::msgs::Boolean &_req,
                            ignition::msgs::Boolean &_res);

  /// \brief Helper function to start the competition.
  /// \param[in] _simTime Simulation time.
  /// \return True if the run was started.
  public: bool Start(const ignition::msgs::Time &_simTime);

  /// \brief Ignition service callback triggered when the service is called.
  /// \param[in] _req The message containing a flag telling if the game is to
  /// be finished.
  /// \param[out] _res The response message.
  public: bool OnFinishCall(const ignition::msgs::Boolean &_req,
               ignition::msgs::Boolean &_res);

  public: bool OnReportTargets(const ignition::msgs::StringMsg_V &_req,
               ignition::msgs::Boolean &_res);

  /// \brief Check if an entity's position is within the input boundary
  //// \param[in] _ecm Entity component manager
  //// \param[in] _entity Entity id
  //// \param[in] _boundary Axis aligned bounding box
  public: bool CheckEntityInBoundary(const EntityComponentManager &_ecm,
                                     Entity _entity,
                                     const math::AxisAlignedBox &_boundary);

  /// \brief Valid target reports, add time penalties, and update score
  public: void ValidateTargetReports();

  /// \brief Ignition Transport node.
  public: transport::Node node;

  /// \brief Current simulation time.
  public: ignition::msgs::Time simTime;

  /// \brief The simulation time of the start call.
  public: ignition::msgs::Time startSimTime;

  /// \brief Number of simulation seconds allowed.
  public: std::chrono::seconds runDuration{3600};

  /// \brief Thread on which scores are published
  public: std::unique_ptr<std::thread> scoreThread = nullptr;

  /// \brief Thread on which scores are published
  /// \brief Whether the task has started.
  public: bool started = false;

  /// \brief Whether the task has finished.
  public: bool finished = false;

  /// \brief Start time used for scoring.
  public: std::chrono::steady_clock::time_point startTime;

  /// \brief Mutex to protect log stream.
  public: std::mutex logMutex;

  /// \brief Log file output stream.
  public: std::ofstream logStream;

  /// \brief Event file output stream.
  public: std::ofstream eventStream;

  /// \brief Mutex to protect total score.
  public: std::mutex scoreMutex;

  /// \brief Mutex to protect reports
  public: std::mutex reportMutex;

  /// \brief Target reports
  public: std::vector<ignition::msgs::StringMsg_V> reports;

  /// \brief Ignition transport competition clock publisher.
  public: transport::Node::Publisher competitionClockPub;

  /// \brief Logpath.
  public: std::string logPath{"/dev/null"};

  /// \brief Names of the spawned robots.
  public: std::map<Entity, std::string> robotNames;

  /// \brief Initial pose of robots
  public: std::map<Entity, math::Vector3d> robotInitialPos;

 /// \brief Current state.
  public: std::string state = "init";

  /// \brief Time at which the last status publication took place.
  public: std::chrono::steady_clock::time_point lastStatusPubTime;

  /// \brief Time at which the summary.yaml file was last updated.
  public: mutable std::chrono::steady_clock::time_point lastUpdateScoresTime;

  /// \brief Event manager for pausing simulation
  public: EventManager *eventManager{nullptr};

  /// \brief Models with dead batteries.
  public: std::set<std::string> deadBatteries;

  /// \brief Amount of allowed setup time in seconds.
  public: int setupTimeSec = 600;

  /// \brief Mutex to protect the eventCounter.
  public: std::mutex eventCounterMutex;

  /// \brief Counter to create unique id for events
  public: int eventCounter = 0;

  /// \brief Total score.
  public: double totalScore = IGN_DBL_INF;

  /// \brief boundary of competition area
  public: math::AxisAlignedBox geofenceBoundary;

  /// \brief Inner boundary of competition area
  /// This is geofenceBoundary - geofenceBoundaryBuffer
  public: math::AxisAlignedBox geofenceBoundaryInner;

  /// \brief Inner boundary of competition area
  public: double geofenceBoundaryBuffer = 5.0;

  /// \brief Map of robot entity id and bool var to indicate if they are inside
  ///  the competition boundary
  public: std::map<Entity, bool> robotsInBoundary;

  /// \brief Number of times robot moved beyond competition boundary
  public: unsigned int geofenceBoundaryPenaltyCount = 0u;

  /// \brief Number of times vessel is incorrectly identified
  public: unsigned int vesselPenaltyCount = 0u;

  /// \brief Map of vessel to number of times small object is incorrectly
  /// identified
  public: std::map<std::string, unsigned int> smallObjectPenaltyCount;

  /// \brief Map of vessel to number times large object is incorrectly
  /// identified
  public: std::map<std::string, unsigned int> largeObjectPenaltyCount;

  /// \brief Total time penalty in seconds;
  public: int timePenalty = 0;

  /// \brief A map of target vessel name and targets.
  public: std::map<std::string, Target> targets;
};

//////////////////////////////////////////////////
GameLogicPlugin::GameLogicPlugin()
  : dataPtr(new GameLogicPluginPrivate)
{
}

//////////////////////////////////////////////////
GameLogicPlugin::~GameLogicPlugin()
{
  // set eventManager to nullptr so that Finish call does not attempt to
  // pause sim
  this->dataPtr->eventManager = nullptr;
  this->dataPtr->Finish(this->dataPtr->simTime);

  if (this->dataPtr->scoreThread)
    this->dataPtr->scoreThread->join();
}

//////////////////////////////////////////////////
void GameLogicPlugin::Configure(const ignition::gazebo::Entity & /*_entity*/,
                           const std::shared_ptr<const sdf::Element> &_sdf,
                           ignition::gazebo::EntityComponentManager & /*_ecm*/,
                           ignition::gazebo::EventManager & _eventMgr)
{
  this->dataPtr->eventManager = &_eventMgr;

  // Check if the game logic plugin has a <logging> element.
  // example:
  // <logging>
  //   <path>/tmp</path>
  // </logging>
  auto sdf = const_cast<sdf::Element *>(_sdf.get());
  const sdf::ElementPtr loggingElem = sdf->GetElement("logging");

  if (loggingElem)
  {
    // Get the logpath from the <path> element, if it exists.
    if (loggingElem->HasElement("path"))
    {
      this->dataPtr->logPath =
        loggingElem->Get<std::string>("path", "/dev/null").first;
    }
    else
    {
      // Make sure that we can access the HOME environment variable.
      char *homePath = getenv("HOME");
      if (!homePath)
      {
        ignerr << "Unable to get HOME environment variable. Report this error "
          << "to https://github.com/osrf/mbzirc/issues/new. "
          << "MBZIRC logging will be disabled.\n";
      }
      else
      {
        this->dataPtr->logPath = homePath;
      }
    }
  }

  ignmsg << "MBZIRC log path: " << this->dataPtr->logPath << std::endl;
  common::removeAll(this->dataPtr->logPath);
  common::createDirectories(this->dataPtr->logPath);

  // Open the log file.
  std::string filenamePrefix = "mbzirc";
  this->dataPtr->logStream.open(
      (common::joinPaths(this->dataPtr->logPath, filenamePrefix + "_" +
      ignition::common::systemTimeISO() + ".log")).c_str(), std::ios::out);

  // Open the event log file.
  this->dataPtr->eventStream.open(
      (common::joinPaths(this->dataPtr->logPath, "events.yml")).c_str(),
      std::ios::out);

  // Get the run duration seconds.
  if (_sdf->HasElement("run_duration_seconds"))
  {
    this->dataPtr->runDuration = std::chrono::seconds(
        _sdf->Get<int>("run_duration_seconds"));

    ignmsg << "Run duration set to " << this->dataPtr->runDuration.count()
           << " seconds.\n";
  }

  // Get competition geofence boundary.
  if (_sdf->HasElement("geofence"))
  {
    auto boundsElem = sdf->GetElement("geofence");

    if (boundsElem->HasElement("center") && boundsElem->HasElement("size"))
    {
      auto center = boundsElem->GetElement("center")->Get<math::Vector3d>();
      auto size = boundsElem->GetElement("size")->Get<math::Vector3d>();
      math::Vector3d max = center + size * 0.5;
      math::Vector3d min = center - size * 0.5;
      this->dataPtr->geofenceBoundary = math::AxisAlignedBox(min, max);
      this->dataPtr->geofenceBoundaryInner =
          math::AxisAlignedBox(min + this->dataPtr->geofenceBoundaryBuffer,
                               max - this->dataPtr->geofenceBoundaryBuffer);
      ignmsg << "Geofence boundary min: " << min << ", max: " << max << std::endl;
    }
    else
    {
      ignerr << "<geofence> is missing <center> and <size> SDF elements."
             << std::endl;
    }
  }

  // Get the setup duration seconds.
  if (_sdf->HasElement("setup_duration_seconds"))
  {
    this->dataPtr->setupTimeSec = std::chrono::seconds(
        _sdf->Get<int>("setup_duration_seconds")).count();

    ignmsg << "Setup duration set to " << this->dataPtr->setupTimeSec
           << " seconds.\n";
  }

  // Get target vessels and objects
  if (_sdf->HasElement("target"))
  {
    auto targetElem = sdf->GetElement("target");
    while (targetElem)
    {
      if (targetElem->HasElement("vessel"))
      {
        GameLogicPluginPrivate::Target target;

        // get target vessel name
        auto vesselTargetElem = targetElem->GetElement("vessel");
        std::string vessel = vesselTargetElem->Get<std::string>();
        target.vessel = vessel;
        ignmsg << "Target vessel: " << vessel << std::endl;;

        // get a list of small target objects on this vessel
        if (targetElem->HasElement("small_object"))
        {
          auto smallObjTargetElem = targetElem->GetElement("small_object");
          while (smallObjTargetElem)
          {
            std::string smallObj = smallObjTargetElem->Get<std::string>();
            target.smallObjects.insert(smallObj);
            smallObjTargetElem = smallObjTargetElem->GetNextElement("small_object");
            ignmsg << "  Small object: " << smallObj << std::endl;;
          }
        }

        // get a list of large target objects on this vessel
        if (targetElem->HasElement("large_object"))
        {
          auto largeObjTargetElem = targetElem->GetElement("large_object");
          while (largeObjTargetElem)
          {
            std::string largeObj = largeObjTargetElem->Get<std::string>();
            target.largeObjects.insert(largeObj);
            largeObjTargetElem = largeObjTargetElem->GetNextElement("large_object");
            ignmsg << "  Large object: " << largeObj << std::endl;;
          }
        }

        this->dataPtr->targets[vessel] = target;

      }
      else
      {
        ignerr << "<target> element must have a <vessel> child element" << std::endl;
      }

      targetElem = targetElem->GetNextElement("target");
    }
  }

  this->dataPtr->node.Advertise("/mbzirc/start",
      &GameLogicPluginPrivate::OnStartCall, this->dataPtr.get());

  this->dataPtr->node.Advertise("/mbzirc/finish",
      &GameLogicPluginPrivate::OnFinishCall, this->dataPtr.get());

  this->dataPtr->node.Advertise("/mbzirc/report/targets",
      &GameLogicPluginPrivate::OnReportTargets, this->dataPtr.get());

  this->dataPtr->scoreThread.reset(new std::thread(
        &GameLogicPluginPrivate::PublishScore, this->dataPtr.get()));

  this->dataPtr->competitionClockPub =
    this->dataPtr->node.Advertise<ignition::msgs::Clock>("/mbzirc/run_clock");


  ignmsg << "Starting MBZIRC" << std::endl;

  // Make sure that there are score files.
  this->dataPtr->UpdateScoreFiles(this->dataPtr->simTime);
}

//////////////////////////////////////////////////
void GameLogicPluginPrivate::OnBatteryMsg(
    const ignition::msgs::BatteryState &_msg,
    const transport::MessageInfo &_info)
{
  ignition::msgs::Time localSimTime(this->simTime);
  if (_msg.percentage() <= 0)
  {
    std::vector<std::string> topicParts = common::split(_info.Topic(), "/");
    std::string name = "_unknown_";

    // Get the name of the model from the topic name, where the topic name
    // look like '/model/{model_name}/detach'.
    if (topicParts.size() > 1)
      name = topicParts[1];

    // Make sure the event is logged once.
    if (this->deadBatteries.find(name) == this->deadBatteries.end())
    {
      this->deadBatteries.emplace(name);
      {
        this->LogEvent("dead_battery", name);
      }
    }
  }
}

//////////////////////////////////////////////////
void GameLogicPlugin::PreUpdate(const UpdateInfo &_info,
    EntityComponentManager &_ecm)
{
}

//////////////////////////////////////////////////
void GameLogicPlugin::PostUpdate(
    const ignition::gazebo::UpdateInfo &_info,
    const ignition::gazebo::EntityComponentManager &_ecm)
{
  // Store sim time
  int64_t s, ns;
  std::tie(s, ns) = ignition::math::durationToSecNsec(_info.simTime);
  this->dataPtr->simTime.set_sec(s);
  this->dataPtr->simTime.set_nsec(ns);

  // Capture the names of the robots. We only do this until the team
  // triggers the start signal.
  if (!this->dataPtr->started)
  {
    _ecm.Each<gazebo::components::Sensor,
              gazebo::components::ParentEntity>(
        [&](const gazebo::Entity &,
            const gazebo::components::Sensor *,
            const gazebo::components::ParentEntity *_parent) -> bool
        {
          // Get the model. We are assuming that a sensor is attached to
          // a link.
          auto parent = _ecm.Component<gazebo::components::ParentEntity>(
              _parent->Data());
          auto model = parent;

          // find top level model
          while (parent && _ecm.Component<gazebo::components::Model>(
                 parent->Data()))
          {
            model = parent;
            parent = _ecm.Component<gazebo::components::ParentEntity>(
                parent->Data());
          }

          if (model)
          {
            // \todo(anyone)
            // Check if the robot has beyond the staging area.
            // we need to trigger the /mbzirc/start.

            // Get the model name
            Entity entity = model->Data();
            auto mName =
              _ecm.Component<gazebo::components::Name>(entity);
            if (this->dataPtr->robotNames.find(entity) ==
                this->dataPtr->robotNames.end())
            {
              this->dataPtr->robotNames[entity] = mName->Data();
              this->dataPtr->robotInitialPos[entity] =
                  _ecm.Component<components::Pose>(entity)->Data().Pos();

              // Subscribe to battery state in order to log battery events.
              std::string batteryTopic = std::string("/model/") +
                mName->Data() + "/battery/linear_battery/state";
              this->dataPtr->node.Subscribe(batteryTopic,
                  &GameLogicPluginPrivate::OnBatteryMsg, this->dataPtr.get());
            }
          }
          return true;
        });

    // Start automatically if setup time has elapsed.
    if (this->dataPtr->simTime.sec() >= this->dataPtr->setupTimeSec)
    {
      this->dataPtr->Start(this->dataPtr->simTime);
    }
    else
    {
      // Start automatically if a robot moves for more than x meters
      // from initial position
      for (const auto &it : this->dataPtr->robotInitialPos)
      {
        Entity robotEnt = it.first;
        math::Vector3d robotInitPos = it.second;

        auto poseComp = _ecm.Component<components::Pose>(robotEnt);
        if (poseComp)
        {
          double distance = poseComp->Data().Pos().Distance(robotInitPos);
          if (distance > 5.0)
          {
            this->dataPtr->Log(this->dataPtr->simTime)
                << "Robot moved outside of start gate." << std::endl;
            this->dataPtr->Start(this->dataPtr->simTime);
          }
        }
      }
    }
  }

  // check if robots are inside the competition boundary
  for (const auto &it : this->dataPtr->robotNames)
  {
    Entity robotEnt = it.first;
    std::string robotName = it.second;
    bool wasInBounds = true;
    auto bIt = this->dataPtr->robotsInBoundary.find(robotEnt);
    if (bIt == this->dataPtr->robotsInBoundary.end())
      this->dataPtr->robotsInBoundary[robotEnt] = wasInBounds;
    else
      wasInBounds = bIt->second;

    // If robot was outside of boundary, allow some buffer distance before
    // counting it as returning to the inside boundary.
    // This is so that we don't immediately trigger another exceed_boundary
    // event if the robot oscillates slightly at the boundary line,
    // e.g. USV motion due to waves
    auto boundary = wasInBounds ? this->dataPtr->geofenceBoundary :
        this->dataPtr->geofenceBoundaryInner;

    bool isInBounds = this->dataPtr->CheckEntityInBoundary(_ecm,
        robotEnt, boundary);

    if (isInBounds != wasInBounds)
    {
      // robot moved outside the boundary!
      if (!isInBounds)
      {
        this->dataPtr->geofenceBoundaryPenaltyCount++;
        if (this->dataPtr->geofenceBoundaryPenaltyCount == 1)
        {
          this->dataPtr->timePenalty +=
              this->dataPtr->kTimePenalties.at(
              GameLogicPluginPrivate::BOUNDARY_1);
          this->dataPtr->LogEvent("exceed_boundary_1", robotName);
          this->dataPtr->UpdateScoreFiles(this->dataPtr->simTime);
        }
        else if (this->dataPtr->geofenceBoundaryPenaltyCount >= 2)
        {
          this->dataPtr->timePenalty =
              this->dataPtr->kTimePenalties.at(
              GameLogicPluginPrivate::BOUNDARY_2);
          this->dataPtr->LogEvent("exceed_boundary_2", robotName);
          // terminate run
          this->dataPtr->Finish(this->dataPtr->simTime);
        }
      }
      this->dataPtr->robotsInBoundary[robotEnt] = isInBounds;
    }
  }

  // validate target reports
  this->dataPtr->ValidateTargetReports();

  // Get the start sim time in nanoseconds.
  auto startSimTime = std::chrono::nanoseconds(
      this->dataPtr->startSimTime.sec() * 1000000000 +
      this->dataPtr->startSimTime.nsec());

  // Compute the elapsed competition time.
  auto elapsedCompetitionTime = this->dataPtr->started ?
    _info.simTime - startSimTime : std::chrono::seconds(0);

  // Compute the remaining competition time.
  auto remainingCompetitionTime = this->dataPtr->started &&
    this->dataPtr->runDuration != std::chrono::seconds(0) ?
    this->dataPtr->runDuration - elapsedCompetitionTime :
    std::chrono::seconds(0) ;

  // Check if the allowed time has elapsed. If so, then mark as finished.
  if ((this->dataPtr->started && !this->dataPtr->finished) &&
      this->dataPtr->runDuration != std::chrono::seconds(0) &&
      remainingCompetitionTime <= std::chrono::seconds(0))
  {
    ignmsg << "Time limit[" <<  this->dataPtr->runDuration.count()
      << "s] reached.\n";
    this->dataPtr->Finish(this->dataPtr->simTime);
  }

  auto currentTime = std::chrono::steady_clock::now();
  if (currentTime - this->dataPtr->lastStatusPubTime > std::chrono::seconds(1))
  {
    ignition::msgs::Clock competitionClockMsg;
    ignition::msgs::Header::Map *mapData =
      competitionClockMsg.mutable_header()->add_data();
    mapData->set_key("phase");
    if (this->dataPtr->started)
    {
      mapData->add_value(this->dataPtr->finished ? "finished" : "run");
      auto secondsRemaining = std::chrono::duration_cast<std::chrono::seconds>(
          remainingCompetitionTime);
      competitionClockMsg.mutable_sim()->set_sec(secondsRemaining.count());
    }
    else if (!this->dataPtr->finished)
    {
      mapData->add_value("setup");
      competitionClockMsg.mutable_sim()->set_sec(
          this->dataPtr->setupTimeSec - this->dataPtr->simTime.sec());
    }
    else
    {
      // It's possible for a team to call Finish before starting.
      mapData->add_value("finished");
    }

    this->dataPtr->competitionClockPub.Publish(competitionClockMsg);

    this->dataPtr->lastStatusPubTime = currentTime;
  }

  // Periodically update the score file.
  if (!this->dataPtr->finished && currentTime -
      this->dataPtr->lastUpdateScoresTime > std::chrono::seconds(1))
  {
    this->dataPtr->UpdateScoreFiles(this->dataPtr->simTime);
  }
}

/////////////////////////////////////////////////
bool GameLogicPluginPrivate::CheckEntityInBoundary(
    const EntityComponentManager &_ecm, Entity _entity,
    const math::AxisAlignedBox &_boundary)
{
  auto poseComp = _ecm.Component<components::Pose>(_entity);
  if (!poseComp)
  {
    ignerr << "Pose component not found for Entity: " << _entity
           << std::endl;
    return false;
  }

  // this should be world pose since it is a top level model
  math::Pose3d pose = poseComp->Data();
  return _boundary.Contains(pose.Pos());
}


/////////////////////////////////////////////////
void GameLogicPluginPrivate::PublishScore()
{
  transport::Node::Publisher scorePub =
    this->node.Advertise<ignition::msgs::Float>("/mbzirc/score");
  ignition::msgs::Float msg;

  while (!this->finished)
  {
    {
      std::lock_guard<std::mutex> lock(this->scoreMutex);
      msg.set_data(this->totalScore);
    }

    scorePub.Publish(msg);
    IGN_SLEEP_S(1);
  }
}

/////////////////////////////////////////////////
bool GameLogicPluginPrivate::OnFinishCall(const ignition::msgs::Boolean &_req,
  ignition::msgs::Boolean &_res)
{
  ignition::msgs::Time localSimTime(this->simTime);
  if (this->started && _req.data() && !this->finished)
  {
    ignmsg << "User triggered OnFinishCall." << std::endl;
    this->Log(localSimTime) << "User triggered OnFinishCall." << std::endl;
    this->logStream.flush();

    this->Finish(localSimTime);
    _res.set_data(true);
  }
  else
    _res.set_data(false);

  return true;
}

/////////////////////////////////////////////////
bool GameLogicPluginPrivate::OnStartCall(const ignition::msgs::Boolean &_req,
  ignition::msgs::Boolean &_res)
{
  ignition::msgs::Time localSimTime(this->simTime);
  if (_req.data())
    _res.set_data(this->Start(localSimTime));
  else
    _res.set_data(false);

  return true;
}

/////////////////////////////////////////////////
bool GameLogicPluginPrivate::Start(const ignition::msgs::Time &_simTime)
{
  bool result = false;

  if (!this->started && !this->finished)
  {
    result = true;
    this->started = true;
    this->startTime = std::chrono::steady_clock::now();
    this->startSimTime = _simTime;
    ignmsg << "Scoring has Started" << std::endl;
    this->Log(_simTime) << "scoring_started" << std::endl;
    this->LogEvent("started");
  }

  // Update files when scoring has started.
  this->UpdateScoreFiles(_simTime);

  return result;
}

/////////////////////////////////////////////////
void GameLogicPluginPrivate::Finish(const ignition::msgs::Time &_simTime)
{
  // Pause simulation when finished. Always send this request, just to be
  // safe.
  if (this->eventManager)
    this->eventManager->Emit<events::Pause>(true);

  if (this->finished)
    return;

  // Elapsed time
  int realElapsed = 0;
  int simElapsed = 0;

  // Update the score.yml and summary.yml files. This function also
  // returns the time point used to calculate the elapsed real time. By
  // returning this time point, we can make sure that this function (the
  // ::Finish function) uses the same time point.
  std::chrono::steady_clock::time_point currTime =
    this->UpdateScoreFiles(_simTime);

  if (this->started)
  {

    double score = 0.0;
    {
      std::lock_guard<std::mutex> lock(this->scoreMutex);
      score = this->totalScore;
    }

    realElapsed = std::chrono::duration_cast<std::chrono::seconds>(
        currTime - this->startTime).count();

    simElapsed = _simTime.sec() - this->startSimTime.sec();

    ignmsg << "Scoring has finished. Elapsed real time: "
          << realElapsed << " seconds. Elapsed sim time: "
          << simElapsed << " seconds. " << std::endl;

    this->Log(_simTime) << "finished_elapsed_real_time " << realElapsed
      << " s." << std::endl;
    this->Log(_simTime) << "finished_elapsed_sim_time " << simElapsed
      << " s." << std::endl;
    this->Log(_simTime) << "finished_score " << score << std::endl;
    this->Log(_simTime) << "time_penalty " << this->timePenalty << std::endl;
    this->logStream.flush();

    this->LogEvent("finished");
  }

  this->finished = true;
}

/////////////////////////////////////////////////
bool GameLogicPluginPrivate::OnReportTargets(
    const ignition::msgs::StringMsg_V &_req,
    ignition::msgs::Boolean &_res)
{
  std::lock_guard<std::mutex> lock(this->reportMutex);
  this->reports.push_back(_req);
  _res.set_data(true);
  return true;
}

/////////////////////////////////////////////////
void GameLogicPluginPrivate::ValidateTargetReports()
{
  std::lock_guard<std::mutex> lock(this->reportMutex);
  for (auto &req : this->reports)
  {
    if (req.data_size() <= 0)
    {
      this->LogEvent("target_reported", "empty_report");
      continue;
    }

    std::string vessel = req.data(0);
    if (vessel.empty())
    {
      this->LogEvent("target_reported", "target_vessel_missing");
      continue;
    }

    auto it = this->targets.find(vessel);
    if (it != this->targets.end())
    {
      auto target = it->second;

      // target has not been report - valid report
      if (!target.vesselReported)
      {
        target.vesselReported = true;
        this->targets[vessel] = target;
        this->LogEvent("target_reported", "vessel_id_success");
        continue;
      }
      // target has already been reported
      // team is reporting objects
      else if (req.data_size() > 1)
      {
        // small object target
        std::string smallObj = req.data(1);
        if (!smallObj.empty())
        {
          auto sIt = target.smallObjects.find(smallObj);
          bool validObj = sIt != target.smallObjects.end();
          if (validObj)
          {
            auto reportIt = target.smallObjectsReported.find(smallObj);
            if (reportIt == target.smallObjectsReported.end())
            {
              target.smallObjectsReported.insert(smallObj);
              this->targets[vessel] = target;
              this->LogEvent("target_reported", "small_object_id_success");
              continue;
            }
            else
            {
              this->LogEvent("target_reported", "small_object_id_duplicate");
            }
          }
          else
          {
            // add penalty for incorrectly identifying small object
            std::string logData = "small_object_id_failure";
            unsigned int count = 0u;
            auto it = this->smallObjectPenaltyCount.find(vessel);
            if (it != this->smallObjectPenaltyCount.end())
              count = it->second;

            this->smallObjectPenaltyCount[vessel] = ++count;
            if (count == 1u)
            {
              this->timePenalty +=
                  this->kTimePenalties.at(LARGE_OBJECT_ID_1);
              this->UpdateScoreFiles(this->simTime);
              logData += "_1";
              this->LogEvent("target_reported", logData);
            }
            else if (count == 2u)
            {
              this->timePenalty +=
                  this->kTimePenalties.at(LARGE_OBJECT_ID_2);
              this->UpdateScoreFiles(this->simTime);
              logData += "_2";
              this->LogEvent("target_reported", logData);
            }
            else
            {
              this->timePenalty +=
                  this->kTimePenalties.at(LARGE_OBJECT_ID_3);
              logData += "_3";
              this->LogEvent("target_reported", logData);
              // terminate run
              this->Finish(this->simTime);
              break;
            }
          }
        }
        else if (req.data_size() > 2)
        {
          // large object target
          std::string largeObj = req.data(2);
          if (!largeObj.empty())
          {
            auto sIt = target.largeObjects.find(largeObj);
            bool validObj = sIt != target.largeObjects.end();
            if (validObj)
            {
              auto reportIt = target.largeObjectsReported.find(largeObj);
              if (reportIt == target.largeObjectsReported.end())
              {
                target.largeObjectsReported.insert(largeObj);
                this->targets[vessel] = target;
                this->LogEvent("target_reported", "large_object_id_success");
                continue;
              }
              else
              {
                this->LogEvent("target_reported", "large_object_id_duplicate");
              }
            }
            else
            {
              // add penalty for incorrectly identifying large object
              std::string logData = "large_object_id_failure";
              unsigned int count = 0u;
              auto it = this->largeObjectPenaltyCount.find(vessel);
              if (it != this->largeObjectPenaltyCount.end())
                count = it->second;

              this->largeObjectPenaltyCount[vessel] = ++count;
              if (count == 1u)
              {
                this->timePenalty +=
                    this->kTimePenalties.at(LARGE_OBJECT_ID_1);
                this->UpdateScoreFiles(this->simTime);
                this->LogEvent("target_reported", logData + "_1");
              }
              else if (count == 2u)
              {
                this->timePenalty +=
                    this->kTimePenalties.at(LARGE_OBJECT_ID_2);
                this->UpdateScoreFiles(this->simTime);
                this->LogEvent("target_reported", logData + "_2");
              }
              else
              {
                this->timePenalty +=
                    this->kTimePenalties.at(LARGE_OBJECT_ID_3);
                this->LogEvent("target_reported", logData + "_3");
                // terminate run
                this->Finish(this->simTime);
                break;
              }
            }
          }
        }
      }
      else
      {
        this->LogEvent("target_reported", "vessel_id_duplicate");
      }
    }
    else
    {
      // add penalty for incorrectly identifying vessel
      std::string logData = "vessel_id_failure";
      this->vesselPenaltyCount++;
      if (this->vesselPenaltyCount == 1u)
      {
        this->timePenalty +=
            this->kTimePenalties.at(TARGET_VESSEL_ID_1);
        this->UpdateScoreFiles(this->simTime);
        this->LogEvent("target_reported", logData + "_1");
      }
      else if (this->vesselPenaltyCount == 2u)
      {
        this->timePenalty +=
            this->kTimePenalties.at(TARGET_VESSEL_ID_2);
        this->UpdateScoreFiles(this->simTime);
        this->LogEvent("target_reported", logData + "_2");
      }
      else
      {
        this->timePenalty +=
            this->kTimePenalties.at(TARGET_VESSEL_ID_3);
        this->LogEvent("target_reported", logData + "_3");
        // terminate run
        this->Finish(this->simTime);
        break;
      }
    }
  }
  this->reports.clear();
}

/////////////////////////////////////////////////
std::chrono::steady_clock::time_point GameLogicPluginPrivate::UpdateScoreFiles(
    const ignition::msgs::Time &_simTime)
{
  std::lock_guard<std::mutex> lock(this->logMutex);

  // Elapsed time
  int realElapsed = 0;
  int simElapsed = 0;
  std::chrono::steady_clock::time_point currTime =
    std::chrono::steady_clock::now();

  if (this->started)
  {
    simElapsed = _simTime.sec() - this->startSimTime.sec();
    realElapsed = std::chrono::duration_cast<std::chrono::seconds>(
        currTime - this->startTime).count();
  }

  // Output a run summary
  std::ofstream summary(this->logPath + "/summary.yml", std::ios::out);

  summary << "was_started: " << this->started << std::endl;
  summary << "sim_time_duration_sec: " << simElapsed << std::endl;
  summary << "real_time_duration_sec: " << realElapsed << std::endl;
  summary << "model_count: " << this->robotNames.size() << std::endl;
  summary << "time_penalty: " << this->timePenalty << std::endl;
  summary.flush();

  // Output a score file with just the final score
  std::ofstream scoreFile(this->logPath + "/score.yml", std::ios::out);
  {
    std::lock_guard<std::mutex> lock(this->scoreMutex);
    this->totalScore = (math::equal(this->timePenalty, IGN_INT32_MAX)) ?
        IGN_INT32_MAX : simElapsed + this->timePenalty;

    scoreFile << this->totalScore << std::endl;
  }
  scoreFile.flush();

  this->lastUpdateScoresTime = currTime;
  return currTime;
}

/////////////////////////////////////////////////
void GameLogicPluginPrivate::LogEvent(const std::string &_type,
    const std::string &_data)
{
  // Elapsed time
  int realElapsed = 0;
  int simElapsed = 0;
  std::chrono::steady_clock::time_point currTime =
    std::chrono::steady_clock::now();
  if (this->started)
  {
    realElapsed = std::chrono::duration_cast<std::chrono::seconds>(
        currTime - this->startTime).count();
    simElapsed = this->simTime.sec() - this->startSimTime.sec();
  }

  std::lock_guard<std::mutex> lock(this->eventCounterMutex);
  std::lock_guard<std::mutex> lock2(this->scoreMutex);
  std::ostringstream stream;
  stream
    << "- event:\n"
    << "  id: " << this->eventCounter << "\n"
    << "  type: " << _type << "\n"
    << "  time_sec: " << this->simTime.sec() << "\n"
    << "  elapsed_real_time: " << realElapsed << "\n"
    << "  elapsed_sim_time: " << simElapsed << "\n"
    << "  total_score: " << this->totalScore << std::endl;
  if (!_data.empty())
    stream << "  data: " << _data << std::endl;

  this->eventStream << stream.str();
  this->eventStream.flush();
  this->eventCounter++;

  this->Log(this->simTime) << "Logged Event:\n" << stream.str() << std::endl;
}

/////////////////////////////////////////////////
std::ofstream &GameLogicPluginPrivate::Log(
    const ignition::msgs::Time &_simTime)
{
  this->logStream << _simTime.sec()
                  << " " << _simTime.nsec() << " ";
  return this->logStream;
}
