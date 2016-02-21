/* 
 * Copyright (c) 2015, Daniel Swann <hs@dswann.co.uk>
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the owner nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */


#include "CGatekeeper_door_hs25.h"
#include "CNHmqtt.h"

using namespace std;


CGatekeeper_door_hs25::CGatekeeper_door_hs25()
{
  _id = 0;
  _base_topic = "";
  _log = NULL;
  _db = NULL;
  door_short_name = "";
  _handle = "";
  _entry_announce = "";
  memset(&_last_valid_read, 0, sizeof(_last_valid_read));
}

CGatekeeper_door_hs25::~CGatekeeper_door_hs25()
{
  dbg("Deleted");
}

void CGatekeeper_door_hs25::set_opts(int id, string base_topic, CLogging *log, CNHDBAccess *db, InstCBI *cb, string entry_announce, int read_timeout, string door_state)
{
  _id  = id;
  _base_topic = base_topic;
  _log = log;
  _db  = db;
  _cb  = cb;
  _entry_announce = entry_announce;
  _read_timeout = read_timeout;

  // Now the door_id has been set, get the list of door bells that should be rang when the button is pushed
  _door_bells.clear();

  // Get a list of all bells to be rung...
  dbrows bells;
  db->sp_gatekeeper_get_door_bells(_id, &bells);

  dbg("Door button will ring:");
  for (dbrows::const_iterator iterator = bells.begin(), end = bells.end(); iterator != end; ++iterator) 
  {
    door_bell bell;
    dbrow row = *iterator;

    bell.mqtt_topic   = row["bell_topic"].asStr();
    bell.mqtt_message = row["bell_message"].asStr();
    _door_bells.push_back(bell);

    dbg("\t" + bell.mqtt_topic + "\t" + bell.mqtt_message);
  }

  _door_state = get_door_state_from_str(door_state);

  dbg("Configured");
}

void CGatekeeper_door_hs25::dbg(string msg)
{
  if (_log == NULL)
    return;

  _log->dbg("CGatekeeper_door_hs25(" + door_short_name + ")", msg);
}

CGatekeeper_door_hs25::DoorState CGatekeeper_door_hs25::get_door_state_from_str(string door_state)
{
  if (door_state == "OPEN")
    return DS_OPEN;
  else if (door_state == "CLOSED")
    return DS_CLOSED;

  return DS_UNKNWON;
}

void CGatekeeper_door_hs25::process_door_event(string type, string payload)
{
  if ((_db == NULL) || (_cb == NULL))
    return;

  time_t current_time;
  string display_message;
  string err;
  string unlock_topic = _base_topic + "/" + CNHmqtt::itos(_id) + "/Unlock";
  
  char door_side = 'Z'; // default to Z: applies to both sides


  // Get door side message relates to (if applicable)
  if ((type.substr(0, 2) == "A/") || (type.substr(0, 2) == "B/"))
  {
    door_side = type[0];
    type = type.substr(2, string::npos);
  }

  /*
  if (type=="DoorState")
  {
    if ((payload.substr(0, ((string)("Door Opened by:")).length() ) == "Door Opened by:") && (_handle != ""))
    {
      _db->sp_set_door_state(_id, "OPEN");

      if (_last_seen.length() > 1)
      {
        _cb->cbiSendMessage(_entry_announce + "/known", "Door opened by: " + _handle + " (last seen " + _last_seen + " ago)");
      }
      else
      {
        dbg("No last seen time set");
        _cb->cbiSendMessage(_entry_announce, payload + " " + _handle);
      }
      _handle = "";
      _last_seen = "";
    }
    else if (payload=="Door Closed")
    {
      dbg("Ignoring door closed message");
      _db->sp_set_door_state(_id, "CLOSED");
    }
    else if (payload=="Door Time Out")
    {
      _handle = "";
      _last_seen = "";
      _db->sp_log_event("DOOR_TIMEOUTtype", CNHmqtt::itos(_id));
    }
    else if (payload=="Door Opened")
    {
      _cb->cbiSendMessage(_entry_announce + "/unknown", "Door opened");
      _db->sp_set_door_state(_id, "OPEN");
    }
    else _cb->cbiSendMessage(_entry_announce, payload); // Else just pass the message on verbatim
  }
  */
  
  if (type=="DoorState")
  {
    CGatekeeper_door_hs25::DoorState new_door_state = get_door_state_from_str(payload);

    if (new_door_state != _door_state)
    {
      _db->sp_set_door_state(_id, payload);
      _door_state = new_door_state;
    }
  }


  else if (type=="DoorButton")
  {
    // Send message to all bells that need to be rang
    for (list<door_bell>::iterator i=_door_bells.begin(); i != _door_bells.end(); ++i)
      _cb->cbiSendMessage((*i).mqtt_topic, (*i).mqtt_message);

    // Send a message with the door name for the matrix displays / IRC / slack
    _cb->cbiSendMessage(_base_topic + "/DoorButton", door_short_name);

    // Log an event recording this door button was pushed
    _db->sp_log_event("DOORBELL", CNHmqtt::itos(_id));
  }

  else if (type=="RFID")
  {
    int access_result=0;
    time(&current_time);

    if(_db->sp_gatekeeper_check_rfid(payload, _id, display_message, _handle, _last_seen, access_result, err))
    {
      dbg("Call to sp_gatekeeper_check_rfid failed");
      display_message_lcd(door_side, "Access Denied: internal error", 2000);
    } else
    {
      display_message_lcd(door_side, display_message, 2000);

      if (access_result == 1)
      {
        beep(door_side);
        _cb->cbiSendMessage(unlock_topic, "1");
        time(&_last_valid_read);
      }
    }
  }

  else if (type=="Keypad")
  {
    _db->sp_check_pin(payload, _id, display_message, _handle, err);
    dbg("err = [" + err + "]");
    _cb->cbiSendMessage(unlock_topic, display_message);
  }

}

void CGatekeeper_door_hs25::display_message_lcd(char side, string message, int duration)
// Send message to be displayed on the LCD of one side of the door
{
  char payload[32 + 5 + 1] = ""; // display is 32 char (2x16), + 5 for duration + terminator
  string display_topic = _base_topic + "/" + CNHmqtt::itos(_id) + "/" + side + "/Display";

  snprintf(payload, sizeof(payload), "%04d:%s", duration, message.c_str());
  payload[sizeof(payload)-1] = '\0';

  _cb->cbiSendMessage(display_topic, payload);
}

void CGatekeeper_door_hs25::beep(char side, int tone, int duration)
{
  char payload[11] = ""; 
  string buzzer_topic = _base_topic + "/" + CNHmqtt::itos(_id) + "/" + side + "/Buzzer";

  snprintf(payload, sizeof(payload), "%05d:%04d", tone, duration);
  payload[sizeof(payload)-1] = '\0';

  _cb->cbiSendMessage(buzzer_topic, payload);
}
