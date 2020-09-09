#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <UI_stmIPM.h>
#include <SD.h>
String  webpage = "";
const char* ssid     = "jomblo";
const char* password = "hilman123";
#include "CSS.h"
#include "FS.h"
#include <SPI.h>

typedef struct {
  int    lcnt; // Sequential log count
  String ltime; // Time record of when reading was taken
  int kondisi;
  int temp;  // Temperature values, short unsigned 16-bit integer to reduce memory requirement, saved as x10 more to preserve 0.1 resolution
  int ibus;
  int vbus;
  int pbus;
  int kecepatan;
  int total_waktu;
} record_type;

int index_ptr,log_count, max_temp, min_temp;
int log_time_unit;
int const table_size     = 72;
record_type sensor_data[table_size+1];
uint16_t vBus = 0, pBus = 0, iBus = 0;
int16_t kecepatan = 0,  temp_kulkas = 0;
uint8_t Status = 0, kontrol = 0, gpio=0, io_pin=0;
long interval_update,log_interval=120000;
long time_last = 0, time_last1 = 0;
ESP8266WebServer server(80);
bool SPIFFS_present = false;
bool      AScale, auto_smooth, AUpdate;
bool log_delete_approved,update_speed=0;
String lastcall,DataFile = "/datalog.csv";
String motorState = "STOP",kondisi;
int16_t speed_in=0;
int16_t speed_default=3000;

void Motor_Control();
void Homepage();
void display_data_sensor();
void SPIFFS_dir();
void ReportSPIFFSNotPresent();
void SendHTML_Stop();
void SendHTML_Header();
void SendHTML_Content();
void StartSPIFFS();
void File_Delete();
void SPIFFS_file_delete(String filename);
void SelectInput(String heading1, String command, String arg_calling_name);
void ReportFileNotPresent(String target);
void DownloadFile(String filename);
void File_Download();
void systemSetup();

void setup() 
{
  Serial.begin(115200); // Set serial port speed
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);
  Serial.println(WiFi.localIP());
  StartSPIFFS();

  server.on("/",Homepage);
  server.on("/data",display_data_sensor);
  server.on("/dir",SPIFFS_dir);
  server.on("/delete",File_Delete);
  server.on("/download",File_Download);
  server.on("/setting",systemSetup);
  ui_IPM_init(9600, 10, 5000);
  
  server.begin();
  Serial.println("HTTP server started");
  if (!SPIFFS.begin()) 
    {
      Serial.println("SPIFFS initialisation failed...");
      SPIFFS_present = false; 
    }
  else
    {
      Serial.println(F("SPIFFS initialised... file access enabled..."));
      SPIFFS_present = true; 
    }
  File datafile = SPIFFS.open(DataFile, "a+");
    if (datafile == true) 
    { // if the file is available, write to it
      datafile.println("No;status;Kecepatan;iBus;vBus;pBus;Temperature."); // TAB delimited
      Serial.println(((log_count<10)?"0":"")+String(log_count)+" New Record Added");
    }
    datafile.close();
    
}
void loop() {
  server.handleClient();
  Motor_Control();

}

