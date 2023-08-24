#include <Arduino.h>
//#include <FS.h>
#include <SD.h>
#include <SPIFFS.h>
#include <M5Unified.h>
#include <nvs.h>
#include <Avatar.h>
//#define USE_DOGFACE
#ifdef USE_DOGFACE
#include <faces/DogFace.h>
#endif

#include <AudioOutput.h>
#include <AudioFileSourceBuffer.h>
#include <AudioGeneratorMP3.h>
#include "AudioOutputM5Speaker.h"
#include <AudioFileSourcePROGMEM.h>
#include <google-tts.h>
#include <ServoEasing.hpp> // https://github.com/ArminJo/ServoEasing       

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "rootCACertificate.h"
#include <ArduinoJson.h>
#include <ESP32WebServer.h>
#include <ESPmDNS.h>
#include <deque>
#include "CloudSpeechClient.h"

// Default LANG_CODE
String LANG_CODE = "ja-JP";
//String LANG_CODE = "en-US";
//String LANG_CODE "es-MX";

// 保存する質問と回答の最大数
const int MAX_HISTORY = 5;

// 過去の質問と回答を保存するデータ構造
std::deque<String> chatHistory;

#define USE_SDCARD
#define WIFI_SSID "SET YOUR WIFI SSID"
#define WIFI_PASS "SET YOUR WIFI PASS"
#define OPENAI_APIKEY "SET YOUR OPENAI APIKEY"
#define GOOGL_APIKEY "SET YOUR GOOGL APIKEY"

#define USE_SERVO
#ifdef USE_SERVO
#if defined(ARDUINO_M5STACK_Core2)
//  #define SERVO_PIN_X 13  //Core2 PORT C
//  #define SERVO_PIN_Y 14
  #define SERVO_PIN_X 33  //Core2 PORT A
  #define SERVO_PIN_Y 32
#elif defined( ARDUINO_M5STACK_FIRE )
  #define SERVO_PIN_X 21
  #define SERVO_PIN_Y 22
#elif defined( ARDUINO_M5Stack_Core_ESP32 )
  #define SERVO_PIN_X 21
  #define SERVO_PIN_Y 22
#endif
#endif

TTS tts;
HTTPClient http;
WiFiClient client;

/// set M5Speaker virtual channel (0-7)
static constexpr uint8_t m5spk_virtual_channel = 0;
using namespace m5avatar;
Avatar avatar;
const Expression expressions_table[] = {
  Expression::Neutral,
  Expression::Happy,
  Expression::Sleepy,
  Expression::Doubt,
  Expression::Sad,
  Expression::Angry
};

ESP32WebServer server(80);

String OPENAI_API_KEY = "";
extern String GOOGLE_API_KEY;

char* text1 = "みなさんこんにちは、私の名前はスタックチャンです、よろしくね。";
char* text2 = "Hello everyone, my name is Stack Chan, nice to meet you.";

// C++11 multiline string constants are neato...
static const char HEAD[] PROGMEM = R"KEWL(
<!DOCTYPE html>
<html lang="ja">
<head>
  <meta charset="UTF-8">
  <title>AI Stack-chan</title>
</head>)KEWL";


static const char APIKEY_HTML[] PROGMEM = R"KEWL(
<!DOCTYPE html>
<html>
  <head>
    <meta charset="UTF-8">
    <title>API Key Settings</title>
  </head>
  <body>
    <h1>API Key Settings</h1>
    <form>
      <label for="role1">OpenAI API Key</label>
      <input type="text" id="openai" name="openai" oninput="adjustSize(this)"><br>
      <label for="role2">Google API Key</label>
      <input type="text" id="google" name="google" oninput="adjustSize(this)"><br>
      <button type="button" onclick="sendData()">Submit</button>
    </form>
    <script>
      function adjustSize(input) {
        input.style.width = ((input.value.length + 1) * 8) + 'px';
      }
      function sendData() {
        // FormDataオブジェクトを作成
        const formData = new FormData();

        // 各ロールの値をFormDataオブジェクトに追加
        const openaiValue = document.getElementById("openai").value;
        if (openaiValue !== "") formData.append("openai", openaiValue);

        const googleValue = document.getElementById("google").value;
        if (googleValue !== "") formData.append("google", googleValue);

	    // POSTリクエストを送信
	    const xhr = new XMLHttpRequest();
	    xhr.open("POST", "/apikey_set");
	    xhr.onload = function() {
	      if (xhr.status === 200) {
	        alert("Data transmitted!");
	      } else {
	        alert("Transmission failed.");
	      }
	    };
	    xhr.send(formData);
	  }
	</script>
  </body>
</html>)KEWL";

static const char ROLE_HTML[] PROGMEM = R"KEWL(
<!DOCTYPE html>
<html>
<head>
	<title>role-setting</title>
	<meta charset="UTF-8">
	<meta name="viewport" content="width=device-width, initial-scale=1.0">
	<style>
		textarea {
			width: 80%;
			height: 200px;
			resize: both;
		}
	</style>
</head>
<body>
	<h1>role-setting</h1>
	<form onsubmit="postData(event)">
		<label for="textarea">Describe roles here.:</label><br>
		<textarea id="textarea" name="textarea"></textarea><br><br>
		<input type="submit" value="Submit">
	</form>
	<script>
		function postData(event) {
			event.preventDefault();
			const textAreaContent = document.getElementById("textarea").value.trim();
//			if (textAreaContent.length > 0) {
				const xhr = new XMLHttpRequest();
				xhr.open("POST", "/role_set", true);
				xhr.setRequestHeader("Content-Type", "text/plain;charset=UTF-8");
			// xhr.onload = () => {
			// 	location.reload(); // 送信後にページをリロード
			// };
			xhr.onload = () => {
				document.open();
				document.write(xhr.responseText);
				document.close();
			};
				xhr.send(textAreaContent);
//        document.getElementById("textarea").value = "";
				alert("Data sent successfully!");
//			} else {
//				alert("Please enter some text before submitting.");
//			}
		}
	</script>
