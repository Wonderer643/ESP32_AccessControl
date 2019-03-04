#include <WiFi.h>
#include <ESPmDNS.h>
//#include <ArduinoOTA.h>
#include <Update.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Ds1302.h>
#include <esp_wifi.h>
#include <LinkedList.h>
#include <SPI.h>
#include <MFRC522.h>
#include "SPIFFS.h"

//18,19,21,22,23 PINS are for RFID board

#define PIN_BUT 26 

#define PIN_ENA 27
#define PIN_DAT 12
#define PIN_CLK 13 

#define RFID_SS_PIN 21
#define RFID_RST_PIN 22

#define DEBUG_MEM_PRINT_DELAY 5000

#define GEN  "General"
#define WIFI "Wifi"
#define RFID "RFID"
#define NVS_NAMESAPCE "ESP32"

#define PRESSED_TIME_DFLT 1000UL
#define RELEASED_TIME_DFLT 10000UL
#define ON_EXTEND_DFLT true
#define INVERT_DFLT false

#define SSID_NAME_DFLT "ESP32ap"
#define SSID_PASS_DFLT "12345678"
#define SSID_SHOW_DFLT true
#define SSID_TX_DFLT 78
#define wifi_channel 9

#define WWW_USER_DFLT "admin"
#define WWW_PASS_DFLT "admin"

#define RFID_USE_DFLT false
#define WIFI_USE_DFLT false
#define WIFIPROB_USE_DFLT false
#define BLUE_USE_DFLT false

#define MAX_RFID_LIST_SIZE 250
#define MAX_WIFI_LIST_SIZE 99
#define MAX_BLUE_LIST_SIZE 4

unsigned long pRESSED_TIME; 
unsigned long rELEASED_TIME; 
boolean on_extend;
boolean relay_inverted;

char ssid_name[32];
char ssid_pass[32];
boolean ssid_show;
uint8_t ssid_tx;

char www_username[32];
char www_password[32];

typedef struct
{
  char name[13];
  unsigned long uid;
  boolean paused;
} rfid_id_t;

typedef struct
{
  char name[13];
  uint8_t mac[6];
  boolean paused;
} macs_id_t;

LinkedList<rfid_id_t> rfid_list;
LinkedList<macs_id_t> wifi_list;

boolean rfid_use;
boolean wifi_use;
boolean wifiprob_use;

const int8_t tx_powers[]={-4,8,20,28,34,44,52,56,60,66,72,78};

wifi_event_id_t wifiprob_event;
wifi_event_id_t wifi_con_event;
wifi_event_id_t wifi_discon_event;

boolean wificon_found_flag;
boolean wifiprob_found_flag;
boolean rfid_found_flag;

uint64_t buttonPressTime;
boolean buttonPressFlag;
uint64_t buttonReleaseTime;
boolean buttonReleaseFlag;

unsigned long free_mem_time;

char rfidscanned[255];

#define FORMAT_SPIFFS_IF_FAILED true
#define LOG_PATH "/"
#define LOG_SUFFIX ".log"
#define LOG_NAME "access"
#define LOG_CURRENT ""
#define LOG_FILE LOG_PATH LOG_NAME LOG_CURRENT LOG_SUFFIX 
boolean no_log;
#define LOGFILE_MAX_SIZE 100000 //in bytes
#define SPIFFS_FREE_SPACE_LIMIT 50 //in percents. keep it as 50 as speed is degraded if it is more than 70

//Buzzer global variables and defaults
#define BUZZER_PIN 2
static const double buzzer_good_freq = 2000;
static const double buzzer_bad_freq = 500;
static const uint8_t buzzer_channel = 0;
static const uint8_t buzzer_resolution = 8;
//typedef enum {
//  vol_min=0;
//  vol_25=32;
//  vol_50=64;
//  vol_75=96;
//  vol_max=128;
//} volume_t;
uint8_t buzzer_volume=128;
uint64_t buzzerStartTime;
boolean buzzerStartedFlag;
unsigned long bUZZER_TIME;

//HeartBin global variables and defaults.
#define PIN_HB_LED 25
uint64_t heartbeatLedTime;
boolean heartbeatLedStatus;
static const uint64_t hEARTBEAT_TIME = 500;

//Reset button variables and defaults
#define PIN_RESET_BUT 15
static const uint64_t rESET_TIME = 3000; //If button is pressed for less then rESTART_TIME milliseconds nothing happens. 
uint64_t resetPressedTime;
static const uint64_t hEARTBEAT_TIME_RESET = 200;


//////////////////////////////////////////////////////////////////////////////////
Preferences pref;
WebServer server(80);
Ds1302 rtc(PIN_ENA, PIN_CLK, PIN_DAT);
MFRC522 mfrc522(RFID_SS_PIN, RFID_RST_PIN);   // Create MFRC522 instance.

///////////////////////////////////////////////////////////////////////////////////

void writetolog(const char * prefix, const char * nm,const char *st) {

  if (!no_log) {
    //Check the used filesystem size and remove old files
    int used_p;
    int total;
    int used;
    total=SPIFFS.totalBytes();    
    if (total==0) {
      ESP_LOGE(GEN,"Total SPIFFS size is ZERO. Probably SPIFFS not available. Working with no logs");          
      return;
    }
    do {
      total=SPIFFS.totalBytes();
      used=SPIFFS.usedBytes();
      ESP_LOGD(GEN,"SPIFFS used space %d bytes from %d bytes",used,total);    
      if (total>0) {
        used_p = (used / total)*100;
      } else {
        used_p = 0;
      }
      if ((total>0) && (used_p >= SPIFFS_FREE_SPACE_LIMIT)) {
          deleteOldFile();      
      }
    } while (used_p >= SPIFFS_FREE_SPACE_LIMIT);

    //there is enough space in the filesystem now, thus open log file for append
    File logfile = SPIFFS.open(LOG_FILE,FILE_APPEND);
//    File logfile = SPIFFS.open(LOG_FILE,FILE_WRITE);
   
    if (!logfile || logfile.isDirectory()) {
      ESP_LOGE(GEN,"Failed to open current logfile %s",logfile);    
      return;
    }

//    if (!logfile.seek(logfile.size()-1)) {
//      ESP_LOGE(GEN,"Failed to seek to the end of the file %s",logfile.name);          
//    }

    //Rotate log file if it's size is big
//    ESP_LOGD(GEN,"Logfile %s is opened with size %d",LOG_FILE,logfile.size());    
//    if (logfile.size() >= LOGFILE_MAX_SIZE) {
    ESP_LOGD(GEN,"Logfile %s is opened with size %d",LOG_FILE,logfile.position());    
    if (logfile.position() >= LOGFILE_MAX_SIZE) {
      logfile.close();
      rotateLogs();
      //call same recursivly to do write to create a new file and write there
      writetolog(prefix,nm,st);
      return;
    }
    char date[32];
    getTimeShort(date);
    char logline[255];
    sprintf(logline,"%s-%s-%s-%s\r\n",date,prefix,nm,st);
    if (!logfile.write((const uint8_t*)logline,strlen(logline))) {
      ESP_LOGE(GEN,"Failed to write to logfile %s",logfile);          
    }
    logfile.close();
  } else {
    ESP_LOGE(GEN,"Working with no logging as SPIFFS mount failed");        
  }
}

void rotateLogs(){
  //Try to rename files from /access_XX.log to the /access_XY.log starting from high number to lower number.
  //If no such file, just skip
  char logfile_new_name[64];  
  char logfile_old_name[64];    
  
  ESP_LOGD(GEN,"Rotating log files");                     
     
  //Rotate log files from /access_00.log to /access_01.log
  for (int xx=98; xx>=0; xx--) { //start from 98 as new file name will be xx+1 = 99
    sprintf(logfile_old_name,"%s%s_%02d%s",LOG_PATH,LOG_NAME,xx,LOG_SUFFIX);
    ESP_LOGD(GEN,"Check if file %s exists",logfile_old_name);
    if (SPIFFS.exists(logfile_old_name)) {
      sprintf(logfile_new_name,"%s%s_%02d%s",LOG_PATH,LOG_NAME,(xx+1),LOG_SUFFIX);      
      ESP_LOGD(GEN,"Try to rename it from %s to the %s",logfile_old_name,logfile_new_name);
      if (SPIFFS.rename(logfile_old_name,logfile_new_name)) {
        ESP_LOGI(GEN,"File %s renamed to %s",logfile_old_name,logfile_new_name,LOGFILE_MAX_SIZE);                      
      } else {
        ESP_LOGE(GEN,"Error renaming file %s to %s",logfile_old_name,logfile_new_name);                      
      }
    }
  }
  //Now rotate lof file /access.log to the /access_00.log
  sprintf(logfile_old_name,"%s",LOG_FILE);      
  sprintf(logfile_new_name,"%s%s_%02d%s",LOG_PATH,LOG_NAME,0,LOG_SUFFIX);      
  if (SPIFFS.rename(logfile_old_name,logfile_new_name)) {
    ESP_LOGI(GEN,"File %s renamed to %s as file is more than %d bytes",logfile_old_name,logfile_new_name,LOGFILE_MAX_SIZE);                      
  } else {
    ESP_LOGE(GEN,"Error renaming file %s to %s",logfile_old_name,logfile_new_name);                      
  }
}

void deleteOldFile() {
  char most_old_file[64]="";  

  ESP_LOGD(GEN,"Deleting the oldest file");
  
  for (uint8_t xx=99; xx>=0; xx--) { //start from 99 as this is the biggest number file may have
    sprintf(most_old_file,"%s%s_%02d%s",LOG_PATH,LOG_NAME,xx,LOG_SUFFIX);
    ESP_LOGD(GEN,"Check if file %s exists",most_old_file);
    if (SPIFFS.exists(most_old_file)) {
      ESP_LOGD(GEN,"Try to remove file %s",most_old_file);      
      if (SPIFFS.remove(most_old_file)) {
        ESP_LOGI(GEN,"File %s is removed",most_old_file);
        restartSPIFFS();
      } else {
        ESP_LOGE(GEN,"Error removing %s file",most_old_file);        
      }
    }
  }
}