void Motor_Control()
{
  if (Serial.available())
  {
    kontrol = Serial.read();
    Serial.println(kontrol);
  }

  if (kontrol == 'a'||kondisi=="on"||kondisi=="ON")
  {
    delay(10);
    exec_commandFrame(CMD_FAULT_ACK);
    delay(10);
    exec_commandFrame(CMD_START_MOTOR);
  }
  else if (kontrol == 'b'||kondisi=="off"||kondisi=="OFF")exec_commandFrame(CMD_STOP_MOTOR);
  kontrol = 0;

  if (update_speed)
  {
    delay(1);
    exec_rampFrame(speed_in, 6000);
    update_speed = 0;
  }
  if(kecepatan>0){
    kondisi="OFF";
  }
interval_update=log_interval;
  if ((millis() - time_last) >= interval_update)
  {
    Status = uFrame_readVal(REG_STATUS);
    if (Status == 10 || Status == 11)
    {
      delay(10);
      exec_commandFrame(CMD_FAULT_ACK);
    }
    else if (Status == 6)
    { 
      motorState = "RUN";
    }
    else if (Status == 4)
    {
      motorState = "START";
    }
    else 
      {
        motorState = "STOP";
      }
      
    delay(1);
    io_pin = 1;
    delay(1);
    pBus = 150; // Daya motor dalam satuan W
    delay(1);
    vBus = 600; // Tegangan motor dalam satuan V
    delay(1);
    kecepatan = 6004; // Daya motor dalam satuan W
    delay(1);
    temp_kulkas = -20;
    Status=10;
    // delay(1);
    // io_pin = uFrame_readVal(REG_GPIO);
    // delay(1);
    // pBus = uFrame_readVal(REG_MOTOR_POWER); // Daya motor dalam satuan W
    // delay(1);
    // vBus = uFrame_readVal(REG_BUS_VOLTAGE); // Tegangan motor dalam satuan V
    // delay(1);
    // kecepatan = sFrame_readVal(REG_SPEED_MEAS); // Daya motor dalam satuan W
    // //delay(1);
    // //temp_kulkas = sFrame_readVal(REG_ADC_TEMP);
    // temp_kulkas =0;

    if (vBus != 0)iBus = (float)((pBus * 1000) / vBus); // arus motor dalam satuan mA
    else iBus = 0;
    if(io_pin) gpio=1;
    else gpio=0;

    Serial.print("SW:");
    Serial.print(gpio);
    Serial.print("\tV:");
    Serial.print(vBus);
    Serial.print("v\tI:");
    Serial.print(iBus);
    Serial.print("mA\tP:");
    Serial.print(pBus);
    Serial.print("W\tS:");
    Serial.print(kecepatan);
    Serial.print("rpm\tT:");
    Serial.print(temp_kulkas);
    Serial.println("'C");
    
    log_count += 1; 
    sensor_data[index_ptr].lcnt      = log_count;  // Record current log number, time, temp and humidity readings 
    sensor_data[index_ptr].kondisi   = Status;
    sensor_data[index_ptr].temp      = temp_kulkas;
    sensor_data[index_ptr].kecepatan = kecepatan;
    sensor_data[index_ptr].vbus      = vBus;
    sensor_data[index_ptr].ibus      = iBus;
    sensor_data[index_ptr].pbus      = pBus;
    File datafile = SPIFFS.open(DataFile, "a+");
    if (datafile == true) 
    { // if the file is available, write to it
      datafile.println(((log_count<10)?"0":"")+String(log_count)+";"+String(Status)+";"+String(kecepatan)+";"+String(iBus)+";"+String(vBus)+";"+String(pBus)+";"+String(temp_kulkas)+"."); // TAB delimited
      Serial.println(((log_count<10)?"0":"")+String(log_count)+" New Record Added");
    }
    datafile.close();
    index_ptr += 1;
    if (index_ptr > table_size) 
      { 
        index_ptr = table_size;
        for (int i = 0; i < table_size; i++) 
          { 
            sensor_data[i].lcnt      = sensor_data[i+1].lcnt;  
            sensor_data[i].kondisi   = sensor_data[i+1].kondisi;
            sensor_data[i].temp      = sensor_data[i+1].temp;
            sensor_data[i].kecepatan = sensor_data[i+1].kecepatan;
            sensor_data[i].vbus      = sensor_data[i+1].vbus;
            sensor_data[i].ibus      = sensor_data[i+1].ibus;
            sensor_data[i].pbus      = sensor_data[i+1].pbus;
          }
        sensor_data[index_ptr].lcnt      = log_count;  // Record current log number, time, temp and humidity readings 
        sensor_data[index_ptr].kondisi   = Status;
        sensor_data[index_ptr].temp      = temp_kulkas;
        sensor_data[index_ptr].kecepatan = kecepatan;
        sensor_data[index_ptr].vbus      = vBus;
        sensor_data[index_ptr].ibus      = iBus;
        sensor_data[index_ptr].pbus      = pBus; 
      }
    time_last = millis();
  }
}