</body>
</html>)KEWL";
String speech_text = "";
String speech_text_buffer = "";
//DynamicJsonDocument chat_doc(1024);
DynamicJsonDocument chat_doc(1024*10);
String json_ChatString = "{\"model\": \"gpt-3.5-turbo-0613\",\"messages\": [{\"role\": \"user\", \"content\": \"""\"}]}";

bool init_chat_doc(const char *data)
{
  DeserializationError error = deserializeJson(chat_doc, data);
  if (error) {
    Serial.println("DeserializationError");
    return false;
  }
  String json_str; //= JSON.stringify(chat_doc);
  serializeJsonPretty(chat_doc, json_str);  // 文字列をシリアルポートに出力する
  Serial.println(json_str);
    return true;
}

void handleRoot() {
  server.send(200, "text/plain", "hello from m5stack!");
}

void handleNotFound(){
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
//  server.send(404, "text/plain", message);
  server.send(404, "text/html", String(HEAD) + String("<body>") + message + String("</body>"));
}

//void VoiceText_tts(char *text,char *tts_parms) ;
void google_tts(char *text, char *lang);
void handle_speech() {
  String message = server.arg("say");
  String expression = server.arg("expression");
  int expr = 0;
  Serial.println(expression);
  if(expression != ""){
    expr = expression.toInt();
    if(expr < 0) expr = 0;
    if(expr > 5) expr = 5;
  }
//  message = message + "\n";
  Serial.println(message);
  ////////////////////////////////////////
  // 音声の発声
  ////////////////////////////////////////
  avatar.setExpression(expressions_table[expr]);
  google_tts((char*)message.c_str(),(char*)LANG_CODE.c_str());
//  avatar.setExpression(expressions_table[0]);
  server.send(200, "text/plain", String("OK"));
}

String https_post_json(const char* url, const char* json_string, const char* root_ca) {
  String payload = "";
  WiFiClientSecure *client = new WiFiClientSecure;
  if(client) {
    client -> setCACert(root_ca);
    {
      // Add a scoping block for HTTPClient https to make sure it is destroyed before WiFiClientSecure *client is 
      HTTPClient https;
//      https.setTimeout( 25000 ); 
      https.setTimeout( 50000 ); 
  
      Serial.print("[HTTPS] begin...\n");
      if (https.begin(*client, url)) {  // HTTPS
        Serial.print("[HTTPS] POST...\n");
        // start connection and send HTTP header
        https.addHeader("Content-Type", "application/json");
//        https.addHeader("Authorization", "Bearer YOUR_API_KEY");
        https.addHeader("Authorization", String("Bearer ") + OPENAI_API_KEY);
        int httpCode = https.POST((uint8_t *)json_string, strlen(json_string));
  
        // httpCode will be negative on error
        if (httpCode > 0) {
          // HTTP header has been send and Server response header has been handled
          Serial.printf("[HTTPS] POST... code: %d\n", httpCode);
  
          // file found at server
          if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
            payload = https.getString();
          }
        } else {
          Serial.printf("[HTTPS] POST... failed, error: %s\n", https.errorToString(httpCode).c_str());
        }  
        https.end();
      } else {
        Serial.printf("[HTTPS] Unable to connect\n");
      }
      // End extra scoping block
    }  
    delete client;
  } else {
    Serial.println("Unable to create client");
  }
  return payload;
}

String chatGpt(String json_string) {
  String response = "";
//  String json_string = "{\"model\": \"gpt-3.5-turbo\",\"messages\": [{\"role\": \"user\", \"content\": \"" + text + "\"},{\"role\": \"system\", \"content\": \"あなたは「スタックちゃん」と言う名前の小型ロボットとして振る舞ってください。\"},{\"role\": \"system\", \"content\": \"あなたはの使命は人々の心を癒すことです。\"},{\"role\": \"system\", \"content\": \"幼い子供の口調で話してください。\"}]}";
  avatar.setExpression(Expression::Doubt);
  if(LANG_CODE == "ja-JP") {
    avatar.setSpeechText("考え中…");
  }else{
    avatar.setSpeechText("I'm thinking...");
  }
  String ret = https_post_json("https://api.openai.com/v1/chat/completions", json_string.c_str(), root_ca_openai);
  avatar.setExpression(Expression::Neutral);
  avatar.setSpeechText("");
  Serial.println(ret);
  if(ret != ""){
    DynamicJsonDocument doc(2000);
    DeserializationError error = deserializeJson(doc, ret.c_str());
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      avatar.setExpression(Expression::Sad);
      if(LANG_CODE == "ja-JP") {
        avatar.setSpeechText("エラーです");
        response = "エラーです";
      } else {
        avatar.setSpeechText("Error.");
        response = "Error.";
      }
      delay(1000);
      avatar.setSpeechText("");
      avatar.setExpression(Expression::Neutral);
    }else{
      const char* data = doc["choices"][0]["message"]["content"];
      Serial.println(data);
      response = String(data);
    }
  } else {
    avatar.setExpression(Expression::Sad);
    if(LANG_CODE == "ja-JP") {
      avatar.setSpeechText("わかりません");
      response = "わかりません";
    } else {
      avatar.setSpeechText("I don't understand.");
      response = "I don't understand.";
    }
    delay(1000);
    avatar.setSpeechText("");
    avatar.setExpression(Expression::Neutral);
  }
  return response;
}

String InitBuffer = "";

