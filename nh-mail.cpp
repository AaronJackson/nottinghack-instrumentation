/* 
 * Copyright (c) 2012, Daniel Swann <hs@dswann.co.uk>
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

#include "nh-mail.h"

#include <iostream>
#include <cstdlib>
#include <unistd.h>

nh_mail::nh_mail(int argc, char *argv[]) 
{
  log = NULL;
  string config_file = "";
  config_file_parsed = false;
  int c;
  std::stringstream out;
  reader = NULL;
  mosq_connected = false;
  log = new CLogging();
  string logfile;
  
  mosquitto_lib_init();
  mosq = NULL;
    
  /* Read in command line parameters:
   * -c <config file>       specify config file
   */
  
  opterr = 0;
  optopt = 1;
  optind = 1;
  while ((c = getopt (argc, argv, "c:")) != -1)
    switch (c)
      {
        case 'c':
          config_file = optarg;
          break;
        
        case '?':
          if (optopt == 'c')
            log->dbg ("Option -c requires an argument.");
          else  
            log->dbg("Unknown option given");
          return;
        
        default:
          log->dbg("nh_mail::nh_mail ?!?");
      }
      
  nh_mail::mosq_server = "127.0.0.1";
  nh_mail::mosq_port = 1883;   
  nh_mail::mqtt_topic = "nh/mail/rx";
  
  pthread_mutex_init (&mosq_mutex, NULL);
      
  if (config_file != "")
  {
    reader = new INIReader(config_file);
    
    if (reader->ParseError() < 0) 
    {
        log->dbg("Failed to open/parse config file [" + config_file + "]");
    } else
    {
      config_file_parsed = true;
      mosq_server = reader->Get("mqtt", "host", "localhost");
      mosq_port   = reader->GetInteger("mqtt", "port", 1883);
      mqtt_topic  = reader->Get("nh-mail", "topic", "nh/mail/rx"); 
      logfile     = reader->Get("mqtt", "logfile", ""); 
    }    
  }
  
  if(!log->open_logfile(logfile))
    exit(1);    
  
  log->dbg("mosq_server = " + mosq_server);
  out << "" << mosq_port;
  log->dbg("mosq_port = "   + out.str());  

}

nh_mail::~nh_mail()
{
  if (mosq_connected && (mosq != NULL))
  {
    mosquitto_disconnect(mosq);         
    log->dbg("Disconnected from mosquitto");
  }
  
  if (mosq != NULL)
    mosquitto_destroy(mosq); 
  
  mosquitto_lib_cleanup();
  
  if (reader!=NULL)
  {
    delete reader;
    reader=NULL;
  }
  
  if (log!=NULL)
  {
    delete log;
    log=NULL;
  }
}

string nh_mail::get_str_option(string section, string option, string def_value)
{
  if (config_file_parsed && reader != NULL)
    return reader->Get(section, option, def_value); 
  else
    return def_value;
}

int nh_mail::get_int_option(string section, string option, int def_value)
{
  if (config_file_parsed && reader != NULL)
    return reader->GetInteger(section, option, def_value); 
  else
    return def_value;
}

int nh_mail::mosq_connect()
{
  std::stringstream out;
  out << "nh-mail" << "-" << getpid();
  
  log->dbg("Connecting to Mosquitto as [" + out.str() + "]");;
  
  mosq = mosquitto_new(out.str().c_str(), this);  
  if(!mosq)
  {
    cout << "mosquitto_new() failed!";
    return -1;
  }  
  
  mosquitto_connect_callback_set(mosq, nh_mail::connect_callback);
//  mosquitto_message_callback_set(mosq, nh_mail::message_callback);  
  
    // int mosquitto_connect(struct mosquitto *mosq, const char *host, int port, int keepalive, bool clean_session);
  if(mosquitto_connect(mosq, mosq_server.c_str(), mosq_port, 300, true)) 
  {
    log->dbg("mosq_connnect failed!");
    mosquitto_destroy(mosq);
    mosq = NULL;
    return -1;
  }
  mosq_connected = true;
    
  return 0;       
}

void nh_mail::connect_callback(void *obj, int result)
{
  nh_mail *m = (nh_mail*)obj;

  if(!result)
  {  
    m->log->dbg("Connected to mosquitto.");  
  } else
  {
    m->log->dbg("Failed to connect to mosquitto!");
  }
}

int nh_mail::message_send(string topic, string message, bool no_debug)
{
  int ret;
  
  if (!no_debug)
    log->dbg("Sending message,  topic=[" + topic + "], message=[" + message + "]");
  pthread_mutex_lock(&mosq_mutex);
  ret = mosquitto_publish(mosq, NULL, topic.c_str(), message.length(), (uint8_t*)message.c_str(), 0, false);
  pthread_mutex_unlock(&mosq_mutex);
  return ret;
}

int nh_mail::message_send(string topic, string message)
{
  return message_send(topic, message, false);
}

int nh_mail::message_loop(void)
{
  string dbgmsg="";
  
  if (mosq==NULL)
    return -1;
  
  mosquitto_loop(mosq, 50);
  
  return 0;
}

int main(int argc, char *argv[])
{
  nh_mail *nh;
  
  string input_line;
  string mail_from;
  string mail_subject;
  string cmdline;
  string searchstr;
  
  while ((cin) && (((mail_from == "") || (mail_subject == ""))))
  {
    getline(cin, input_line);
    
    if (input_line.substr(0, 6) == "From: ")    
    {
      mail_from = input_line.substr(6, string::npos);
      mail_from = mail_from.substr(0, mail_from.find_first_of('<'));
    
      // Remove any quotes
      for (unsigned int i = 0; i < mail_from.length(); i++)
        if (mail_from[i] == '\"')
          mail_from.replace(i, 1,"");
    }
    
    if (input_line.substr(0, 9) == "Subject: ")    
    {
      mail_subject = input_line.substr(9, string::npos);
     
      searchstr = "[Nottinghack] ";
      if (mail_subject.find(searchstr) != string::npos)
      {
        mail_subject = mail_subject.substr(mail_subject.find(searchstr) + searchstr.length(), string::npos);
      }
    }
  }
  
  // Got From and Subject. Don't care about the rest of the mail, but need to 
  // read it to keep fetchmail happy.
  while (cin)
    getline(cin, input_line);
  
  if ((mail_from != "") && (mail_subject != ""))
  {
    nh = new nh_mail(argc, argv);
    nh->mosq_connect();
    nh->message_loop();
    mail_from.erase(mail_from.find_last_not_of(" \n\r\t")+1);
    nh->message_send(nh->mqtt_topic + "/" + mail_from, mail_subject);
    nh->message_loop();

    delete nh; 
  }

}