void Homepage()
{
  log_delete_approved = false; // Prevent accidental SD-Card deletion
  SendHTML_Header();
  webpage += F("<br>");
  webpage += F("<table border='3'>");
  webpage += F("<tr>");
  webpage += F("<td><span class='sensor-labels'>Kondisi</span></td>");
  webpage += ("<td>"+ String(Status)+"</td>"); // tampilkan ID
  webpage += F("</tr>");
  webpage += F("<tr>");
  webpage += F("<td><span class='sensor-labels'>Kecepatan</span></td>");
  webpage += ("<td>"+ String(kecepatan)+"</td>");
  webpage += F("</tr>");
  webpage += F("<tr>");
  webpage += F("<td><span class='sensor-labels'>Arus</span></td>");
  webpage += ("<td>"+ String(iBus)+"</td>");
  webpage += F("</tr>");
  webpage += F("<tr>");
  webpage += F("<td><span class='sensor-labels'>Tegangan</span></td>");
  webpage += ("<td>"+ String(vBus)+"</td>");
  webpage += F("</tr>");
  webpage += F("<tr>");
  webpage += F("<td><span class='sensor-labels'>Daya</span></td>");
  webpage += ("<td>"+ String(pBus)+"</td>");
  webpage += F("</tr>");
  webpage += F("<tr>");
  webpage += F("<td><span class='sensor-labels'>Suhu</span></td>");
  webpage += ("<td>"+ String(temp_kulkas)+"</td>");
  webpage += F("</tr>");
  webpage += F("</table>");
  webpage += F("<br>");
  webpage += F("<br>");
  append_page_footer();
  SendHTML_Content();
  SendHTML_Stop();
}
void SendHTML_Header()
{
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate"); 
  server.sendHeader("Pragma", "no-cache"); 
  server.sendHeader("Expires", "-1"); 
  server.setContentLength(CONTENT_LENGTH_UNKNOWN); 
  server.send(200, "text/html", ""); // Empty content inhibits Content-length header so we have to close the socket ourselves. 
  append_page_header();
  server.sendContent(webpage);
  webpage = "";
}
void display_data_sensor()
{
 log_delete_approved = false; // Prevent accidental SD-Card deletion
  webpage = ""; // don't delete this command, it ensures the server works reliably!
  append_page_header();
  webpage += F("<table border='3'>");
  webpage += F("<tr>");
  webpage += F("<th><span class='sensor-labels'>ID</span></th>");
  webpage += F("<th><span class='sensor-labels'> Arus</span></th>");
  webpage += F("<th><span class='sensor-labels'>Tegangan</span></th>");
  webpage += F("<th><span class='sensor-labels'>Daya</span></th>");
  webpage += F("<th><span class='sensor-labels'>Suhu</span></th>");
  webpage += F("<th><span class='sensor-labels'>Kecepatan</span></th>");
  webpage += F("</tr>");
   for (int i = 0; i < index_ptr; i=i+1) {
     webpage += F("<tr>");
     webpage += ("<td>"+ String(sensor_data[i].lcnt)+"</td>"); // tampilkan ID
     webpage +=("<td>"+String((sensor_data[i].ibus))+"</td>");// tampilkan proses
     webpage +=("<td>"+String((sensor_data[i].vbus))+"</td>");// tampilkan tahap
     webpage +=("<td>"+String((sensor_data[i].pbus))+"</td>");// tampilkan suhu
     webpage +=("<td>"+String((sensor_data[i].temp))+"</td>");// tampilkan data waktu record
     webpage +=("<td>"+String((sensor_data[i].kecepatan))+"</td>");// tampilkan suhu
     webpage += F("<tr>");
   }
  webpage += F("</table>");
  append_page_footer();
  server.send(200, "text/html", webpage);
  webpage = "";
  lastcall = "temp_humi";
}