void handle_chat() {
  static String response = "";
  String text = server.arg("text");
  Serial.println(InitBuffer);
  init_chat_doc(InitBuffer.c_str());
  // 質問をチャット履歴に追加
  chatHistory.push_back(text);
    // チャット履歴が最大数を超えた場合、古い質問と回答を削除
  if (chatHistory.size() > MAX_HISTORY * 2)
  {
    chatHistory.pop_front();
    chatHistory.pop_front();
  }

  for (int i = 0; i < chatHistory.size(); i++)
  {
    JsonArray messages = chat_doc["messages"];
    JsonObject systemMessage1 = messages.createNestedObject();
    if(i % 2 == 0) {
      systemMessage1["role"] = "user";
    } else {
      systemMessage1["role"] = "assistant";
    }
    systemMessage1["content"] = chatHistory[i];
  }

  String json_string;
  serializeJson(chat_doc, json_string);
  if(speech_text=="" && speech_text_buffer == "") {
    response = chatGpt(json_string);
    speech_text = response;
    // 返答をチャット履歴に追加
    chatHistory.push_back(response);
  } else {
    response = "busy";
  }
  // Serial.printf("chatHistory.max_size %d \n",chatHistory.max_size());
  // Serial.printf("chatHistory.size %d \n",chatHistory.size());
  // for (int i = 0; i < chatHistory.size(); i++)
  // {
  //   Serial.print(i);
  //   Serial.println("= "+chatHistory[i]);
  // }
  serializeJsonPretty(chat_doc, json_string);
  Serial.println("====================");
  Serial.println(json_string);
  Serial.println("====================");
  server.send(200, "text/html", String(HEAD)+String("<body>")+response+String("</body>"));
}


String Role_JSON = "";
void exec_chatGPT(String text) {
  static String response = "";
  init_chat_doc(Role_JSON.c_str());

  String role = chat_doc["messages"][0]["role"];
  if(role == "user") {chat_doc["messages"][0]["content"] = text;}
  String json_string;
  serializeJson(chat_doc, json_string);

  response = chatGpt(json_string);
  speech_text = response;
//  server.send(200, "text/html", String(HEAD)+String("<body>")+response+String("</body>"));
}

void handle_apikey() {
  // ファイルを読み込み、クライアントに送信する
  server.send(200, "text/html", APIKEY_HTML);
}

void handle_apikey_set() {
  // POST以外は拒否
  if (server.method() != HTTP_POST) {
    return;
  }
  // openai
  String openai = server.arg("openai");
  // voicetxt
   String google = server.arg("google");
 
  OPENAI_API_KEY = openai;
  // tts_user = voicetext;
  GOOGLE_API_KEY = google;
  Serial.println(openai);
   Serial.println(google);

  uint32_t nvs_handle;
  if (ESP_OK == nvs_open("apikey", NVS_READWRITE, &nvs_handle)) {
    nvs_set_str(nvs_handle, "openai", openai.c_str());
    nvs_set_str(nvs_handle, "google", google.c_str());
    nvs_close(nvs_handle);
  }
  server.send(200, "text/plain", String("OK"));
}

void handle_role() {
  // ファイルを読み込み、クライアントに送信する
  server.send(200, "text/html", ROLE_HTML);
}

bool save_json(){
  // SPIFFSをマウントする
  if(!SPIFFS.begin(true)){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return false;
  }

  // JSONファイルを作成または開く
  File file = SPIFFS.open("/data.json", "w");
  if(!file){
    Serial.println("Failed to open file for writing");
    return false;
  }

  // JSONデータをシリアル化して書き込む
  serializeJson(chat_doc, file);
  file.close();
  return true;
}

/**
 * アプリからテキスト(文字列)と共にRoll情報が配列でPOSTされてくることを想定してJSONを扱いやすい形に変更
 * 出力形式をJSONに変更
*/
void handle_role_set() {
  // POST以外は拒否
  if (server.method() != HTTP_POST) {
    return;
  }
  String role = server.arg("plain");
  if (role != "") {
    init_chat_doc(InitBuffer.c_str());
    JsonArray messages = chat_doc["messages"];
    JsonObject systemMessage1 = messages.createNestedObject();
    systemMessage1["role"] = "system";
    systemMessage1["content"] = role;
  } else {
    init_chat_doc(json_ChatString.c_str());
    //会話履歴をクリア
    chatHistory.clear();
  }
  InitBuffer="";
  serializeJson(chat_doc, InitBuffer);
  Serial.println("InitBuffer = " + InitBuffer);
  Role_JSON = InitBuffer;

  // JSONデータをspiffsへ出力する
  save_json();

  // 整形したJSONデータを出力するHTMLデータを作成する
  String html = "<html><body><pre>";
  serializeJsonPretty(chat_doc, html);
  html += "</pre></body></html>";

  // HTMLデータをシリアルに出力する
  Serial.println(html);
  server.send(200, "text/html", html);
//  server.send(200, "text/plain", String("OK"));
};
void handle_role_set2() {
  // POST以外は拒否
  if (server.method() != HTTP_POST) {
    return;
  }
  String role = server.arg("plain");
  if (role != "") {
    JsonArray messages = chat_doc["messages"];
    JsonObject systemMessage1 = messages.createNestedObject();
    systemMessage1["role"] = "system";
    systemMessage1["content"] = role;
  } else {
    init_chat_doc(json_ChatString.c_str());
  }

  // JSONデータをspiffsへ出力する
  save_json();

  // 整形したJSONデータを出力するHTMLデータを作成する
  String html = "<html><body><pre>";
  serializeJsonPretty(chat_doc, html);
  html += "</pre></body></html>";
  // String json_str; //= JSON.stringify(chat_doc);
  // serializeJsonPretty(chat_doc, json_str);  // 文字列をシリアルポートに出力する
  // Serial.println(json_str);
//  server.send(200, "text/html", String(HEAD)+String("<body>")+json_str+String("</body>"));

  // HTMLデータをシリアルに出力する
  Serial.println(html);
  server.send(200, "text/html", html);
//  server.send(200, "text/plain", String("OK"));
};

// 整形したJSONデータを出力するHTMLデータを作成する
void handle_role_get() {

  String html = "<html><body><pre>";
  serializeJsonPretty(chat_doc, html);
  html += "</pre></body></html>";

  // HTMLデータをシリアルに出力する
  Serial.println(html);
  server.send(200, "text/html", String(HEAD) + html);
};