char * rfid2string(unsigned long id, char *str) {
  if (str == NULL) {
      return NULL;
  }
  sprintf(str,"%010lu",id);

  return str;
}

unsigned long rfid2mac(char *str) {
  unsigned long mac;
  if (str == NULL) {
      return 0;
  }  
  mac = (unsigned long )strtoul(str,NULL,10);
  return mac;
}

char *mac2string(uint8_t *id, char *str) {
  if (id == NULL || str == NULL) {
      return NULL;
  }
  uint8_t *p = id;
  sprintf(str, "%02X:%02X:%02X:%02X:%02X:%02X",
            p[0], p[1], p[2], p[3], p[4], p[5]);
  
  return str;  
}

uint8_t *mac2mac(char *str, uint8_t *mac) {
  char *q1 = strtok(str,":");
  mac[0] = strtoul(q1,NULL,16);
  for (int i=1;i<6;i++) {
    mac[i]= (uint8_t) strtoul (strtok(NULL,":"),NULL,16);
  }
  return mac;
}
  
boolean readNVS() {
    char bda_str[13];
    if (!pref.begin(NVS_NAMESAPCE, false)) {
      ESP_LOGE(GEN,"Error init Pref library.");      
      return false;
    }

    pRESSED_TIME=pref.getULong("PRT",PRESSED_TIME_DFLT);
    rELEASED_TIME=pref.getULong("RLT",RELEASED_TIME_DFLT);
    on_extend=pref.getBool("EXT",ON_EXTEND_DFLT);
    relay_inverted=pref.getBool("INV",INVERT_DFLT);
    ESP_LOGI(GEN,"Read key PRT(PRESSED_TIME)=%d, RLT(RELEASED_TIME)=%d, EXT(EXTEND_TIME)=%d, INV(RELAY_INVERTED)=%d",pRESSED_TIME,rELEASED_TIME,on_extend,relay_inverted);

    strncpy(ssid_name,SSID_NAME_DFLT,31);
    pref.getString("SUS",ssid_name,31);
    strncpy(ssid_pass,SSID_PASS_DFLT,31);
    pref.getString("SPS",ssid_pass,31);
    ssid_show=pref.getBool("SVS",SSID_SHOW_DFLT);
    ssid_tx=pref.getUInt("STX",SSID_TX_DFLT);
    ESP_LOGI(GEN,"Read keys SUS(SSID_USER)=%s, SPS(SSID_PASS)=%s, SVS(SSID_SHOW)=%d and STX(SSID_TX)=%d",ssid_name,ssid_pass,ssid_show,ssid_tx);    
    
    strncpy(www_username,WWW_USER_DFLT,31);
    pref.getString("AUS",www_username,31);
    strncpy(www_password,WWW_PASS_DFLT,31);
    pref.getString("APS",www_password,31);
    ESP_LOGI(GEN,"Read keys AUS(ADMIN_USER)=%s and APS(ADMIN_PASS)=%s",www_username,www_password);

    rfid_use = pref.getBool("RUS",RFID_USE_DFLT);
    uint8_t rfid_list_size=pref.getUInt("RSZ");
    char index[8];
    rfid_list.clear();
    for (unsigned int i=0; i<rfid_list_size; i++) {
      rfid_id_t rfid;
      sprintf (index,"RIT_%03d",i);
      pref.getBytes(index, &rfid,(size_t) sizeof(rfid_id_t));
      rfid_list.add(rfid);
    }
    ESP_LOGI(GEN,"Read keys RUS(RFID_USE)=%d, RSZ(RFID_List_Size)=%d and elements of RFID_LIST UIDs from RIT_000 till RIT_%03d",rfid_use,rfid_list_size,rfid_list_size-1);
    
    wifi_use = pref.getBool("WUS",WIFI_USE_DFLT);
    wifiprob_use = pref.getBool("WPB",WIFIPROB_USE_DFLT);    
    uint8_t wifi_list_size=pref.getUInt("WSZ");
    wifi_list.clear();
    for (unsigned int i=0; i<wifi_list_size; i++) {
      macs_id_t wifi;
      sprintf (index,"WIT_%03d",i);
      pref.getBytes(index, &wifi,(size_t) sizeof(macs_id_t));
      wifi_list.add(wifi);
    }
    ESP_LOGI(GEN,"Read keys WUS(WIFI_USE)=%d, WPB(WIFI_PROB_USE)=%d, WSZ(WIFI_List_Size)=%d and elements of WIFI_LIST MACS from WIT_000 till WIT_%03d",wifi_use,wifiprob_use,wifi_list_size,wifi_list_size-1);    

    bUZZER_TIME=pref.getULong("BTM",500);
    buzzer_volume = (uint8_t) pref.getUInt("BVL",128);    
    ESP_LOGI(GEN,"Read keys BTM(Buzzer time)=%d, BVL(Buzzer volume)=%d",bUZZER_TIME,buzzer_volume);        
      
    pref.end();
    
    return true;
}

int8_t checkSSID_TX() {
  wifi_power_t power;
  power = WiFi.getTxPower();
  ESP_LOGI(GEN,"Max TX power value from device via WiFi.getTxPower is %d",power);  
  
  return (int8_t) power;
}

void setTX(int8_t power) {
  WiFi.setTxPower((wifi_power_t) power);
}

void getTime(char *str) {
    Ds1302::DateTime now = {0,0,0,0,0,0,0};

    if (rtc.isHalted()) {
      ESP_LOGE(GEN,"DS1302 halted.");
      sprintf(str,"0000-00-00 00:00:00");
      return;
    }
    rtc.getDateTime(&now);
    
    sprintf(str,"2%03d-%02d-%02d %02d:%02d:%02d",now.year,now.month,now.day,now.hour,now.minute,now.second);
}

void getTimeShort(char *str) {
    Ds1302::DateTime now = {0,0,0,0,0,0,0};

    if (rtc.isHalted()) {
      ESP_LOGE(GEN,"DS1302 halted.");
      sprintf(str,"00000000000000");
      return;
    }
    rtc.getDateTime(&now);
    
    sprintf(str,"2%03d%02d%02d%02d%02d%02d",now.year,now.month,now.day,now.hour,now.minute,now.second);
}

void checkAuth () {
  if (!server.authenticate(www_username, www_password)) {
    return server.requestAuthentication();
  }
}