void SPIFFS_dir()
{
  String str;
  if (SPIFFS_present) 
  { 
    Dir dir = SPIFFS.openDir("/");
    SendHTML_Header();
    webpage += F("<h3 class='rcorners_m'>SPIFFS Memory Contents</h3><br>");
    webpage += F("<table align='center'>");
    webpage += F("<tr><th>Name/Type</th><th style='width:40%'>File Size</th></tr>");
    while (dir.next()) 
    {
      Serial.print(dir.fileName());
      webpage += "<tr><td>"+String(dir.fileName())+"</td>";
      str  = dir.fileName();
      str += " / ";
      if(dir.fileSize()) 
      {
        File f = dir.openFile("r");
        Serial.println(f.size());
        int bytes = f.size();
        String fsize = "";
        if (bytes < 1024)                     fsize = String(bytes)+" B";
        else if(bytes < (1024 * 1024))        fsize = String(bytes/1024.0,3)+" KB";
        else if(bytes < (1024 * 1024 * 1024)) fsize = String(bytes/1024.0/1024.0,3)+" MB";
        else                                  fsize = String(bytes/1024.0/1024.0/1024.0,3)+" GB";
        webpage += "<td>"+fsize+"</td></tr>";
        f.close();
      }
        str += String(dir.fileSize());
        str += "\r\n";
        Serial.println(str);
    }
    webpage += F("</table>");
    SendHTML_Content();
    append_page_footer();
    SendHTML_Content();
    SendHTML_Stop();   // Stop is needed because no content length was sent
  } else ReportSPIFFSNotPresent();
}

void SendHTML_Content()
{
  server.sendContent(webpage);
  webpage = "";
}

void SendHTML_Stop()
{
  server.sendContent("");
  server.client().stop(); // Stop is needed because no content length was sent
}

void ReportSPIFFSNotPresent()
{
  SendHTML_Header();
  webpage += F("<h3>No SPIFFS Card present</h3>"); 
  webpage += F("<a href='/'>[Back]</a><br><br>");
  append_page_footer();
  SendHTML_Content();
  SendHTML_Stop();
}

void StartSPIFFS()
{
  boolean SPIFFS_Status;
  SPIFFS_Status = SPIFFS.begin();
  if (SPIFFS_Status == false)
  { // Most likely SPIFFS has not yet been formated, so do so
    #ifdef ESP8266
      Serial.println("Formatting SPIFFS Please wait .... ");
      if (SPIFFS.format() == true) Serial.println("SPIFFS formatted successfully");
      if (SPIFFS.begin() == false) Serial.println("SPIFFS failed to start...");
    #else
      SPIFFS.begin();
      File datafile = SPIFFS.open("/"+DataFile, FILE_READ);
      if (!datafile || !datafile.isDirectory()) {
        Serial.println("SPIFFS failed to start..."); // If ESP32 nothing more can be done, so delete and then create another file
        SPIFFS.remove("/"+DataFile); // The file is corrupted!!
        datafile.close();
      }
    #endif
  } else Serial.println("SPIFFS Started successfully...");
}

void File_Delete()
{
  if (server.args() > 0 ) 
  { // Arguments were received
    if (server.hasArg("delete")) SPIFFS_file_delete(server.arg(0));
  }
  else SelectInput("Select a File to Delete","delete","delete");
}

void ReportFileNotPresent(String target)
{
  SendHTML_Header();
  webpage += F("<h3>File does not exist</h3>"); 
  webpage += F("<a href='/"); webpage += target + "'>[Back]</a><br><br>";
  append_page_footer();
  SendHTML_Content();
  SendHTML_Stop();
}

void SPIFFS_file_delete(String filename) 
{ // Delete the file 
  if (SPIFFS_present) 
  { 
    SendHTML_Header();
    File dataFile = SPIFFS.open("/"+filename, "r"); // Now read data from SPIFFS Card 
    if (dataFile)
    {
      if (SPIFFS.remove("/"+filename)) 
      {
        Serial.println(F("File deleted successfully"));
        webpage += "<h3>File '"+filename+"' has been erased</h3>"; 
        webpage += F("<a href='/delete'>[Back]</a><br><br>");
      }
      else
      { 
        webpage += F("<h3>File was not deleted - error</h3>");
        webpage += F("<a href='delete'>[Back]</a><br><br>");
      }
    } else ReportFileNotPresent("delete");
    append_page_footer(); 
    SendHTML_Content();
    SendHTML_Stop();
  } else ReportSPIFFSNotPresent();
} 