void handle_face() {
  String expression = server.arg("expression");
  expression = expression + "\n";
  Serial.println(expression);
  switch (expression.toInt())
  {
    case 0: avatar.setExpression(Expression::Neutral); break;
    case 1: avatar.setExpression(Expression::Happy); break;
    case 2: avatar.setExpression(Expression::Sleepy); break;
    case 3: avatar.setExpression(Expression::Doubt); break;
    case 4: avatar.setExpression(Expression::Sad); break;
    case 5: avatar.setExpression(Expression::Angry); break;  
  } 
  server.send(200, "text/plain", String("OK"));
}

void handle_setting() {
  String value = server.arg("volume");
  String lang = server.arg("lang");
  if(lang != "")  LANG_CODE = lang;
//  volume = volume + "\n";
  Serial.println(value);
  if(value == "") value = "180";
  size_t volume = value.toInt();
  {
    uint32_t nvs_handle;
    if (ESP_OK == nvs_open("setting", NVS_READWRITE, &nvs_handle)) {
      if(volume > 255) volume = 255;
      nvs_set_u32(nvs_handle, "volume", volume);
      if(lang != "")  nvs_set_str(nvs_handle, "lang", (char*)lang.c_str());
      nvs_close(nvs_handle);
    }
  }
  M5.Speaker.setVolume(volume);
  M5.Speaker.setChannelVolume(m5spk_virtual_channel, volume);
  server.send(200, "text/plain", String("OK"));
}

/// set M5Speaker virtual channel (0-7)
//static constexpr uint8_t m5spk_virtual_channel = 0;
static AudioOutputM5Speaker out(&M5.Speaker, m5spk_virtual_channel);
AudioGeneratorMP3 *mp3;
AudioFileSourceBuffer *buff = nullptr;
AudioFileSourcePROGMEM *file = nullptr;
uint8_t mp3buff[1024*60];

// Called when a metadata event occurs (i.e. an ID3 tag, an ICY block, etc.
void MDCallback(void *cbData, const char *type, bool isUnicode, const char *string)
{
  const char *ptr = reinterpret_cast<const char *>(cbData);
  (void) isUnicode; // Punt this ball for now
  // Note that the type and string may be in PROGMEM, so copy them to RAM for printf
  char s1[32], s2[64];
  strncpy_P(s1, type, sizeof(s1));
  s1[sizeof(s1)-1]=0;
  strncpy_P(s2, string, sizeof(s2));
  s2[sizeof(s2)-1]=0;
  Serial.printf("METADATA(%s) '%s' = '%s'\n", ptr, s1, s2);
  Serial.flush();
}

// Called when there's a warning or error (like a buffer underflow or decode hiccup)
void StatusCallback(void *cbData, int code, const char *string)
{
  const char *ptr = reinterpret_cast<const char *>(cbData);
  // Note that the string may be in PROGMEM, so copy it to RAM for printf
  char s1[64];
  strncpy_P(s1, string, sizeof(s1));
  s1[sizeof(s1)-1]=0;
  Serial.printf("STATUS(%s) '%d' = '%s'\n", ptr, code, s1);
  Serial.flush();
}

#ifdef USE_SERVO
#define START_DEGREE_VALUE_X 90
//#define START_DEGREE_VALUE_Y 90
#define START_DEGREE_VALUE_Y 85 //
ServoEasing servo_x;
ServoEasing servo_y;
#endif

void lipSync(void *args)
{
  float gazeX, gazeY;
  int level = 0;
  DriveContext *ctx = (DriveContext *)args;
  Avatar *avatar = ctx->getAvatar();
  for (;;)
  {
    level = abs(*out.getBuffer());
    if(level<100) level = 0;
    if(level > 15000)
    {
      level = 15000;
    }
    float open = (float)level/15000.0;
    avatar->setMouthOpenRatio(open);
    avatar->getGaze(&gazeY, &gazeX);
    avatar->setRotation(gazeX * 5);
    delay(50);
  }
}

bool servo_home = false;

void servo(void *args)
{
  float gazeX, gazeY;
  DriveContext *ctx = (DriveContext *)args;
  Avatar *avatar = ctx->getAvatar();
  for (;;)
  {
#ifdef USE_SERVO
    if(!servo_home)
    {
    avatar->getGaze(&gazeY, &gazeX);
    servo_x.setEaseTo(START_DEGREE_VALUE_X + (int)(15.0 * gazeX));
    if(gazeY < 0) {
      int tmp = (int)(10.0 * gazeY);
      if(tmp > 10) tmp = 10;
      servo_y.setEaseTo(START_DEGREE_VALUE_Y + tmp);
    } else {
      servo_y.setEaseTo(START_DEGREE_VALUE_Y + (int)(10.0 * gazeY));
    }
    } else {
//     avatar->setRotation(gazeX * 5);
//     float b = avatar->getBreath();
       servo_x.setEaseTo(START_DEGREE_VALUE_X); 
//     servo_y.setEaseTo(START_DEGREE_VALUE_Y + b * 5);
       servo_y.setEaseTo(START_DEGREE_VALUE_Y);
    }
    synchronizeAllServosStartAndWaitForAllServosToStop();
#endif
    delay(50);
  }
}

void Servo_setup() {
#ifdef USE_SERVO
  if (servo_x.attach(SERVO_PIN_X, START_DEGREE_VALUE_X, DEFAULT_MICROSECONDS_FOR_0_DEGREE, DEFAULT_MICROSECONDS_FOR_180_DEGREE)) {
    Serial.print("Error attaching servo x");
  }
  if (servo_y.attach(SERVO_PIN_Y, START_DEGREE_VALUE_Y, DEFAULT_MICROSECONDS_FOR_0_DEGREE, DEFAULT_MICROSECONDS_FOR_180_DEGREE)) {
    Serial.print("Error attaching servo y");
  }
  servo_x.setEasingType(EASE_QUADRATIC_IN_OUT);
  servo_y.setEasingType(EASE_QUADRATIC_IN_OUT);
  setSpeedForAllServos(30);

  servo_x.setEaseTo(START_DEGREE_VALUE_X); 
  servo_y.setEaseTo(START_DEGREE_VALUE_Y);
  synchronizeAllServosStartAndWaitForAllServosToStop();
#endif
}

