#ifndef CANOPEN_H
#define CANOPEN_H

#include <iostream>
#include <map>
#include <vector>
#include <string>
#include <chrono>
#include <unordered_map>
#include <functional>
#include <cstdlib>
#include <thread>
#include <math.h>
#include <libpcan.h>
#include <utility>
#include <fcntl.h>    // for O_RDWR
#include <stdint.h>
#include <inttypes.h>

namespace canopen {

  /***************************************************************/
  //		Definitions
  /***************************************************************/

  extern std::chrono::milliseconds syncInterval;

  class Device {
  public:
    Device() {};
  Device(uint8_t CANid) : CANid_(CANid), desiredVel_(0), actualVel_(0),
      actualPos_(0), desiredPos_(0), initialized(false), NMTState_("initialisation"), motorState_("NOT_READY_TO SWITCH_ON") {};
  Device(uint8_t CANid, std::string name, std::string group, std::string bus) : 
    CANid_(CANid), name_(name), group_(group), deviceFile_(bus), 
      desiredVel_(0), actualVel_(0),
      actualPos_(0), desiredPos_(0), initialized(false) {};

    std::string motorState_;
    std::string NMTState_;
    

    inline void setVel(double vel) { desiredVel_ = vel; }
    inline void updateDesiredPos() { 
      desiredPos_ += desiredVel_ * (syncInterval.count() / 1000.0); }

    uint8_t CANid_;
    // uint16_t motorState_;
    std::string deviceFile_;
    std::string group_;
    std::string name_;
    bool initialized;
    bool voltage_enabled_;
    bool driveReferenced_;
    double actualPos_; // unit = rad
    double desiredPos_; // unit = rad
    double actualVel_; // unit = rad/sec
    double desiredVel_; // unit = rad/sec
    std::chrono::milliseconds timeStamp_msec_; 
    std::chrono::microseconds timeStamp_usec_; 
  };

  extern std::map<uint8_t, Device> devices; // CANid->Device object

  class DeviceGroup {
  public:
    DeviceGroup() {};
  DeviceGroup(std::vector<uint8_t> CANids) : CANids_(CANids) {};
  DeviceGroup(std::vector<uint8_t> CANids, std::vector<std::string> names) : 
    CANids_(CANids), names_(names) {};

    inline std::vector<double> getActualPos() {
      std::vector<double> actualPos;
      for (uint8_t CANid : CANids_)
	actualPos.push_back(devices[CANid].actualPos_);
      return actualPos;
    }
    inline std::vector<double> getDesiredPos() {
      std::vector<double> desiredPos;
      for (auto CANid : CANids_)
	desiredPos.push_back(devices[CANid].desiredPos_);
      return desiredPos;
    }
    inline std::vector<double> getActualVel() {
      std::vector<double> actualVel;
      for (auto CANid : CANids_)
	actualVel.push_back(devices[CANid].actualVel_);
      return actualVel;
    }
    inline std::vector<double> getDesiredVel() {
      std::vector<double> desiredVel;
      for (auto CANid : CANids_)
	desiredVel.push_back(devices[CANid].desiredVel_);
      return desiredVel;
    }
    inline void setVel(std::vector<double> velocities) {
      for (int i=0; i<velocities.size(); i++) {
	devices[CANids_[i]].desiredVel_ = velocities[i];
	std::cout << "setVel CANID " << (int) CANids_[i] 
		  << " name: " << names_[i] << " vel: " << velocities[i] << std::endl;
      }
    }

    std::vector<uint8_t> CANids_;
    std::vector<std::string> names_;
  };

  
  extern std::map<std::string, DeviceGroup> deviceGroups;
  // ^ DeviceGroup name (e.g. "tray", "arm1", etc.) -> DeviceGroup object

  inline int32_t rad2mdeg(double phi) {
    return static_cast<int32_t>( round( phi/(2*M_PI)*360000.0 ) ); }
  inline double mdeg2rad(int32_t alpha) {
    return static_cast<double>( static_cast<double>(alpha)/360000.0*2*M_PI ); }

  /***************************************************************/
  //		State machines
  /***************************************************************/

  void setMNTState(uint16_t CANid, std::string targetState);						
  void setMotorState(uint16_t CANid, std::string targetState);

  /***************************************************************/
  //		Init sequence
  /***************************************************************/

  extern bool atFirstInit;
  // ^ true only at first call of the init() function, to prevent 
  // from launching the listener thread twice
  extern HANDLE h; // PCAN device handl

  bool openConnection(std::string devName);
  void init(std::string deviceFile, std::chrono::milliseconds syncInterval);

  /***************************************************************/
  //		Thread initialization
  /***************************************************************/

  void initListenerThread(std::function<void ()> const& listener);
  void initNodeguardThread(std::function<void ()> const& nodeguard);
  void initDeviceManagerThread(std::function<void ()> const& deviceManager);

  /***************************************************************/
  //		NMT Protocol
  /***************************************************************/