void handleRoot () {
  checkAuth ();
  String r = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
  r += "<style>body{margin:0;font-family:Arial,Helvetica,sans-serif}.topnav{overflow:hidden;background-color:#333;position:fixed;z-index:2;top:0;width:100%}.topnav .menuhead{background-color:#666;color:#f2f2f2;text-align:left;text-decoration:none;padding:14px 16px;font-size:17px;position:relative}.topnav a{float:left;display:block;color:#f2f2f2;text-align:center;padding:14px 16px;text-decoration:none;font-size:13px;position:relative}.topnav a:hover{background-color:#ddd;color:black}.active{background-color:#09f;color:white}.topnav .icon{display:none}@media screen and (max-width: 600px){.topnav a:not(:first-child){display:none}.topnav a.icon{float:left;display:block}}@media screen and (max-width: 600px){.topnav.responsive{position:fixed}.topnav.responsive .icon{position:relative;left:0;top:0}.topnav.responsive a{float:none;display:block;text-align:left}}.cellhead{border:none;padding:8px 8px;text-decoration:none;font-size:16px}.cell{border:1px solid black;padding:8px 8px;text-decoration:none;font-size:13px}.cell-disabled{border:1px solid black;padding:8px 8px;text-decoration:none;font-size:13px;background-color:#ccc}.cellcross{border:none;padding:8px 8px;text-decoration:none;font-size:13px;color:grey}.cellcross:hover{border:none;padding:8px 8px;text-decoration:none;font-size:13px;color:red;cursor:pointer}.cellpause{border:none;padding:8px 8px;text-decoration:none;font-size:13px;color:grey}.cellpause:hover{border:none;padding:8px 8px;text-decoration:none;font-size:13px;color:blue;cursor:pointer}.cellpaused{border:none;padding:8px 8px;text-decoration:none;font-size:13px;color:blue}.cellpaused:hover{border:none;padding:8px 8px;text-decoration:none;font-size:13px;color:grey;cursor:pointer}.celladd{background-color:white;border:none;padding:4px 8px;text-decoration:none;font-size:17px;color:grey}.celladd:hover{border:none;padding:4px 8px;text-decoration:none;font-size:17px;color:#0f0;cursor:pointer}.switch{position:relative;display:inline-block;width:60px;height:24px}.switch input{display:none}.slider{position:absolute;cursor:pointer;top:0;left:0;right:0;bottom:0;background-color:#ccc;border-radius:24px;-webkit-transition: .2s;transition: .2s}.slider:before{position:absolute;content:'';height:16px;width:16px;left:4px;bottom:4px;background-color:white;border-radius:50%;-webkit-transition: .2s;transition: .2s}input:checked+.slider{background-color:#09f}input:focus+.slider{box-shadow:0 0 1px #09f}input:checked+.slider:before{-webkit-transform:translateX(36px);-ms-transform:translateX(36px);transform:translateX(36px)}.button{background-color:#09f;border:none;border-radius:5px;color:white;padding:8px 32px;text-align:center;text-decoration:none;display:inline-block;font-size:16px;margin:4px 2px;-webkit-transition-duration:0.4s;transition-duration:0.4s;cursor:pointer}.button:hover{background-color:#ddd;color:white;border:none}.button:focus{outline:none}.mytextbox{!background-color: #09f;!border: 1px;!border-color: #09f;border-radius:5px;!color: white;padding:8px 32px;text-align:center;text-decoration:none;!display: inline-block;font-size:16px;margin:4px 2px;!-webkit-transition-duration: 0.4s;!transition-duration: 0.4s;!cursor: pointer}.mylabel{vertical-align:top;height:24;line-height:24px}</style>";
  r += "</head><body>";
  r += "<div class='topnav' id='myTopnav'>";
  r += "<div class='menuhead' id='menuhead'></div>";
  r += "<a href='javascript:void(0);' class='icon' onclick='myFunction()'>&#9776;</a>";
  String r1 = "<a class='menu' onclick='myFunction()' href='#";
  r += r1 + "general' id='general'>General</a>";
  r += r1 + "rfid' id='rfid'>RFID</a>";
  r += r1 + "wifi' id='wifi'>WiFi</a>";
  r += r1 + "ota' id='ota'>OTA</a>";
  r += r1 + "logs' id='logs'>Logs</a>";
  r += "</div>";
  r += "<div style='padding-left:16px; padding-top:88px' id='body'></div>";
  r += "<br><div><p style='color:#bbb; text-align:center;' id='rfidscanned'></p></div>";
  r += "<br><br><footer><p style=\"color:#ddd; text-align:center;\">";
  r += "Compiled at " + String(__DATE__) + " " + String(__TIME__);
  r += "</p></footer>";
  r += "<script>function rfidscannedRead(){var xhttp = new XMLHttpRequest();xhttp.onreadystatechange = function() {if (this.readyState == 4 && this.status == 200) {document.getElementById('rfidscanned').innerHTML = this.responseText;startTime();}};xhttp.open('GET', 'rfidscanned.txt', true);xhttp.send();setTimeout(rfidscannedRead,2000);}function myFunction(){var x=document.getElementById('myTopnav');if(x.className==='topnav'){x.className+=' responsive';}else{x.className='topnav';}}</script> <script>var menus=document.getElementById('myTopnav').getElementsByClassName('menu');for(var i=0;i<menus.length;i++){menus[i].addEventListener('click',function(){var current=document.getElementsByClassName('active');if(current.length>0){current[0].className=current[0].className.replace(' active','');} this.className+=' active';document.getElementById('menuhead').innerHTML=this.innerHTML;loadDoc(this.id+'.txt');});}</script> <script>window.onload=function(e){rfidscannedRead();var hash=window.location.hash;var hashid=hash.substring(1,hash.length);if (hashid!==null && hashid!==\"\"){document.getElementById(hashid).className+=' active';document.getElementById('menuhead').innerHTML=document.getElementById(hashid).innerHTML;loadDoc(hashid+'.txt');}}</script> <script>function loadDoc(element){document.getElementById('body').innerHTML='';var xhttp=new XMLHttpRequest();xhttp.onreadystatechange=function(){if(this.readyState==4&&this.status==200){document.getElementById('body').innerHTML=this.responseText;startTime();}};xhttp.open('GET',element,true);xhttp.send();}</script> <script>function startTime(){var today=new Date();var d=today.getDate();var mon=today.getMonth()+1;var y=today.getYear();y=y+1900;var h=today.getHours();var m=today.getMinutes();var s=today.getSeconds();m=checkTime(m);s=checkTime(s);mon=checkTime(mon);d=checkTime(d);var ele2=document.getElementById('cldate');if(ele2){ele2.value=y+''+mon+''+d+''+h+''+m+''+s;} var ele=document.getElementById('clientdate');if(ele){ele.innerHTML=y+'-'+mon+'-'+d+' '+h+':'+m+':'+s;var t=setTimeout(startTime,500);}} function checkTime(i){if(i<10){i='0'+i};return i;}</script>";
  r += "<script>function _(el){return document.getElementById(el);}function uploadFile(){var file = _(\"update\").files[0];var formdata = new FormData();formdata.append(\"update\", file);var ajax = new XMLHttpRequest();ajax.upload.addEventListener(\"progress\", progressHandler, false);ajax.addEventListener(\"load\", completeHandler, false);ajax.addEventListener(\"error\", errorHandler, false);ajax.addEventListener(\"abort\", abortHandler, false);ajax.open(\"POST\", \"start_ota.html\");ajax.send(formdata);}function progressHandler(event) {_(\"loaded_n_total\").innerHTML = \"Uploaded \" + event.loaded + \" bytes of \" + event.total;var percent = (event.loaded / event.total) * 100;_(\"progressBar\").value = Math.round(percent);_(\"status\").innerHTML = Math.round(percent) + \"% uploaded... please wait\";}function completeHandler(event) {_(\"status\").innerHTML = event.target.responseText;_(\"progressBar\").value = 0;}function errorHandler(event) {_(\"status\").innerHTML = \"Upload Failed\";}function abortHandler(event) {_(\"status\").innerHTML = \"Upload Aborted\";}</script>";
  r += "<script>function loadFile(element){var current = document.getElementsByClassName('act');if (current.length > 0) {current[0].className = current[0].className.replace(' act', '');}document.getElementById(element).className += ' act';document.getElementById('downurl').setAttribute('href','downFile.txt?file=' + document.getElementById(element + 'a').innerHTML);document.getElementById('filehere').innerHTML = '';var xhttp = new XMLHttpRequest();xhttp.onreadystatechange = function() {if (this.readyState == 4 && this.status == 200) {document.getElementById('filehere').innerHTML = this.responseText;}};xhttp.open('GET', 'loadFile.txt?file=' + document.getElementById(element + 'a').innerHTML, true);xhttp.send();}  function delActive() {var current = document.getElementsByClassName('act');if (current.length > 0) {var z = document.getElementById(current[0].id + 'a').innerHTML;document.getElementById('body').innerHTML = '';var xhttp = new XMLHttpRequest();xhttp.onreadystatechange = function() {if (this.readyState == 4 && this.status == 200) {document.getElementById('body').innerHTML = this.responseText;}};xhttp.open('GET', 'delActive.txt?file=' + z, true);xhttp.send();}}</script>";
  r += "</body></html>";
  server.send(200, "text/html", r);
}

void handlerfidscanned () {
  String r = "";  
  if (rfidscanned != NULL) {
    r += rfidscanned;
  } else {
    r += " "; 
  }
  //server.setContentLength(r.length());
  //server.sendHeader("Connection", "close");    
  server.send(200, "text/html", r);
}
  