void google_tts(char *text, char *lang) {
  Serial.println("tts Start");
  String link =  "http" + tts.getSpeechUrl(text, lang).substring(5);
  Serial.println(link);

  http.begin(client, link);
  http.setReuse(true);
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
//    cb.st(STATUS_HTTPFAIL, PSTR("Can't open HTTP request"));
    return ;
  }
  WiFiClient *ttsclient = http.getStreamPtr();
  ttsclient->setTimeout( 10000 ); 
  if (ttsclient->available() > 0) {
    int i = 0;
    int len = sizeof(mp3buff);
    int count = 0;

    bool data_end = false;
    while (!data_end) {
      if(ttsclient->available() > 0) {

      int bytesread = ttsclient->read(&mp3buff[i], len);
//     Serial.printf("%d Bytes Read\n",bytesread);
      i = i + bytesread;
      if(i > sizeof(mp3buff))
      {
        break;
      } else {
        len = len - bytesread;
        if(len <= 0) break;
      }

      }
      {
        Serial.printf(" %d Bytes Read\n",i);
        int lastms = millis();
        data_end = true;
        while (millis()-lastms < 600) { //データ終わりか待ってみる
          if (ttsclient->available() > 0) {data_end = false; break;}
           yield();
        }
      }

    }

    Serial.printf("Total %d Bytes Read\n",i);
    ttsclient->stop();
    http.end();
    file = new AudioFileSourcePROGMEM(mp3buff, i);
    mp3->begin(file, &out);
  }
}

struct box_t
{
  int x;
  int y;
  int w;
  int h;
  int touch_id = -1;

  void setupBox(int x, int y, int w, int h) {
    this->x = x;
    this->y = y;
    this->w = w;
    this->h = h;
  }
  bool contain(int x, int y)
  {
    return this->x <= x && x < (this->x + this->w)
        && this->y <= y && y < (this->y + this->h);
  }
};
static box_t box_servo;
static box_t box_stt;

void Wifi_setup() {
  // 前回接続時情報で接続する
  while (WiFi.status() != WL_CONNECTED) {
    M5.Display.print(".");
    Serial.print(".");
    delay(500);
    // 10秒以上接続できなかったら抜ける
    if ( 10000 < millis() ) {
      break;
    }
  }
  M5.Display.println("");
  Serial.println("");
  // 未接続の場合にはSmartConfig待受
  if ( WiFi.status() != WL_CONNECTED ) {
    WiFi.mode(WIFI_STA);
    WiFi.beginSmartConfig();
    M5.Display.println("Waiting for SmartConfig");
    Serial.println("Waiting for SmartConfig");
    while (!WiFi.smartConfigDone()) {
      delay(500);
      M5.Display.print("#");
      Serial.print("#");
      // 30秒以上接続できなかったら抜ける
      if ( 30000 < millis() ) {
        Serial.println("");
        Serial.println("Reset");
        ESP.restart();
      }
    }
    // Wi-fi接続
    M5.Display.println("");
    Serial.println("");
    M5.Display.println("Waiting for WiFi");
    Serial.println("Waiting for WiFi");
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      M5.Display.print(".");
      Serial.print(".");
      // 60秒以上接続できなかったら抜ける
      if ( 60000 < millis() ) {
        Serial.println("");
        Serial.println("Reset");
        ESP.restart();
      }
    }
  }
}

// void info_spiffs(){
//   FSInfo fs_info;
//   SPIFFS.info(fs_info);
//   Serial.print("SPIFFS Total bytes: ");
//   Serial.println(fs_info.totalBytes);
//   Serial.print("SPIFFS Used bytes: ");
//   Serial.println(fs_info.usedBytes);
//   Serial.print("SPIFFS Free bytes: ");
//   Serial.println(fs_info.totalBytes - fs_info.usedBytes);
// }