  const uint8_t NMT_start_remote_node = 0x01;								
  const uint8_t NMT_stop_remote_node = 0x02;  								
  const uint8_t NMT_enter_pre_operational = 0x80;							
  const uint8_t NMT_reset_node = 0x81;									
  const uint8_t NMT_reset_communication = 0x82;	

  extern TPCANMsg NMTmsg;

  inline void sendNMT(uint8_t CANid, uint8_t command) {
    NMTmsg.DATA[1] = command;
    NMTmsg.DATA[0] = CANid;
    CAN_Write(h, &NMTmsg);
  }

  /***************************************************************/
  //		SDO Protocol
  /***************************************************************/

  struct SDOkey {
    uint16_t index;
    uint8_t subindex;
    inline SDOkey(TPCANRdMsg m) : index((m.Msg.DATA[2] << 8) + m.Msg.DATA[1]),
				  subindex(m.Msg.DATA[3]) {}
    inline SDOkey(uint16_t i, uint8_t s) : index(i), subindex(s) {};
  };

  const SDOkey statusword(0x6041, 0x0);
  const SDOkey controlword(0x6040, 0x0);
  const SDOkey sync_timeout_factor(0x200e, 0x0);
  const SDOkey ip_time_units(0x60C2, 0x1);
  const SDOkey ip_time_index(0x60C2, 0x2);
  const int8_t ip_time_index_milliseconds = 0xFD;
  const int8_t ip_time_index_hundredmicroseconds = 0xFC;
  const uint8_t sync_timeout_factor_disable_timeout = 0;

  const SDOkey modes_of_operation(0x6060, 0x0);
  const SDOkey modes_of_operation_display(0x6061, 0x0);
  const uint8_t modes_of_operation_homing_mode = 0x6;
  const uint8_t modes_of_operation_profile_position_mode = 0x1;
  const uint8_t modes_of_operation_velocity_mode = 0x2;
  const uint8_t modes_of_operation_profile_velocity_mode = 0x3;
  const uint8_t modes_of_operation_torque_profile_mode = 0x4;
  const uint8_t modes_of_operation_interpolated_position_mode = 0x7;

  const uint16_t controlword_shutdown = 6;
  const uint16_t controlword_switch_on = 7;
  const uint16_t controlword_start_homing = 16;
  const uint16_t controlword_enable_operation = 15;
  const uint16_t controlword_enable_ip_mode = 16;
  const uint16_t controlword_fault_reset_0 = 0x00;
  const uint16_t controlword_fault_reset_1 = 0x80;

  inline bool operator<(const SDOkey &a, const SDOkey&b) {
    return a.index < b.index || (a.index == b.index && a.subindex < b.subindex); }

  extern std::map<SDOkey, std::function<void (uint8_t CANid, BYTE data[8])> >
    incomingDataHandlers;

  void statusword_incoming(uint8_t CANid, BYTE data[8]);
  void modes_of_operation_display_incoming(uint8_t CANid, BYTE data[8]);

  void sendSDO(uint8_t CANid, SDOkey sdo);
  void sendSDO(uint8_t CANid, SDOkey sdo, uint32_t value);
  void sendSDO(uint8_t CANid, SDOkey sdo, int32_t value);
  void sendSDO(uint8_t CANid, SDOkey sdo, uint16_t value);
  void sendSDO(uint8_t CANid, SDOkey sdo, uint8_t value);

  /***************************************************************/
  //		PDO Protocol
  /***************************************************************/

  extern std::map<uint16_t, std::function<void (const TPCANRdMsg m)> >
    incomingPDOHandlers;

  void deviceManager();		// responsible for automatically sending PDOs

  extern std::function< void (uint16_t CANid, double positionValue) > sendPos;

  void schunkDefaultPDO_incoming(uint8_t CANid, const TPCANRdMsg m);
  void schunkDefaultPDOOutgoing(uint16_t CANid, double positionValue);

  extern TPCANMsg syncMsg;
  inline void sendSync() {
    CAN_Write(h, &syncMsg);
  }

  /***************************************************************/
  //		Nodeguard Protocol
  /***************************************************************/

  const uint16_t basic_guard_time = 500;								// guard time for a single device in ms
  const SDOkey guard_time(0x100C,0x0);
  const uint16_t guard_time_value = basic_guard_time * devices.size();
  const SDOkey life_time_factor(0x100D,0x0);
  const uint8_t life_time_factor_value = devices.size() * 2;

  void incomingNodeguardHandler(uint8_t CANid, BYTE data[8]);

  void nodeGuard();

  extern TPCANMsg nodeguardMsg;

  inline void sendNodeguard(uint8_t CANid) {
    nodeguardMsg.ID = 0x700 + CANid;
    CAN_Write(h, &nodeguardMsg);
  }

  /***************************************************************/
  //		Receive Data
  /***************************************************************/

  void defaultListener();

}

#endif