void handleGeneral (){
  checkAuth ();
  String r = "<div align=\"center\"><h3>Administrative GUI username/password</h3><form action=\"\" onsubmit=\"loadDoc('gen-admin.txt?'+'admin-name='+document.getElementById(&quot;admin-name&quot;).value+'&admin-pass='+document.getElementById(&quot;admin-pass&quot;).value)\"> Admin GUI name:<br><input align=\"center\" type=\"text\" required id=\"admin-name\" class=\"mytextbox\" name=\"admin-name\" placeholder=\"Admin GUI name\" style=\"width:240px\" value=\"";
  //Admin Gui Username here
  r += www_username;
  r += "\"></input><br> Admin GUI password:<br><input align=\"center\" type=\"text\" required id=\"admin-pass\" class=\"mytextbox\" name=\"admin-pass\" placeholder=\"Admin GUI password\" style=\"width:240px\" value=\"";
  //Admin GUI password here
  r += www_password;
  r += "\"></input><br><br> <input align=\"center\" type=\"submit\" value=\"Submit ADMIN changes\" class=\"button\" style=\"width:240px\"></form></div> <br><hr><br><div align=\"center\"><h3>General WiFI parameters</h3><form action=\"\" onsubmit=\"loadDoc( 'gen-ssid.txt?'+ 'ssid-name='+document.getElementById(&quot;ssid-name&quot;).value+ '&ssid-pass='+document.getElementById(&quot;ssid-pass&quot;).value+ '&ssid-show='+document.getElementById(&quot;ssid-show&quot;).checked+ '&ssid-tx='+document.getElementById(&quot;ssid-tx&quot;).value )\"> SSID name:<br><input align=\"center\" type=\"text\" required id=\"ssid-name\" class=\"mytextbox\" name=\"ssid-name\" placeholder=\"SSID name\" style=\"width:240px\" value=\"";
  //SSID name here
  r += ssid_name;
  r += "\"></input><br> SSID password:<br><input align=\"center\" type=\"text\" required id=\"ssid-pass\" class=\"mytextbox\" name=\"ssid-pass\" placeholder=\"SSID password\" style=\"width:240px\" value=\"";
  //SSID password here
  r += ssid_pass;
  r +="\"></input><br><br> <label class=\"mylabel\" for=\"ssidshow\">SSID Visibility</label> <label class=\"switch\"> <input type=\"checkbox\" id=\"ssid-show\" name=\"ssid-show\"";
  //SSID visibility check box value here: either "" or " checked"
  if (ssid_show) {
    r += " checked";
  }
  r += "> <span class=\"slider\"></span> </label><br><br> <label for=\"ssid-tx\">TX max power:</label> <select name=\"ssid-tx\" id=\"ssid-tx\" size=\"1\" class=\"mytextbox\">";
  int i=12;
  int8_t tx=checkSSID_TX();    
  while ((--i) >= 0) {
    r += "<option value=\"";
    r += tx_powers[i];
    if (tx == tx_powers[i]) {
      r += "\" selected>";
    } else {
      r += "\">";      
    }
    r += tx_powers[i];
    r += "</option>";
  }
  r += "</select><br><br> <input align=\"center\" type=\"submit\" value=\"Submit SSID changes\" class=\"button\" style=\"width:240px\"></form></div> <br>";

  r += "<hr><br><div align=\"center\"><h3>Relay On/Off times</h3><form action=\"\" onsubmit=\"loadDoc( 'gen-relay.txt?'+ 'relay-on='+document.getElementById(&quot;relay-on&quot;).value+ '&relay-off='+document.getElementById(&quot;relay-off&quot;).value+'&on-extend='+document.getElementById(&quot;on-extend&quot;).checked+'&invert='+document.getElementById(&quot;invert&quot;).checked)\"> Relay ON time (msec):<br><input align=\"center\" type=\"text\" required id=\"relay-on\" class=\"mytextbox\" name=\"relay-on\" placeholder=\"Relay ON time (msec)\" style=\"width:240px\" value=\"";
  r += pRESSED_TIME; //Button ON time here 
  r += "\"></input><br><br>";
  r += "<label class=\"mylabel\" for=\"on-extend\">Triggers extend ON time </label><label class=\"switch\"><input type=\"checkbox\" id=\"on-extend\" name=\"on-extend\" ";
  if (on_extend) {
    r += " checked";
  }
  r += "><span class=\"slider\"></span></label><br><br>";
  r += "Relay OFF time (msec) (triggers received will be ignored):<br><input align=\"center\" type=\"text\" required id=\"relay-off\" class=\"mytextbox\" name=\"relay-off\" placeholder=\"Relay OFF time (msec)\" style=\"width:240px\" value=\"";
  r += rELEASED_TIME; //button OFF time here
  r += "\"></input><br><br>";
  r += "<label class=\"mylabel\" for=\"invert\">Invert Relay </label><label class=\"switch\"><input type=\"checkbox\" id=\"invert\" name=\"invert\" ";
  if (relay_inverted) {
    r += " checked";
  }
  r += "><span class=\"slider\"></span></label><br><br>";  
  r += "<input align=\"center\" type=\"submit\" value=\"Submit relay timing changes\" class=\"button\" style=\"width:240px\"></form></div> <br>";

  r += "<hr><br><div align=\"center\"><h3>Buzzer values</h3><form action=\"\" onsubmit=\"loadDoc('gen-buzzer.txt?'+'buzzer-time='+document.getElementById(&quot;buzzer-time&quot;).value+'&buzzer-volume='+document.getElementById(&quot;buzzer-volume&quot;).value)\">";
  r += "Buzzer time (msec):<br><input align=\"center\" type=\"text\" required id=\"buzzer-time\" class=\"mytextbox\" name=\"buzzer-time\" placeholder=\"Buzzer time (msec)\" style=\"width:240px\" value=\"";
  r += bUZZER_TIME;
  r += "\"></input><br>";
  r += "<label  for=\"buzzer-volume\">Buzzer volume:</label><select name=\"buzzer-volume\" id=\"buzzer-volume\" size=\"1\" class=\"mytextbox\">";
  for (uint8_t i=0;i<=128;i=i+32) {
    r += "<option value=\"";
    r += String(i);
    if (buzzer_volume >= i && buzzer_volume < i+32) {
      r += "\" selected>";    
    } else {
      r += "\">";
    }
    switch (i) {
      case 0: r += "Off"; break; 
      case 32: r += "25%"; break; 
      case 64: r += "50%"; break; 
      case 96: r += "75%"; break;
      case 128: r += "100%"; break;       
    }
  }
  r += "</select><br><br>";
  r += "<input align=\"center\" type=\"submit\" value=\"Submit buzzer changes\" class=\"button\" style=\"width:240px\">";
  r += "</form></div><br>";

  r += "<hr><br><div align=\"center\"><h3>Device Date and time (for logging)</h3><form action=\"\" onsubmit=\"loadDoc( 'gen-date.txt?'+ 'datetime='+document.getElementById(&quot;cldate&quot;).value )\"><table id=\"datetime\" align=\"center\" width=\"50%\" style=\"border:none;\"><tr><th width=\"50%\" align=\"center\" class=\"cellhead\">Device date/time</th><th width=\"50%\" align=\"center\" class=\"cellhead\">Client date/time</th></tr><tr><td style=\"border:none;\" align=\"center\" class=\"cell\" id=\"devdate\">";
  char tnow[20];
  getTime(tnow);
  r += String(tnow); //Device date time here
  r += "</td><td style=\"border:none;\" align=\"center\" class=\"cell\" id=\"clientdate\">00</td></tr></table> <input type=\"hidden\" id=\"cldate\" name=\"cldate\" value=\"\"></input> <input align=\"center\" type=\"submit\" value=\"Set device date time\" class=\"button\" style=\"width:240px\"></form></div> <br><hr><br>";
  server.send(200, "text/html", r);  
}

void handleGenAdmin(){
  //String admin_name=server.arg("admin-name");
  //String admin_pass=server.arg("admin-pass");

  strncpy(www_username,server.arg("admin-name").c_str(),31);
  strncpy(www_password,server.arg("admin-pass").c_str(),31);

  if (!pref.begin(NVS_NAMESAPCE, false)) {
    ESP_LOGE(GEN,"Error init Pref library.");
    handleGeneral ();
  }
  pref.putString("AUS",www_username);    
  pref.putString("APS",www_password);    
  pref.end(); 
  
  ESP_LOGI(GEN,"Admin_GUI_username=%s and Admin_GUI_Password=%s are set.",www_username,www_password);
 
  handleGeneral ();
}

void handleGenSSID(){
//ESP_LOGI(GEN,"ssid-show=%s, assid_show=%s",ssid-show,assid_show);
  strncpy(ssid_name,server.arg("ssid-name").c_str(),31);
  strncpy(ssid_pass,server.arg("ssid-pass").c_str(),31);
  if (server.arg("ssid-show") == "true") {
    ssid_show=true;
  } else {
    ssid_show=false;    
  }
  ssid_tx = (uint8_t) server.arg("ssid-tx").toInt();
  //esp_wifi_set_max_tx_power(ssid_tx);
  
  if (!pref.begin(NVS_NAMESAPCE, false)) {
    ESP_LOGE(GEN,"Error init Pref library.");
    handleGeneral ();
  }
  pref.putString("SUS",ssid_name);    
  pref.putString("SPS",ssid_pass);   
  pref.putBool("SVS",ssid_show);
  pref.putUInt("STX",ssid_tx);
  pref.end(); 
  
  ESP_LOGI(GEN,"SSID_User=%s, SSID_Pass=%s, SSID_visibility=%d and SSID_TX=%d are set",ssid_name,ssid_pass,ssid_show,ssid_tx);

  restartWiFi();
  
  handleGeneral ();
}

void handleGenRelay(){
  pRESSED_TIME = strtoul(server.arg("relay-on").c_str(),NULL,10);
  rELEASED_TIME = strtoul(server.arg("relay-off").c_str(),NULL,10);
  if (server.arg("on-extend") == "true") {
    on_extend=true;
  } else {
    on_extend=false;    
  }
  if (server.arg("invert") == "true") {
    relay_inverted=true;
  } else {
    relay_inverted=false;    
  }
    
  if (!pref.begin(NVS_NAMESAPCE, false)) {
    ESP_LOGE(GEN,"Error init Pref library.");      
    handleGeneral ();
  }
  pref.putULong("PRT",pRESSED_TIME);    
  pref.putULong("RLT",rELEASED_TIME);    
  pref.putBool("EXT",on_extend);
  pref.putBool("INV",relay_inverted);
  pref.end(); 
  
  ESP_LOGI(GEN,"Relay_ON=%d, Relay_OFF=%d, Extend_time=%d and Relay_Inverted=%d are set",pRESSED_TIME,rELEASED_TIME,on_extend,relay_inverted);

  if (!relay_inverted) {
    digitalWrite(PIN_BUT, LOW); 
  } else {
    digitalWrite(PIN_BUT, HIGH);
  }
    
  handleGeneral ();
}

void handleBuzzer(){
  bUZZER_TIME = strtoul(server.arg("buzzer-time").c_str(),NULL,10);
  buzzer_volume = (uint8_t) server.arg("buzzer-volume").toInt();
    
  if (!pref.begin(NVS_NAMESAPCE, false)) {
    ESP_LOGE(GEN,"Error init Pref library.");      
    handleGeneral ();
  }
  pref.putULong("BTM",bUZZER_TIME);    
  pref.putUInt("BVL",buzzer_volume);    

  pref.end(); 
  
  ESP_LOGI(GEN,"Buzzer_time=%d, Buzzer_volume=%d are set",bUZZER_TIME,buzzer_volume);
    
  handleGeneral ();
}