void setup()
{
  auto cfg = M5.config();

  cfg.external_spk = true;    /// use external speaker (SPK HAT / ATOMIC SPK)
//cfg.external_spk_detail.omit_atomic_spk = true; // exclude ATOMIC SPK
//cfg.external_spk_detail.omit_spk_hat    = true; // exclude SPK HAT
  cfg.internal_mic = true;

  M5.begin(cfg);

  { /// custom setting
    auto spk_cfg = M5.Speaker.config();
    /// Increasing the sample_rate will improve the sound quality instead of increasing the CPU load.
    spk_cfg.sample_rate = 96000; // default:64000 (64kHz)  e.g. 48000 , 50000 , 80000 , 96000 , 100000 , 128000 , 144000 , 192000 , 200000
    spk_cfg.task_pinned_core = APP_CPU_NUM;
    M5.Speaker.config(spk_cfg);
  }
  M5.Speaker.begin();

  Servo_setup();
  M5.Lcd.setTextSize(2);
  Serial.println("Connecting to WiFi");
  WiFi.disconnect();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
#ifndef USE_SDCARD
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  OPENAI_API_KEY = String(OPENAI_APIKEY);
  GOOGLE_API_KEY = String(GOOGL_APIKEY);
#else
  /// settings
  if (SD.begin(GPIO_NUM_4, SPI, 25000000)) {
    /// wifi
    auto fs = SD.open("/wifi.txt", FILE_READ);
    if(fs) {
      size_t sz = fs.size();
      char buf[sz + 1];
      fs.read((uint8_t*)buf, sz);
      buf[sz] = 0;
      fs.close();

      int y = 0;
      for(int x = 0; x < sz; x++) {
        if(buf[x] == 0x0a || buf[x] == 0x0d)
          buf[x] = 0;
        else if (!y && x > 0 && !buf[x - 1] && buf[x])
          y = x;
      }
      WiFi.begin(buf, &buf[y]);
    } else {
       WiFi.begin();
    }

    uint32_t nvs_handle;
    if (ESP_OK == nvs_open("apikey", NVS_READWRITE, &nvs_handle)) {
      /// radiko-premium
      fs = SD.open("/apikey.txt", FILE_READ);
      if(fs) {
        size_t sz = fs.size();
        char buf[sz + 1];
        fs.read((uint8_t*)buf, sz);
        buf[sz] = 0;
        fs.close();
        int y = 0;
        for(int x = 0; x < sz; x++) {
          if(buf[x] == 0x0a || buf[x] == 0x0d)
            buf[x] = 0;
          else if (!y && x > 0 && !buf[x - 1] && buf[x])
            y = x;
        }
        nvs_set_str(nvs_handle, "openai", buf);
        nvs_set_str(nvs_handle, "google", &buf[y]);
        Serial.println(buf);
        Serial.println(&buf[y]);
      }
      
      nvs_close(nvs_handle);
    }
    SD.end();
  } else {
    WiFi.begin();
  }

  {
    uint32_t nvs_handle;
    if (ESP_OK == nvs_open("apikey", NVS_READONLY, &nvs_handle)) {
      Serial.println("nvs_open");

      size_t length1;
       size_t length2;
       if(ESP_OK == nvs_get_str(nvs_handle, "openai", nullptr, &length1) && ESP_OK == nvs_get_str(nvs_handle, "google", nullptr, &length2) && length1 && length2) {
        Serial.println("nvs_get_str");
        char openai_apikey[length1 + 1];
        char google_apikey[length2 + 1];
        if(ESP_OK == nvs_get_str(nvs_handle, "openai", openai_apikey, &length1) && ESP_OK == nvs_get_str(nvs_handle, "google", google_apikey, &length2)) {
          OPENAI_API_KEY = String(openai_apikey);
          GOOGLE_API_KEY = String(google_apikey);
          Serial.println(OPENAI_API_KEY);
          Serial.println(GOOGLE_API_KEY);
        }
      }
      nvs_close(nvs_handle);
    }
  }
  
#endif
  {
    uint32_t nvs_handle;
    if (ESP_OK == nvs_open("setting", NVS_READONLY, &nvs_handle)) {
      size_t volume;
      nvs_get_u32(nvs_handle, "volume", &volume);
      if(volume > 255) volume = 255;
      M5.Speaker.setVolume(volume);
      M5.Speaker.setChannelVolume(m5spk_virtual_channel, volume);

      size_t length1;
      if(ESP_OK == nvs_get_str(nvs_handle, "lang", nullptr, &length1) && length1) {
        Serial.println("nvs_get_str");
        char google_lang[length1 + 1];
        if(ESP_OK == nvs_get_str(nvs_handle, "lang", google_lang, &length1)) {
          LANG_CODE = String(google_lang);
          Serial.println(OPENAI_API_KEY);
        }
      }
      nvs_close(nvs_handle);
    } else {
      if (ESP_OK == nvs_open("setting", NVS_READWRITE, &nvs_handle)) {
        size_t volume = 180;
        // LANG_CODE = "en-US";
        nvs_set_u32(nvs_handle, "volume", volume);
        nvs_set_str(nvs_handle, "lang", (char*)LANG_CODE.c_str());
        nvs_close(nvs_handle);
        M5.Speaker.setVolume(volume);
        M5.Speaker.setChannelVolume(m5spk_virtual_channel, volume);
        nvs_close(nvs_handle);
      }
    }
  }

  M5.Lcd.print("Connecting");
  Wifi_setup();
  M5.Lcd.println("\nConnected");
  Serial.printf_P(PSTR("Go to http://"));
  M5.Lcd.print("Go to http://");
  Serial.println(WiFi.localIP());
  M5.Lcd.println(WiFi.localIP());

   if (MDNS.begin("m5stack")) {
    Serial.println("MDNS responder started");
    M5.Lcd.println("MDNS responder started");
  }
  delay(1000);
  server.on("/", handleRoot);

  server.on("/inline", [](){
    server.send(200, "text/plain", "this works as well");
  });

  // And as regular external functions:
  server.on("/speech", handle_speech);
  server.on("/face", handle_face);
  server.on("/chat", handle_chat);
  server.on("/apikey", handle_apikey);
  server.on("/setting", handle_setting);
  server.on("/apikey_set", HTTP_POST, handle_apikey_set);
  server.on("/role", handle_role);
  server.on("/role_set", HTTP_POST, handle_role_set);
  server.on("/role_get", handle_role_get);
  server.onNotFound(handleNotFound);

  init_chat_doc(json_ChatString.c_str());
  // SPIFFSをマウントする
  if(SPIFFS.begin(true)){
    // JSONファイルを開く
    File file = SPIFFS.open("/data.json", "r");
    if(file){
      DeserializationError error = deserializeJson(chat_doc, file);
      if(error){
        Serial.println("Failed to deserialize JSON");
        init_chat_doc(json_ChatString.c_str());
      }
      serializeJson(chat_doc, InitBuffer);
      Role_JSON = InitBuffer;
      String json_str; 
      serializeJsonPretty(chat_doc, json_str);  // 文字列をシリアルポートに出力する
      Serial.println(json_str);
    } else {
      Serial.println("Failed to open file for reading");
      init_chat_doc(json_ChatString.c_str());
    }
  } else {
    Serial.println("An Error has occurred while mounting SPIFFS");
  }

  server.begin();
  Serial.println("HTTP server started");
  M5.Lcd.println("HTTP server started");  
  
  Serial.printf_P(PSTR("/ to control the chatGpt Server.\n"));
  M5.Lcd.print("/ to control the chatGpt Server.\n");
  delay(3000);

  audioLogger = &Serial;
  mp3 = new AudioGeneratorMP3();
//  mp3->RegisterStatusCB(StatusCallback, (void*)"mp3");

#ifdef USE_DOGFACE
  static Face* face = new DogFace();
  static ColorPalette* cp = new ColorPalette();
  cp->set(COLOR_PRIMARY, TFT_BLACK);  //AtaruFace
  cp->set(COLOR_SECONDARY, TFT_WHITE);
  cp->set(COLOR_BACKGROUND, TFT_WHITE);
  avatar.setFace(face);
  avatar.setColorPalette(*cp);
  avatar.init(8); //Color Depth8
#else
  avatar.init();
#endif
  avatar.addTask(lipSync, "lipSync");
  avatar.addTask(servo, "servo");
  avatar.setSpeechFont(&fonts::efontJA_16);
  box_servo.setupBox(80, 120, 80, 80);
  box_stt.setupBox(0, 0, M5.Display.width(), 60);
  randomSeed(millis());
}