void SelectInput(String heading1, String command, String arg_calling_name)
{
  SendHTML_Header();
  webpage += F("<h3>"); webpage += heading1 + "</h3>"; 
  webpage += F("<FORM action='/"); webpage += command + "' method='post'>"; // Must match the calling argument e.g. '/chart' calls '/chart' after selection but with arguments!
  webpage += F("<input type='text' name='"); webpage += arg_calling_name; webpage += F("' value=''><br>");
  webpage += F("<type='submit' name='"); webpage += arg_calling_name; webpage += F("' value=''><br><br>");
  webpage += F("<a href='/'>[Back]</a><br><br>");
  append_page_footer();
  SendHTML_Content();
  SendHTML_Stop();
}
void File_Download()
{ // This gets called twice, the first pass selects the input, the second pass then processes the command line arguments
  if (server.args() > 0 ) 
  { // Arguments were received
    if (server.hasArg("download")) DownloadFile(server.arg(0));
  }
  else SelectInput("Enter filename to download","download","download");
}
void DownloadFile(String filename)
{
  if (SPIFFS_present) 
  { 
    File download = SPIFFS.open("/"+filename,  "r");
    if (download) 
    {
      server.sendHeader("Content-Type", "text/text");
      server.sendHeader("Content-Disposition", "attachment; filename="+filename);
      server.sendHeader("Connection", "close");
      server.streamFile(download, "application/octet-stream");
      download.close();
    } else ReportFileNotPresent("download"); 
  } else ReportSPIFFSNotPresent();
}

void systemSetup() 
{
  webpage = ""; // don't delete this command, it ensures the server works reliably!
  append_page_header();
  String IPaddress="192.168.4.1";
  webpage += F("<h3 style=\"color:orange;font-size:24px\">Configuration</h3>");
  webpage += F("<meta http-equiv='refresh' content='200'/ URL=http://");
  webpage += IPaddress+ "/setting>";
    webpage += "<form action='http://"+IPaddress+"/setting' method='POST'>";
  webpage += "Kondisi Motor (currently = "+String(kondisi)+")<br>";
  webpage += F("<input type='text' name='kontrol_motor' value=''><br>");
  webpage += F("<input type='submit' value='Enter'><br><br>");
  webpage += F("</form>");
  webpage += "<form action='http://"+IPaddress+"/setting' method='POST'>";
  webpage += "Kecepatan Motor (currently = "+String(speed_in)+"Rpm)<br>";
  webpage += F("<input type='text' name='kecepatan_in' value=''><br>");
  webpage += F("<input type='submit' value='Enter'><br><br>");
  webpage += F("</form>");
  webpage += "<form action='http://"+IPaddress+"/setting' method='POST'>";
  webpage += "Logging Interval (currently = "+String(log_interval/1000)+"-Secs) (1=1secs)<br>";
  webpage += F("<input type='text' name='log_interval_in' value=''><br>");
  webpage += F("<input type='submit' value='Enter'><br><br>");
  webpage += F("</form>");
  append_page_footer();
  server.send(200, "text/html", webpage); // Send a response to the client asking for input
  if (server.args() > 0 ) 
  { // Arguments were received
    for ( uint8_t i = 0; i < server.args(); i++ ) 
    {
      String Argument_Name   = server.argName(i);
      String client_response = server.arg(i);
      if (Argument_Name == "kecepatan_in") 
      {
        if (client_response.toInt()) speed_in = client_response.toInt(); //else kecepatan = speed_default;
        Serial.println(speed_in);
        update_speed=1;
      }
      if (Argument_Name == "log_interval_in") 
      {
        if (client_response.toInt())
          {
            log_interval = 1000* client_response.toInt();
            Serial.println(log_interval);
          } 
        
      }
      if (Argument_Name == "kontrol_motor") 
      {
        kondisi=client_response;
        Serial.println(kondisi);
      }
    }
  }
  webpage = "";
 // update_log_time();
}