void handleGenDate(){
  String adatetime=server.arg("datetime");
  Ds1302::DateTime dt;

  dt.year=adatetime.substring(2, 4).toInt();
  dt.month = adatetime.substring(4, 6).toInt();
  dt.day = adatetime.substring(6, 8).toInt(); 
  dt.hour = adatetime.substring(8, 10).toInt();
  dt.minute = adatetime.substring(10, 12).toInt();
  dt.second = adatetime.substring(12, 14).toInt(); 
  dt.dow = 0;

  rtc.setDateTime(&dt);

  ESP_LOGI(GEN,"Device date and time (20%d/%d/%d %d:%d:%d) is set.",dt.year,dt.month,dt.day,dt.hour,dt.minute,dt.second);
 
  handleGeneral ();
}

  
void handleRfid(){
  checkAuth ();
  char rfid_str[12];
  String r = "<div align=\"center\"><h3>Use RFID channel</h3> <label class=\"switch\"> <input type=\"checkbox\" id=\"rfid_use\" onchange=\"loadDoc('rfid-switch.txt?'+'switch='+document.getElementById(&quot;rfid_use&quot;).checked)\"";
  if (rfid_use) {
    r += " checked"; //wifi connect use option, either "" or " checked";  
  }
  r += "> <span class=\"slider\"></span> </label></div> <br> <br><div align=\"center\"><form action=\"\" onsubmit=\"loadDoc('rfid-add.txt?'+'mlabel='+document.getElementById(&quot;mlabel&quot;).value+'&mac='+document.getElementById(&quot;mac&quot;).value)\">";
  r += "<table id=\"rfidTable\" align=\"center\" width=\"50%\" style=\"border:none;\">";
  r += "<tr><th></th><th colspan=\"4\" align=\"left\" class=\"cellhead\">RFID addresses to scan for</th></tr>";
  r += "<tr><th width=5%></th><th width=\"30%\" align=\"left\" class=\"cellhead\">Name</th><th width=\"55%\" align=\"left\" class=\"cellhead\">ID</th><th width=\"5%\" align=\"left\" class=\"cellhead\"></th><th width=\"5%\" align=\"left\" class=\"cellhead\"></th></tr>";

  for (unsigned int i=0; i<rfid_list.size(); i++) {
    rfid_id_t rfid;
    rfid = rfid_list.get(i);
    if (rfid.paused) {
      r +="<tr>";
      r += "<td class=\"cell\" align=\"right\" style=\"border:none;\">";
      r += String(i+1) + ".";
      r += "</td>";
      r +="<td class=\"cell-disabled\">";
      r += rfid.name; //Nick name for MAC
      r += "</td><td class=\"cell-disabled\">";
      r += rfid2string(rfid.uid,rfid_str); //Value for MAC
      r += "</td><td align=\"center\" class=\"cellcross\" onclick=\"loadDoc('rfid-del.txt?delrow=";
      r += i; //Row id number
      r += "');\">&#10060;</td><td align=\"center\" class=\"cellpaused\" onclick=\"loadDoc('rfid-unpause.txt?row=";
      r += i;
      r += "');\">&#10074;&#10074;</td>";
      r += "</tr>";      
    } else {
      r += "<tr>";
      r += "<td class=\"cell\" align=\"right\" style=\"border:none;\">";
      r += String(i+1) + ".";
      r += "</td>";  
      r += "<td class=\"cell\">";
      r += rfid.name; //Nick name for MAC
      r += "</td><td class=\"cell\">";
      r += rfid2string(rfid.uid,rfid_str); //Value for MAC
      r += "</td><td align=\"center\" class=\"cellcross\" onclick=\"loadDoc('rfid-del.txt?delrow=";
      r += i;
      r += "');\">&#10060;</td><td align=\"center\" class=\"cellpause\" onclick=\"loadDoc('rfid-pause.txt?row=";
      r += i;
      r += "');\">&#10074;&#10074;</td>";
      r += "</tr>";      
    }
  
  }
  if (rfid_list.size()<MAX_RFID_LIST_SIZE) 
  {  
    //If not reached max number, then show this code
    r += "<tr>";
    r +="<td class=\"cell\" align=\"right\" style=\"border:none;\"></td>";
    r += "<td class=\"cell\"><input id=\"mlabel\" type=\"text\" required pattern=\"[0-9A-Za-z]{1,12}\" title=\"Label with up to 12 characters\" style=\"border:none;\"></input></td><td class=\"cell\"><input id=\"mac\" type=\"text\" required pattern=\"[0-9]{1,10}\" title=\"Up to 10 digits\" style=\"border:none;\"></input></td><td><input type=\"submit\" value=\"&#10004;\" class=\"celladd\" ></td><td></td>";
    r += "</tr>";
  }
  
  r +="</table></form></div>";
//Add here last scaneed RFID.UID just for reference  
  server.send(200, "text/html", r);    
}

void handleRfidSwitch(){
  if (server.arg("switch") == "true") {
    rfid_use=true;
  } else {
    rfid_use=false;    
  }
  if (!pref.begin(NVS_NAMESAPCE, false)) {
    ESP_LOGE(GEN,"Error init Pref library.");
    handleRfid();
  }
  pref.putBool("RUS",rfid_use);
  pref.end(); 
  
  handleRfid();

}

void handleRfidAdd(){
  //String amac = server.arg("mac");
  //amac.toUpperCase();
  
  rfid_id_t rfid;
  strncpy(rfid.name,server.arg("mlabel").c_str(),12);
  char * amac_str = strdup(server.arg("mac").c_str());  
  rfid.uid = rfid2mac(amac_str);
  free(amac_str);
  rfid.paused=false;
  rfid_list.add(rfid);

  if (!pref.begin(NVS_NAMESAPCE, false)) {
    ESP_LOGE(GEN,"Error init Pref library.");      
    handleRfid();
  }
  pref.putUInt("RSZ",(unsigned int) rfid_list.size()); //Update new list size
  char index[8];
  unsigned int ind=rfid_list.size()-1;
  sprintf (index,"RIT_%03d",ind); //Builed new element key
  pref.putBytes(index,&rfid,(size_t) sizeof(rfid_id_t)); //Add new element to NVS
  pref.end(); 
  
  handleRfid();
}

void handleRfidDel(){
  unsigned int ind = strtoul(server.arg("delrow").c_str(),NULL,10);  
  rfid_list.remove(ind);

  if (!pref.begin(NVS_NAMESAPCE, false)) {
    ESP_LOGE(GEN,"Error init Pref library.");      
    handleRfid();
  }
  pref.putUInt("RSZ",(unsigned int) rfid_list.size()); //Update new list size
  char index[8];
  ind=rfid_list.size()-1;
  for (unsigned int i=0;i<=ind;i++) {
    rfid_id_t rfid;
    rfid = rfid_list.get(i);        
    sprintf (index,"RIT_%03d",i); //Builed new element key
    pref.putBytes(index,&rfid,(size_t) sizeof(rfid_id_t)); //Add new element to NVS
  }

  pref.end(); 
  handleRfid();
}

void handleRfidPause(){
  unsigned int ind = strtoul(server.arg("row").c_str(),NULL,10);  
  rfid_id_t rfid;
  
  rfid = rfid_list.get(ind);
  rfid.paused = true;
  rfid_list.set(ind, rfid);

  if (!pref.begin(NVS_NAMESAPCE, false)) {
    ESP_LOGE(GEN,"Error init Pref library.");      
    handleRfid();
  }
  char index[8];
  sprintf (index,"RIT_%03d",ind); //Builed updated element key
  pref.putBytes(index,&rfid,(size_t) sizeof(rfid_id_t)); //Update element to NVS
  pref.end(); 
  
  handleRfid();
}

void handleRfidUnpause(){
  unsigned int ind = strtoul(server.arg("row").c_str(),NULL,10);  
  rfid_id_t rfid;
  
  rfid = rfid_list.get(ind);
  rfid.paused = false;
  rfid_list.set(ind, rfid);

  if (!pref.begin(NVS_NAMESAPCE, false)) {
    ESP_LOGE(GEN,"Error init Pref library.");      
    handleRfid();
  }
  char index[8];
  sprintf (index,"RIT_%03d",ind); //Builed updated element key
  pref.putBytes(index,&rfid,(size_t) sizeof(rfid_id_t)); //Update element to NVS
  pref.end(); 

  handleRfid();
}

void handleWifi(){
  checkAuth ();
  char wifi_str[255];
  String r="<div align=\"center\"><h3>Use WiFi connects</h3><label class=\"switch\"><input type=\"checkbox\" id=\"wifi_use\" onchange=\"loadDoc('wifi-connect.txt?'+'switch='+document.getElementById(&quot;wifi_use&quot;).checked)\"";
  if (wifi_use) {
    r += " checked"; //wifi connect use option, either "" or " checked";  
  }
  r += "><span class=\"slider\"></span></label><br><br>";
  r += "<h3>Use WiFi MAC probes</h3> <label class=\"switch\"> <input type=\"checkbox\" id=\"wifiprob_use\" onchange=\"loadDoc('wifi-probes.txt?'+'switch='+document.getElementById(&quot;wifiprob_use&quot;).checked)\"";
  if (wifiprob_use) {
    r += " checked"; //wifi connect use option, either "" or " checked";  
  }
  r += "> <span class=\"slider\"></span> </label></div> <br> <br><div align=\"center\"><form action=\"\" onsubmit=\"loadDoc('wifi-add.txt?'+'mlabel='+document.getElementById(&quot;mlabel&quot;).value+'&mac='+document.getElementById(&quot;mac&quot;).value)\">";
  r += "<table id=\"wifiTable\" align=\"center\" width=\"50%\" style=\"border:none;\">";
  r += "<tr><th></th><th colspan=\"4\" align=\"left\" class=\"cellhead\">WIFI addresses to scan for</th></tr>";
  r += "<tr><th width=5%></th><th width=\"30%\" align=\"left\" class=\"cellhead\">Name</th><th width=\"55%\" align=\"left\" class=\"cellhead\">ID</th><th width=\"5%\" align=\"left\" class=\"cellhead\"></th><th width=\"5%\" align=\"left\" class=\"cellhead\"></th></tr>";
  
  //Here starts rows for wifi data
  for (unsigned int i=0; i<wifi_list.size(); i++) {  
    macs_id_t wifi;
    wifi = wifi_list.get(i);  
    if (wifi.paused) {
      //if paused then one code, if unpaused then another
      r +="<tr>";
      r += "<td class=\"cell\" align=\"right\" style=\"border:none;\">";
      r += String(i+1) + ".";
      r += "</td>";      
      r +="<td class=\"cell-disabled\">";
      r += wifi.name; //Nick name for MAC
      r += "</td><td class=\"cell-disabled\">";
      r += mac2string(wifi.mac,wifi_str); //Value for MAC
      r += "</td><td align=\"center\" class=\"cellcross\" onclick=\"loadDoc('wifi-del.txt?delrow=";
      r += i; //Row id number
      r += "');\">&#10060;</td><td align=\"center\" class=\"cellpaused\" onclick=\"loadDoc('wifi-unpause.txt?row=";
      r += i;
      r += "');\">&#10074;&#10074;</td>";
      r += "</tr>";
    } else {
      r += "<tr>";
      r += "<td class=\"cell\" align=\"right\" style=\"border:none;\">";
      r += String(i+1) + ".";
      r += "</td>";       
      r += "<td class=\"cell\">";
      r += wifi.name; //Nick name for MAC
      r += "</td><td class=\"cell\">";
      r += mac2string(wifi.mac,wifi_str); //Value for MAC
      r += "</td><td align=\"center\" class=\"cellcross\" onclick=\"loadDoc('wifi-del.txt?delrow=";
      r += i;
      r += "');\">&#10060;</td><td align=\"center\" class=\"cellpause\" onclick=\"loadDoc('wifi-pause.txt?row=";
      r += i;
      r += "');\">&#10074;&#10074;</td>";
      r += "</tr>";
    }
  }

  if (wifi_list.size()<MAX_WIFI_LIST_SIZE) 
  { 
    //If not reached max number, then show this code
    r += "<tr>";
    r += "<td class=\"cell\" align=\"right\" style=\"border:none;\"></td>";      
    r += "<td class=\"cell\"><input id=\"mlabel\" type=\"text\" required pattern=\"[0-9A-Za-z]{1,12}\" title=\"Label with up to 12 characters\" style=\"border:none;\"></input></td><td class=\"cell\"><input id=\"mac\" type=\"text\" required pattern=\"([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}\" title=\"11:22:33:44:55:66\" style=\"border:none;\"></input></td><td><input type=\"submit\" value=\"&#10004;\" class=\"celladd\" ></td><td></td>";
    r += "</tr>";
  }
   
  r +="</table></form></div>";
  server.send(200, "text/html", r);  
}