String keywords[] = {"(Neutral)", "(Happy)", "(Sleepy)", "(Doubt)", "(Sad)", "(Angry)"};
void addPeriodBeforeKeyword(String &input, String keywords[], int numKeywords) {
  int prevIndex = 0;
  for (int i = 0; i < numKeywords; i++) {
    int index = input.indexOf(keywords[i]);
    while (index != -1) {
      if(LANG_CODE == "ja-JP") {
        if (index > 0 && input.charAt(index-1) != '。') {
          input = input.substring(0, index) + "。" + input.substring(index);
        }
      } else {
        if (index > 0 && input.charAt(index-1) != '.') {
          input = input.substring(0, index) + "." + input.substring(index);
        }
      }
      prevIndex = index + keywords[i].length() + 1; // update prevIndex to after the keyword and period
      index = input.indexOf(keywords[i], prevIndex);
    }
  }
//  Serial.println(input);
}

int expressionIndx = -1;
String expressionString[] = {"Neutral","Happy","Sleepy","Doubt","Sad","Angry",""};
String emotion_parms[]= {
  "&emotion_level=2&emotion=happiness",
  "&emotion_level=3&emotion=happiness",
  "&emotion_level=2&emotion=sadness",
  "&emotion_level=1&emotion=sadness",
  "&emotion_level=4&emotion=sadness",
  "&emotion_level=4&emotion=anger"};
int tts_emotion_no = 0;
//String random_words[18] = {"あなたは誰","楽しい","怒った","可愛い","悲しい","眠い","ジョークを言って","泣きたい","怒ったぞ","こんにちは","お疲れ様","詩を書いて","疲れた","お腹空いた","嫌いだ","苦しい","俳句を作って","歌をうたって"};
String random_words[18] = {"楽しい","お菓子食べたい","あなたは誰","可愛い","悲しい","眠い","ジョークを言って","賢いね","笑い","こんにちは","お疲れ様","詩を書いて","疲れた","お腹空いた","大好き","気持ち良いね","俳句を作って","歌をうたって"};
int random_time = -1;
bool random_speak = true;

void getExpression(String &sentence, int &expressionIndx){
    Serial.println("sentence="+sentence);
    int startIndex = sentence.indexOf("(");
    if(startIndex >= 0) {
      int endIndex = sentence.indexOf(")", startIndex);
      if(endIndex > 0) {
        String extractedString = sentence.substring(startIndex + 1, endIndex); // 括弧を含まない部分文字列を抽出
//        Serial.println("extractedString="+extractedString);
        sentence.remove(startIndex, endIndex - startIndex + 1); // 括弧を含む部分文字列を削除
//        Serial.println("sentence="+sentence);
        if(extractedString != "") {
          expressionIndx = 0;
          while(1) {
            if(expressionString[expressionIndx] == extractedString)
            {
              avatar.setExpression(expressions_table[expressionIndx]);
              break;
            }
            if(expressionString[expressionIndx] == "") {
              expressionIndx = -1;
              break;
            }
            expressionIndx++;
          }
        } else {
          expressionIndx = -1;
        }
      }
    }
}

String separator_tbl[2][7] = {{"。","？","！","、","","　",""},{":",",",".","?","!","\n",""}};

int search_separator(String text, int tbl){
  int i = 0;
  int dotIndex_min = 1000;
  int dotIndex;
  while(separator_tbl[tbl][i] != ""){
    dotIndex = text.indexOf(separator_tbl[tbl][i++]);
    if((dotIndex != -1)&&(dotIndex < dotIndex_min)) dotIndex_min = dotIndex;
  }
  if(dotIndex_min == 1000) return -1;
  else return dotIndex_min;

}
// int search_separator1(String text, int tbl){
//   int i = 0;
//   int dotIndex;
//   while(separator_tbl[tbl][i] != ""){
//     dotIndex = text.indexOf(separator_tbl[tbl][i++]);
//     if(dotIndex != -1) return dotIndex;
//   }
//   return -1;
// }

