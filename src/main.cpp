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
const unsigned long WEATHER_INTERVAL = 300000; // 5 phút kiểm tra 1 lần

// --- GIAO DIỆN 1: Trang cấu hình WiFi (Chế độ AP) ---
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

// --- GIAO DIỆN 2: Trang Dashboard điều khiển nội bộ (Chế độ STA) ---
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

// Trả về giao diện tùy theo chế độ
void handleRoot()
{
  if (isAPMode)
  {
    server.send(200, "text/html", AP_HTML);
  }
  else
  {
    server.send(200, "text/html", STA_HTML);
  }
}

// Quét mạng WiFi cho trang AP
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

// Lưu thông tin WiFi từ AP
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

    String html = "<html><body><h2>Da luu WiFi! ESP32 dang khoi dong lai de ket noi...</h2></body></html>";
    server.send(200, "text/html", html);
    delay(2000);
    ESP.restart();
  }
  else
  {
    server.send(400, "text/plain", "Thieu thông tin!");
  }
}

// Cập nhật GPS chính xác từ Dashboard nội bộ
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

    Serial.printf("\n[GPS] Da cap nhat toa do moi: Lat=%.6f, Lon=%.6f\n", userLat, userLon);
    checkWeather(); // Kiểm tra thời tiết ngay với tọa độ mới
    server.send(200, "text/plain", "OK");
  }
  else
  {
    server.send(400, "text/plain", "Thieu toa do!");
  }
}

// Xóa cài đặt WiFi để cấu hình lại
void handleReset()
{
  preferences.begin("wifi_config", false);
  preferences.clear();
  preferences.end();

  String html = "<html><body><h2>Da xoa cau hinh WiFi! ESP32 dang khoi dong lai...</h2></body></html>";
  server.send(200, "text/html", html);
  delay(2000);
  ESP.restart();
}

// Bật AP Mode
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

  Serial.println("\n--- CHẾ ĐỘ ACCESS POINT ---");
  Serial.print("WiFi AP: ");
  Serial.println(AP_SSID);
  Serial.print("IP Web Config: ");
  Serial.println(WiFi.softAPIP());
}

// Kết nối WiFi đã lưu
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
    Serial.println("\n-> Ket noi thanh cong!");
    Serial.print("Dia chi IP noi bo: ");
    Serial.println(WiFi.localIP());

    // Khởi tạo Web Server ở chế độ Station
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

  // Đọc dữ liệu đã lưu
  preferences.begin("wifi_config", true);
  ssid = preferences.getString("ssid", "");
  pass = preferences.getString("pass", "");
  userLat = preferences.getFloat("lat", 0.0);
  userLon = preferences.getFloat("lon", 0.0);
  preferences.end();

  if (userLat != 0.0 && userLon != 0.0)
  {
    hasCustomGPS = true;
  }

  if (!connectWiFi())
  {
    startAPMode();
  }
  else
  {
    checkWeather(); // Kiểm tra thời tiết ngay khi khởi động xong
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
    server.handleClient(); // Xử lý các yêu cầu truy cập Web Dashboard nội bộ

    // Định kỳ 5 phút kiểm tra thời tiết
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

// Hàm lấy thông tin thời tiết & Mưa
void checkWeather()
{
  if (OPENWEATHER_API_KEY == "YOUR_OPENWEATHERMAP_API_KEY")
  {
    Serial.println("[Canch bao] Vui long nhap OpenWeatherMap API Key!");
    return;
  }

  HTTPClient http;

  // Nếu người dùng chưa từng cập nhật GPS thủ công, ESP32 tự lấy GPS tương đối qua IP
  if (!hasCustomGPS)
  {
    Serial.println("Dang lay vi tri tu dong qua ip-api.com...");
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
        Serial.printf("Vi tri theo IP: %s (Lat: %.4f, Lon: %.4f)\n",
                      ipDoc["city"].as<const char *>(), userLat, userLon);
      }
    }
    http.end();
  }

  // Nếu vẫn không lấy được tọa độ thì dừng lại
  if (userLat == 0.0 && userLon == 0.0)
  {
    Serial.println("Khong co toan do hop le de kiem tra thoi tiet!");
    return;
  }

  // Gọi OpenWeatherMap API
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
    String city = doc["name"].as<String>();

    Serial.println("\n========== THONG TIN THOI TIET ==========");
    Serial.printf("Khu vuc: %s\n", city.c_str());
    Serial.printf("Nhiet do: %.1f C\n", temp);
    Serial.printf("Trang thai: %s (ID: %d)\n", description.c_str(), weatherId);

    // Kiểm tra các mã ID thời tiết báo Mưa (200 - 531)
    if (weatherId >= 200 && weatherId <= 531)
    {
      Serial.println("===> THONG BAO: HIENTAI DANG CO MUA! <===");
    }
    else
    {
      Serial.println("Trời không mưa.");
    }
    Serial.println("=========================================\n");
  }
  else
  {
    Serial.printf("Loi OpenWeatherMap HTTP: %d\n", httpCode);
  }
  http.end();
}