void handleWifiConnect(){
  if (server.arg("switch") == "true") {
    wifi_use=true;
    wificon_found_flag=true;    
  } else {
    wifi_use=false;  
    wificon_found_flag=false;
  }
  if (!pref.begin(NVS_NAMESAPCE, false)) {
    ESP_LOGE(GEN,"Error init Pref library.");
    handleWifi();
  }
  pref.putBool("WUS",wifi_use);
  pref.end(); 

  if (wifi_use) {
    wifi_con_event = WiFi.onEvent(WiFiConEventCB, WiFiEvent_t::SYSTEM_EVENT_AP_STACONNECTED);
//    wifi_discon_event = WiFi.onEvent(WiFiDisconEventCB, WiFiEvent_t::SYSTEM_EVENT_AP_STADISCONNECTED);
  } else {
    WiFi.removeEvent(wifi_con_event);    
//    WiFi.removeEvent(wifi_discon_event);        //Consider not to remove this event in order to be able clear FLAG that phone is connected for having button ON all connected time. 
  }
  
  handleWifi();
}

void handleWifiProbes(){
  if (server.arg("switch") == "true") {
    wifiprob_use=true;
  } else {
    wifiprob_use=false;    
  }
  if (!pref.begin(NVS_NAMESAPCE, false)) {
    ESP_LOGE(GEN,"Error init Pref library.");
    handleWifi();
  }
  pref.putBool("WPB",wifiprob_use);
  pref.end(); 

  if (wifiprob_use) {
    wifiprob_event = WiFi.onEvent(WiFiProbeEventCB, WiFiEvent_t::SYSTEM_EVENT_AP_PROBEREQRECVED);
    esp_wifi_set_event_mask(WIFI_EVENT_MASK_NONE);
  } else {
    WiFi.removeEvent(wifiprob_event);
    esp_wifi_set_event_mask(WIFI_EVENT_MASK_AP_PROBEREQRECVED);        
  }
  handleWifi();
}

void handleWifidAdd(){
  String amac = server.arg("mac");
  amac.toUpperCase();  

  macs_id_t wifi;
  strncpy(wifi.name,server.arg("mlabel").c_str(),12);
  //char amac_str[32];
  //strncpy(amac_str,amac.c_str(),18);
  char * amac_str = strdup(amac.c_str());
  mac2mac(amac_str,wifi.mac);
  free(amac_str);
  wifi.paused=false;
  wifi_list.add(wifi);

  if (!pref.begin(NVS_NAMESAPCE, false)) {
    ESP_LOGE(GEN,"Error init Pref library.");      
    handleWifi();
  }
  pref.putUInt("WSZ",(unsigned int) wifi_list.size()); //Update new list size
  char index[8];
  unsigned int ind=wifi_list.size()-1;
  sprintf (index,"WIT_%03d",ind); //Builed new element key
  pref.putBytes(index,&wifi,(size_t) sizeof(macs_id_t)); //Add new element to NVS
  pref.end(); 
    
  handleWifi();
}

void handleWifiDel(){
  unsigned int ind = strtoul(server.arg("delrow").c_str(),NULL,10);  
  wifi_list.remove(ind);

  if (!pref.begin(NVS_NAMESAPCE, false)) {
    ESP_LOGE(GEN,"Error init Pref library.");      
    handleWifi();
  }
  pref.putUInt("WSZ",(unsigned int) wifi_list.size()); //Update new list size
  char index[8];
  ind=wifi_list.size()-1;
  for (unsigned int i=0;i<=ind;i++) {
    macs_id_t wifi;
    wifi = wifi_list.get(i);        
    sprintf (index,"WIT_%03d",i); //Builed new element key
    pref.putBytes(index,&wifi,(size_t) sizeof(macs_id_t)); //Add new element to NVS
  }
  pref.end();   
  handleWifi();
}

void handleWifiPause(){
  unsigned int ind = strtoul(server.arg("row").c_str(),NULL,10);  
  macs_id_t wifi;
  
  wifi = wifi_list.get(ind);
  wifi.paused = true;
  wifi_list.set(ind, wifi);

  if (!pref.begin(NVS_NAMESAPCE, false)) {
    ESP_LOGE(GEN,"Error init Pref library.");      
    handleWifi();
  }
  char index[8];
  sprintf (index,"WIT_%03d",ind); //Builed updated element key
  pref.putBytes(index,&wifi,(size_t) sizeof(macs_id_t)); //Update element to NVS
  pref.end(); 
    
  handleWifi();

}

void handleWifiUnpause(){
  unsigned int ind = strtoul(server.arg("row").c_str(),NULL,10);  
  macs_id_t wifi;
  
  wifi = wifi_list.get(ind);
  wifi.paused = false;
  wifi_list.set(ind, wifi);

  if (!pref.begin(NVS_NAMESAPCE, false)) {
    ESP_LOGE(GEN,"Error init Pref library.");      
    handleWifi();
  }
  char index[8];
  sprintf (index,"WIT_%03d",ind); //Builed updated element key
  pref.putBytes(index,&wifi,(size_t) sizeof(macs_id_t)); //Update element to NVS
  pref.end();   
  handleWifi();
}

void handleOta(){
  checkAuth ();
  String r = "<form onsubmit=\"uploadFile()\" method=\"POST\" enctype='multipart/form-data'><p align=\"center\">";
  r += "<input type=\"file\" name=\"update\" id=\"update\" required class=\"button\" style=\"width:50%\">";
  r += "<br><input type=\"submit\" value=\"Start Upgrade\" class=\"button\" style=\"width:25%\">";
  r += "<br><progress id=\"progressBar\" value=\"0\" max=\"100\" style=\"width:50%;\"></progress></p>";
  r += "<br><p align=\"center\"><h3 id=\"status\"></h3></p>";
  r += "<br><p align=\"center\" id=\"loaded_n_total\"></p>";
  r += "</form>";
  
  server.send(200, "text/html", r); 

}

void handleStartOta(){
  String r = "<html><head><h3>OTA update result</h3></head><body>";
  r += (Update.hasError()) ? "FAIL" : "OK";
  r += "<br><br>Restaring in 5 seconds...</body></html>";
  server.sendHeader("Connection", "close");
  server.send(200, "text/html", r);  
  delay(5000);
  ESP.restart();
}