void loop()
{
  static int lastms = 0;
  static int lastms1 = 0;

  if (random_time >= 0 && millis() - lastms1 > random_time)
  {
    lastms1 = millis();
    random_time = 40000 + 1000 * random(30);
    if (!mp3->isRunning() && speech_text=="" && speech_text_buffer == "") {
      exec_chatGPT(random_words[random(18)]);
    }
  }

  if (M5.BtnA.wasPressed()&&(!mp3->isRunning()))
  {
    M5.Speaker.tone(1000, 100);
    String tmp;
    String lang;
    if(random_speak) {
      if(LANG_CODE == "ja-JP") {
        tmp = "独り言始めます。";
        lang = "ja-JP";
      } else {
        tmp = "I'll start talking to myself.";       
        lang = "en-US";
      }
      lastms1 = millis();
      random_time = 40000 + 1000 * random(30);
    } else {
      if(LANG_CODE == "ja-JP") {
        tmp = "独り言やめます。";
        lang = "ja-JP";
      } else {
        tmp = "I'll stop talking to myself.";
        lang = "en-US";
      }
      random_time = -1;
    }
    random_speak = !random_speak;
    avatar.setExpression(Expression::Happy);
//    google_tts((char*)tmp.c_str(),"ja-JP");
//    google_tts((char*)tmp.c_str(),"en-US");
    google_tts((char*)tmp.c_str(),(char*)lang.c_str());
    avatar.setExpression(Expression::Neutral);
    Serial.println("mp3 begin");
  }

  // if (Serial.available()) {
  //   char kstr[256];
  //   size_t len = Serial.readBytesUntil('\r', kstr, 256);
  //   kstr[len]=0;
  //   avatar.setExpression(Expression::Happy);
  //   VoiceText_tts(kstr, tts_parms2);
  //   avatar.setExpression(Expression::Neutral);
	// }

  M5.update();
#if defined(ARDUINO_M5STACK_Core2)
  auto count = M5.Touch.getCount();
  if (count)
  {
    auto t = M5.Touch.getDetail();
    if (t.wasPressed())
    {          
      if (box_stt.contain(t.x, t.y)&&(!mp3->isRunning()))
      {
        M5.Speaker.tone(1000, 100);
        delay(200);
        bool prev_servo_home = servo_home;
        random_speak = true;
        random_time = -1;
#ifdef USE_SERVO
        servo_home = true;
#endif
        avatar.setExpression(Expression::Happy);
        if(LANG_CODE == "ja-JP") {
          avatar.setSpeechText("御用でしょうか？");
        }else{
          avatar.setSpeechText("May I help you?");
        }
        M5.Speaker.end();
        Serial.println("\r\nRecord start!\r\n");
        Audio* audio = new Audio();
        audio->Record();
        Serial.println("Record end\r\n");
        Serial.println("音声認識開始");
        if(LANG_CODE == "ja-JP") {
          avatar.setSpeechText("わかりました");
        }else{
          avatar.setSpeechText("I understand.");
        }
        CloudSpeechClient* cloudSpeechClient = new CloudSpeechClient(USE_APIKEY);
        String ret = cloudSpeechClient->Transcribe(audio);
        delete cloudSpeechClient;
        delete audio;
        delay(500);
#ifdef USE_SERVO
        servo_home = prev_servo_home;
#endif
        Serial.println("音声認識終了");
        Serial.println("音声認識結果");
        if(ret != "") {
          //M5.Lcd.println(ret);
          if (!mp3->isRunning() && speech_text=="" && speech_text_buffer == "") {
            exec_chatGPT(ret);
          }
        } else {
          Serial.println("音声認識失敗");
          avatar.setExpression(Expression::Sad);
          if(LANG_CODE == "ja-JP") {
            avatar.setSpeechText("聞き取れませんでした");
          } else {
            avatar.setSpeechText("I didn't hear it.");
          //response = "I don't understand.";
          }
          delay(2000);
          avatar.setSpeechText("");
          avatar.setExpression(Expression::Neutral);
        }
        M5.Speaker.begin();
      }
#ifdef USE_SERVO
      if (box_servo.contain(t.x, t.y))
      {
        servo_home = !servo_home;
        M5.Speaker.tone(1000, 100);
      }
#endif
    }
  }
#endif

  if (M5.BtnC.wasPressed())
  {
    M5.Speaker.tone(1000, 100);
    avatar.setExpression(Expression::Happy);
    if(LANG_CODE == "ja-JP") {
      google_tts(text1,"ja-JP");
    } else {
      google_tts(text2,"en-US");
    }
    avatar.setExpression(Expression::Neutral);
    Serial.println("mp3 begin");
  }

  if(speech_text != ""){
    speech_text_buffer = speech_text;
    speech_text = "";
    addPeriodBeforeKeyword(speech_text_buffer, keywords, 6);
    Serial.println("-----------------------------");
    Serial.println(speech_text_buffer);
//---------------------------------
    String sentence = speech_text_buffer;
    int dotIndex;
    if(LANG_CODE == "ja-JP") {
      dotIndex = search_separator(speech_text_buffer, 0);
      //dotIndex = speech_text_buffer.indexOf("。");
    } else {
      dotIndex = search_separator(speech_text_buffer, 1);
      //dotIndex =speech_text_buffer.indexOf(".");
    }
    if (dotIndex != -1) {
      if(LANG_CODE == "ja-JP") {
        dotIndex += 3;
      }else{
        dotIndex += 2;
      }
      sentence = speech_text_buffer.substring(0, dotIndex);
      Serial.println(sentence);
      speech_text_buffer = speech_text_buffer.substring(dotIndex);
    }else{
      speech_text_buffer = "";
    }
//----------------
    getExpression(sentence, expressionIndx);
//----------------
    if(expressionIndx < 0) avatar.setExpression(Expression::Happy);
    google_tts((char*)sentence.c_str(), (char*)LANG_CODE.c_str());
    if(expressionIndx < 0) avatar.setExpression(Expression::Neutral);
  }

  if (mp3->isRunning()) {
    // if (millis()-lastms > 1000) {
    //   lastms = millis();
    //   Serial.printf("Running for %d ms...\n", lastms);
    //   Serial.flush();
    //  }
    if (!mp3->loop()) {
      mp3->stop();
      if(file != nullptr){delete file; file = nullptr;}
      Serial.println("mp3 stop");
//      avatar.setExpression(Expression::Neutral);
      if(speech_text_buffer != ""){
        String sentence = speech_text_buffer;
        int dotIndex;
        if(LANG_CODE == "ja-JP") {
          dotIndex = search_separator(speech_text_buffer, 0);
          //dotIndex =speech_text_buffer.indexOf("。");
        } else {
          dotIndex = search_separator(speech_text_buffer, 1);
          //dotIndex =speech_text_buffer.indexOf(".");
        }
        if (dotIndex != -1) {
          if(LANG_CODE == "ja-JP") {
            dotIndex += 3;
          }else{
            dotIndex += 2;
          }
          sentence = speech_text_buffer.substring(0, dotIndex);
          Serial.println(sentence);
          speech_text_buffer = speech_text_buffer.substring(dotIndex);
        }else{
          speech_text_buffer = "";
        }
//----------------
        getExpression(sentence, expressionIndx);
//----------------
        if(expressionIndx < 0) avatar.setExpression(Expression::Happy);
        google_tts((char*)sentence.c_str(), (char*)LANG_CODE.c_str());
        if(expressionIndx < 0) avatar.setExpression(Expression::Neutral);
      } else {
        avatar.setExpression(Expression::Neutral);
        expressionIndx = -1;
      }
    }
  } else {
  server.handleClient();
  }
//delay(100);
}
