#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>

// --- CẤU HÌNH AP & OPENWEATHERMAP ---
const char *AP_SSID = "DeskSetup-Config";
const char *AP_PASS = "12345678";
const String OPENWEATHER_API_KEY = "API Weather";

WebServer server(80);
DNSServer dnsServer;
Preferences preferences;

// Biến lưu trữ thông tin
String ssid = "";
String pass = "";
float userLat = 0.0;
float userLon = 0.0;
bool hasCustomGPS = false;

// Trạng thái hệ thống
bool isAPMode = false;
unsigned long lastWeatherCheck = 0;
const unsigned long WEATHER_INTERVAL = 300000;

const char AP_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <meta charset="UTF-8">
    <title>DeskSetup - Cau hinh WiFi</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #f0f2f5; color: #333; }
        .container { max-width: 400px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
        h2 { text-align: center; color: #007bff; }
        label { font-weight: bold; display: block; margin-top: 15px; }
        select, input[type="password"] { width: 100%; padding: 10px; margin-top: 5px; box-sizing: border-box; border: 1px solid #ccc; border-radius: 4px; }
        button { width: 100%; background: #28a745; color: white; border: none; padding: 12px; margin-top: 20px; border-radius: 4px; font-size: 16px; cursor: pointer; }
    </style>
</head>
<body>
    <div class="container">
        <h2>Cau hinh WiFi DeskSetup</h2>
        <form action="/save" method="POST">
            <label for="ssid">Chon WiFi:</label>
            <select name="ssid" id="ssid_select">
                <option value="">Dang quet mang...</option>
            </select>
            
            <label for="pass">Mat khau WiFi:</label>
            <input type="password" name="pass" placeholder="Nhap mat khau">

            <button type="submit">Luu & Ket noi</button>
        </form>
    </div>
    <script>
        window.onload = function() {
            fetch('/scan').then(response => response.json()).then(data => {
                let select = document.getElementById('ssid_select');
                select.innerHTML = '';
                data.forEach(item => {
                    let option = document.createElement('option');
                    option.value = item.ssid;
                    option.text = item.ssid + ' (' + item.rssi + ' dBm)';
                    select.appendChild(option);
                });
            });
        };
    </script>
</body>
</html>
)rawliteral";

const char STA_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <meta charset="UTF-8">
    <title>DeskSetup Dashboard</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #eef2f5; color: #333; }
        .container { max-width: 400px; margin: 0 auto; background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
        h2 { text-align: center; color: #007bff; margin-bottom: 5px; }
        .btn { width: 100%; padding: 12px; margin-top: 10px; border: none; border-radius: 4px; font-size: 15px; cursor: pointer; color: white; }
        .gps-btn { background: #17a2b8; }
        .reset-btn { background: #dc3545; }
        .status { margin-top: 15px; padding: 10px; background: #f8f9fa; border: 1px solid #ddd; border-radius: 4px; font-size: 14px; word-break: break-all; }
    </style>
</head>
<body>
    <div class="container">
        <h2>DeskSetup Dashboard</h2>
        <p style="text-align:center; color:#666; font-size:13px;">Thiết bị đã kết nối WiFi thành công</p>
        
        <button class="btn gps-btn" onclick="updateGPS()">Cap nhat GPS chinh xac tu thiet bi</button>
        <div id="gps_status" class="status">Trang thai GPS: Mac dinh / Theo IP</div>

        <form action="/reset" method="POST" style="margin-top: 20px;">
            <button type="submit" class="btn reset-btn" onclick="return confirm('Ban co chac muon xoa WiFi va cau hinh lai?')">Xoa cau hinh WiFi</button>
        </form>
    </div>

    <script>
        function updateGPS() {
            let statusDiv = document.getElementById('gps_status');
            if (navigator.geolocation) {
                statusDiv.innerHTML = "Dang lay toa do GPS...";
                navigator.geolocation.getCurrentPosition(
                    (position) => {
                        let lat = position.coords.latitude;
                        let lon = position.coords.longitude;
                        
                        fetch('/update_gps?lat=' + lat + '&lon=' + lon)
                        .then(response => response.text())
                        .then(msg => {
                            statusDiv.innerHTML = "Da cap nhat GPS: " + lat.toFixed(4) + ", " + lon.toFixed(4);
                        });
                    },
                    (error) => {
                        statusDiv.innerHTML = "Loi lay GPS: " + error.message + "<br><small>Luu y: Neu truy cap qua IP HTTP, trinh duyat co the chan Geolocation.</small>";
                    }
                );
            } else {
                statusDiv.innerHTML = "Trinh duyat khong ho tro Geolocation.";
            }
        }
    </script>
</body>
</html>
)rawliteral";

void checkWeather();

// Phân loại thời tiết
String getDetailedWeatherStatus(int id, float temp)
{
  // Giông bão (200 - 232)
  if (id >= 200 && id <= 232)
  {
    if (id == 211 || id == 212)
      return "GIONG BAO MANH DAU DAU";
    return "SOT SET & GIONG BAO";
  }

  // Mưa phùn (300 - 321)
  if (id >= 300 && id <= 321)
    return "MUA PHUN RẢI RÁC / MUA LAM RAM";

  // Mưa rào & Mưa to (500 - 531)
  if (id >= 500 && id <= 531)
  {
    if (id == 500)
      return "MUA NHO / MUA RAO NHE";
    if (id == 501)
      return "MUA VUA TẦM TÃ";
    if (id >= 502 && id <= 504)
      return "MUA RAT TO / MUA XOI XA";
    if (id >= 520)
      return "MUA RAO RỪNG RỰC KÈM GIÓ";
    return "TROIC MUA";
  }

  // Tuyết (600 - 622)
  if (id >= 600 && id <= 622)
    return "CO TUYET ROI";

  // Sương mù / Bụi mờ (701 - 781)
  if (id == 701 || id == 741)
    return "SUONG MU DAY DAC";
  if (id == 721)
    return "SUONG MÙ MỜ / MÂY MÙ";
  if (id == 781)
    return "LỐC XOÁY / BAO SUONG";
  if (id >= 700 && id <= 781)
    return "AM U / KHOANG KHONG MỜ";

  // Trời Quang / Nắng (800)
  if (id == 800)
  {
    if (temp >= 36.0)
      return "NANG GAY GAT / NANG OI OI";
    if (temp >= 32.0)
      return "NANG NONG OI AM";
    if (temp >= 25.0)
      return "TROIC NANG DEP / QUANG MAY";
    return "NANG NHE TRONG TREN";
  }

  // Nhiều Mây (801 - 804)
  if (id == 801)
    return "NANG XEN MÂY / IT MÂY";
  if (id == 802)
    return "MÂY RẢI RÁC / TROI MAT";
  if (id == 803)
    return "NHIỀU MÂY / MÂY U AM";
  if (id == 804)
    return "MÂY AM U AM DAM (AM U)";

  return "THỜI TIẾT BÌNH THƯỜNG";
}

void handleRoot()
{
  server.send(200, "text/html", isAPMode ? AP_HTML : STA_HTML);
}

void handleScan()
{
  int n = WiFi.scanNetworks();
  JsonDocument doc;
  JsonArray array = doc.to<JsonArray>();
  for (int i = 0; i < n; ++i)
  {
    JsonObject obj = array.add<JsonObject>();
    obj["ssid"] = WiFi.SSID(i);
    obj["rssi"] = WiFi.RSSI(i);
  }
  String jsonString;
  serializeJson(doc, jsonString);
  server.send(200, "application/json", jsonString);
}

void handleSave()
{
  if (server.hasArg("ssid") && server.hasArg("pass"))
  {
    ssid = server.arg("ssid");
    pass = server.arg("pass");
    preferences.begin("wifi_config", false);
    preferences.putString("ssid", ssid);
    preferences.putString("pass", pass);
    preferences.end();
    server.send(200, "text/html", "<html><body><h2>Da luu WiFi! ESP32 đang khoi dong lai...</h2></body></html>");
    delay(2000);
    ESP.restart();
  }
  else
  {
    server.send(400, "text/plain", "Thieu thong tin!");
  }
}

void handleUpdateGPS()
{
  if (server.hasArg("lat") && server.hasArg("lon"))
  {
    userLat = server.arg("lat").toFloat();
    userLon = server.arg("lon").toFloat();
    hasCustomGPS = true;
    preferences.begin("wifi_config", false);
    preferences.putFloat("lat", userLat);
    preferences.putFloat("lon", userLon);
    preferences.end();
    Serial.printf("\n[GPS] Da cap nhat toa do: Lat=%.6f, Lon=%.6f\n", userLat, userLon);
    checkWeather();
    server.send(200, "text/plain", "OK");
  }
  else
  {
    server.send(400, "text/plain", "Thieu toa do!");
  }
}

void handleReset()
{
  preferences.begin("wifi_config", false);
  preferences.clear();
  preferences.end();
  server.send(200, "text/html", "<html><body><h2>Da xoa WiFi! Khoi dong lai...</h2></body></html>");
  delay(2000);
  ESP.restart();
}

void startAPMode()
{
  isAPMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  dnsServer.start(53, "*", WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/scan", handleScan);
  server.on("/save", HTTP_POST, handleSave);
  server.onNotFound(handleRoot);
  server.begin();

  Serial.println("\n--- ACCESS POINT MODE ---");
  Serial.print("WiFi AP: ");
  Serial.println(AP_SSID);
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
}

bool connectWiFi()
{
  if (ssid == "")
    return false;
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());

  Serial.print("Dang ket noi WiFi: ");
  Serial.print(ssid);
  int count = 0;
  while (WiFi.status() != WL_CONNECTED && count < 20)
  {
    delay(500);
    Serial.print(".");
    count++;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\n-> Ket noi thành cong!");
    Serial.print("IP Noic bo: ");
    Serial.println(WiFi.localIP());

    server.on("/", handleRoot);
    server.on("/update_gps", handleUpdateGPS);
    server.on("/reset", HTTP_POST, handleReset);
    server.begin();
    return true;
  }
  Serial.println("\n-> Ket noi that bai!");
  return false;
}

void setup()
{
  Serial.begin(115200);

  preferences.begin("wifi_config", true);
  ssid = preferences.getString("ssid", "");
  pass = preferences.getString("pass", "");
  userLat = preferences.getFloat("lat", 0.0);
  userLon = preferences.getFloat("lon", 0.0);
  preferences.end();

  if (userLat != 0.0 && userLon != 0.0)
    hasCustomGPS = true;

  if (!connectWiFi())
  {
    startAPMode();
  }
  else
  {
    checkWeather();
  }
}

void loop()
{
  if (isAPMode)
  {
    dnsServer.processNextRequest();
    server.handleClient();
  }
  else
  {
    server.handleClient();
    if (millis() - lastWeatherCheck >= WEATHER_INTERVAL)
    {
      lastWeatherCheck = millis();
      if (WiFi.status() == WL_CONNECTED)
      {
        checkWeather();
      }
    }
  }
}

void checkWeather()
{
  if (OPENWEATHER_API_KEY == "YOUR_OPENWEATHERMAP_API_KEY")
  {
    Serial.println("[Canh bao] Nhap OpenWeatherMap API Key!");
    return;
  }

  HTTPClient http;

  if (!hasCustomGPS)
  {
    http.begin("http://ipwho.is/");
    int code = http.GET();
    if (code == HTTP_CODE_OK)
    {
      JsonDocument ipDoc;
      deserializeJson(ipDoc, http.getString());
      if (ipDoc["success"].as<bool>() == true)
      {
        userLat = ipDoc["latitude"].as<float>();
        userLon = ipDoc["longitude"].as<float>();
      }
    }
    http.end();
  }

  if (userLat == 0.0 && userLon == 0.0)
  {
    Serial.println("Khong co toa do hop le!");
    return;
  }

  String url = "http://api.openweathermap.org/data/2.5/weather?lat=" + String(userLat, 6) +
               "&lon=" + String(userLon, 6) + "&appid=" + OPENWEATHER_API_KEY + "&units=metric&lang=vi";

  http.begin(url);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK)
  {
    JsonDocument doc;
    deserializeJson(doc, http.getString());

    int weatherId = doc["weather"][0]["id"].as<int>();
    String description = doc["weather"][0]["description"].as<String>();
    float temp = doc["main"]["temp"].as<float>();
    float feelsLike = doc["main"]["feels_like"].as<float>();
    int humidity = doc["main"]["humidity"].as<int>();
    float windSpeed = doc["wind"]["speed"].as<float>();
    int clouds = doc["clouds"]["all"].as<int>();
    String city = doc["name"].as<String>();

    // Lấy trạng thái thời tiết phân loại chi tiết
    String statusDetail = getDetailedWeatherStatus(weatherId, temp);

    Serial.println("\n========== THONG TIN THOI TIET CHI TIET ==========");
    Serial.printf("Khu vuc       : %s\n", city.c_str());
    Serial.printf("Trang thai    : %s (Mô tả OpenWeather: %s)\n", statusDetail.c_str(), description.c_str());
    Serial.printf("Nhiet do      : %.1f °C (Cam giac nhu: %.1f °C)\n", temp, feelsLike);
    Serial.printf("Do am         : %d %%\n", humidity);
    Serial.printf("Toc do gio    : %.1f m/s\n", windSpeed);
    Serial.printf("Do che phu may: %d %%\n", clouds);
    Serial.println("==================================================\n");
  }
  else
  {
    Serial.printf("Loi OpenWeatherMap HTTP: %d\n", httpCode);
  }
  http.end();
}