void handleUploadFile(){
  HTTPUpload& upload = server.upload();
  
  stopAllScans();
  
  if (upload.status == UPLOAD_FILE_START) {
    ESP_LOGI(GEN,"Update: %s\n", upload.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    /* flashing firmware to ESP*/
    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Update.printError(Serial);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (Update.end(true)) { //true to set the size to the current progress
      ESP_LOGI(GEN,"Update Success: %u\nRebooting...\n", upload.totalSize);
    } else {
      Update.printError(Serial);
    }
  }
}
     
void handleLogs(){
  checkAuth ();

  String r = "<style>ul li:hover {  background-color: #ddd; }.mylist a {  display: block;  text-decoration: none;  color: #000000;    padding: 5px 10px;  }.act {  background-color: #0099ff;}</style>";
  r += "<br><div style=\"margin: auto; width:90%;\"><div style=\"margin: auto; display: inline-block; *display: inline; width:25%; vertical-align: top;\"><h3>Files list</h3>";
  r += "<ul id=\"mylisttop\" style=\"list-style-type:none; padding-left: 10px;\">";

  File root = SPIFFS.open(LOG_PATH);
  if(root){
    if(root.isDirectory()){
      File file = root.openNextFile();
      int id=1;
      while(file){
          if(!file.isDirectory()){
              r += "<li class=\"mylist\" id=\"";
              r += String(id);
              r += "\"><a id=\"";
              r += String(id);
              r += "a\" href=\"#";
              r += String(id);          
              r += "\" onclick='loadFile(";
              r += String(id);          
              r += ")'>";
              r += String(file.name());
              r +="</a></li>";          
          } else {
            ESP_LOGD(GEN,"Skipping %s as it is directory",file.name());
          }
          file.close();
          file = root.openNextFile();
          id++;
      }      
    } else {
       ESP_LOGD(GEN,"%s is not a directory",LOG_PATH);
    }
    root.close();    
  } else {
    ESP_LOGE(GEN,"Failed to open directory %s",LOG_PATH);
  }
  
  r += "</ul></div><div style=\"margin: auto; display: inline-block; *display: inline; width:70%; vertical-align: top;\">";
  r += "<textarea id=\"filehere\" style=\"resize:none; width: 100%\" rows=\"30\" readonly></textarea></div></div>";
  r += "<div style=\"text-align: center; padding:20px 0px;\"><div style=\"display: inline-block; padding:0px 20px;\">";
  r += "<a href=\"#5\" id=\"downurl\" download><button class=\"button\" onclick='downActive()'>Download selected</button></a>";
  r += "</div><div style=\"display: inline-block; padding:0px 20px;\">";
  r += "<a href=\"#6\"><button class=\"button\" onclick='delActive()'>Delete selected</button></a>";
  r += "</div></div>";
  
  server.send(200, "text/html", r); 

}

void handleLoadFile() {
  String file_name=server.arg("file");
  File file = SPIFFS.open(file_name,FILE_READ);
  if (file) {
    size_t sent = server.streamFile(file,"text/plain");
    ESP_LOGD(GEN,"Send file %s content with size %d to client.",file_name.c_str(),sent);    
    file.close();
  } else {
    ESP_LOGE(GEN,"Error opening file %s for reading",file_name.c_str());
  }
}

void handleLogsDel(){
  String file_name=server.arg("file");
  if (SPIFFS.remove(file_name)) {
    ESP_LOGI(GEN,"File %s removed from WEB GUI",file_name.c_str());
    restartSPIFFS();
  } else {
    ESP_LOGE(GEN,"Error removing file %s from WEB GUI",file_name.c_str());    
  }
  
  handleLogs();
}

void handleLogsDown() {
  String file_name=server.arg("file"); 
  File file = SPIFFS.open(file_name,FILE_READ);
  if (file) {
    server.sendHeader("Content-Description","File Transfer");
    server.sendHeader("Content-Type","application/octet-stream");
    String tmp = String(file.name());
    tmp = "attachment; filename=\"" + tmp.substring(1) + "\"";
    server.sendHeader("Content-Disposition",tmp);
    server.sendHeader("Content-Transfer-Encoding","binary");
    server.sendHeader("Expires","0");
    server.sendHeader("Cache-Control","must-revalidate");
    server.sendHeader("Pragma","public");
    server.sendHeader("Content-Length",String(file.size()));    
    size_t sent = server.streamFile(file,"application/octet-stream");
    ESP_LOGD(GEN,"Send file %s content with size %d to client.",file_name.c_str(),sent);    
    file.close();
  } else {
    ESP_LOGE(GEN,"Error opening file %s for reading",file_name.c_str());
  }  
}
/*
void initWiFi() {
  wifi_config_t ap_config;
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    
  ESP_ERROR_CHECK(esp_wifi_init(&cfg)); //if not stop as critical error
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM)); //if not stop as critical error
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP)); //if not stop as critical error

  strncpy(ap_config.ap.ssid, ssid_name, 32);
  strncpy(ap_config.ap.password, ssid_pass, 32);
  ap_config.ap.ssid_len = strlen(ssid_name);
  ap_config.ap.ssid_len.channel = 0,
  ap_config.ap.ssid_len.authmode = WIFI_AUTH_WPA2_PSK,
  ap_config.ap.ssid_len.max_connection = 4,
  ap_config.ap.ssid_len.beacon_interval = 100,      
  if (ssid_show) {
    ap_config.ap.ssid_hidden = 0;
  } else {
    ap_config.ap.ssid_hidden = 1;    
  }
  
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config)); //if not stop as critical error
  ESP_ERROR_CHECK( esp_wifi_start() ); //if not stop as critical error
}
*/

void restartWiFi() {
  ESP_LOGI(GEN,"Disconnecting WiFi AP mode");  
  WiFi.softAPdisconnect();
  
  ESP_LOGI(GEN,"Configuring access point with SSDI=%s, PASS=%s, SSID_Hidden=%d",ssid_name,ssid_pass,(uint8_t)(!ssid_show));
  //Instead of this write own function
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid_name, ssid_pass,wifi_channel,(uint8_t)(!ssid_show),4);

  ESP_LOGI(GEN,"Setting TX power to %d",ssid_tx);
  setTX(ssid_tx);
  
  ESP_LOGI(GEN,"Wait 100 ms for AP_START...");
  delay(100);
  
  ESP_LOGI(GEN,"Set softAPConfig");
  IPAddress Ip(192, 168, 1, 1);
  IPAddress NMask(255, 255, 255, 0);
  WiFi.softAPConfig(Ip, Ip, NMask);

  ESP_LOGI(GEN,"Set Hostname");
  WiFi.softAPsetHostname(ssid_name);

  IPAddress myIP = WiFi.softAPIP();
  ESP_LOGI(GEN,"AP IP address: %s",myIP.toString().c_str());  

  MDNS.end();
  if (!MDNS.begin(ssid_name)) {
    ESP_LOGI(GEN,"Error setting up MDNS responder!");
  }
  MDNS.addService("http", "tcp", 80);

}

void WiFiProbeEventCB(WiFiEvent_t event, WiFiEventInfo_t info)
{
    char st[24];
    ESP_LOGI(WIFI,"[WiFi-event] event: SYSTEM_EVENT_AP_PROBEREQRECVED for MAC: %s with RSSI: %d",mac2string(info.ap_probereqrecved.mac,st),info.ap_probereqrecved.rssi);

    for (unsigned int i=0; i<wifi_list.size(); i++) {
      macs_id_t wifi;
      wifi = wifi_list.get(i);
      uint8_t compare_result = strncmp((char*)info.ap_probereqrecved.mac,(char*)wifi.mac,6);
      if (compare_result == 0) {
        ESP_LOGI(WIFI,"MAC: %s matched WIFI_LIST[%d] mac:%s",mac2string(info.ap_probereqrecved.mac,st),i,mac2string(wifi.mac,st));
        //Writing to the SPIFFS log file
        writetolog("WP",wifi.name,st);        
        wifiprob_found_flag=true;
      }
    }
}

void WiFiConEventCB(WiFiEvent_t event, WiFiEventInfo_t info)
{
    char st[24];
    ESP_LOGI(WIFI,"[WiFi-event] event: SYSTEM_EVENT_AP_STACONNECTED for MAC: %s",mac2string(info.sta_connected.mac,st));    

    //Writing to the SPIFFS log file
    writetolog("WC","CONNECT",st);
    wificon_found_flag=true;
}

void WiFiDisconEventCB(WiFiEvent_t event, WiFiEventInfo_t info)
{
    char st[24];
    uint8_t sta_num=WiFi.softAPgetStationNum();
    
    ESP_LOGI(WIFI,"[WiFi-event] event: SYSTEM_EVENT_AP_STADISCONNECTED for MAC: %s. Still connected stations: %d",mac2string(info.sta_disconnected.mac,st),sta_num);
    //Writing to the SPIFFS log file
    writetolog("WD","DISCONNECT",st);

    if (sta_num == 0) {      
      wificon_found_flag=false;
    }
}

void stopAllScans(){
  if (wifiprob_use) {
    WiFi.removeEvent(wifiprob_event);
  }
  esp_wifi_set_event_mask(WIFI_EVENT_MASK_AP_PROBEREQRECVED);          
}

void pressButton () {
    //Stop all bluetooth PINGs with setting flag
    
    if (!buttonPressFlag) {
      ESP_LOGI(BTAG,"Pushing button for %d milliseconds.",pRESSED_TIME);                        
    }
    if (!relay_inverted) {
      digitalWrite(PIN_BUT, HIGH); //Set button high //COnsider option for setting this LOW, while keeping it HIGH rest of time
    } else {
      digitalWrite(PIN_BUT, LOW); //Set button high //COnsider option for setting this LOW, while keeping it HIGH rest of time      
    }
    buttonPressTime=millis();
    buttonPressFlag=true;
//    delay (pRESSED_TIME);
//    ESP_LOGI(BTAG,"Releasing button and waiting for %d milliseconds",rELEASED_TIME);
//    digitalWrite(PIN_BUT, 0); //Set button low                     
//    delay (rELEASED_TIME); 
    //Continue bluetooth PINGs with re-setting flag    
}

void restartSPIFFS () {
  //return; //no need to restart
  ESP_LOGD(GEN,"Starting/Restarting SPIFFS.");  
  SPIFFS.end();
  if(!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)){
      ESP_LOGE(GEN,"SPIFFS Mount Failed");
      no_log=true;
  } else {
    no_log=false;
  }
}

void startBuzzer(double fr) {
  ledcWriteTone(buzzer_channel, fr);  
  ledcWrite(buzzer_channel, buzzer_volume);  
  buzzerStartTime=millis();
  buzzerStartedFlag=true;
  ESP_LOGD(GEN,"Buzzer started. Freq=%f, Volume=%u",fr,buzzer_volume);      
}

void stopBuzzer() {
  ledcWrite(buzzer_channel, 0);    
  buzzerStartedFlag=false;  
  ESP_LOGD(GEN,"Buzzer stopped");        
}

void heartbeat() {
  if (millis()-heartbeatLedTime >= hEARTBEAT_TIME) {
    switch (heartbeatLedStatus) {
      case false: 
        digitalWrite(PIN_HB_LED,HIGH);    
        heartbeatLedStatus=true;
        heartbeatLedTime = millis();
        break;
      case true: 
        digitalWrite(PIN_HB_LED,LOW);    
        heartbeatLedStatus=false;
        heartbeatLedTime = millis();      
        break;
    }
  }
}

void resetDevice() {
  //erase all keys from NVS
  if (!pref.begin(NVS_NAMESAPCE, false)) {
    ESP_LOGE(GEN,"Error init Pref library.");      
    return;
  }
  writetolog("ST","RESET","Erasing all key values in NVS");
  if (!pref.clear()) {
    ESP_LOGE(GEN,"Error clearing key values from NVS.");          
  }
  pref.end();  
}

