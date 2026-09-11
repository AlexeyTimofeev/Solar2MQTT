#include "core/LogSerial.h"

#include "core/DiagLog.h"

namespace
{
void waitForConsole(unsigned long timeoutMs)
{
#if ARDUINO_USB_CDC_ON_BOOT
    const unsigned long started = millis();
    while (!Serial && (millis() - started) < timeoutMs)
    {
        delay(10);
    }
    delay(20);
#else
    (void)timeoutMs;
#endif
}
// The web serial console allocates per line (std::string growth, AsyncWebSocket::textAll) and a failed allocation
// aborts the board, so it is skipped while memory is short (e.g. during a firmware download). Serial and the RTC log
// still get every line.
bool webSerialAffordable()
{
    return ESP.getMaxAllocHeap() > 8192;
}
} // namespace

LogSerialClass LogSerial;

void LogSerialClass::begin(unsigned long baud)
{
    Serial.begin(baud);
    _started = true;
    waitForConsole(1200);
}

void LogSerialClass::begin(AsyncWebServer *server, unsigned long baud, size_t bufferSize)
{
    if (!Serial)
    {
        Serial.begin(baud);
        _started = true;
        waitForConsole(1200);
    }

    webSerial.begin(server, nullptr, "/webserialws");
    webSerial.setBuffer(bufferSize);
    webSerial.onMessage([](const std::string &msg)
                        {
        Serial.print("[WebSerial RX] ");
        Serial.println(msg.c_str()); });
}

void LogSerialClass::onMessage(std::function<void(const std::string &)> cb)
{
    webSerial.onMessage(cb);
}

void LogSerialClass::setBuffer(size_t size)
{
    webSerial.setBuffer(size);
}

int LogSerialClass::available()
{
    return Serial.available();
}

int LogSerialClass::read()
{
    return Serial.read();
}

int LogSerialClass::peek()
{
    return Serial.peek();
}

void LogSerialClass::flush()
{
    Serial.flush();
}

size_t LogSerialClass::write(uint8_t byte)
{
    Serial.write(byte);
    if (webSerialAffordable())
    {
        webSerial.write(&byte, 1);
    }
    DiagLog::write(&byte, 1);
    return 1;
}

size_t LogSerialClass::write(const uint8_t *buffer, size_t size)
{
    Serial.write(buffer, size);
    if (webSerialAffordable())
    {
        webSerial.write(buffer, size);
    }
    DiagLog::write(buffer, size);
    return size;
}

LogSerialClass::operator bool()
{
    return _started;
}