void checkReset() {
  //init variable on loop start
  uint8_t resetState = 1;
  resetPressedTime = 0;
  do {
    resetState = digitalRead(PIN_RESET_BUT);
    if (resetState == LOW) {
      //record reset pressed time
      if (resetPressedTime == 0) {
        resetPressedTime = millis();
      }
      //check if time passed with pressed reset button
      if (millis()-resetPressedTime >= rESET_TIME) {
        //log that device is resetted to deault settings.
        ESP_LOGI(GEN,"Reset button pressed for more then 3000 seconds");                  
        writetolog("ST","RESET","Reset to defaults.");
        resetDevice();
        delay(500);
        ESP.restart();
      }
      //start blinking yellow led much faster, and start counting 3 seconds
      if (millis()-heartbeatLedTime >= hEARTBEAT_TIME_RESET) {
        switch (heartbeatLedStatus) {
          case false: 
            digitalWrite(PIN_HB_LED,HIGH);    
            heartbeatLedStatus=true;
            heartbeatLedTime = millis();
            break;
          case true: 
           digitalWrite(PIN_HB_LED,LOW);    
            heartbeatLedStatus=false;
            heartbeatLedTime = millis();      
            break;
        }
      }   
    }
  } while (resetState == LOW); //loop until reset button unpressed
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);

  //Init RTC clock. 
  rtc.init();
  if (rtc.isHalted()) {
    ESP_LOGI(GEN,"DS1302 not attached. DateTime is not available");    
  } else {
    char dt[32];
    getTime(dt);
    ESP_LOGI(GEN,"Reading time from DS1302 - %s. If it is not correct set it via WEB GUI",dt);
  }
  
  ESP_LOGI(GEN,"Running %s, Compiled at %s %s, Version %s",__FILE__, __DATE__, __TIME__, __VERSION__);
  
  if (!readNVS()) { //if result is not true, then error
    ESP_LOGE(GEN,"FATAL Error reading configuration data. Will try to restart in 5 seconds.");
    fflush(stdout);
    delay(5000);
    ESP.restart();    
  }
  ESP_LOGI(GEN,"Done reading configuration data");

  restartWiFi();

/*  ESP_LOGI(GEN,"Starting ArduinoOTA");
  ArduinoOTA.setHostname(ssid_name);

  ArduinoOTA
    .onStart([]() {
      stopAllScans();
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
          ESP_LOGI(GEN,"ArduinoOTA update finished. Rebooting in 5 seconds");
          stopAllScans();
          delay(5000);
          ESP.restart();  
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      ESP_LOGI(GEN,"Progress: %u%%\r", (progress / (total / 100)));      
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) ESP_LOGE(GEN,"Auth Failed");
      else if (error == OTA_BEGIN_ERROR) ESP_LOGE(GEN,"Begin Failed");
      else if (error == OTA_CONNECT_ERROR) ESP_LOGE(GEN,"Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) ESP_LOGE(GEN,"Receive Failed");
      else if (error == OTA_END_ERROR) ESP_LOGE(GEN,"End Failed");
    });  
  ArduinoOTA.begin();
*/
  server.on("/", handleRoot);
  server.on("/rfidscanned.txt",handlerfidscanned);
  server.on("/index.html", handleRoot);  
  server.on("/index.htm", handleRoot);    
  server.on("/general.txt", handleGeneral);
  server.on("/gen-ssid.txt", HTTP_GET, handleGenSSID);
  server.on("/gen-admin.txt", HTTP_GET, handleGenAdmin);  
  server.on("/gen-relay.txt", HTTP_GET, handleGenRelay);
  server.on("/gen-buzzer.txt", HTTP_GET, handleBuzzer);  
  server.on("/gen-date.txt", HTTP_GET, handleGenDate);
  
  server.on("/rfid.txt", handleRfid);
  server.on("/rfid-switch.txt", handleRfidSwitch);
  server.on("/rfid-add.txt", handleRfidAdd);
  server.on("/rfid-del.txt", handleRfidDel);
  server.on("/rfid-pause.txt", handleRfidPause);
  server.on("/rfid-unpause.txt", handleRfidUnpause);

  server.on("/wifi.txt", handleWifi);
  server.on("/wifi-connect.txt", handleWifiConnect);
  server.on("/wifi-probes.txt", handleWifiProbes);  
  server.on("/wifi-add.txt", handleWifidAdd);
  server.on("/wifi-del.txt", handleWifiDel);
  server.on("/wifi-pause.txt", handleWifiPause);
  server.on("/wifi-unpause.txt", handleWifiUnpause);

  server.on("/ota.txt", handleOta);
  server.on("/start_ota.html", HTTP_POST, handleStartOta, handleUploadFile);  
      
  server.on("/logs.txt", handleLogs);
  server.on("/loadFile.txt", handleLoadFile);
  server.on("/delActive.txt", handleLogsDel);  
  server.on("/downFile.txt", handleLogsDown);    

  ESP_LOGI(GEN,"Starting WebServer");
  server.begin();

//Init RFID libs
  SPI.begin(); // Init SPI bus
  mfrc522.PCD_Init(); // Init MFRC522

  wifiprob_found_flag=false;
  if (wifiprob_use) {
    wifiprob_event = WiFi.onEvent(WiFiProbeEventCB, WiFiEvent_t::SYSTEM_EVENT_AP_PROBEREQRECVED);
    esp_wifi_set_event_mask(WIFI_EVENT_MASK_NONE);
  }

  wificon_found_flag=false;
  if (wifi_use) {
    wifi_con_event = WiFi.onEvent(WiFiConEventCB, WiFiEvent_t::SYSTEM_EVENT_AP_STACONNECTED);
  }
  wifi_discon_event = WiFi.onEvent(WiFiDisconEventCB, WiFiEvent_t::SYSTEM_EVENT_AP_STADISCONNECTED);

  free_mem_time=0;

//Setup Relay (button) output
  if (!relay_inverted) {
    digitalWrite(PIN_BUT, LOW); 
  } else {
    digitalWrite(PIN_BUT, HIGH);
  }  
  pinMode( PIN_BUT, OUTPUT);

//Setup buzzer
  ledcSetup(buzzer_channel, buzzer_good_freq, buzzer_resolution);
  ledcAttachPin(BUZZER_PIN, buzzer_channel);

//Setup heartbeat LED
  digitalWrite( PIN_HB_LED, LOW);
  pinMode( PIN_HB_LED, OUTPUT);  
  heartbeatLedStatus=false;

//Setup reset button
  pinMode(PIN_RESET_BUT, INPUT_PULLUP);
  
//Setup SPIFFS for logs
  restartSPIFFS();

  writetolog("ST","STARTED","STARTED");    

  //End of setup()
}

void loop() {
  // put your main code here, to run repeatedly:
//  ArduinoOTA.handle();
  server.handleClient();

  //Process RFID
  if (rfid_use) {
    if ( mfrc522.PICC_IsNewCardPresent()) {
      if (mfrc522.PICC_ReadCardSerial()) {
        uint8_t uid[12];
  
        //instructs the PICC when in the ACTIVE state to go to a "STOP" state
        mfrc522.PICC_HaltA(); 
        // "stop" the encryption of the PCD, it must be called after communication with authentication, otherwise new communications can not be initiated
        mfrc522.PCD_StopCrypto1();  
  
        //We support only 4 least significant bytes, so do compare with only them
        unsigned long uid_src_l = (mfrc522.uid.uidByte[mfrc522.uid.size-4]<<24) | (mfrc522.uid.uidByte[mfrc522.uid.size-3]<<16) |(mfrc522.uid.uidByte[mfrc522.uid.size-2]<<8) | (mfrc522.uid.uidByte[mfrc522.uid.size-1]);        
  
        //Set last scanned RFID value to show on the page with auto update.
        sprintf(rfidscanned,"Last scanned RFID id: DEC-%010lu HEX-%08lX",uid_src_l,uid_src_l);
        ESP_LOGI(RFID,"%s",rfidscanned);      
        
        for (unsigned int i=0; i<rfid_list.size(); i++) {
          rfid_id_t rfid;
          rfid = rfid_list.get(i);
          if (!rfid.paused) {
            if (rfid.uid == uid_src_l) {
              char st[32];
              ESP_LOGI(RFID,"ID: %s matched RFID_LIST[%d]",rfid2string(uid_src_l,st),i);
              //Writing to the SPIFFS log file
              writetolog("RF",rfid.name,st);
              rfid_found_flag=true;
            }
          }
        }
        if (rfid_found_flag) {
          startBuzzer(buzzer_good_freq);
        } else {
          startBuzzer(buzzer_bad_freq); 
        }
      }   
    }
  }
  
  if (rfid_found_flag || wificon_found_flag || wifiprob_found_flag) {
    //press button
    if (on_extend) {
      pressButton();
    } else {
      if (!buttonPressFlag) {
        pressButton();
      }
    }

//    blueping_found_flag=false;        
    wifiprob_found_flag=false;
    //No wificon_found_flag=false, as it is unset in DISCONNECT WIFI EVENT function    
    rfid_found_flag=false;
  }

  if (buttonPressFlag) {
    if (millis()-buttonPressTime >= pRESSED_TIME) {
      buttonPressFlag=false;
      ESP_LOGI(BTAG,"Releasing button and waiting for %d milliseconds",rELEASED_TIME);
      if (!relay_inverted) {
        digitalWrite(PIN_BUT, LOW); 
      } else {
        digitalWrite(PIN_BUT, HIGH);
      }
      
      buttonReleaseFlag=true;
      buttonReleaseTime=millis();
    }
  }
  if (buttonReleaseFlag) {
    if (millis()-buttonReleaseTime >= rELEASED_TIME) {
      buttonReleaseFlag=false;      
    }
  }
  
  if (buzzerStartedFlag) {
    if (millis()-buzzerStartTime >= bUZZER_TIME) {
      stopBuzzer();
    }
  }

  checkReset();

  heartbeat();

  //Some debug here each 1 seconds
  if (millis()-free_mem_time >= DEBUG_MEM_PRINT_DELAY) {
    ESP_LOGD(GEN,"Free mem: %d bytes",ESP.getFreeHeap());
    free_mem_time=millis();
  }
}
